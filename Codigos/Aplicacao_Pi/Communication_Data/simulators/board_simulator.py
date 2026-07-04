"""Camada 1 · Transporte — BoardSimulator (modo 2 · fake Python).

Simula o firmware SIBA/SAME: recebe CMD e devolve TELEMETRY_LOG senoidal.
É um FAKE (não roda nenhuma linha do firmware real) — para validar contra o
firmware de verdade, use o modo native_sim. Movido de `Ethernet.py` sem mudar
o comportamento; agora reaproveita os codecs de domínio.
"""
import math
import random
import socket
import struct
import threading
import time

from Communication_Data import codecs


class BoardSimulator(threading.Thread):
    """Simula o comportamento do firmware: recebe comandos e envia telemetria."""

    def __init__(self, name, config, pc_rx_port, board_tx_port):
        super().__init__(name=f"Sim_{name}", daemon=True)
        self.name_board = name
        self.config = config
        self.pc_rx_port = pc_rx_port
        self.board_tx_port = board_tx_port
        self.running = True

        # Estado interno do Mock (simulando sensores)
        self.sensors = {}
        self.gains = {}
        self.offsets = {}
        self._init_sensor_state()

    def _init_sensor_state(self):
        """Inicializa valores base para todos os campos da telemetria."""
        log_cfg = codecs.telemetry_layout(self.config)
        for field in log_cfg:
            self.sensors[field['name']] = 0.0
            self.gains[field['name']] = 1.0
            self.offsets[field['name']] = 0.0
            if 'bat' in field['name'].lower() or 'volt' in field['name'].lower():
                self.sensors[field['name']] = 12.0

    def run(self):
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
                try:
                    data, _ = sock_rx.recvfrom(1024)
                    self._handle_command(data)
                except socket.timeout:
                    pass

                now = time.time()
                if now - last_send > 0.1:
                    payload = self._generate_telemetry_packet(now - start_time)
                    sock_tx.sendto(payload, ('127.0.0.1', self.pc_rx_port))
                    last_send = now

    def _handle_command(self, data):
        """Decodifica CMD e aplica lógica simplificada de calibração mock."""
        decoded = codecs.decode_cmd(data)
        if decoded is None:
            return
        try:
            f, _instr, t, val, a = decoded

            enums = self.config.get('enums', {})
            func_list = enums.get('func_type', [])
            func_name = func_list[f] if f < len(func_list) else ""

            if "BATTERY" in func_name:
                actions_list = enums.get('battery_action', [])
            else:
                actions_list = enums.get('default_action', [])

            target_list = enums.get('target_type', [])

            if t < len(target_list):
                target_enum_name = target_list[t].replace("TARGET_", "").lower()
                action_name = actions_list[a] if a < len(actions_list) else ""

                target_key = None
                if target_enum_name in self.sensors:
                    target_key = target_enum_name
                else:
                    for k in self.sensors.keys():
                        if target_enum_name in k or k in target_enum_name:
                            target_key = k
                            break

                if not target_key:
                    return

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
        log_cfg = codecs.telemetry_layout(self.config)
        fmt = codecs.telemetry_format(log_cfg)

        values = []
        for field in log_cfg:
            name = field['name']
            if name == 'timestamp':
                values.append(int(elapsed * 1000) & 0xFFFFFFFF)
            elif name == 'status':
                values.append(1)  # OK
            else:
                base = self.sensors.get(name, 0.0)
                gain = self.gains.get(name, 1.0)
                offset = self.offsets.get(name, 0.0)

                noise = (random.random() - 0.5) * 0.1
                wave = math.sin(elapsed + random.random()) * 2.0
                val = ((base + wave + noise) * gain) + offset
                # Campos inteiros (uint32_t/log_status, ex.: error_code) exigem
                # int no struct.pack — não pode empacotar float neles.
                if codecs.TYPE_MAP.get(field['type'], 'f') != 'f':
                    values.append(int(val) & 0xFFFFFFFF)
                else:
                    values.append(float(val))

        return struct.pack(fmt, *values)
