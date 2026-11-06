
import socket

HOST = "0.0.0.0"
PORT = 5555

sock = socket.socket(
    socket.AF_INET,
    socket.SOCK_DGRAM
)

sock.bind((HOST, PORT))
sock.settimeout(30)

print("MIOS UDP SERVER: LISTO", flush=True)
print("Escuchando en puerto 5555", flush=True)

try:
    data, address = sock.recvfrom(2048)

    print("Origen:", address)
    print("Recibido:", data)

    if data == b"HOLA UDP MIOS":
        sock.sendto(b"MIOS UDP OK", address)
        print("Respuesta enviada: MIOS UDP OK")
    else:
        print("Mensaje inesperado; sin respuesta")

except socket.timeout:
    print("TIMEOUT: no llego ningun datagrama")
finally:
    sock.close()
