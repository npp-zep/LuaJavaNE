package com.luajava.compat;

import java.io.*;

public class ProcessCompat {
    public static String exec(String command) throws Exception {
        Process p = Runtime.getRuntime().exec(command);
        BufferedReader reader = new BufferedReader(new InputStreamReader(p.getInputStream()));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = reader.readLine()) != null) sb.append(line).append("\n");
        reader.close();
        p.waitFor();
        return sb.toString();
    }
    public static int execExitCode(String command) throws Exception {
        Process p = Runtime.getRuntime().exec(command);
        return p.waitFor();
    }
}
