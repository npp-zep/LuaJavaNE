package com.luajava;

import java.io.IOException;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.LinkedHashSet;
import java.util.Set;

/**
 * 运行期附加 JAR / class 目录的类加载器。
 *
 * JNI 的 FindClass 只能看到声明 native 方法的类所在的加载器，因此运行期新增的
 * JAR 必须通过这里显式解析（java.import 会先试 FindClass，失败后回退到 resolve）。
 *
 * 作用域是进程级：JVM 无法卸载类，同名类也无法在两个加载器中并存，所以所有
 * LuaRuntime 共享同一个 URLClassLoader。
 */
public final class JarLoader {

    private static final Set<Path> loaded = new LinkedHashSet<>();

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
    public static synchronized int load(String path) throws IOException {
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
            String name = p.getFileName().toString().toLowerCase();
            if (!name.endsWith(".jar") && !name.endsWith(".zip")) {
                throw new IOException("not a jar/zip file: " + p);
            }
        }
        if (!Files.isReadable(p)) {
            throw new IOException("path not readable: " + p);
        }
        if (!loaded.add(p)) {
            return 0;
        }
        try {
            loader.add(p.toUri().toURL());
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

    // URLClassLoader.addURL 是 protected，通过子类暴露。
    private static final class Loader extends URLClassLoader {
        Loader(URL[] urls, ClassLoader parent) {
            super(urls, parent);
        }

        void add(URL url) {
            addURL(url);
        }
    }
}
