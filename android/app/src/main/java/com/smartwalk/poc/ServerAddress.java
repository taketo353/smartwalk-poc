package com.smartwalk.poc;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.Locale;

final class ServerAddress {
    private final URI origin;

    private ServerAddress(URI origin) {
        this.origin = origin;
    }

    static ServerAddress parse(String input) {
        try {
            URI uri = new URI(input.trim());
            String host = uri.getHost();
            String scheme = uri.getScheme();
            int port = uri.getPort();
            if (host == null || scheme == null || uri.getUserInfo() != null
                    || uri.getQuery() != null || uri.getFragment() != null
                    || !(uri.getRawPath().isEmpty() || uri.getRawPath().equals("/"))
                    || port == 0 || port > 65535) {
                return null;
            }
            scheme = scheme.toLowerCase(Locale.ROOT);
            if (!scheme.equals("https") && !(scheme.equals("http") && localIpv4(host))) {
                return null;
            }
            return new ServerAddress(new URI(scheme, null, host, port, null, null, null));
        } catch (URISyntaxException error) {
            return null;
        }
    }

    private static boolean localIpv4(String host) {
        String[] parts = host.split("\\.", -1);
        if (parts.length != 4) return false;
        int[] octets = new int[4];
        for (int index = 0; index < parts.length; index++) {
            String part = parts[index];
            if (!part.matches("0|[1-9][0-9]{0,2}")) return false;
            octets[index] = Integer.parseInt(part);
            if (octets[index] > 255) return false;
        }
        return octets[0] == 10
                || (octets[0] == 192 && octets[1] == 168)
                || (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31)
                || host.equals("127.0.0.1");
    }

    boolean allows(URI address) {
        return origin.getScheme().equalsIgnoreCase(address.getScheme())
                && origin.getHost().equalsIgnoreCase(address.getHost())
                && address.getUserInfo() == null
                && portOf(origin) == portOf(address);
    }

    private static int portOf(URI address) {
        if (address.getPort() >= 0) return address.getPort();
        return "https".equalsIgnoreCase(address.getScheme()) ? 443 : 80;
    }

    String urlFor(String path) {
        return origin.resolve(path).toString();
    }

    @Override
    public String toString() {
        return origin.toString();
    }
}
