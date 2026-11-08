package com.luajava;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.jar.JarEntry;
import java.util.jar.JarFile;

/**
 * 运行期附加 JAR / class 目录的类加载器。
 *
 * JNI 的 FindClass 只能看到声明 native 方法的类所在的加载器，因此运行期新增的
 * JAR 必须通过这里显式解析（java.import 会先试 FindClass，失败后回退到 resolve）。
 *
 * 作用域是进程级：JVM 无法卸载类，同名类也无法在两个加载器中并存，所以所有
 * LuaRuntime 共享同一个 URLClassLoader。
 *
 * 本地库：JVM 按“声明 native 方法的类所属加载器”查找 native 实现，父加载器
 * System.load 的库对子加载器定义的类不可见。因此这里重写 findLibrary，把 JAR 内
 * 与当前平台匹配的 .so/.dylib/.dll 提取到临时目录后按库名返回，JAR 代码里的
 * System.loadLibrary("foo") 才能绑定成功。
 */
public final class JarLoader {

    private static final Set<Path> loaded = new LinkedHashSet<>();

    // 类名 -> 来源 JAR，用于检测同名类覆盖
    private static final Map<String, Path> classOwner = new ConcurrentHashMap<>();

    // 库名 -> 已提取的绝对路径，供 findLibrary 查询
    private static final Map<String, Path> nativeLibs = new ConcurrentHashMap<>();

    private static final List<String> warnings = new ArrayList<>();
    private static final Set<String> warned = new LinkedHashSet<>();
    private static final int WARN_LIMIT = 10;

    // 父加载器使用 LuaJavaNE 自身的加载器（即应用 classloader），保持父优先委派，
    // JVM 与应用原有的类行为不变。
    private static final Loader loader =
        new Loader(new URL[0], JarLoader.class.getClassLoader());

    private JarLoader() {
    }

    /**
     * 附加一个 JAR/ZIP 或 class 目录。
     *
     * @return 1 表示新增成功；0 表示之前已加载过（去重）
     * @throws IOException 路径不存在、不可读或不是 jar/zip/目录
     */
    public static int load(String path) throws IOException {
        return load(path, false);
    }

    /**
     * 附加一个 JAR/ZIP 或 class 目录，可选提取并绑定其中的本地库。
     *
     * @param natives true 时扫描 JAR 内与当前平台匹配的 .so/.dylib/.dll，
     *                提取到临时目录并注册给 findLibrary
     * @return 1 表示新增成功；0 表示之前已加载过（去重）
     * @throws IOException 路径不存在、不可读、不是 jar/zip/目录，或本地库提取失败
     */
    public static synchronized int load(String path, boolean natives) throws IOException {
        if (path == null || path.isEmpty()) {
            throw new IOException("empty path");
        }
        Path p = Paths.get(path).toAbsolutePath().normalize();
        if (!Files.exists(p)) {
            throw new IOException("path not found: " + p);
        }
        boolean isDir = Files.isDirectory(p);
        if (!isDir) {
            if (!Files.isRegularFile(p)) {
                throw new IOException("not a file or directory: " + p);
            }
            String name = p.getFileName().toString().toLowerCase(Locale.ROOT);
            if (!name.endsWith(".jar") && !name.endsWith(".zip")) {
                throw new IOException("not a jar/zip file: " + p);
            }
        }
        if (!Files.isReadable(p)) {
            throw new IOException("path not readable: " + p);
        }
        if (natives && isDir) {
            warn("native libraries are not extracted from directories: " + p);
        }
        if (!loaded.add(p)) {
            return 0;
        }
        try {
            loader.add(p.toUri().toURL());
            if (isDir) {
                indexDirectory(p);
            } else {
                indexJar(p, natives);
            }
        } catch (IOException e) {
            loaded.remove(p);
            throw e;
        }
        return 1;
    }

    /**
     * 用运行期加载器解析类名。找不到时返回 null（不初始化类）。
     */
    public static Class<?> resolve(String binaryName) {
        if (binaryName == null || binaryName.isEmpty()) {
            return null;
        }
        try {
            return Class.forName(binaryName, false, loader);
        } catch (ClassNotFoundException | LinkageError e) {
            return null;
        }
    }

    /** 供异步调用、代理等需要 ClassLoader 的场景使用。 */
    public static ClassLoader loader() {
        return loader;
    }

    /** 已加载路径快照，便于诊断。 */
    public static synchronized String[] loadedPaths() {
        String[] out = new String[loaded.size()];
        int i = 0;
        for (Path p : loaded) {
            out[i++] = p.toString();
        }
        return out;
    }

