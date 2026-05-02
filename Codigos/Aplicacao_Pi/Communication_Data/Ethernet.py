import socket
import threading
import subprocess
import yaml
import os
import time

class EthernetManager:
    def __init__(self, siba_yaml, same_yaml, handle_callback=None):
        self.running = False
        self.threads = []
        self.callback = handle_callback if handle_callback else self._default_handle_mock
        
        # Carregamento de Configurações
        self.configs = {
            "SIBA": self._load_yaml(siba_yaml),
            "SAME": self._load_yaml(same_yaml)
        }
        
        # --- ENDEREÇAMENTO SIBA ---
        self.ip_siba = self.configs["SIBA"].get("DEFAULT_IP_BOARD", "169.254.97.200")
        self.port_siba_rx = 5555  # Porta onde o PC ESCUTA a SIBA (PEER_PORT no C)
        self.port_siba_tx = 5000  # Porta onde a SIBA ESCUTA o PC (LOCAL_PORT no C)

        # --- ENDEREÇAMENTO SAME ---
        self.ip_same = self.configs["SAME"].get("DEFAULT_IP_BOARD", "169.254.97.100")
        self.port_same_rx = 4445  # Porta onde o PC ESCUTA a SAME (Mude no C para 4445!)
        self.port_same_tx = 4444  # Porta onde a SAME ESCUTA o PC

    def _load_yaml(self, path):
        if not os.path.exists(path):
            print(f"[ERRO] Arquivo {path} não encontrado.")
            return {}
        with open(path, 'r') as f:
            return yaml.safe_load(f)

    def check_connectivity(self, ip):
        """Verifica se o hardware está presente no barramento."""
        print(f"[NET] Verificando link com {ip}...")
        try:
            res = subprocess.run(
                ["ping", "-c", "1", "-W", "1", ip],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            )
            return res.returncode == 0
        except Exception as e:
            print(f"[NET] Falha ao executar ping: {e}")
            return False

    def start(self):
        """Inicia os workers de escuta em threads separadas."""
        if self.running: return

        siba_ok = self.check_connectivity(self.ip_siba)
        same_ok = self.check_connectivity(self.ip_same)
        
        if not (siba_ok or same_ok):
            print("[AVISO] Nenhuma placa respondeu ao teste de link.")

        self.running = True
        
        # Cada thread bindada em sua própria porta de RX
        t_siba = threading.Thread(target=self._listen_worker, 
                                  args=("SIBA", self.port_siba_rx, self.ip_siba), 
                                  name="Thread_SIBA", daemon=True)
        t_same = threading.Thread(target=self._listen_worker, 
                                  args=("SAME", self.port_same_rx, self.ip_same), 
                                  name="Thread_SAME", daemon=True)
        
        self.threads = [t_siba, t_same]
        for t in self.threads:
            t.start()
            p = self.port_siba_rx if "SIBA" in t.name else self.port_same_rx
            print(f"[NET] {t.name} iniciada na porta {p}")

    def _listen_worker(self, name, port, expected_ip):
        """Loop de escuta UDP isolado por porta."""
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                sock.bind(('', port))
            except Exception as e:
                print(f"[ERRO] Bind falhou em {port} para {name}: {e}")
                return

            sock.settimeout(1.0)
            while self.running:
                try:
                    data, addr = sock.recvfrom(2048)
                    if addr[0] == expected_ip:
                        self.callback(name, data, addr)
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running: print(f"[ERRO] Listener {name}: {e}")
                    break

    def send_data(self, protocol, hex_payload):
        """Mantido para compatibilidade: envia hex string."""
        raw = bytes.fromhex(hex_payload.replace(" ", ""))
        return self.send_command(protocol, raw)

    def send_command(self, protocol, raw_bytes):
        """Envia bytes diretamente para a porta TX da placa alvo."""
        
        print(f"[INF] Tentando enviar para {protocol}")
        
        if protocol.upper() == "SAME":
            target = (self.ip_same, self.port_same_tx)
        else:
            target = (self.ip_siba, self.port_siba_tx)
        
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            try:
                sock.sendto(raw_bytes, target)
                return True
            except Exception as e:
                print(f"[ERRO] Falha ao enviar para {protocol}: {e}")
                return False

    def stop(self):
        self.running = False
        for t in self.threads: t.join(timeout=2.0)
        print("[NET] Interface de rede encerrada.")

    def _default_handle_mock(self, protocol, data, addr):
        print(f"[MOCK RECV] {protocol} ({addr[0]}): {data.hex(' ').upper()}")