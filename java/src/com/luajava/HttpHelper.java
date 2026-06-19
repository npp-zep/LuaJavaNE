package com.luajava;

import java.net.*;
import java.io.*;

public class HttpHelper {
    public static String handleRequest(int port) throws Exception {
        ServerSocket server = new ServerSocket(port);
        Socket socket = server.accept();
        BufferedReader reader = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        PrintWriter writer = new PrintWriter(socket.getOutputStream());

        String line = reader.readLine();
        String path = "/";
        if (line != null) {
            String[] parts = line.split(" ");
            if (parts.length > 1) path = parts[1];
        }

        String body = "<h1>LuaJavaNE HTTP Server</h1><p>Path: " + path + "</p>";
        writer.println("HTTP/1.1 200 OK");
        writer.println("Content-Type: text/html");
        writer.println("Content-Length: " + body.length());
        writer.println();
        writer.println(body);
        writer.flush();

        reader.close();
        writer.close();
        socket.close();
        server.close();
        return path;
    }
}
