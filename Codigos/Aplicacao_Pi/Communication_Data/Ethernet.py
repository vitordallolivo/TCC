import socket
import threading
import subprocess
import yaml
import os
import time

class EthernetManager:
    def __init__(self, siba_yaml, same_yaml, handle_callback=None):
        """
        Gerencia as conexões UDP para SIBA e SAME.
        :param handle_callback: Função que receberá (origem, dados) para processamento.
        """
        self.running = False
        self.threads = []
        self.callback = handle_callback if handle_callback else self._default_handle_mock
        
        # Carregamento de Configurações
        self.configs = {
            "SIBA": self._load_yaml(siba_yaml),
            "SAME": self._load_yaml(same_yaml)
        }
        
        # Atributos de Rede
        self.ip_siba = self.configs["SIBA"].get("DEFAULT_IP_BOARD", "169.254.97.101")
        self.port_siba = self.configs["SIBA"].get("DEFAULT_PORT", 5001)
        
        self.ip_same = self.configs["SAME"].get("DEFAULT_IP_BOARD", "169.254.97.100")
        self.port_same = self.configs["SAME"].get("DEFAULT_PORT", 5000)

    def _load_yaml(self, path):
        if not os.path.exists(path):
            print(f"[ERRO] Arquivo {path} não encontrado.")
            return {}
        with open(path, 'r') as f:
            return yaml.safe_load(f)

    def check_connectivity(self, ip):
        """
        Rotina de pré-inicialização. 
        Verifica se o hardware está presente no barramento.
        """
        print(f"[NET] Verificando link com {ip}...")
        try:
            # Ping rápido: 1 pacote, 1 segundo de timeout
            res = subprocess.run(
                ["ping", "-c", "1", "-W", "1", ip],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            return res.returncode == 0
        except Exception as e:
            print(f"[NET] Falha ao executar ping: {e}")
            return False

    def start(self):
        """Inicia os workers de escuta em threads separadas."""
        if self.running:
            return

        # Verificação inicial (estilo check-up de conexão)
        siba_ok = self.check_connectivity(self.ip_siba)
        same_ok = self.check_connectivity(self.ip_same)
        
        if not (siba_ok or same_ok):
            print("[AVISO] Nenhuma placa respondeu ao teste de link. Verifique cabos/switch.")

        self.running = True
        
        # Criando listeners para cada protocolo
        t_siba = threading.Thread(target=self._listen_worker, 
                                  args=("SIBA", self.port_siba), 
                                  name="Thread_SIBA", daemon=True)
        t_same = threading.Thread(target=self._listen_worker, 
                                  args=("SAME", self.port_same), 
                                  name="Thread_SAME", daemon=True)
        
        self.threads = [t_siba, t_same]
        for t in self.threads:
            t.start()
            print(f"[NET] {t.name} iniciada na porta {self.port_siba if 'SIBA' in t.name else self.port_same}")

    def _listen_worker(self, name, port):
        """Loop de escuta UDP."""
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            # Permite reutilizar a porta se o app reiniciar rápido
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(('', port))
            sock.settimeout(1.0) # Timeout para permitir verificar self.running

            while self.running:
                try:
                    data, addr = sock.recvfrom(2048)
                    # Filtra se os dados vêm do IP esperado (opcional, mas recomendado)
                    expected_ip = self.ip_siba if name == "SIBA" else self.ip_same
                    
                    if addr[0] == expected_ip:
                        self.callback(name, data, addr)
                    else:
                        print(f"[NET] Dado ignorado de fonte desconhecida: {addr[0]}")
                        
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        print(f"[ERRO] Listener {name}: {e}")
                    break

    def _default_handle_mock(self, protocol, data, addr):
        """Handle MOCK padrão (será substituído pela GUI futuramente)."""
        print(f"--- [MOCK RECV] ---")
        print(f"Protocolo: {protocol} | Fonte: {addr[0]}")
        print(f"HEX: {data.hex(' ').upper()}")
        print(f"-------------------")

    def stop(self):
        """Finaliza as threads de rede de forma segura."""
        self.running = False
        for t in self.threads:
            t.join(timeout=2.0)
        print("[NET] Interface de rede encerrada.")

    def send_data(self, protocol, hex_payload):
        """Método auxiliar para envio de comandos via UDP."""
        target_ip = self.ip_same if protocol == "SAME" else self.ip_siba
        target_port = self.port_same if protocol == "SAME" else self.port_siba
        
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            try:
                sock.sendto(bytes.fromhex(hex_payload.replace(" ", "")), (target_ip, target_port))
            except Exception as e:
                print(f"[ERRO] Falha ao enviar para {protocol}: {e}")
    
    def send_command(self, protocol, raw_bytes):
        """Envia um payload de bytes diretamente para a placa alvo."""
        target_ip = self.ip_same if protocol == "SAME" else self.ip_siba
        target_port = self.port_same if protocol == "SAME" else self.port_siba
        
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            try:
                sock.sendto(raw_bytes, (target_ip, target_port))
                return True
            except Exception as e:
                print(f"[ERRO] Falha ao enviar bytes para {protocol}: {e}")
                return False