import socket
import threading
import queue
import subprocess
import yaml
import os
import struct
import time
import math
import random

TYPE_MAP = {"float": "f", "uint32_t": "I", "log_status": "I", "uint8_t": "B"}

class BoardSimulator(threading.Thread):
    """Simula o comportamento do Firmware: recebe comandos e envia telemetria."""
    def __init__(self, name, config, pc_rx_port, board_tx_port):
        super().__init__(name=f"Sim_{name}", daemon=True)
        self.name_board = name
        self.config = config
        self.pc_rx_port = pc_rx_port
        self.board_tx_port = board_tx_port
        self.running = True
        
        # Estado interno do Mock (Simulando sensores)
        self.sensors = {}
        self.gains = {}
        self.offsets = {}
        self._init_sensor_state()

    def _init_sensor_state(self):
        """Inicializa valores base para todos os campos da telemetria."""
        log_cfg = self.config.get("structs", {}).get("TELEMETRY_LOG", [])
        for field in log_cfg:
            self.sensors[field['name']] = 0.0
            self.gains[field['name']] = 1.0
            self.offsets[field['name']] = 0.0
            # Define um valor base para sensores de tensão não iniciarem em zero
            if 'bat' in field['name'].lower() or 'volt' in field['name'].lower():
                self.sensors[field['name']] = 12.0

    def run(self):
        # Socket para receber comandos (escuta na porta de TX da board)
        # Socket para enviar telemetria (envia para a porta de RX do PC)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock_rx, \
             socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock_tx:
            try:
                sock_rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                sock_rx.bind(('127.0.0.1', self.board_tx_port))
            except Exception as e:
                print(f"[SIM] Erro ao iniciar socket RX {self.name_board}: {e}")
                return

            sock_rx.settimeout(0.05)
            
            last_send = 0
            start_time = time.time()

            while self.running:
                # 1. Processa Comandos Recebidos do PC (SET, START, etc)
                try:
                    data, _ = sock_rx.recvfrom(1024)
                    self._handle_command(data)
                except socket.timeout:
                    pass

                # 2. Gera Telemetria (10Hz)
                now = time.time()
                if now - last_send > 0.1:
                    payload = self._generate_telemetry_packet(now - start_time)
                    sock_tx.sendto(payload, ('127.0.0.1', self.pc_rx_port))
                    last_send = now

    def _handle_command(self, data):
        """Decodifica struct CMD: func(B), instr(B), target(B), value(f), action(b)"""
        if len(data) < 8: return
        try:
            f, i, t, val, a = struct.unpack('<BBBfb', data)

            enums = self.config.get('enums', {})
            func_list = enums.get('func_type', [])
            func_name = func_list[f] if f < len(func_list) else ""

            # Seleciona a lista de ações baseada no tipo de função (Bateria vs Geral)
            if "BATTERY" in func_name:
                actions_list = enums.get('battery_action', [])
            else:
                actions_list = enums.get('default_action', [])

            target_list = enums.get('target_type', [])

            if t < len(target_list):
                # O target_name vem do Enum (ex: TARGET_PRESSURE -> pressure)
                target_enum_name = target_list[t].replace("TARGET_", "").lower()
                action_name = actions_list[a] if a < len(actions_list) else ""

                # Tenta mapear o target do comando para uma chave real no dicionário de sensores (telemetria)
                target_key = None
                if target_enum_name in self.sensors:
                    target_key = target_enum_name
                else:
                    # Busca por correspondência parcial (ex: enum 'P1' bate em log 'p1_voltage')
                    for k in self.sensors.keys():
                        if target_enum_name in k or k in target_enum_name:
                            target_key = k
                            break
                
                if not target_key: return

                # Lógica de Calibração Mock
                if "LINEAR" in action_name or "GAIN" in action_name:
                    self.gains[target_key] = val
                    print(f"[SIM] {self.name_board} Mock Gain: {target_key} -> {val}")
                elif "OFFSET" in action_name:
                    self.offsets[target_key] = val
                    print(f"[SIM] {self.name_board} Mock Offset: {target_key} -> {val}")
                else:
                    self.sensors[target_key] = val
                    print(f"[SIM] {self.name_board} Mock Value: {target_key} -> {val}")
        except Exception as e:
            print(f"[SIM] Erro ao processar comando mock: {e}")

    def _generate_telemetry_packet(self, elapsed):
        """Cria o buffer binário baseado na definição de TELEMETRY_LOG."""
        log_cfg = self.config.get("structs", {}).get("TELEMETRY_LOG", [])
        fmt = "<" + "".join(TYPE_MAP.get(f['type'], 'f') for f in log_cfg)
        
        values = []
        for field in log_cfg:
            name = field['name']
            if name == 'timestamp':
                values.append(int(elapsed * 1000) & 0xFFFFFFFF)
            elif name == 'status':
                values.append(1) # OK
            else:
                # Simula uma oscilação senoidal em torno do valor setado
                base = self.sensors.get(name, 0.0)
                gain = self.gains.get(name, 1.0)
                offset = self.offsets.get(name, 0.0)
                
                noise = (random.random() - 0.5) * 0.1
                wave = math.sin(elapsed + random.random()) * 2.0
                values.append(float(((base + wave + noise) * gain) + offset))
        
        return struct.pack(fmt, *values)

