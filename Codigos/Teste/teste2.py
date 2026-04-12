import socket
import threading

# Configurações
LISTEN_IP = "0.0.0.0"
LISTEN_PORT = 8080
TARGET_IP = "169.254.97.100"
TARGET_PORT = 5000

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((LISTEN_IP, LISTEN_PORT))

# Função que roda em paralelo só para receber os dados (Telemetria)
def receive_data():
    print(f"[*] Escutando telemetria na porta {LISTEN_PORT}...")
    while True:
        try:
            data, addr = sock.recvfrom(1024)
            # Imprime apagando a linha atual para não quebrar o texto do input
            print(f"\r[RX {addr[0]}:{addr[1]}] {data.decode(errors='ignore')}")
        except Exception as e:
            print(f"Erro RX: {e}")
            break

# Inicia a thread de recepção
rx_thread = threading.Thread(target=receive_data, daemon=True)
rx_thread.start()

print(f"[*] Digite comandos para enviar para {TARGET_IP}:{TARGET_PORT}")
print("[*] Digite 'sair' para encerrar.\n")

# Loop principal para enviar dados (Comandos para a placa)
while True:
    mensagem = input()
    if mensagem.lower() == 'sair':
        break
    
    if mensagem:
        sock.sendto(mensagem.encode(), (TARGET_IP, TARGET_PORT))
        print(f"[TX] -> {mensagem}")

sock.close()