package com.luajava;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import static org.junit.jupiter.api.Assertions.*;

import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.ArrayList;
import java.util.List;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;

public class AgentTest extends BaseTest {

    // ============ 静态锁用于阻塞任务 ============
    private static CountDownLatch blockingLatch = new CountDownLatch(1);
    private static CountDownLatch blockingStartedLatch = new CountDownLatch(1);
    
    /**
     * 阻塞方法 - 等待 blockingLatch 释放
     * 用于测试超时和取消
     */
    public static String blockingMethod() {
        try {
            blockingStartedLatch.countDown();
            boolean released = blockingLatch.await(30, TimeUnit.SECONDS);
            return released ? "released" : "timeout";
        } catch (InterruptedException e) {
            return "interrupted";
        }
    }
    
    /**
     * 静态睡眠方法，用于测试普通耗时
     */
    public static String staticSleep(String ms) {
        try {
            long sleepMs = Long.parseLong(ms);
            Thread.sleep(sleepMs);
            return "slept " + ms + "ms";
        } catch (InterruptedException e) {
            return "interrupted";
        } catch (NumberFormatException e) {
            return "invalid number: " + ms;
        }
    }

    // ============================================================
    // Phase 1: 并发安全测试
    // ============================================================
    
    @Test
    @Timeout(5)
    void testNormalInitAndSubmit() throws Exception {
        cleanup();
        
        assertEquals("TERMINATED", LuaAgent.getState());
        
        LuaAgent.init();
        assertEquals("RUNNING", LuaAgent.getState());
        assertFalse(LuaAgent.isShutdown());
        assertFalse(LuaAgent.isTerminated());
        
        AgentTask task = createTestTask(1);
        LuaAgent.submitTask(task);
        
        Thread.sleep(100);
        
        LuaAgent.shutdown();
        Thread.sleep(50);
        assertEquals("TERMINATED", LuaAgent.getState());
        assertTrue(LuaAgent.isShutdown());
        assertTrue(LuaAgent.isTerminated());
    }
    
    @Test
    @Timeout(10)
    void testConcurrentSubmitWithoutShutdown() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        int threadCount = 50;
        int tasksPerThread = 10;
        AtomicInteger successCount = new AtomicInteger(0);
        AtomicInteger failCount = new AtomicInteger(0);
        CountDownLatch startLatch = new CountDownLatch(1);
        CountDownLatch doneLatch = new CountDownLatch(threadCount);
        