class BoardWorker(threading.Thread):
    """Thread dedicada para uma placa específica (SIBA ou SAME)."""
    def __init__(self, name, ip, rx_port, tx_port, callback):
        super().__init__(name=f"Worker_{name}", daemon=True)
        self.name_board = name
        self.ip = ip
        self.rx_port = rx_port
        self.tx_port = tx_port
        self.callback = callback
        self.tx_queue = queue.Queue() # Buffer de comandos TX
        self.running = True

    def run(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                sock.bind(('', self.rx_port))
            except Exception as e:
                print(f"[ERRO] Bind {self.name_board} na porta {self.rx_port}: {e}")
                return

            sock.settimeout(0.1) # Timeout curto para processar TX e RX quase em paralelo
            while self.running:
                # 1. RX Logic (Recebe Telemetria)
                try:
                    data, addr = sock.recvfrom(2048)
                    # No modo Mock, aceita de localhost. No real, filtra pelo IP.
                    if addr[0] == self.ip or addr[0] == '127.0.0.1':
                        self.callback(self.name_board, data, addr)
                except (socket.timeout, OSError):
                    pass

                # 2. TX Logic
                try:
                    while not self.tx_queue.empty():
                        payload = self.tx_queue.get_nowait()
                        # Se o IP for localhost, manda localmente
                        target_ip = '127.0.0.1' if self.ip == '127.0.0.1' else self.ip
                        sock.sendto(payload, (target_ip, self.tx_port))
                except Exception as e:
                    print(f"[ERRO] Falha TX {self.name_board}: {e}")

    def update_ip(self, new_ip):
        """Atualiza o IP alvo e reinicia o filtro do worker se necessário."""
        self.ip = new_ip

class EthernetManager:
    def __init__(self, siba_yaml, same_yaml, handle_callback=None):
        self.running = False
        self.workers = {}
        self.simulators = {}
        self.mock_enabled = {"SIBA": False, "SAME": False}
        self.callback = handle_callback
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
        self.port_same_rx = 4445  # Porta onde o PC ESCUTA a SAME
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

    def check_subnet(self, target_ip):
        """Verifica se o PC está na mesma subrede que o hardware (169.254.97.x)."""
        try:
            # Pega o IP local que sairia para o alvo
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.connect((target_ip, 80))
                local_ip = s.getsockname()[0]
            
            target_prefix = ".".join(target_ip.split(".")[:3])
            local_prefix = ".".join(local_ip.split(".")[:3])
            
            if target_prefix == local_prefix:
                return True, local_ip
            return False, local_ip
        except:
            return False, "0.0.0.0"

    def start_worker(self, board):
        """Inicia especificamente um worker se ele não existir."""
        board = board.upper()
        if board in self.workers: return
        
        ip = self.ip_siba if board == "SIBA" else self.ip_same
        rx = self.port_siba_rx if board == "SIBA" else self.port_same_rx
        tx = self.port_siba_tx if board == "SIBA" else self.port_same_tx
        
        self.workers[board] = BoardWorker(board, ip, rx, tx, self.callback)
        self.workers[board].start()
        print(f"[NET] Listener para {board} ativado.")

    def set_mock_mode(self, board, enabled):
        """Liga ou desliga o simulador interno para uma placa."""
        board = board.upper()
        if board not in ["SIBA", "SAME"]: return
        
        # Garante que o worker existe para ouvir o simulador
        if board not in self.workers:
            self.start_worker(board)
        
        self.mock_enabled[board] = enabled
        worker = self.workers[board]
        
        cfg = self.configs[board]
        if board.lower() in cfg: cfg = cfg[board.lower()]

        if enabled:
            print(f"[MOCK] Ativando simulador para {board}...")
            # Salva o IP real e muda o worker para ouvir localhost
            worker.original_ip = worker.ip
            worker.ip = '127.0.0.1'
            
            sim = BoardSimulator(board, cfg, worker.rx_port, worker.tx_port)
            self.simulators[board] = sim
            sim.start()
        else:
            print(f"[MOCK] Desativando simulador para {board}...")
            if board in self.simulators:
                self.simulators[board].running = False
                self.simulators[board].join()
                del self.simulators[board]
            # Restaura o IP real no worker
            worker.ip = getattr(worker, 'original_ip', worker.ip)

    def start(self):
        """Inicia os workers de escuta em threads separadas."""
        if self.running: return
        self.running = True

    def send_command(self, protocol, raw_bytes):
        """Aloca o comando no buffer da thread específica para envio."""
        p = protocol.upper()
        if p in self.workers:
            self.workers[p].tx_queue.put(raw_bytes)
            return True
        return False

    def stop(self):
        self.running = False
        for w in self.workers.values():
            w.running = False
            w.join(timeout=1.0)
        print("[NET] Interface de rede encerrada.")