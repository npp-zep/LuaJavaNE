package com.luajava.compat;

import java.net.*;
import java.io.*;

public class IOCompat {
    // ---- ServerSocket ----
    public static ServerSocket serverSocket(int port) throws Exception {
        return new ServerSocket(port);
    }
    public static Socket serverAccept(ServerSocket server) throws Exception {
        return server.accept();
    }
    public static int serverGetPort(ServerSocket server) { return server.getLocalPort(); }
    public static void serverClose(ServerSocket server) throws Exception { server.close(); }

    // ---- Socket ----
    public static InputStream socketGetInput(Socket socket) throws Exception {
        return socket.getInputStream();
    }
    public static OutputStream socketGetOutput(Socket socket) throws Exception {
        return socket.getOutputStream();
    }
    public static void socketClose(Socket socket) throws Exception { socket.close(); }

    // ---- InputStream ----
    public static int inputRead(InputStream in) throws Exception { return in.read(); }
    public static int inputReadBuf(InputStream in, byte[] buf) throws Exception {
        return in.read(buf);
    }
    public static void inputClose(InputStream in) throws Exception { in.close(); }

    // ---- OutputStream ----
    public static void outputWrite(OutputStream out, byte[] buf) throws Exception {
        out.write(buf);
    }
    public static void outputFlush(OutputStream out) throws Exception { out.flush(); }
    public static void outputClose(OutputStream out) throws Exception { out.close(); }
}