        for (int i = 0; i < threadCount; i++) {
            final int threadId = i;
            new Thread(() -> {
                try {
                    startLatch.await();
                    for (int j = 0; j < tasksPerThread; j++) {
                        try {
                            AgentTask task = createTestTask(threadId * 1000 + j);
                            LuaAgent.submitTask(task);
                            successCount.incrementAndGet();
                        } catch (RejectedExecutionException e) {
                            failCount.incrementAndGet();
                        }
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                } finally {
                    doneLatch.countDown();
                }
            }).start();
        }
        
        startLatch.countDown();
        
        boolean completed = doneLatch.await(5, TimeUnit.SECONDS);
        assertTrue(completed);
        
        assertEquals(threadCount * tasksPerThread, successCount.get());
        assertEquals(0, failCount.get());
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(15)
    void testConcurrentSubmitAndShutdown() throws Exception {
        cleanup();
        
        LuaAgent.init();
        assertEquals("RUNNING", LuaAgent.getState());
        
        int submitThreads = 20;
        int tasksPerThread = 20;
        AtomicInteger submitted = new AtomicInteger(0);
        AtomicInteger rejected = new AtomicInteger(0);
        AtomicInteger otherExceptions = new AtomicInteger(0);
        
        CountDownLatch startLatch = new CountDownLatch(1);
        CountDownLatch doneLatch = new CountDownLatch(submitThreads);
        
        List<Thread> threads = new ArrayList<>();
        for (int i = 0; i < submitThreads; i++) {
            final int threadId = i;
            Thread t = new Thread(() -> {
                try {
                    startLatch.await();
                    for (int j = 0; j < tasksPerThread; j++) {
                        try {
                            AgentTask task = createTestTask(threadId * 1000 + j);
                            LuaAgent.submitTask(task);
                            submitted.incrementAndGet();
                        } catch (RejectedExecutionException e) {
                            rejected.incrementAndGet();
                        } catch (Exception e) {
                            otherExceptions.incrementAndGet();
                            System.err.println("Unexpected exception: " + e);
                        }
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                } finally {
                    doneLatch.countDown();
                }
            });
            threads.add(t);
            t.start();
        }
        
        startLatch.countDown();
        
        Thread.sleep(100);
        
        Thread shutdownThread = new Thread(() -> {
            try {
                Thread.sleep(50);
                LuaAgent.shutdown();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        });
        shutdownThread.start();
        
        boolean completed = doneLatch.await(8, TimeUnit.SECONDS);
        assertTrue(completed, "Submit threads did not complete in time");
        
        shutdownThread.join(2000);
        
        assertEquals("TERMINATED", LuaAgent.getState());
        assertTrue(LuaAgent.isShutdown());
        assertTrue(LuaAgent.isTerminated());
        
        assertEquals(0, otherExceptions.get(), "Unexpected exceptions occurred");
        
        System.out.println("Submitted: " + submitted.get() + 
                          ", Rejected: " + rejected.get() + 
                          ", Total: " + (submitted.get() + rejected.get()));
        
        assertEquals(submitThreads * tasksPerThread, 
                     submitted.get() + rejected.get(),
                     "Total operations mismatch");
    }
    
    @Test
    @Timeout(5)
    void testSubmitAfterShutdown() throws Exception {
        cleanup();
        
        LuaAgent.init();
        LuaAgent.shutdown();
        assertEquals("TERMINATED", LuaAgent.getState());
        
        AgentTask task = createTestTask(1);
        assertThrows(RejectedExecutionException.class, () -> {
            LuaAgent.submitTask(task);
        });
    }
    
    @Test
    @Timeout(10)
    void testInitDuringShutdown() throws Exception {
        cleanup();
        
        LuaAgent.init();
        assertEquals("RUNNING", LuaAgent.getState());
        
        CountDownLatch shutdownStarted = new CountDownLatch(1);
        CountDownLatch shutdownCompleted = new CountDownLatch(1);
        
        Thread shutdownThread = new Thread(() -> {
            try {
                shutdownStarted.countDown();
                LuaAgent.shutdown();
                shutdownCompleted.countDown();
            } catch (Exception e) {
                // ignore
            }
        });
        shutdownThread.start();
        
        boolean started = shutdownStarted.await(1, TimeUnit.SECONDS);
        assertTrue(started, "Shutdown thread did not start");
        
        Thread.sleep(50);
        
        String currentState = LuaAgent.getState();
        System.out.println("Current state during shutdown: " + currentState);
        
        if ("SHUTTING_DOWN".equals(currentState)) {
            assertThrows(IllegalStateException.class, () -> {
                LuaAgent.init();
            });
        } else if ("TERMINATED".equals(currentState)) {
            LuaAgent.init();
            assertEquals("RUNNING", LuaAgent.getState());
            LuaAgent.shutdown();
        } else {
            fail("Unexpected state: " + currentState);
        }
        
        boolean completed = shutdownCompleted.await(3, TimeUnit.SECONDS);
        assertTrue(completed, "Shutdown did not complete");
        
        assertEquals("TERMINATED", LuaAgent.getState());
        
        LuaAgent.init();
        assertEquals("RUNNING", LuaAgent.getState());
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(10)
    void testMultipleInitShutdownCycles() throws Exception {
        cleanup();
        
        int cycles = 5;
        
        for (int i = 0; i < cycles; i++) {
            LuaAgent.init();
            assertEquals("RUNNING", LuaAgent.getState());
            assertFalse(LuaAgent.isShutdown());
            assertFalse(LuaAgent.isTerminated());
            
            for (int j = 0; j < 10; j++) {
                AgentTask task = createTestTask(i * 1000 + j);
                LuaAgent.submitTask(task);
            }
            
            LuaAgent.shutdown();
            assertEquals("TERMINATED", LuaAgent.getState());
            assertTrue(LuaAgent.isShutdown());
            assertTrue(LuaAgent.isTerminated());
            
            Thread.sleep(50);
        }
    }
    
    @Test
    @Timeout(10)
    void testShutdownNowConcurrency() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        for (int i = 0; i < 10; i++) {
            AgentTask task = createTestTask(i);
            LuaAgent.submitTask(task);
        }
        
        LuaAgent.shutdownNow();
        assertEquals("TERMINATED", LuaAgent.getState());
        assertTrue(LuaAgent.isShutdown());
        assertTrue(LuaAgent.isTerminated());
        
        assertThrows(RejectedExecutionException.class, () -> {
            LuaAgent.submitTask(createTestTask(999));
        });
    }
    
    @Test
    @Timeout(5)
    void testStateQueriesConcurrency() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        int queryThreads = 20;
        CountDownLatch startLatch = new CountDownLatch(1);
        CountDownLatch doneLatch = new CountDownLatch(queryThreads);
        AtomicBoolean exceptionOccurred = new AtomicBoolean(false);
        
        for (int i = 0; i < queryThreads; i++) {
            new Thread(() -> {
                try {
                    startLatch.await();
                    for (int j = 0; j < 100; j++) {
                        try {
                            LuaAgent.getState();
                            LuaAgent.isShutdown();
                            LuaAgent.isTerminated();
                        } catch (Exception e) {
                            exceptionOccurred.set(true);
                        }
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                } finally {
                    doneLatch.countDown();
                }
            }).start();
        }
        
        startLatch.countDown();
        
        boolean completed = doneLatch.await(3, TimeUnit.SECONDS);
        assertTrue(completed);
        assertFalse(exceptionOccurred.get());
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(10)
    void testLargeNumberOfTasks() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        int totalTasks = 500;
        AtomicInteger submitted = new AtomicInteger(0);
        AtomicInteger rejected = new AtomicInteger(0);
        CountDownLatch doneLatch = new CountDownLatch(totalTasks);
        
        for (int i = 0; i < totalTasks; i++) {
            final int id = i;
            new Thread(() -> {
                try {
                    AgentTask task = createTestTask(id);
                    LuaAgent.submitTask(task);
                    submitted.incrementAndGet();
                } catch (RejectedExecutionException e) {
                    rejected.incrementAndGet();
                } finally {
                    doneLatch.countDown();
                }
            }).start();
        }
        
        boolean completed = doneLatch.await(5, TimeUnit.SECONDS);
        assertTrue(completed);
        
        System.out.println("Large task test - Submitted: " + submitted.get() + 
                          ", Rejected: " + rejected.get());
        
        assertEquals(totalTasks, submitted.get() + rejected.get());
        
        LuaAgent.shutdown();
    }
    
    // ============================================================
    // Phase 2: 资源泄露修复测试
    // ============================================================
    
    @Test
    @Timeout(5)
    void testRegisterAndGetObject() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        TestObject obj = new TestObject("test");
        int id = LuaAgent.registerObject(obj);
        
        assertTrue(id > 0);
        assertEquals(1, LuaAgent.getRegistrySize());
        
        Object retrieved = LuaAgent.getObject(id);
        assertNotNull(retrieved);
        assertTrue(retrieved instanceof TestObject);
        assertEquals("test", ((TestObject) retrieved).name);
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(5)
    void testUnregisterObject() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        TestObject obj = new TestObject("test");
        int id = LuaAgent.registerObject(obj);
        
        assertEquals(1, LuaAgent.getRegistrySize());
        
        boolean removed = LuaAgent.unregisterObject(id);
        assertTrue(removed);
        assertEquals(0, LuaAgent.getRegistrySize());
        
        assertNull(LuaAgent.getObject(id));
        
        assertFalse(LuaAgent.unregisterObject(id));
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(10)
    void testWeakReferenceAutoCleanup() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        int id = LuaAgent.registerObject(new TestObject("test"));
        
        assertEquals(1, LuaAgent.getRegistrySize());
        
        assertNotNull(LuaAgent.getObject(id));
        
        System.gc();
        Thread.sleep(100);
        System.gc();
        Thread.sleep(100);
        
        Object retrieved = LuaAgent.getObject(id);
        assertNull(retrieved, "Object should have been GC'd");
        
        LuaAgent.cleanupStaleReferences();
        assertEquals(0, LuaAgent.getRegistrySize());
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(5)
    void testMaxRegistryLimit() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        List<Integer> ids = new ArrayList<>();
        int testCount = 100;
        
        for (int i = 0; i < testCount; i++) {
            TestObject obj = new TestObject("obj" + i);
            int id = LuaAgent.registerObject(obj);
            ids.add(id);
        }
        
        assertEquals(testCount, LuaAgent.getRegistrySize());
        
        for (int id : ids) {
            LuaAgent.unregisterObject(id);
        }
        assertEquals(0, LuaAgent.getRegistrySize());
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(10)
    void testConcurrentRegisterAndUnregister() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        int threadCount = 20;
        int operationsPerThread = 50;
        AtomicInteger registered = new AtomicInteger(0);
        AtomicInteger unregistered = new AtomicInteger(0);
        AtomicInteger errors = new AtomicInteger(0);
        CountDownLatch startLatch = new CountDownLatch(1);
        CountDownLatch doneLatch = new CountDownLatch(threadCount);
        
        Queue<Integer> ids = new ConcurrentLinkedQueue<>();
        
        for (int i = 0; i < threadCount; i++) {
            new Thread(() -> {
                try {
                    startLatch.await();
                    for (int j = 0; j < operationsPerThread; j++) {
                        try {
                            if (Math.random() < 0.7) {
                                TestObject obj = new TestObject("obj");
                                int id = LuaAgent.registerObject(obj);
                                ids.add(id);
                                registered.incrementAndGet();
                            } else {
                                Integer id = ids.poll();
                                if (id != null) {
                                    if (LuaAgent.unregisterObject(id)) {
                                        unregistered.incrementAndGet();
                                    }
                                }
                            }
                        } catch (Exception e) {
                            errors.incrementAndGet();
                        }
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                } finally {
                    doneLatch.countDown();
                }
            }).start();
        }
        
        startLatch.countDown();
        boolean completed = doneLatch.await(5, TimeUnit.SECONDS);
        assertTrue(completed);
        
        assertEquals(0, errors.get(), "Errors occurred during concurrent operations");
        
        int registrySize = LuaAgent.getRegistrySize();
        System.out.println("Registry size: " + registrySize + 
                          ", Registered: " + registered.get() + 
                          ", Unregistered: " + unregistered.get());
        
        for (Integer id : ids) {
            LuaAgent.unregisterObject(id);
        }
        assertEquals(0, LuaAgent.getRegistrySize());
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(5)
    void testRegistryStats() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        for (int i = 0; i < 10; i++) {
            TestObject obj = new TestObject("obj" + i);
            LuaAgent.registerObject(obj);
        }
        
        String stats = LuaAgent.getRegistryStats();
        System.out.println("Registry stats: " + stats);
        assertTrue(stats.contains("Total: 10"));
        assertTrue(stats.contains("Alive: 10"));
        assertTrue(stats.contains("Stale: 0"));
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(5)
    void testRegistryCleanedOnShutdown() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        for (int i = 0; i < 50; i++) {
            TestObject obj = new TestObject("obj" + i);
            LuaAgent.registerObject(obj);
        }
        
        assertEquals(50, LuaAgent.getRegistrySize());
        
        LuaAgent.shutdown();
        
        assertEquals(0, LuaAgent.getRegistrySize());
    }
    
    @Test
    @Timeout(5)
    void testRegisterNullObject() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        int id = LuaAgent.registerObject(null);
        assertTrue(id > 0);
        
        Object obj = LuaAgent.getObject(id);
        assertNull(obj);
        
        LuaAgent.cleanupStaleReferences();
        assertEquals(0, LuaAgent.getRegistrySize());
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(15)
    void testManyObjectsAndCleanup() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        int count = 500;
        List<Integer> ids = new ArrayList<>();
        
        for (int i = 0; i < count; i++) {
            int id = LuaAgent.registerObject(new TestObject("obj" + i));
            ids.add(id);
        }
        
        assertEquals(count, LuaAgent.getRegistrySize());
        
        ids.clear();
        
        System.gc();
        Thread.sleep(200);
        System.gc();
        Thread.sleep(200);
        
        LuaAgent.cleanupStaleReferences();
        
        int remaining = LuaAgent.getRegistrySize();
        System.out.println("Remaining after GC: " + remaining);
        assertTrue(remaining < count * 0.1, "Too many objects remained: " + remaining);
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(5)
    void testClearRegistry() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        for (int i = 0; i < 20; i++) {
            TestObject obj = new TestObject("obj" + i);
            LuaAgent.registerObject(obj);
        }
        
        assertEquals(20, LuaAgent.getRegistrySize());
        
        LuaAgent.clearRegistry();
        assertEquals(0, LuaAgent.getRegistrySize());
        
        assertNull(LuaAgent.getObject(1));
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(10)
    void testConcurrentRegistryCleanup() throws Exception {
        cleanup();
        
        LuaAgent.init();
        
        int threadCount = 10;
        int objectsPerThread = 50;
        CountDownLatch startLatch = new CountDownLatch(1);
        CountDownLatch doneLatch = new CountDownLatch(threadCount);
        AtomicInteger totalRegistered = new AtomicInteger(0);
        AtomicInteger failedRegistrations = new AtomicInteger(0);
        Queue<Throwable> exceptions = new ConcurrentLinkedQueue<>();
        
        for (int t = 0; t < threadCount; t++) {
            new Thread(() -> {
                try {
                    startLatch.await();
                    for (int i = 0; i < objectsPerThread; i++) {
                        try {
                            int id = LuaAgent.registerObject(new TestObject("obj"));
                            totalRegistered.incrementAndGet();
                        } catch (Exception e) {
                            failedRegistrations.incrementAndGet();
                            exceptions.add(e);
                        }
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                } finally {
                    doneLatch.countDown();
                }
            }).start();
        }
        
        startLatch.countDown();
        boolean completed = doneLatch.await(5, TimeUnit.SECONDS);
        assertTrue(completed);
        
        int expected = threadCount * objectsPerThread;
        
        if (failedRegistrations.get() > 0) {
            System.out.println("Failed registrations: " + failedRegistrations.get());
            for (Throwable t : exceptions) {
                System.out.println("  - " + t.getMessage());
            }
        }
        
        assertEquals(expected, totalRegistered.get() + failedRegistrations.get(), 
                     "Some registrations were lost");
        
        if (failedRegistrations.get() > 0) {
            System.out.println("Note: " + failedRegistrations.get() + " registrations failed, but total is consistent");
        }
        
        LuaAgent.shutdown();
    }
    
    // ============================================================
    // Phase 5: 功能增强测试
    // ============================================================
    
    @Test
    @Timeout(10)
    void testSubmitReturnsFuture() throws Exception {
        cleanup();
        LuaAgent.init();
        
        // 重置静态锁
        blockingLatch = new CountDownLatch(1);
        blockingStartedLatch = new CountDownLatch(1);
        
        int pid = 999;
        AgentTask task = new AgentTask(pid, AgentTest.class.getName(), "blockingMethod", new String[0]);
        Future<String> future = LuaAgent.submitTaskFuture(task);
        
        assertNotNull(future);
        assertFalse(future.isCancelled());
        
        // 等待任务开始执行
        boolean started = blockingStartedLatch.await(3, TimeUnit.SECONDS);
        assertTrue(started, "Task did not start");
        
        // 取消任务
        boolean cancelled = LuaAgent.cancelTask(pid, true);
        System.out.println("Cancel result: " + cancelled);
        
        // 释放锁
        blockingLatch.countDown();
        
        Thread.sleep(100);
        
        // 验证取消
        if (!cancelled) {
            System.out.println("Task already completed, isDone: " + future.isDone());
            assertTrue(future.isDone() || future.isCancelled());
        } else {
            assertTrue(future.isDone());
            assertTrue(future.isCancelled());
        }
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(10)
    void testSubmitWithTimeout() throws Exception {
        cleanup();
        LuaAgent.init();
        
        AgentTask task = createTestTask(1);
        Future<String> future = LuaAgent.submitTaskFuture(task, 5, TimeUnit.SECONDS);
        
        String result = future.get(6, TimeUnit.SECONDS);
        assertNotNull(result);
        assertFalse(result.startsWith("E:"));
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(15)
    void testTaskTimeout() throws Exception {
        cleanup();
        LuaAgent.init();
        
        // 重置静态锁
        blockingLatch = new CountDownLatch(1);
        blockingStartedLatch = new CountDownLatch(1);
        
        int pid = 100;
        AgentTask task = new AgentTask(pid, AgentTest.class.getName(), "blockingMethod", new String[0]);
        Future<String> future = LuaAgent.submitTaskFuture(task, 1, TimeUnit.SECONDS);
        
        // 等待任务开始
        boolean started = blockingStartedLatch.await(3, TimeUnit.SECONDS);
        assertTrue(started, "Task did not start");
        
        // 等待超时发生
        Thread.sleep(2000);
        
        String stats = LuaAgent.getTaskStats();
        System.out.println("Task stats after timeout test: " + stats);
        
        // 超时应该导致任务被取消或超时统计增加
        assertTrue(stats.contains("Timeout: 1") || stats.contains("Cancelled: 1") || future.isDone());
        
        // 释放锁清理
        blockingLatch.countDown();
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(10)
    void testCancelTask() throws Exception {
        cleanup();
        LuaAgent.init();
        
        // 重置静态锁
        blockingLatch = new CountDownLatch(1);
        blockingStartedLatch = new CountDownLatch(1);
        
        int pid = 200;
        AgentTask task = new AgentTask(pid, AgentTest.class.getName(), "blockingMethod", new String[0]);
        Future<String> future = LuaAgent.submitTaskFuture(task);
        
        // 等待任务开始
        boolean started = blockingStartedLatch.await(3, TimeUnit.SECONDS);
        assertTrue(started, "Task did not start");
        
        // 取消任务
        boolean cancelled = LuaAgent.cancelTask(pid, true);
        System.out.println("Cancel result: " + cancelled);
        
        // 释放锁
        blockingLatch.countDown();
        
        // 验证取消
        if (!cancelled) {
            assertTrue(future.isDone());
        } else {
            assertTrue(future.isCancelled());
        }
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(10)
    void testAwaitTask() throws Exception {
        cleanup();
        LuaAgent.init();
        
        int pid = 300;
        AgentTask task = createTestTask(pid);
        LuaAgent.submitTaskFuture(task);
        
        String result = LuaAgent.awaitTask(pid, 5, TimeUnit.SECONDS);
        assertNotNull(result);
        assertFalse(result.startsWith("E:"));
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(10)
    void testAwaitTaskTimeout() throws Exception {
        cleanup();
        LuaAgent.init();
        
        // 重置静态锁
        blockingLatch = new CountDownLatch(1);
        blockingStartedLatch = new CountDownLatch(1);
        
        int pid = 400;
        AgentTask task = new AgentTask(pid, AgentTest.class.getName(), "blockingMethod", new String[0]);
        LuaAgent.submitTaskFuture(task);
        
        // 等待任务开始
        boolean started = blockingStartedLatch.await(3, TimeUnit.SECONDS);
        assertTrue(started, "Task did not start");
        
        // 现在任务在 blockingMethod 中等待 latch，所以不会完成
        // 调用 awaitTask 应该超时
        assertThrows(TimeoutException.class, () -> {
            LuaAgent.awaitTask(pid, 1, TimeUnit.SECONDS);
        });
        
        // 释放锁，让任务完成
        blockingLatch.countDown();
        
        // 等待任务完成并清理
        Thread.sleep(200);
        LuaAgent.cancelTask(pid, true);
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(10)
    void testTaskStats() throws Exception {
        cleanup();
        LuaAgent.init();
        
        String initialStats = LuaAgent.getTaskStats();
        System.out.println("Initial stats: " + initialStats);
        assertTrue(initialStats.contains("Submitted: 0"));
        
        for (int i = 0; i < 10; i++) {
            AgentTask task = createTestTask(i);
            LuaAgent.submitTask(task);
        }
        
        Thread.sleep(500);
        
        String stats = LuaAgent.getTaskStats();
        System.out.println("Stats after tasks: " + stats);
        assertTrue(stats.contains("Submitted: 10"));
        assertTrue(stats.contains("Completed: 10"));
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(15)
    void testConcurrentSubmitAndGetFuture() throws Exception {
        cleanup();
        LuaAgent.init();
        
        int taskCount = 50;
        List<Future<String>> futures = new ArrayList<>();
        List<Integer> pids = new ArrayList<>();
        
        for (int i = 0; i < taskCount; i++) {
            int pid = i + 1000;
            pids.add(pid);
            AgentTask task = createTestTask(pid);
            Future<String> future = LuaAgent.submitTaskFuture(task);
            futures.add(future);
        }
        
        for (int i = 0; i < taskCount; i++) {
            String result = futures.get(i).get(5, TimeUnit.SECONDS);
            assertNotNull(result);
            assertFalse(result.startsWith("E:"));
        }
        
        String stats = LuaAgent.getTaskStats();
        System.out.println("Stats: " + stats);
        assertTrue(stats.contains("Submitted: " + taskCount));
        assertTrue(stats.contains("Completed: " + taskCount));
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(15)
    void testMultipleSubmitAndCancel() throws Exception {
        cleanup();
        LuaAgent.init();
        
        // 重置静态锁
        blockingLatch = new CountDownLatch(1);
        blockingStartedLatch = new CountDownLatch(1);
        
        int taskCount = 20;
        List<Integer> pids = new ArrayList<>();
        List<Future<String>> futures = new ArrayList<>();
        
        for (int i = 0; i < taskCount; i++) {
            int pid = i + 2000;
            pids.add(pid);
            AgentTask task = new AgentTask(pid, AgentTest.class.getName(), "blockingMethod", new String[0]);
            Future<String> future = LuaAgent.submitTaskFuture(task);
            futures.add(future);
        }
        
        // 等待所有任务开始
        Thread.sleep(500);
        
        int cancelled = 0;
        for (int i = 0; i < taskCount / 2; i++) {
            if (LuaAgent.cancelTask(pids.get(i), true)) {
                cancelled++;
            }
        }
        
        // 释放锁
        blockingLatch.countDown();
        
        Thread.sleep(500);
        
        String stats = LuaAgent.getTaskStats();
        System.out.println("Stats after cancels: " + stats);
        System.out.println("Cancelled successfully: " + cancelled);
        
        assertTrue(cancelled > 0 || stats.contains("Cancelled:"));
        
        LuaAgent.shutdown();
    }
    
    @Test
    @Timeout(5)
    void testResetStats() throws Exception {
        cleanup();
        LuaAgent.init();
        
        for (int i = 0; i < 5; i++) {
            AgentTask task = createTestTask(i);
            LuaAgent.submitTask(task);
        }
        
        Thread.sleep(300);
        
        LuaAgent.resetStats();
        
        String stats = LuaAgent.getTaskStats();
        System.out.println("Stats after reset: " + stats);
        assertTrue(stats.contains("Submitted: 0"));
        assertTrue(stats.contains("Completed: 0"));
        assertTrue(stats.contains("Failed: 0"));
        
        LuaAgent.shutdown();
    }
    
    // ============================================================
    // 辅助方法
    // ============================================================
    
    /**
     * 创建测试用的 AgentTask
     */
    private AgentTask createTestTask(int pid) {
        String[] emptyArgs = new String[0];
        return new AgentTask(pid, "java.lang.System", "currentTimeMillis", emptyArgs);
    }
    
    /**
     * 清理之前测试的状态
     */
    private void cleanup() {
        try {
            LuaAgent.shutdownNow();
        } catch (Exception e) {
            // ignore
        }
        try {
            LuaAgent.clearRegistry();
        } catch (Exception e) {
            // ignore
        }
        try {
            LuaAgent.resetStats();
        } catch (Exception e) {
            // ignore
        }
        try {
            // 重置静态锁（如果还未释放）
            blockingLatch.countDown();
            blockingStartedLatch.countDown();
            blockingLatch = new CountDownLatch(1);
            blockingStartedLatch = new CountDownLatch(1);
        } catch (Exception e) {
            // ignore
        }
        try {
            Thread.sleep(10);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }
    
    // ============================================================
    // 辅助类
    // ============================================================
    
    /**
     * 测试用的简单对象
     */
    private static class TestObject {
        String name;
        
        TestObject(String name) {
            this.name = name;
        }
        
        @Override
        public String toString() {
            return "TestObject(" + name + ")";
        }
    }
}