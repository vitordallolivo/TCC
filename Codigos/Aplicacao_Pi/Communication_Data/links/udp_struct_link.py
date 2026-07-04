"""Camada 1 · Transporte — link UDP `CMD`/`TELEMETRY_LOG` (SIBA/SAME).

Generalização do antigo `BoardWorker` de `Ethernet.py`: uma thread que faz
RX (telemetria, placa->PC) e TX (comandos, PC->placa) por UDP, nas portas
`pc_rx`/`board_rx`. O parsing do payload NÃO acontece aqui — só bytes: quem
decodifica é a camada de domínio (`codecs`) acionada pela GUI via callback.
"""
import queue
import socket
import time

from Communication_Data.links.base import BoardLink


class UdpStructLink(BoardLink):
    """Link UDP genérico para placas que falam CMD/TELEMETRY_LOG.

    `pc_rx_port`  — porta onde o PC ESCUTA (telemetria da placa).
    `board_rx_port` — porta onde a PLACA escuta (comandos do PC).
    (correspondem a rx_port/tx_port do antigo BoardWorker.)
    """

    def __init__(self, board_id, ip, pc_rx_port, board_rx_port, callback):
        super().__init__(board_id, callback)
        self.ip = ip
        self.rx_port = pc_rx_port      # nome mantido p/ compat com test_io/mock
        self.tx_port = board_rx_port
        self.tx_queue = queue.Queue()
        # Guardado por set_mock_mode/native_sim para restaurar o IP real.
        self.original_ip = ip

    def send(self, raw_bytes):
        self.tx_queue.put(raw_bytes)

    def run(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                sock.bind(('', self.rx_port))
            except Exception as e:
                print(f"[ERRO] Bind {self.board_id} na porta {self.rx_port}: {e}")
                return

            sock.settimeout(0.1)  # curto: processa TX e RX quase em paralelo
            while self.running:
                # 1. RX (telemetria). No mock/native_sim aceita localhost; no
                #    hardware real filtra pelo IP configurado.
                try:
                    data, addr = sock.recvfrom(2048)
                    if addr[0] == self.ip or addr[0] == '127.0.0.1':
                        self.rx_count += 1
                        self.last_rx_time = time.time()
                        self.callback(self.board_id, data, addr)
                except (socket.timeout, OSError):
                    pass

                # 2. TX (comandos enfileirados)
                try:
                    while not self.tx_queue.empty():
                        payload = self.tx_queue.get_nowait()
                        target_ip = '127.0.0.1' if self.ip == '127.0.0.1' else self.ip
                        sock.sendto(payload, (target_ip, self.tx_port))
                        self.tx_count += 1
                except Exception as e:
                    print(f"[ERRO] Falha TX {self.board_id}: {e}")

    def update_ip(self, new_ip):
        """Atualiza o IP alvo em runtime (usado pelo Board Manager)."""
        self.ip = new_ip
