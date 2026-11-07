
import socket

HOST = "0.0.0.0"
PORT = 8080

BODY = b"Hola desde MiOS"

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

print("MIOS HTTP SERVER: LISTO", flush=True)
print("Escuchando en puerto 8080", flush=True)

try:
    connection, address = server.accept()

    with connection:
        print(
            "HTTP: CONEXION TCP ESTABLECIDA",
            flush=True
        )

        print("Origen:", address, flush=True)

        connection.settimeout(15)

        request = bytearray()

        try:
            while b"\r\n\r\n" not in request:
                chunk = connection.recv(1024)

                if not chunk:
                    break

                request.extend(chunk)

                if len(request) > 4096:
                    raise ValueError(
                        "Solicitud demasiado grande"
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
                    b"Content-Length: "
                    + str(len(BODY)).encode("ascii")
                    + b"\r\n"
                    b"Connection: close\r\n"
                    b"\r\n"
                    + BODY
                )

                connection.sendall(response)

                print(
                    "HTTP: GET RECIBIDO CORRECTAMENTE",
                    flush=True
                )

                print(
                    "HTTP: 200 OK ENVIADO",
                    flush=True
                )

            else:
                print(
                    "HTTP: PETICION NO VALIDA",
                    flush=True
                )

        except (socket.timeout, ValueError) as error:
            print("HTTP ERROR:", error, flush=True)

except socket.timeout:
    print(
        "HTTP: TIMEOUT SIN CONEXION",
        flush=True
    )

finally:
    server.close()
