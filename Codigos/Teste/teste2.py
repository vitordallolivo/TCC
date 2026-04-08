import socket
import sys

def main():
    # Cria socket UDP
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # Permite reutilizar endereço (útil para reiniciar rápido)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    # Habilita recebimento de broadcast (opcional, mas seguro)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    # Escuta em todas as interfaces (''), porta 5000
    server_address = ('', 9999)
    print(f"Aguardando pacotes UDP na porta {server_address[1]}...")
    sock.bind(server_address)

    try:
        while True:
            data, address = sock.recvfrom(65535)  # buffer grande
            print(f"Recebido {len(data)} bytes de {address}: {data}")
    except KeyboardInterrupt:
        print("\nEncerrado pelo usuário.")
        sock.close()
        sys.exit(0)

if __name__ == "__main__":
    main()