    /** 取出并清空本次累积的告警（Lua 侧作为 java.load 的第二个返回值）。 */
    public static synchronized String[] drainWarnings() {
        String[] out = warnings.toArray(new String[0]);
        warnings.clear();
        return out;
    }

    // ---------- 索引与本地库 ----------

    private static void indexJar(Path jar, boolean natives) throws IOException {
        List<String> nativeEntries = new ArrayList<>();
        try (JarFile jf = new JarFile(jar.toFile())) {
            Enumeration<JarEntry> it = jf.entries();
            while (it.hasMoreElements()) {
                JarEntry e = it.nextElement();
                if (e.isDirectory()) {
                    continue;
                }
                String name = e.getName();
                if (name.endsWith(".class")) {
                    String binary = toBinaryName(name);
                    if (binary != null) {
                        String resource = binary.replace('.', '/') + ".class";
                        Path owner = classOwner.putIfAbsent(binary, jar);
                        if (owner != null && !owner.equals(jar)) {
                            warn("class " + binary + " exists in both " + owner + " and " + jar
                                + "; the copy in " + jar + " will not be used");
                        } else if (owner == null && parentHas(resource)) {
                            warn("class " + binary + " is already provided by the application classpath;"
                                + " the copy in " + jar + " is shadowed");
                        }
                    }
                } else if (natives && isNativeEntry(name)) {
                    nativeEntries.add(name);
                }
            }
            if (!natives) {
                return;
            }
            if (nativeEntries.isEmpty()) {
                warn("no native library found in " + jar
                    + " (requested natives extraction)");
                return;
            }
            Path dir = nativeDir(jar);
            int accepted = 0;
            for (String entry : nativeEntries) {
                if (!matchesPlatform(entry)) {
                    continue;
                }
                Path extracted = extract(jf, entry, dir);
                String libName = libraryName(entry);
                if (libName == null) {
                    continue;
                }
                Path previous = nativeLibs.putIfAbsent(libName, extracted);
                if (previous != null && !previous.equals(extracted)) {
                    warn("native library '" + libName + "' already provided by " + previous
                        + "; ignoring " + extracted);
                    continue;
                }
                accepted++;
            }
            if (accepted == 0) {
                warn("no native library matching " + platform() + " found in " + jar
                    + " (" + nativeEntries.size() + " entries skipped)");
            }
        }
    }

    private static void indexDirectory(Path dir) throws IOException {
        try (java.util.stream.Stream<Path> stream = Files.walk(dir)) {
            stream.filter(Files::isRegularFile)
                .filter(f -> f.getFileName().toString().endsWith(".class"))
                .forEach(f -> {
                    String rel = dir.relativize(f).toString().replace('\\', '/');
                    String binary = toBinaryName(rel);
                    if (binary != null) {
                        String resource = binary.replace('.', '/') + ".class";
                        Path owner = classOwner.putIfAbsent(binary, dir);
                        if (owner != null && !owner.equals(dir)) {
                            warn("class " + binary + " exists in both " + owner + " and " + dir
                                + "; the copy in " + dir + " will not be used");
                        } else if (owner == null && parentHas(resource)) {
                            warn("class " + binary + " is already provided by the application classpath;"
                                + " the copy in " + dir + " is shadowed");
                        }
                    }
                });
        }
    }

    /** 应用类加载器是否已能提供该资源（不触发类初始化）。 */
    private static boolean parentHas(String resource) {
        ClassLoader app = JarLoader.class.getClassLoader();
        return app != null && app.getResource(resource) != null;
    }

    private static String toBinaryName(String entry) {
        String name = entry;
        if (name.startsWith("META-INF/versions/")) {
            int slash = name.indexOf('/', "META-INF/versions/".length());
            if (slash < 0) {
                return null;
            }
            name = name.substring(slash + 1);
        }
        if (name.equals("module-info.class") || name.endsWith("/module-info.class")) {
            return null;
        }
        if (!name.endsWith(".class")) {
            return null;
        }
        return name.substring(0, name.length() - 6).replace('/', '.');
    }

    private static boolean isNativeEntry(String entry) {
        String n = entry.toLowerCase(Locale.ROOT);
        return n.endsWith(".so") || n.endsWith(".dylib") || n.endsWith(".jnilib")
            || n.endsWith(".dll");
    }

