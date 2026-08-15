# 异步回调（Callback）改造可行性分析与方案

> 目标：把当前"阻塞/协程式等待"（`java.await`）改成**回调驱动**（on-complete callback），同时**保留 `java.checkPromise` 轮询机制**，让用户按需二选一。
> 本文为纯分析 + 改造方案文档，不涉及源码改动。

---

## 1. 背景与目标

### 1.1 现状

LuaJavaNE 的异步任务链当前提供 **3 个消费原语**（都在 `java` 库里注册，见 [lualibjava.c 的 javalib 表](file:///workspace/LuaJavaNE/native/lualib/lualibjava.c#L1349-L1366)）：

| 原语 | 机制 | 使用场景 | 现状问题 |
|---|---|---|---|
| `java.await(id)` | 协程 `lua_yield`，靠 `java.complete` 用 `lua_resume` 续体 | 需要手动包 `coroutine.create`，见 [PromiseTest.java](file:///workspace/LuaJavaNE/test/PromiseTest.java#L10-L17) | 只能用于协程内；等待期协程挂起，逻辑不直观；且当前 `LuaPromise.complete` 的 `LUA_LOCK` 被注释（[luajava.c](file:///workspace/LuaJavaNE/native/jni/luajava.c#L862-L884)），跨线程续体存在隐患 |
| `java.checkPromise(id)` | 轮询：返回 `done, result...`，未完成返回 `false, nil` | 主流程/REPL 中循环检查，见 [async.lua](file:///workspace/LuaJavaNE/examples/async.lua#L4-L13) | 需要用户手写 `while/repeat` 轮询 + `Thread.sleep`；**读完 done 后未清理 PromiseEntry**（[lualib_async.c](file:///workspace/LuaJavaNE/native/lualib/lualib_async.c#L136-L190)），与 [AgentV2.md](file:///workspace/LuaJavaNE/docs/AgentV2.md#L84) 的"自动清理"描述不符，存在内存泄漏 |
| `java.complete(id, ...)` | 手动完成原语 | 仅测试/协程续体用 | 不建议用户直接调用 |

### 1.2 目标

1. 新增**回调式**消费 API（把 `await` 的"等待"语义替换为"完成时回调"）。
2. **保留** `checkPromise` 轮询，二者并存，用户自选。
3. 顺带修复 PromiseEntry 生命周期清理问题。
4. 维持现有类型序列化格式（`S/I/N/B/O/E/M` 前缀）不变，保证两种消费方式结果语义一致。

---

## 2. 目标 API 设计（草案）

### 2.1 主推方案：`java.onComplete(id, callback)`

> 说明：`then` 是 Lua 保留关键字，不能作为字段名直接访问（`java.then(...)` 会报 `<name> expected near 'then'`），故最终采用非保留字 `onComplete`。

```lua
local java = require("java")

local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")

-- 回调方式（新增）：结果到达时由后台线程自动调用
java.onComplete(id, function(err, ...)
    if err then
        print("失败:", err)                 -- err 为 "E:..." 错误字符串
    else
        print("结果:", ...)                  -- 与 checkPromise 返回值语义完全一致
    end
end)

-- 轮询方式（保留）：二者可任选其一
-- repeat
--     local done, result = java.checkPromise(id)
-- until done
```

**回调签名约定**：`callback(err, result...)`
- `err == nil`：成功，`result...` 与 `checkPromise` 返回的结果值**完全一致**（含多返回值、`O:` 对象 id、`nil`）。
- `err == string`：失败，内容即 `E:` 前缀后的错误信息。

**提交时一步到位（可选增强）**：`java.runAsync(id, cls, mtd, ..., callback)` —— 当最后一个参数是 `function` 时自动视为回调，等于 `runAsync + onComplete` 的语法糖。是否提供取决于复杂度权衡，见 §5.4。

### 2.2 与 `java.await` 的关系

- `java.await`（协程原语）与回调 API **正交**，可继续保留（PromiseTest 依赖它）。
- 若按"把 await 改成回调"严格推进，也可将 `await` 标记为 `@deprecated` 并逐步移除。
- 建议：**新增 `onComplete`、保留 `checkPromise` 与 `await`**，三机制并存但文档明确推荐前两者。是否移除 `await` 属产品决策，不影响本文方案主体。

---

## 3. 可行性分析

### 3.1 结论

**完全可行，改动面小、风险可控。** 所有底层积木都已存在：

1. **后台完成入口已就绪**：`LuaAgent` 线程池任务完成后会调用 native `Java_com_luajava_LuaAgent_complete`（[LuaAgent.java](file:///workspace/LuaJavaNE/java/src/com/luajava/LuaAgent.java#L148) → [lualib_async.c](file:///workspace/LuaJavaNE/native/lualib/lualib_async.c#L17-L35)）。这个函数就是天然的"工作线程 → 派发回调"入口，只需在写完 `result`/`done` 后追加派发逻辑。
2. **跨线程调用 Lua 已有先例**：`Java_com_luajava_LuaPromise_complete/resumeExceptionally` 已经从 Java 线程对另一 `lua_State` 执行 `lua_rawgeti + lua_resume`（[luajava.c](file:///workspace/LuaJavaNE/native/jni/luajava.c#L845-L930)），证明"从工作线程操作 Lua 状态"在架构上被允许。
3. **递归锁可用**：`lua_mutex` 是 `PTHREAD_MUTEX_RECURSIVE`（[luajava.c](file:///workspace/LuaJavaNE/native/jni/luajava.c#L27-L35)），回调内部再调用 Java 互操作不会死锁（重入安全）。
4. **结果解析逻辑已存在**：`java_checkPromise` 内已有一套完整的 `S/I/N/B/O/E/M` 解析器（[lualib_async.c](file:///workspace/LuaJavaNE/native/lualib/lualib_async.c#L136-L190)），抽出为共享函数即可复用于回调派发，保证两种方式结果语义一致。
5. **JNIEnv 现成**：complete 回调由 JNI 直接调用，`env` 已 attach 到工作线程，回调内若再调 Java 方法无需额外 attach。

### 3.2 关键难点（均为可控问题）

| # | 难点 | 说明 | 对策 |
|---|---|---|---|
| 1 | **锁序 / 死锁** | `promise_mutex` 保护全局注册表链表，`lua_mutex` 保护单个 `lua_State`。主线程路径是"持有 `lua_mutex` → 再取 `promise_mutex`"（如 `checkPromise`）。若工作线程反过来"持有 `promise_mutex` → 再取 `lua_mutex`"则死锁 | **铁律：绝不持有 `promise_mutex` 去获取 `lua_mutex`**。派发时先加锁拷贝出 `callbackRef`/`owner`/`result`，立即释放 `promise_mutex`，再取 `lua_mutex` 调回调 |
| 2 | **回调在哪个线程执行** | 派发发生在工作线程上，回调也在该线程运行 | 文档明确：回调应**快速返回**；耗时要再 `runAsync` 提交，不要在回调里 `Thread.sleep` 阻塞池线程 |
| 3 | **回调已注册 vs 已完成的竞态** | `runAsync` 可能先完成，`onComplete` 后到 | `java_onComplete` 注册时检查 `done`，若已完成立即派发（见 §5.3） |
| 4 | **owner 状态被 close** | 全局注册表跨多个 `LuaRuntime`，回调引用属于某个 `lua_State`，状态关闭后引用悬空 | `_close` 时遍历注册表，`luaL_unref` 掉属于该状态的 `callbackRef`（见 §5.5） |
| 5 | **内存泄漏（现状）** | `checkPromise` 读完后不删条目；`entry->result` 用 `strdup` 分配 | 新方案统一清理：回调派发完成 or 轮询消费完成（且无回调）即 `free` 条目（见 §5.6） |
| 6 | **多返回值/对象回传** | `O:` 只回传整数 oid，回调里再 `java.getObject(oid)` 较绕 | 默认与 `checkPromise` 保持**完全一致的返回语义**（oid 整数）；"回调里自动解包为 userdata"列为可选增强（§6.4） |

### 3.3 线程模型图（改造后）

```
Lua 主线程                       工作线程 (LuaAgent 池)
───────                          ──────────────────────
java.promise() ──创建条目────────▶ promise_registry (全局链表, promise_mutex 保护)
java.runAsync(id,...) ──提交────▶ LuaAgent.submitTask
java.onComplete(id, cb) ──注册 cbRef ──▶ entry.callbackRef = luaL_ref(owner)
                                    │
                                    └── 任务完成 ──▶ LuaAgent.complete(pid,result)
                                                        │
                                     Java_com_luajava_LuaAgent_complete
                                     ┌── promise_mutex: 写 result/done, 拷贝 cbRef
                                     ├── promise_mutex 释放
                                     └── lua_mutex: 取 cb 闭包 → push err+结果 → lua_pcall
                                                        │
                                       回调函数在 owner 状态上被调用（工作线程）
```

---

## 4. 改造范围总览

| 文件 | 改动 | 规模 |
|---|---|---|
| [lualibjava_internal.h](file:///workspace/LuaJavaNE/native/lualib/lualibjava_internal.h) | `PromiseEntry` 增加 `owner`、`callbackRef` 字段；声明共享解析函数 | 小 |
| [lualibjava.c](file:///workspace/LuaJavaNE/native/lualib/lualibjava.c) | `java_promise` 初始化新字段；新增 `java_onComplete`；注册 `{"onComplete", ...}`；`_close` 联动清理（或放 luajava.c） | 中 |
| [lualib_async.c](file:///workspace/LuaJavaNE/native/lualib/lualib_async.c) | 抽出 `push_parsed_result`；`checkPromise` 复用并补清理；`Java_com_luajava_LuaAgent_complete` 追加回调派发 | 中 |
| [luajava.c](file:///workspace/LuaJavaNE/native/jni/luajava.c) | `_close` 时遍历 unref 属于该状态的回调（或放 lualibjava.c）；确认派发用 `LUA_LOCK` | 小 |
| Java 侧 (`LuaAgent`/`AgentTask`/`AsyncRunner`) | **无需改动**（complete 回调签名不变） | 无 |
| 测试/文档 | `CallbackTest.java`、更新 `callback.md`/`AgentV2.md` | 中 |

> Java 侧零改动是方案最大的便利点：所有回调逻辑收口在 native 的 complete 入口。

---

## 5. 具体实现方案

### 5.1 数据结构（`lualibjava_internal.h`）

```c
typedef struct PromiseEntry {
    int id;
    lua_State* co;        // 保留：协程 await 用
    lua_State* owner;     // NEW：创建/注册回调所属的 Lua 状态
    int callbackRef;      // NEW：回调闭包在 owner 注册表中的引用，LUA_NOREF 表示无
    int done;
    char* result;         // 序列化结果串（strdup）
    struct PromiseEntry* next;
} PromiseEntry;
```

新增共享函数声明：

```c
// lualib_async.c：把序列化结果串按 S/I/N/B/O/E/M 前缀压入 L 栈（返回压入个数）
extern int push_parsed_result(lua_State* L, const char* r);
// lualibjava.c：注册完成回调（java.onComplete）
extern int java_onComplete(lua_State* L);
```

### 5.2 共享解析函数（`lualib_async.c`）

把 `java_checkPromise` 里 `switch (r[0])` 的解析逻辑原样抽成：

```c
int push_parsed_result(lua_State* L, const char* r) {
    int pushed = 0;
    switch (r[0]) {
        case 'M': /* ...多值，与现逻辑一致... */
        case 'S': lua_pushstring(L, r+2); pushed=1; break;
        case 'I': lua_pushinteger(L, atoll(r+2)); pushed=1; break;
        case 'N': lua_pushnumber(L, atof(r+2)); pushed=1; break;
        case 'B': lua_pushboolean(L, (r[2]=='t'||r[2]=='1')); pushed=1; break;
        case 'O': lua_pushinteger(L, atoi(r+2)); pushed=1; break;   // 对象 id
        case 'E': lua_pushstring(L, r+2); pushed=1; break;          // 错误串
        default:  lua_pushnil(L); pushed=1; break;
    }
    return pushed;
}
```

`java_checkPromise` 改为：

```c
int java_checkPromise(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* e = promise_registry;
    while (e) { if (e->id == id) break; e = e->next; }
    if (!e) { pthread_mutex_unlock(&promise_mutex);
              lua_pushboolean(L, 0); lua_pushnil(L); return 2; }

    lua_pushboolean(L, e->done);
    int n = 1;
    if (e->done && e->result) n += push_parsed_result(L, e->result);
    else lua_pushnil(L);

    // 修复泄漏：已消费（done）且未注册回调 → 立即清理
    if (e->done && e->callbackRef == LUA_NOREF) { /* 从链表摘除并 free(e) */ }
    pthread_mutex_unlock(&promise_mutex);
    return n;
}
```

### 5.3 `java_onComplete`（`lualibjava.c`，新增注册函数）

```c
static int java_onComplete(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* e = promise_registry;
    while (e) { if (e->id == id) break; e = e->next; }
    if (!e) { pthread_mutex_unlock(&promise_mutex);
              return luaL_error(L, "promise not found: %d", id); }

    // 同一 promise 重复 onComplete：unref 旧回调（后者覆盖）
    if (e->callbackRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, e->callbackRef);
    e->owner = L;
    lua_pushvalue(L, 2);
    e->callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

    // 竞态：任务可能已完成 → 拷贝字段后立即派发
    int alreadyDone = e->done;
    char* result = alreadyDone && e->result ? strdup(e->result) : NULL;
    pthread_mutex_unlock(&promise_mutex);

    if (alreadyDone) dispatch_callback(e->owner, e->callbackRef, result);  // §5.4
    if (result) free(result);
    return 0;
}
```

### 5.4 回调派发（`lualib_async.c`，核心）

在 `Java_com_luajava_LuaAgent_complete` 中追加派发（**锁序正确**）：

```c
void dispatch_callback(lua_State* owner, int cbRef, const char* result) {
    if (cbRef == LUA_NOREF || !owner) return;
    LUA_LOCK();                                    // ① 只持 lua_mutex，不再持 promise_mutex
    int base = lua_gettop(owner);                  // ①' 记录栈底：函数可能被工作线程（栈空）
                                                   //     或 C 函数内部（栈上有遗留参数）调用
    lua_rawgeti(owner, LUA_REGISTRYINDEX, cbRef);  // ② 取回调闭包
    if (lua_isfunction(owner, -1)) {
        // ③ 参数：err, result...
        if (result && result[0] == 'E') lua_pushstring(owner, result + 2);  // err
        else { lua_pushnil(owner); if (result) push_parsed_result(owner, result); }
        // ④ 调用：nargs 必须相对 base 计算，否则会把调用方栈上遗留的值误当参数
        int nargs = lua_gettop(owner) - (base + 1);
        if (lua_pcall(owner, nargs, 0, 0) != LUA_OK) {   // ④ 调用
            fprintf(stderr, "java.onComplete callback error: %s\n",
                    lua_tostring(owner, -1));
            lua_pop(owner, 1);
        }
        luaL_unref(owner, LUA_REGISTRYINDEX, cbRef);      // ⑤ 清理引用
    } else {
        lua_pop(owner, 1);
    }
    LUA_UNLOCK();
}
```

`complete` 中的改动（读锁 → 拷贝 → 解锁 → 派发）：

```c
JNIEXPORT void JNICALL Java_com_luajava_LuaAgent_complete
  (JNIEnv* env, jclass cls, jint pid, jstring result) {
    const char* s = (*env)->GetStringUTFChars(env, result, NULL);
    lua_State* owner = NULL; int cbRef = LUA_NOREF; char* copy = NULL;

    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* e = promise_registry;
    while (e) { if (e->id == pid) break; e = e->next; }
    if (e) {
        if (e->result) free(e->result);
        e->result = strdup(s); e->done = 1;
        owner = e->owner; cbRef = e->callbackRef;
        if (cbRef != LUA_NOREF) copy = strdup(s);   // 派发用副本
    }
    pthread_mutex_unlock(&promise_mutex);          // ★ 先解锁，再进 Lua

    if (copy) { dispatch_callback(owner, cbRef, copy); free(copy); }
    (*env)->ReleaseStringUTFChars(env, result, s);
}
```

**锁序总结**：
- 主线程路径：`lua_mutex` →（调用 checkPromise 等）→ `promise_mutex`。
- 工作线程路径：`promise_mutex`（只拷贝，不调 Lua）→ 释放 → `lua_mutex`（派发）。
- 两条路径都**不存在反向持锁**，无死锁。

### 5.5 状态关闭清理（`luajava.c` 的 `_close` 或 `lualibjava.c`）

`LuaRuntime.close()` → native `_close` 时，遍历 `promise_registry`，把所有 `owner == 该状态` 的条目：

- `luaL_unref(L, LUA_REGISTRYINDEX, entry->callbackRef)` 置 `LUA_NOREF`；
- 若已 `done`，直接摘除并 `free`；
- 未 done 且无回调的条目可保留（后续 complete 无副作用，仅写 result；或同样清理并置 `done` 防写）。

> 注意：`_close` 本身已持 `lua_mutex`（或应持），遍历注册表需再取 `promise_mutex`，锁序仍符合 §5.4 规则。

### 5.6 生命周期与内存（顺带修复现状泄漏）

统一规则（写入文档与代码注释）：

1. 有回调：`dispatch_callback` 派发后，`luaL_unref` + 从链表摘除 + `free(entry->result)` + `free(entry)`。
2. 无回调：`checkPromise` 首次读到 `done` 时清理（同 §5.2）。
3. `entry->result` 统一 `strdup`/`free` 配对；`java_complete`（手动）里现存的三段重复写 result 代码可顺手收敛为一段。
4. 对象池侧（`O:` oid → `LuaAgent.registerObject`）已有 `WeakReference` + `MAX_REGISTERED_OBJECTS` 上限与 `cleanupStaleReferences`，无需改动。

### 5.7 注册表与导出

- `lualibjava.c` 的 `javalib[]` 增加：`{"onComplete", java_onComplete}`。
- `lualibjava_internal.h` 声明 `java_onComplete` 与 `push_parsed_result`。
- `CMakeLists.txt` 无需改动（文件不新增）。

---

## 6. 边界情况与风险

| 场景 | 行为/风险 | 处置 |
|---|---|---|
| 任务完成在 `onComplete` 之前 | 回调永不触发 | `java_onComplete` 检测 `done` 立即派发（§5.3） |
| 同一 id 多次 `onComplete` | 回调被覆盖 | 后者覆盖前者（unref 旧的），文档说明 |
| 回调与 `checkPromise` 混用同一 id | 二者都读同一 `result`，语义幂等 | 允许；但**清理只发生一次**（回调派发时清理；若先被 checkPromise 消费且无回调才由它清理）。文档建议一 id 一风格 |
| 回调里再调用 Java（`runAsync`/同步调用） | `lua_mutex` 为递归锁 | 重入安全，无死锁（已有先例） |
| 回调里阻塞（`Thread.sleep`/长计算） | 占用池线程 | 文档警告：回调须快速返回；重活再提交异步任务 |
| 回调抛 Lua 错误 | `lua_pcall` 捕获 | 打 stderr，不崩溃、不影响结果写回 |
| `O:` 对象结果 | 回调收到整数 oid | 与 `checkPromise` 一致；用户 `java.getObject(oid)` 取回 |
| owner 状态在完成前被 `close()` | 引用悬空 | `_close` 统一 unref（§5.5）；派发前检查 `cbRef != LUA_NOREF` |
| 多 `LuaRuntime` 实例 | 注册表全局共享 | `owner` 字段定位归属状态；跨状态回调不支持（无意义） |
| 超时/取消（`submitTaskFuture`/`cancelTask`） | complete 会收到 `E:Task timeout...` | 回调正常收到 err，无需特殊处理 |

---

## 7. 测试计划

新增 `test/CallbackTest.java`（复用 `BaseTest` 模式），覆盖：

1. `callbackStaticMethod`：`onComplete` 回调收到 `parseInt('42')` → 断言值。
2. `callbackConstructor`：`onComplete` 收到 oid → `java.getObject` → 调用实例方法。
3. `callbackError`：类不存在 → 回调 `err` 为 `E:...`。
4. `callbackAfterDone`：先完成再 `onComplete`，立即触发。
5. `callbackThenCheckPromise`：回调已触发后 `checkPromise` 仍返回一致值（或按 §6 语义断言清理后为 `false,nil`）。
6. `callbackReentrant`：回调内再 `runAsync` 新任务（验证递归锁）。
7. `callbackMultipleValues`：多返回值经回调分发。
8. 既有 `PromiseTest`/`AsyncTest`/`AgentTest` 保持全绿（回归）。

---

## 8. 结论与建议

- **可行**。核心改造集中在 `lualib_async.c`（派发 + 解析复用 + 清理）、`lualibjava.c`（`onComplete` 注册 + 字段初始化）、`lualibjava_internal.h`（结构扩展）、`luajava.c`（close 清理）四处，**Java 侧零改动**。
- **建议落地顺序**：① 抽 `push_parsed_result` 并让 `checkPromise` 复用 → ② 扩 `PromiseEntry` + `java_onComplete` → ③ complete 追加派发 → ④ `_close` 清理 → ⑤ `CallbackTest` + 文档（`AgentV2.md`、`callback.md`）。
- **保留 `checkPromise`** 完全符合需求：它是主线程/REPL 场景最直观的轮询手段，回调则适合事件驱动 / 不想占主线程的场景，二者结果语义统一（同一解析函数），用户自选。
- **可选增强**（后续迭代，非本次必需）：`runAsync(..., cb)` 语法糖；回调内自动把 `O:` 解包为 userdata；`java.onError` 单独错误回调。

*本文基于仓库当前实现（v2.2.4）撰写。*
