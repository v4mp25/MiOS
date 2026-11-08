#!/usr/bin/env python3
"""HTTPS bridge for MiOS demos. TLS is verified on WSL, NOT in MiOS.

This is intentionally NOT a generic forward proxy. It only accepts HTTPS
URLs on a small allowlist, refuses redirects, rejects nonstandard ports,
and caps response bodies at 8192 bytes (BINFS capacity).

Run in WSL with:
    python3 tools/mios_https_bridge.py
Use MiOS: DOWNLOAD HTTPS://EXAMPLE.COM/ AS SEGURO.HTML
"""
import http.server
import ssl
import urllib.error
import urllib.parse
import urllib.request

LISTEN_ADDRESS = "0.0.0.0"  # Needed for QEMU user NAT: guest -> host 10.0.2.2.
LISTEN_PORT = 8081
BODY_LIMIT = 8192
ALLOWED_HOSTS = frozenset({
    "example.com", "www.example.com", "example.org", "www.example.org"
})

class RefuseRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None

class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "MiOS-HTTPS-WSL-Bridge/0.1"

    def _reply(self, status: int, payload: bytes, ctype="text/plain; charset=utf-8"):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.send_header("X-MiOS-TLS-Endpoint", "WSL-HOST-NOT-GUEST")
        self.end_headers()
        self.wfile.write(payload)
        self.close_connection = True

    def do_GET(self):
        if self.path != "/fetch":
            self._reply(404, b"Ruta desconocida\n")
            return
        raw_url = self.headers.get("X-MiOS-URL", "")
        if len(raw_url) > 390 or any(ord(c) < 32 or ord(c) > 126 for c in raw_url):
            self._reply(400, b"URL invalida\n")
            return
        try:
            u = urllib.parse.urlsplit(raw_url)
            hostname = (u.hostname or "").lower()
            if (u.scheme.lower() != "https" or hostname not in ALLOWED_HOSTS or
                u.username is not None or u.password is not None or
                u.port not in (None, 443) or not u.path.startswith("/") or
                u.fragment):
                self._reply(403, b"Solo HTTPS publico permitido en la lista\n")
                return
            # Reconstruct the URL only from vetted parts, rejecting opaque userinfo.
            path = urllib.parse.urlunsplit(("https", hostname, u.path,
                                           u.query, ""))
            context = ssl.create_default_context()
            opener = urllib.request.build_opener(
                urllib.request.HTTPSHandler(context=context), RefuseRedirect()
            )
            request = urllib.request.Request(path, headers={
                "User-Agent": "MiOS-HTTPS-WSL-Lab/0.1",
                "Accept-Encoding": "identity",
                "Connection": "close",
            })
            with opener.open(request, timeout=12) as result:
                if result.status != 200:
                    self._reply(502, b"HTTP remoto distinto de 200\n")
                    return
                declared = result.headers.get("Content-Length")
                if declared and int(declared) > BODY_LIMIT:
                    self._reply(413, b"Contenido demasiado grande para BINFS\n")
                    return
                body = result.read(BODY_LIMIT + 1)
                if len(body) > BODY_LIMIT:
                    self._reply(413, b"Contenido demasiado grande para BINFS\n")
                    return
                media = result.headers.get("Content-Type", "application/octet-stream")
                media = "".join(c for c in media if 32 <= ord(c) <= 126)[:100]
                self._reply(200, body, media)
                print("TLS CERTIFICADO VERIFICADO EN WSL:", hostname,
                      "bytes:", len(body), flush=True)
        except urllib.error.HTTPError as exc:
            # Redirects are denied; no switching to non-TLS targets.
            self._reply(502, f"Error HTTPS remoto: {exc.code}\n".encode())
        except (ValueError, ssl.SSLError, OSError, urllib.error.URLError) as exc:
            self._reply(502, b"Fallo de TLS, certificado o conexion\n")
            print("ERROR HTTPS WSL:", type(exc).__name__, flush=True)

    def log_message(self, fmt, *args):
        # Do not print user-controlled URLs or request headers.
        pass

if __name__ == "__main__":
    print("MiOS HTTPS PUENTE LAB: LISTO en puerto 8081", flush=True)
    print("TLS: Python/WSL (NO TLS nativo en MiOS)", flush=True)
    print("Sitios permitidos:", ", ".join(sorted(ALLOWED_HOSTS)), flush=True)
    print("Usar solo en red privada de laboratorio", flush=True)
    http.server.ThreadingHTTPServer((LISTEN_ADDRESS, LISTEN_PORT), Handler).serve_forever()
