package com.luajava;

public class AgentTask {
    int pid;
    String className;
    String methodName;
    String[] args;

    public AgentTask(int pid, String cls, String mtd, String[] args) {
        this.pid = pid;
        this.className = cls;
        this.methodName = mtd;
        this.args = args;
    }
}
