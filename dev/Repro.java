package dev;

import com.luajava.LuaRuntime;
import com.luajava.LuaFunctionObj;

import java.lang.reflect.Field;
import java.lang.ref.WeakReference;
import java.util.*;

/**
 * 代码审查复现实验。
 * 每个 scenario 通过参数选择，且在独立 JVM 中运行，避免原生崩溃相互污染结果。
 *
 * 用法: java -Dluajava.library.path=<abs>/build/luajava.so \
 *           -cp out:dev dev.Repro <scenario>
 *
 * scenarios:
 *   basic        基线：确认 doString/callFunction 可用
 *   split_sync   同步调用返回数组的方法 String.split -> 触发 new_java_object_ud 数组分支
 *   split_async  异步 split -> getObject(oid) 触发数组分支
 *   async_gc     异步构造 String 对象，验证弱引用是否会使其在 getObject 前被 GC
 */
public class Repro {
    static final String JAVA = "java = require 'java'; String = java.import('java.lang.String')";

    public static void main(String[] args) throws Exception {
        String scenario = (args.length > 0) ? args[0] : "basic";
        switch (scenario) {
            case "basic":      basic(); break;
            case "split_sync": split_sync(); break;
            case "split_async":split_async(); break;
            case "async_gc":   async_gc((args.length > 1) ? Integer.parseInt(args[1]) : 200); break;
            case "ctor1":      ctor1(); break;
            case "pool":       pool(); break;
            case "pin":        pin(); break;
            default: System.err.println("unknown scenario: " + scenario);
        }
    }

    // ================= basic =================
    static void basic() throws Exception {
        try (LuaRuntime L = new LuaRuntime()) {
            L.doString("function add(a,b) return a+b end");
            Object r = L.callFunction("add", 3, 5);
            System.out.println("basic add(3,5) = " + r);
            L.doString(JAVA);
            L.doString("function mk() return String:new('Hello World') end");
            Object o = L.callFunction("mk");
            System.out.println("basic String:new -> " + (o == null ? "null" : o.getClass().getName()));
            System.out.println("basic OK");
        }
    }

    // ================= split_sync =================
    // 同步调用实例方法返回 String[]（数组）-> push_boxed_object -> new_java_object_ud 数组分支
    static void split_sync() throws Exception {
        try (LuaRuntime L = new LuaRuntime()) {
            L.doString(JAVA);
            L.doString("function split() local s = String:new('a,b,c') return s:split(',') end");
            System.out.println("split_sync: about to call s:split(',') ...");
            Object arr = L.callFunction("split");
            System.out.println("split_sync returned: " + (arr == null ? "NULL" : arr));
            System.out.println("split_sync OK (no crash)");
        }
    }

    // ================= split_async =================
    // java.runAsyncObj -> split 返回数组，序列化为 O:<id>，再 java.getObject(<id>)
    static void split_async() throws Exception {
        try (LuaRuntime L = new LuaRuntime()) {
            L.doString(JAVA);
            L.doString(
                "function asplit()\n" +
                "  local s = String:new('a,b,c')\n" +
                "  local id = java.promise()\n" +
                "  java.runAsyncObj(id, s, 'split', ',')\n" +
                "  return id\n" +
                "end\n" +
                "function poll(id)\n" +
                "  local done, oid = java.checkPromise(id)\n" +
                "  return done, oid\n" +
                "end");
            Object id = L.callFunction("asplit");
            System.out.println("split_async: task id = " + id);
            // 轮询
            boolean done = false; Object oid = null;
            for (int i = 0; i < 200 && !done; i++) {
                Object[] r = L.callFunctionMultiple("poll", id);
                done = (Boolean) r[0];
                if (done) oid = r.length > 1 ? r[1] : null;
                if (!done) Thread.sleep(10);
            }
            System.out.println("split_async: done=" + done + " oid=" + oid);
            if (done && oid != null) {
                L.doString("function getit(oid) local a = java.getObject(oid) return a end");
                System.out.println("split_async: about to java.getObject(oid) ...");
                Object a = L.callFunction("getit", oid);
                System.out.println("split_async getObject returned: " + a);
            }
            System.out.println("split_async OK (no crash)");
        }
    }

    // ================= ctor1 : 单个静态 runAsync 构造 =================
    static void ctor1() throws Exception {
        try (LuaRuntime L = new LuaRuntime()) {
            L.doString(JAVA);
            L.doString(
                "function ctor1()\n" +
                "  local id = java.promise()\n" +
                "  java.runAsync(id, 'java.lang.String', 'new', 'Hello')\n" +
                "  print('MT-start id='..id)\n" +
                "  local passes = 0\n" +
                "  while passes < 200000 do\n" +
                "    passes = passes + 1\n" +
                "    local done, oid = java.checkPromise(id)\n" +
                "    if done then print('MT-DONE passes='..passes..' oid='..tostring(oid)); return end\n" +
                "  end\n" +
                "  print('MT-TIMEOUT')\n" +
                "end");
            L.callFunction("ctor1");
            System.out.println("ctor1 done");
        }
    }

