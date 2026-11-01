package com.luajava;

public class AgentTask {
    int pid;
    String className;
    String methodName;
    String[] args;
    Object instance;  // 实例对象，静态方法时为 null
    boolean expandArrays;  // true 时 Object[] 结果展开为多值

    // 静态方法构造
    public AgentTask(int pid, String cls, String mtd, String[] args) {
        this(pid, cls, mtd, args, false);
    }

    public AgentTask(int pid, String cls, String mtd, String[] args, boolean expandArrays) {
        this.pid = pid;
        this.className = cls;
        this.methodName = mtd;
        this.args = args;
        this.instance = null;
        this.expandArrays = expandArrays;
    }

    // 实例方法构造
    public AgentTask(int pid, Object instance, String mtd, String[] args) {
        this(pid, instance, mtd, args, false);
    }

    public AgentTask(int pid, Object instance, String mtd, String[] args, boolean expandArrays) {
        this.pid = pid;
        this.instance = instance;
        this.methodName = mtd;
        this.args = args;
        this.className = instance.getClass().getName();
        this.expandArrays = expandArrays;
    }
}
