package com.luajava;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

/**
 * java.onComplete(id, callback) 回调机制的测试。
 * 回调在后台工作线程（LuaAgent 线程池）上执行，测试通过轮询 Lua 全局字符串变量
 * 等待回调完成（利用递归 lua_mutex 保证主线程与工作线程对同一 Lua 状态串行访问）。
 */
public class CallbackTest extends BaseTest {

    /** 轮询等待 Lua 全局字符串变量非 nil（带超时），返回其值 */
    private String awaitGlobal(String name, long timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        String v;
        while ((v = L.getGlobal(name)) == null) {
            if (System.currentTimeMillis() >= deadline) break;
            try { Thread.sleep(10); } catch (InterruptedException e) { break; }
        }
        return v;
    }

    @Test
    void callbackStaticMethod() {
        L.doString("result = nil");
        L.doString("id = java.promise()");
        L.doString("java.runAsync(id, 'java.lang.Integer', 'parseInt', '42')");
        L.doString("java.onComplete(id, function(err, v) result = tostring(v) end)");
        assertEquals("42", awaitGlobal("result", 3000));
    }

    @Test
    void callbackConstructor() {
        L.doString("result = nil");
        L.doString("id = java.promise()");
        L.doString("java.runAsync(id, 'java.lang.String', 'new', 'Hello CB')");
        L.doString(
            "java.onComplete(id, function(err, oid)\n" +
            "  if oid and type(oid) == 'number' then\n" +
            "    local obj = java.getObject(oid)\n" +
            "    result = tostring(obj:length())\n" +
            "  else\n" +
            "    result = 'FAIL:' .. tostring(err)\n" +
            "  end\n" +
            "end)");
        assertEquals("8", awaitGlobal("result", 3000));   // "Hello CB".length() == 8
    }

    @Test
    void callbackError() {
        L.doString("result = nil");
        L.doString("id = java.promise()");
        L.doString("java.runAsync(id, 'java.lang.NonExistent', 'foo', '')");
        L.doString("java.onComplete(id, function(err, v) result = tostring(err) end)");
        String err = awaitGlobal("result", 3000);
        assertNotNull(err, "callback should deliver error");
        assertTrue(err.contains("ClassNotFoundException"), "got: " + err);
    }

    @Test
    void callbackAfterDone() {
        // 任务先完成（用 java.complete 手动完成），再注册回调 → then 检测到 done 立即派发
        L.doString("result = nil");
        L.doString("id = java.promise()");
        L.doString("java.complete(id, 'hello')");
        L.doString("java.onComplete(id, function(err, v) result = tostring(v) end)");
        assertEquals("hello", awaitGlobal("result", 1000));
    }

    @Test
    void callbackThenCheckPromise() {
        // 注册回调后，回调派发会消费条目；再轮询 checkPromise 返回 false,nil
        L.doString("result = nil");
        L.doString("id = java.promise()");
        L.doString("java.runAsync(id, 'java.lang.Integer', 'parseInt', '42')");
        L.doString("java.onComplete(id, function(err, v) result = tostring(v) end)");
        assertNotNull(awaitGlobal("result", 3000));
        L.doString("local d, v = java.checkPromise(id); done = tostring(d); val = tostring(v)");
        assertEquals("false", L.getGlobal("done"));
        assertEquals("nil", L.getGlobal("val"));
    }

    @Test
    void callbackReentrant() {
        // 回调内再次 runAsync + onComplete（验证递归锁重入）
        L.doString("result = nil");
        L.doString("id = java.promise()");
        L.doString("java.runAsync(id, 'java.lang.Integer', 'parseInt', '21')");
        L.doString(
            "java.onComplete(id, function(err, v)\n" +
            "  local id2 = java.promise()\n" +
            "  java.runAsync(id2, 'java.lang.Integer', 'parseInt', tostring(v) .. '0')\n" +
            "  java.onComplete(id2, function(e2, v2) result = tostring(v2) end)\n" +
            "end)");
        assertEquals("210", awaitGlobal("result", 3000));   // 21 * 10
    }

    @Test
    void callbackNotFound() {
        Exception ex = assertThrows(RuntimeException.class, () -> {
            L.doString("java.onComplete(99999, function() end)");
        });
        assertTrue(ex.getMessage().contains("promise not found"));
    }
}