    // ================= pool : 纯 Java 层并发压测（不经过 Lua / promise） =================
    // 直接提交 N 个 AgentTask 到 LuaAgent 线程池并等待，隔离"线程池自身"与"promise 回传"问题。
    static void pool() throws Exception {
        // 隔离线程池测试需加载原生库（complete 是 native 方法），否则 UnsatisfiedLinkError
        String lib = System.getProperty("luajava.library.path");
        if (lib != null && !lib.isEmpty()) System.load(lib);
        com.luajava.LuaAgent.init();
        System.out.println("before: " + com.luajava.LuaAgent.getExecutorStats());
        String[] args0 = new String[0];
        java.util.concurrent.Future<String>[] fs = new java.util.concurrent.Future[8];
        for (int i = 0; i < 8; i++) {
            fs[i] = com.luajava.LuaAgent.submitTaskFuture(new com.luajava.AgentTask(1000 + i, "java.lang.Object", "hashCode", args0));
        }
        int ok = 0;
        for (int i = 0; i < 8; i++) {
            try {
                String r = fs[i].get(5, java.util.concurrent.TimeUnit.SECONDS);
                ok++;
            } catch (Exception e) {
                System.out.println("pool task " + i + " EXC: " + e);
            }
        }
        System.out.println("pool completed=" + ok + "/8 ; " + com.luajava.LuaAgent.getExecutorStats());
        System.out.println("pool " + (ok == 8 ? "OK (all tasks done)" : "FAIL (some tasks hung)"));
        com.luajava.LuaAgent.shutdownNow();
    }

    // ================= pin : 直接验证异步对象强引用在 GC 后仍存活 =================
    // 不经过异步调度，只验证 registerObjectStrong + getObject 的生命周期修复。
    static void pin() throws Exception {
        Object o = new String("pinned-hello");
        int id = com.luajava.LuaAgent.registerObjectStrong(o);
        o = null;
        for (int i = 0; i < 5; i++) {
            System.gc();
            java.util.ArrayList<byte[]> junk = new java.util.ArrayList<>();
            for (int j = 0; j < 2000; j++) junk.add(new byte[4096]);
            Thread.sleep(20);
        }
        Object got = com.luajava.LuaAgent.getObject(id);
        System.out.println("pin: getObject after GC -> " + (got == null ? "NULL (BUG: collected)" : "alive: " + got));
        System.out.println("pin " + (got != null ? "OK (strong ref keeps object alive)" : "FAIL"));
    }
    // 异步构造 N 个 String，任务完成后仅靠 LuaAgent 的 WeakReference 持有。
    // 基线（GC前）getObject 应全部存活；强制 GC 后验证是否被回收 -> 说明异步结果生命周期不可靠。
    static void async_gc(int N) throws Exception {
        try (LuaRuntime L = new LuaRuntime()) {
            L.doString(JAVA);
            L.doString(
                "tids = {}\n" +
                "function submit_all(N)\n" +
                "  for i = 1, N do\n" +
                "    local id = java.promise()\n" +
                "    tids[i] = id\n" +
                "    java.runAsync(id, 'java.lang.String', 'new', 'obj' .. i)\n" +
                "  end\n" +
                "  return N\n" +
                "end\n" +
                "function poll_all(N)\n" +
                "  local done_all = false\n" +
                "  local passes = 0\n" +
                "  while not done_all and passes < 200000 do\n" +
                "    passes = passes + 1\n" +
                "    done_all = true\n" +
                "    local dc = 0\n" +
                "    for i = 1, N do\n" +
                "      if java.checkPromise(tids[i]) then dc = dc + 1 else done_all = false end\n" +
                "    end\n" +
                "    java.yield(2)\n" +
                "    if passes % 5000 == 0 then io.write('poll-pass '..passes..' done_count='..dc..'/'..N..'\\n') io.flush() end\n" +
                "  end\n" +
                "  print('POLL_EXIT done_all='..tostring(done_all)..' passes='..passes)\n" +
                "  task_oids = {}\n" +
                "  for i = 1, N do\n" +
                "    local d, oid = java.checkPromise(tids[i])\n" +
                "    task_oids[i] = oid\n" +
                "  end\n" +
                "  return N\n" +
                "end\n" +
                "function count_alive()\n" +
                "  local alive = 0\n" +
                "  for i = 1, #task_oids do\n" +
                "    local o = java.getObject(task_oids[i])\n" +
                "    if o ~= nil then alive = alive + 1 end\n" +
                "  end\n" +
                "  return alive\n" +
                "end");
            int spawned = (Integer) L.callFunction("submit_all", N);
            System.out.println("async_gc: submitted=" + spawned);
            System.out.println("async_gc: " + com.luajava.LuaAgent.getTaskStats());
            System.out.println("async_gc: " + com.luajava.LuaAgent.getExecutorStats());
            Thread.sleep(3000);
            System.out.println("async_gc after 3s: " + com.luajava.LuaAgent.getTaskStats());
            System.out.println("async_gc after 3s: " + com.luajava.LuaAgent.getExecutorStats());
            L.callFunction("poll_all", N);
            System.out.println("async_gc: poll_all returned");
        }
    }
}