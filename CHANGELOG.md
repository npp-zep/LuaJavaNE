## [Unreleased]

## [2.2.7] - 2026-09-05
### Added
- **`java.listall()` to enumerate the cross-state store**: iterates the FNV-1a hash table and returns a `key -> value` Lua table snapshot of all stored entries, e.g. `for k, v in pairs(java.listall()) do ... end`. Entries whose value was degraded to `nil` (unsupported complex types) are skipped. Registered between `fetch` and `deleteStore`; documented in README and `docs/Java4Lua.md`.
- **Module-aware REPL autocompletion**: the JLine completer now covers the full Lua 5.4 standard library (base globals plus `string`, `table`, `math`, `io`, `os`, `coroutine`, `utf8`, `debug`, `package`) and all LuaJavaNE extension libraries (`java`, `clac`, `utils`, `gc`). Typing a dotted prefix like `string.s` or `clac.batch_` only suggests functions of that module; bare words complete keywords, base functions and module names.
- **`make deb` to build a Debian package**: uses `dpkg-deb` (no debhelper needed) to produce `luajavane_<version>_<arch>.deb`, installing the `luaj` launcher to `/usr/bin/luaj` and the runtime (luajava.so, luajava.jar, jline.jar, scripts, docs, examples) under `/usr/share/luajavane/`. Installable via `sudo apt install ./luajavane_*.deb` or `sudo dpkg -i luajavane_*.deb`. Requires a JRE 17+; the `Depends` alternates cover both Debian/Ubuntu (`default-jre-headless (>= 17) | openjdk-17-jre-headless | openjdk-21-jre-headless`) and Termux (`openjdk-17 | openjdk-21 | openjdk-25`), which has no `*-jre-headless` split.
- **`make deb-termux` for Termux packages**: Termux's dpkg extracts archives against the Android root, so a Debian-style `./usr/...` layout fails with *Read-only file system* (its own packages use `./data/data/com.termux/files/usr/...`). The deb recipe now takes the archive root from `DEB_ARCHIVE_PREFIX` (default `usr`); `make deb-termux` sets it to the full Termux prefix, and the `/bin/luaj` wrapper points at the matching `luaj.sh`.
- **`make deb` auto-detects Termux**: instead of relying solely on the `PREFIX` env var (which may not be exported into `make`), the recipe now also probes the filesystem for `/data/data/com.termux/files/usr`; when either check hits, the Termux archive layout is used automatically and `make deb` prints the chosen `Archive prefix` so the result is visible.

### Fixed
- **`make deb` failed on Termux (`control directory has bad permissions 700`)**: Termux defaults to `umask 077`, so the staged `DEBIAN` control directory was created with mode 700, which `dpkg-deb` rejects. The deb target now explicitly normalizes the whole staging tree (`chmod -R u=rwX,go=rX`) and sets `DEBIAN/control` to 0644, independent of the environment umask.
- **REPL autocompletion no longer appends a trailing space**: JLine itself appends a space after a completed word whenever the `Candidate` is marked `complete` — even with a `null` suffix. The completer now creates candidates with `complete=false`, so accepting a completion yields `print` / `java.` with no auto-space; whether to add a space is left to the user.
- **REPL continuation prompt (JLine) misjudged indentation when open/close keywords share one line**: `countNesting` only matched lines that were *entirely* a keyword, so a self-closing line such as `if x then print(1) end` was counted as +1 and the REPL wrongly waited for more input in `>>` mode. It now scans tokens and balances same-line pairs — `if..then..end`, `while..do..end`, `for..in..do..end`, `function..end`, standalone `do..end` and `repeat..until` all net to 0, `else`/`elseif` no longer change depth, and keywords inside string literals or `--` comments are ignored.

