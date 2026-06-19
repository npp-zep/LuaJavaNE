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