    /**
     * 按路径里的平台标记过滤：出现“其它平台”的标记就跳过，
     * 没有任何标记的条目视为可用（由运行时 dlopen 再决定）。
     */
    private static boolean matchesPlatform(String entry) {
        String n = entry.toLowerCase(Locale.ROOT).replace('\\', '/');
        String os = osFamily();
        String arch = archFamily();
        String ext = n.substring(n.lastIndexOf('.') + 1);
        if (os.equals("linux") && !ext.equals("so")) return false;
        if (os.equals("mac") && !(ext.equals("dylib") || ext.equals("jnilib"))) return false;
        if (os.equals("win") && !ext.equals("dll")) return false;

        for (String other : new String[]{"linux", "macos", "darwin", "osx", "windows", "win32"}) {
            if (n.contains(other) && !matchesOs(other, os)) {
                return false;
            }
        }
        for (String other : new String[]{"x86_64", "amd64", "x64", "aarch64", "arm64",
                                         "i386", "i686", "armv7"}) {
            if (n.contains(other) && !matchesArch(other, arch)) {
                return false;
            }
        }
        return true;
    }

    private static boolean matchesOs(String marker, String os) {
        switch (os) {
            case "linux": return marker.equals("linux");
            case "mac": return marker.equals("macos") || marker.equals("darwin") || marker.equals("osx");
            case "win": return marker.equals("windows") || marker.equals("win32");
            default: return true;
        }
    }

    private static boolean matchesArch(String marker, String arch) {
        switch (arch) {
            case "x86_64": return marker.equals("x86_64") || marker.equals("amd64") || marker.equals("x64");
            case "aarch64": return marker.equals("aarch64") || marker.equals("arm64");
            case "x86": return marker.equals("i386") || marker.equals("i686");
            case "arm": return marker.startsWith("arm") && !marker.equals("arm64");
            default: return true;
        }
    }

    private static String libraryName(String entry) {
        String base = entry.substring(entry.lastIndexOf('/') + 1);
        int dot = base.lastIndexOf('.');
        if (dot > 0) {
            base = base.substring(0, dot);
        }
        if (base.startsWith("lib")) {
            base = base.substring(3);
        }
        return base.isEmpty() ? null : base;
    }

    private static Path nativeDir(Path jar) throws IOException {
        // 带上文件指纹，避免 JAR 更新后仍复用旧的解压结果
        String stamp = jar + ":" + Files.size(jar) + ":" + Files.getLastModifiedTime(jar).toMillis();
        String tag = Integer.toHexString(stamp.hashCode());
        Path dir = Paths.get(System.getProperty("java.io.tmpdir"), "luajavane-natives", tag);
        Files.createDirectories(dir);
        return dir;
    }

    private static Path extract(JarFile jar, String entry, Path dir) throws IOException {
        Path target = dir.resolve(entry.substring(entry.lastIndexOf('/') + 1));
        try (InputStream in = jar.getInputStream(jar.getEntry(entry))) {
            long size = jar.getEntry(entry).getSize();
            if (Files.isRegularFile(target) && size > 0 && Files.size(target) == size) {
                return target.toAbsolutePath();
            }
            try (OutputStream out = Files.newOutputStream(target)) {
                in.transferTo(out);
            }
        }
        target.toFile().setReadable(true, true);
        target.toFile().setExecutable(true, true);
        return target.toAbsolutePath();
    }

    private static String platform() {
        return osFamily() + "-" + archFamily();
    }

    private static String osFamily() {
        String os = System.getProperty("os.name", "").toLowerCase(Locale.ROOT);
        if (os.contains("linux")) return "linux";
        if (os.contains("mac") || os.contains("darwin")) return "mac";
        if (os.contains("win")) return "win";
        return os.replace(' ', '-');
    }

    private static String archFamily() {
        String a = System.getProperty("os.arch", "").toLowerCase(Locale.ROOT);
        if (a.equals("amd64") || a.equals("x86_64") || a.equals("x64")) return "x86_64";
        if (a.equals("aarch64") || a.equals("arm64")) return "aarch64";
        if (a.startsWith("arm")) return "arm";
        return a;
    }

    private static synchronized void warn(String msg) {
        if (!warned.add(msg)) {
            return;
        }
        if (warnings.size() < WARN_LIMIT) {
            warnings.add(msg);
            System.err.println("[LuaJavaNE] warning: " + msg);
        } else if (warnings.size() == WARN_LIMIT) {
            warnings.add("further warnings suppressed");
            System.err.println("[LuaJavaNE] warning: further warnings suppressed");
        }
    }

    // URLClassLoader.addURL 是 protected，通过子类暴露；findLibrary 决定本地库绑定。
    private static final class Loader extends URLClassLoader {
        Loader(URL[] urls, ClassLoader parent) {
            super(urls, parent);
        }

        void add(URL url) {
            addURL(url);
        }

        @Override
        protected String findLibrary(String name) {
            Path lib = nativeLibs.get(name);
            return lib != null && Files.isRegularFile(lib) ? lib.toString() : null;
        }
    }
}
