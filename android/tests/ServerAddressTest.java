package com.smartwalk.poc;

import java.net.URI;

public final class ServerAddressTest {
    public static void main(String[] arguments) {
        ServerAddress emulator = ServerAddress.parse(" http://10.0.2.2:8000/ ");
        require(emulator != null, "Accept the Android Emulator host address");
        require(emulator.urlFor("/student").equals("http://10.0.2.2:8000/student"), "Build app URLs");
        require(emulator.allows(URI.create("http://10.0.2.2:8000/history")), "Allow same-origin API");
        require(!emulator.allows(URI.create("http://10.0.2.2:8001/status")), "Reject a different port");
        require(!emulator.allows(URI.create("https://10.0.2.2:8000/status")), "Reject a different protocol");
        require(ServerAddress.parse("http://192.168.1.10:8000") != null, "Accept a local Wi-Fi address");
        require(ServerAddress.parse("http://172.20.10.2:8000") != null, "Accept a mobile hotspot address");
        require(ServerAddress.parse("https://example.com") != null, "Accept HTTPS hostnames");
        for (String invalid : new String[] {
                "http://example.com", "http://8.8.8.8", "http://172.32.0.1", "http://192.168.1.1:0",
                "http://192.168.1.1:65536", "http://user@192.168.1.1", "http://192.168.1.1/status",
                "http://192.168.1.1?x=y", "ftp://192.168.1.1", "data:text/html,"}) {
            require(ServerAddress.parse(invalid) == null, "Reject invalid or nonlocal HTTP endpoint: " + invalid);
        }
        System.out.println("Server address checks passed.");
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
