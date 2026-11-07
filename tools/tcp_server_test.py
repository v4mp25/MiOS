
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

    print(
        "TCP: CONEXION ESTABLECIDA",
        flush=True
    )

    print("Origen:", address, flush=True)

    connection.close()

except socket.timeout:
    print(
        "TCP: TIMEOUT, SIN CONEXION COMPLETA",
        flush=True
    )

finally:
    server.close()
