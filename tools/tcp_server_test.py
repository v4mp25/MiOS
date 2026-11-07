
import socket

HOST = "0.0.0.0"
PORT = 8080

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

print("MIOS TCP SERVER: LISTO", flush=True)
print("Escuchando en puerto 8080", flush=True)

try:
    connection, address = server.accept()

    with connection:
        print(
            "TCP: CONEXION ESTABLECIDA",
            flush=True
        )
        print("Origen:", address, flush=True)

        connection.settimeout(15)

        try:
            data = connection.recv(2048)

            print("TCP RX:", data, flush=True)

            if data == b"HOLA TCP MIOS":
                print("TCP: DATOS CORRECTOS!", flush=True)

                connection.sendall(b"MIOS TCP OK")

                print(
                    "TCP: RESPUESTA ENVIADA",
                    flush=True
                )
            else:
                print(
                    "TCP: MENSAJE INESPERADO",
                    flush=True
                )

        except socket.timeout:
            print(
                "TCP: TIMEOUT ESPERANDO DATOS",
                flush=True
            )

except socket.timeout:
    print(
        "TCP: TIMEOUT SIN CONEXION",
        flush=True
    )

finally:
    server.close()
