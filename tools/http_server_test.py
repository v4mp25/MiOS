
import socket
import time

HOST = "0.0.0.0"
PORT = 8080

# Cuerpo de exactamente 3072 bytes.
PREFIX = b"Bienvenido a MiOS\n"

BODY = PREFIX + b"A" * (3072 - len(PREFIX))

assert len(BODY) == 3072

server = socket.socket(
    socket.AF_INET,
    socket.SOCK_STREAM
)

server.setsockopt(
    socket.SOL_SOCKET,
    socket.SO_REUSEADDR,
    1
)

server.bind((HOST, PORT))
server.listen(1)
server.settimeout(120)

print("MIOS HTTP SERVER 0.5: LISTO", flush=True)
print("BODY:", len(BODY), "bytes", flush=True)

try:
    connection, address = server.accept()

    with connection:
        connection.settimeout(15)
        connection.setsockopt(
            socket.IPPROTO_TCP,
            socket.TCP_NODELAY,
            1
        )

        print(
            "HTTP: CONEXION ESTABLECIDA",
            address,
            flush=True
        )

        request = bytearray()

        while b"\r\n\r\n" not in request:
            part = connection.recv(1024)

            if not part:
                break

            request.extend(part)

            if len(request) > 4096:
                raise ValueError(
                    "Peticion demasiado grande"
                )

        print(
            "HTTP REQUEST:",
            request.decode(
                "ascii",
                errors="replace"
            ),
            flush=True
        )

        expected = (
            b"GET / HTTP/1.1\r\n"
            b"Host: 10.0.2.2:8080\r\n"
        )

        if (
            request.startswith(expected)
            and b"\r\n\r\n" in request
        ):
            response = (
                b"HTTP/1.1 200 OK\r\n"
                b"Content-Type: text/plain\r\n"
                b"Content-Length: 3072\r\n"
                b"Connection: close\r\n"
                b"\r\n"
                + BODY
            )

            print(
                "HTTP: RESPUESTA TOTAL:",
                len(response),
                "bytes",
                flush=True
            )

            # Enviar en grupos de 256 bytes.
            # TCP sigue siendo un flujo:
            # el receptor no debe asumir
            # que recv/sendall = un segmento.
            sent = 0

            while sent < len(response):
                chunk = response[sent:sent + 256]

                connection.sendall(chunk)

                sent += len(chunk)

                print(
                    "HTTP TX:",
                    sent,
                    "/",
                    len(response),
                    flush=True
                )

                time.sleep(0.03)

            print(
                "HTTP: RESPUESTA MULTISEGMENTO ENVIADA",
                flush=True
            )

            # Dar tiempo a los ACK antes del FIN.
            time.sleep(0.5)

        else:
            print(
                "HTTP: GET NO VALIDO",
                flush=True
            )

except (socket.timeout, ValueError, OSError) as error:
    print("HTTP ERROR:", error, flush=True)

finally:
    server.close()
