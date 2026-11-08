#!/usr/bin/env python3
"""Local binary HTTP lab for MiOS: arbitrary-byte bodies of 16 KiB.

This server is solely for deterministic DOWNLOAD 0.9 and consecutive-connection
1.0 tests. It does NOT implement HTTPS and is not intended for public exposure.
"""
import socket
import time
import hashlib
import re

HOST = '0.0.0.0'  # Guest reaches host via QEMU user NAT 10.0.2.2
PORT = 8082
BODY_SIZE = 16384


def make_body(path: bytes) -> bytes:
    """Deterministic 16 KiB, with explicit 0x00 and 0xFF bytes."""
    seed = hashlib.sha256(b'MiOS-DOWNLOAD-1.0:' + path).digest()
    return bytes([0, 255]) + bytes(
        seed[(i * 7 + i // 17) % len(seed)] ^ (i & 0xFF)
        for i in range(BODY_SIZE - 2)
    )


with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, PORT))
    server.listen(4)
    print('MiOS BINARY LAB: LISTO 10.0.2.2:8082', flush=True)
    print('BODY:', BODY_SIZE, 'bytes por archivo, incluye 00 y FF', flush=True)
    while True:
        try:
            connection, addr = server.accept()
            with connection:
                connection.settimeout(15)
                request = bytearray()
                while b'\r\n\r\n' not in request and len(request) < 4096:
                    chunk = connection.recv(1024)
                    if not chunk:
                        break
                    request.extend(chunk)
                m = re.match(rb'^GET (/[-A-Za-z0-9._]{1,40}) HTTP/1\.[01]\r\n', request)
                if not m or b'\r\n\r\n' not in request:
                    response = (b'HTTP/1.1 400 Bad Request\r\n'
                                b'Content-Length: 0\r\nConnection: close\r\n\r\n')
                    connection.sendall(response)
                    continue
                path = m.group(1)
                body = make_body(path)
                header = (
                    b'HTTP/1.1 200 OK\r\n'
                    b'Content-Type: application/octet-stream\r\n'
                    b'Content-Length: ' + str(len(body)).encode() + b'\r\n'
                    b'Connection: close\r\n\r\n'
                )
                response = header + body
                for i in range(0, len(response), 768):
                    connection.sendall(response[i:i + 768])
                    time.sleep(0.015)  # allow guest RX ring to drain
                print('BINARY TX:', path.decode('ascii'), len(body), 'bytes',
                      'SHA256', hashlib.sha256(body).hexdigest()[:16], flush=True)
        except (OSError, TimeoutError) as exc:
            print('BINARY LAB WARNING:', type(exc).__name__, flush=True)