## [2.2.6.1] - 2026-08-24
### Fixed
- **Release package now includes `LICENSE`**: previous release packages (2.2.6) were missing the license file. `make release` now copies `LICENSE` into the package. Since the release package does not bundle JUnit, the EPL-1.0 license (JUnit's) is intentionally not copied.

## [2.2.6] - 2026-08-23
### Fixed
- `LuaRuntime.compile()` crash on x86_64 Linux — use-after-free resolved via reference counting on `lua_State`: each `LuaFunctionObj` / `LuaInvocationHandler` holds a reference on the state, which is only `lua_close`d when the last reference is released (even if `LuaRuntime.close()` is called first)
- Static-method colon call syntax `Class:method(...)` now correctly strips the implicit class `self` argument, fixing "method not found" for static methods (e.g. `Math:max(10, 20)`)
- `LuaFunctionObj` JNI method names aligned with the Java declarations (`callMultipleNative` / `destroyNative`)
- Dynamic proxy `Object`-returning interface methods (e.g. `Supplier.get()`) now convert boxed/string results to native Lua values instead of opaque userdata, matching Java→Lua argument/return conversion
- **JVM SIGSEGV / wrong-overload calls**: method and constructor overload resolution no longer binds to the first name/arity-compatible method. A per-argument scoring resolver (`lua_score_with_class`) now prefers the exact primitive match (respecting integer range), and only accepts reference parameters assignable from the Lua boxed value — so `sb:append(5)` picks `append(int)`/`append(long)` instead of `append(StringBuffer)` and passing an Integer to the wrong slot no longer crashes the JVM. Method and constructor calls share the same two-phase (pick-best + invoke) resolver, with the fast signature-based lookup still attempted first.
- **JVM SIGSEGV when calling `System.gc()` (or any later full GC) after a static-method call**: static methods dispatched through the auto-detect path (`Class.method(...)` via `new_method_lookup(..., isStatic=-1)`) ended with `DeleteLocalRef(cls)` being called on a **global** reference (the resolved `cls` was `ml->obj`, a `NewGlobalRef`), corrupting the JNI reference table — the next full GC then crashed in `G1FullGCMarker::mark_and_push`. The cleanup guard now only releases `cls` when it is a genuine local reference (`(jobject)cls != ml->obj`); instance/constructor paths continue to free their local `cls` correctly. Verified with `-Xcheck:jni` (no more "Invalid local JNI handle passed to DeleteLocalRef").

### Changed
- Docs: removed the known "compile() crash on x86_64" limitation; documented static-method colon syntax and the refcount-based lifecycle of `LuaFunctionObj`
- **Unified Java array behavior**: arrays returned by Java methods are now wrapped with the same `Java.Array` userdata as `java.newArray` (0-based indexing, `#` length, element read/write); `String[]` returned by Java now reads back as Lua strings, consistent with `java.newArray("String", n)`
- **Completed Java→Lua argument passing** (`LuaRuntime.callFunction*`): previously only `String`/`Double`/`Integer`/`Boolean` were converted (others became `nil`); now all boxed number types, `Character`, arrays (as `Java.Array`) and arbitrary Java objects (as userdata) are passed through consistently, matching the return-value conversion
- `make test` now launches the JVM with `-Xshare:off` to avoid class-data-sharing archive crashes on some JDK 25 sandbox environments

## [2.2.5] - 2026-08-15
### Added
- **Callback-based async consumption**: `java.onComplete(id, callback)` alongside `checkPromise` polling — user picks either style
  - Callback signature `callback(err, result...)`, `err == nil` means success
  - Immediate dispatch when the task already finished (handles race between completion and registration)
  - Auto-cleanup of the PromiseEntry and callback reference after dispatch
- **`java.yield(ms)` wait primitive**: briefly releases `lua_mutex` so background worker threads can run registered callbacks during wait loops (unlike `Thread.sleep`, which does not release the lock)
- **Reflective method invocation fallback**: when regular signature matching fails, falls back to Java reflection (`Method.invoke`) — supports methods returning arbitrary object types (e.g. `java.math.BigInteger`/`BigDecimal`), static method detection, and overload try-and-continue
- **Examples**: `examples/callback.lua` (callback API), `examples/proxy.lua` (dynamic proxy with `java.yield` wait)

### Changed
- Object indexing now **prefers methods over fields** — e.g. `BigDecimal.scale()` calls the method instead of reading the private `int` field
- Async methods now reliably support **multiple arguments** (previously documented as single-arg)
- Docs refresh: README callback API + complete `java.*` API table; callback docs merged into `docs/AgentV2.md` (removed `docs/callback.md`)

### Fixed
- JVM crash (SIGSEGV in G1 GC) in callback examples caused by the main thread holding `lua_mutex` while background callbacks were dispatched — resolved with `java.yield`
- Method-not-found for methods returning concrete object types (e.g. `java.math` core methods)
- Deadlock-prone `Thread.join()` pattern documented in the proxy example (replaced with `java.yield` wait loop)


## [2.2.4] - 2026-07-17
### Added
- **Java arrays as Lua userdata**: automatic array detection and metadata caching when creating Java object wrappers
  - Array length and type info stored in userdata extra slots
  - Proper array element access and `#` length retrieval from Lua side
  - Supports primitive arrays (int[], double[], boolean[]) and object arrays (String[])

### Changed
- **Optimized `java.store` with hash table**: replaced linear linked-list search with FNV-1a hash table
  - O(1) average lookup time vs O(n) linear scan
  - Dynamic bucket resizing at 0.75 load factor
  - Reduced memory overhead with union type for value storage
  - Added `java.cleanup()` for manual GC of store memory
- **Stripped logging from LuaAgent**: removed all `LOGGER.debug/info/warning/severe` calls for production
  - All core functionality preserved (statistics, registry management, thread pool control, timeout handling)

### Fixed
- **CI build failure on GitHub Actions**:
  - Removed non-standard `bits/pthread_types.h` include
  - Fixed JNI `AttachCurrentThread` type warning
  - Updated workflow for multi-platform release artifacts


## [2.2.3] - 2026-07-09
### Added
- **Multi-platform Release**: Linux x86_64, Linux ARM64, macOS ARM64 pre-built packages
- **version.properties**: single source of truth for project version/metadata
- **Examples**: `examples/hello.lua`, `examples/async.lua`, `examples/clac.lua`
- **make release**: one-step packaging into `release/` directory
- **helpRepl()**: separate REPL-specific help message
- Cross-platform LuaRocks support via `LD_PRELOAD` + `LUA_PATH`/`LUA_CPATH` detection

### Changed
- `LuaJMain.java` reads version/copyright/license from system properties
- `luaj.sh` auto-detects Lua module paths on Linux/macOS/Termux
- CI/CD: matrix build for multi-platform releases, fixed test workflow
- `_VERSION` now shows `LuaJavaNE 2.2.3 (Lua x.x.x, PUC-Rio)`

### Fixed
- External Lua C extensions (e.g. `lfs.so`) now load correctly via `LD_PRELOAD`
- Release package now includes `lib/jline.jar`
- `copyright`/`credits`/`license` no longer show duplicate help text
- Compilation warning in `lualibjava.c` (array index out of bounds)



## [2.2.1] - 2026-06-19
### Added
- **Multi-platform Release**: Linux x86_64, Linux ARM64, macOS ARM64 pre-built packages
- **version.properties**: single source of truth for project version/metadata
- **Examples**: `examples/hello.lua`, `examples/async.lua`, `examples/clac.lua`
- **make release**: one-step packaging into `release/` directory
- **helpRepl()**: separate REPL-specific help message
- Cross-platform LuaRocks support via `LD_PRELOAD` + `LUA_PATH`/`LUA_CPATH` detection

### Changed
- `LuaJMain.java` reads version/copyright/license from system properties
- `luaj.sh` auto-detects Lua module paths on Linux/macOS/Termux
- CI/CD: matrix build for multi-platform releases, fixed test workflow
- `_VERSION` now shows `LuaJavaNE 2.2.1 (Lua x.x.x, PUC-Rio)`

### Fixed
- External Lua C extensions (e.g. `lfs.so`) now load correctly via `LD_PRELOAD`
- Release package now includes `lib/jline.jar`
- `copyright`/`credits`/`license` no longer show duplicate help text


## [2.2.1] - 2026-06-18
🎉 This is the 100th commit of the LuaJavaNE project.
### Added
- **Examples**: `examples/hello.lua` (basic interop), `examples/async.lua` (Agent v2), `examples/clac.lua` (ClacArray)
- **make release**: one-step packaging of `luajava.so`, `luajava.jar`, `luaj.sh`, examples, and docs into `release/`
- `lualibjava_internal.h`: added missing `<pthread.h>` and complete forward declarations for all exported functions
- `CHANGELOG.md` documenting all milestones from v1.0.0 to v2.2.1
### Changed
- Cleaned up stray files (`Review.md`, test artifacts)
### Fixed
- Compilation warning in `lualibjava.c` (array index out of bounds)
- `LD_PRELOAD` pollution in user environment (no longer set by project)


# Changelog

All notable changes to LuaJavaNE will be documented in this file.

## [2.2.0] - 2026-06-17
### Added
- **Cross-platform LuaRocks support**: `luaj.sh` now auto-detects LUA_PATH/LUA_CPATH via pkg-config, lua5.4, or common paths (Termux, Linux, macOS, Homebrew, MacPorts)
- **Agent v2 async API**: `java.runAsync()`, `java.runAsyncObj()`, `java.getObject()` with thread-pool execution and Promise-based result passing
- **ClacArray batch operations**: `clac.array(n)` creates native double arrays, `clac.batch_add/sub/mul/div/sin` operate in pure C memory (83x faster than Lua table loops)
- Full `cmath` functions in `clac`: `erf`, `tgamma`, `lgamma`, `exp2`, `log2`, `hypot`, `atan2`, `copysign`, `nextafter`, etc.
- `LuaAgent` thread pool with daemon threads, auto-shutdown on `LuaRuntime.close()`
- `LuaRuntime` now implements `AutoCloseable` for try-with-resources
- `AsyncRunner` scoring-based overload resolution for constructors and methods
- JUnit `AsyncTest` (8 tests covering constructor, static/instance, multi-return, errors, concurrent, shutdown)
- Project documentation in `docs/async-api.md`

### Changed
- **Project structure refactored**: Lua source in `lua/`, JNI in `native/jni/`, custom libs in `native/lualib/`, Java in `java/src/`
- **Lua source kept vanilla**: custom libraries registered via `lua_custom_init.c` without patching `linit.c` or `lualib.h`
- `CMakeLists.txt` auto-detects Lua directory with `file(GLOB lua-*)` – no version hardcoding
- `Makefile` unified: `make`, `make ninja`, `make test`, `make junit` targets
- Improved error handling in `checkPromise` (returns error strings, no `lua_error` on 'E' case)

### Fixed
- `PromiseEntry.result` uninitialized causing segfaults
- `checkPromise` not returning error messages properly
- `Makefile` duplicate javac steps breaking jar packaging
- `lualib_async.c` symbol redefinition with `lualibjava.c`

## [2.0.0] - 2026-05-16
### Added
- Multi-thread async Java calls with `java.runAsync()` (worker threads, Promise results)
- `AsyncRunner.runStatic` for reflective Java method invocation
- Clac high-performance math library: basic arithmetic, trigonometric, hyperbolic, random, constants
- `make test` runs JUnit suite (AllTests, PromiseTest, AsyncTest)
- REPL enhanced: `--version` shows C compiler info, `--help` detailed usage, `copyright`/`credits`/`license` commands

### Changed
- Moved from `Lua5.4.8/` flat structure to modular `native/`, `java/`, `lua/` directories
- Custom Lua libraries (`java`, `clac`) now registered via `lua_custom_init.c` instead of patching `linit.c`

### Fixed
- `LuaRuntime.compile()` crash on x86_64 (partially, known issue remaining)
- Various memory leaks and JNI reference management

## [1.0.0] - 2026-05-09
### Initial Release
- Basic Lua ↔ Java bidirectional interop
- `java.import()`, field access, method calls, array handling
- Dynamic proxy (`java.createProxy`)
- Annotation binding (`@LuaModule`, `@LuaFunction`)
- REPL with JLine, history, expression evaluation
- Promise/await coroutine model for async
- `store/fetch/deleteStore` C-side global cache
- Clac math library (basic operations)
