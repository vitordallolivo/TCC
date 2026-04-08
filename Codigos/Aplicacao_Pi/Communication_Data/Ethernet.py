import socket
import struct
import numpy as np
from collections import deque
from PySide6.QtCore import QThread, Signal

# ==================================================================
#   Protocolo UDP esperado:
#   Header: 2 bytes  → 0xAA55
#   ID:     2 bytes  → identifica a placa (0x01=SIBA, 0x02=SAME)
#   Seq:    4 bytes  → número de sequência
#   Payload: N amostras de CHANNELS * 4 bytes (float32 little endian)
#
#   Struct C equivalente por amostra:
#   typedef struct {
#       float Va, Vb, Vc;   // Tensões de fase [V]
#       float ia, ib, ic;   // Correntes de fase [A]
#       float rpm;           // Velocidade [RPM]
#       float torque;        // Torque estimado [Nm]
#   } InversorSample_t;
# ==================================================================

HEADER_MAGIC    = 0xAA55
CHANNELS        = 8
BYTES_PER_SAMPLE = CHANNELS * 4   # float32 × 8
HEADER_SIZE     = 8               # 2 (magic) + 2 (id) + 4 (seq)
MAX_POINTS      = 2000

BOARD_SIBA = 0x01
BOARD_SAME = 0x02

CHANNEL_NAMES = ["Va", "Vb", "Vc", "ia", "ib", "ic", "RPM", "Torque"]


class EthernetReceiver(QThread):
    """
    Thread UDP não-bloqueante.
    Emite `data_ready` com dict {nome: valor} a cada pacote válido.
    Emite `connection_changed` com (board_id, bool) ao detectar/perder placa.
    """
    data_ready         = Signal(int, dict)   # (board_id, {canal: valor})
    connection_changed = Signal(int, bool)   # (board_id, conectado)

    def __init__(self, ip: str = "0.0.0.0", port: int = 9999, parent=None):
        super().__init__(parent)
        self._ip   = ip
        self._port = port
        self._running = False

        # Buffers acumulativos por canal
        self.buffers: dict[int, dict[str, deque]] = {
            BOARD_SIBA: {ch: deque(maxlen=MAX_POINTS) for ch in CHANNEL_NAMES},
            BOARD_SAME: {ch: deque(maxlen=MAX_POINTS) for ch in CHANNEL_NAMES},
        }

        # Controle de timeout de conexão
        self._last_seen: dict[int, int] = {BOARD_SIBA: 0, BOARD_SAME: 0}
        self._connected: dict[int, bool] = {BOARD_SIBA: False, BOARD_SAME: False}

    # ------------------------------------------------------------------
    def run(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((self._ip, self._port))
        sock.setblocking(False)
        sock.settimeout(0.05)

        self._running = True
        timeout_ticks = 0

        while self._running:
            try:
                data, _ = sock.recvfrom(4096)
                self._parse(data)
                timeout_ticks = 0
            except socket.timeout:
                timeout_ticks += 1
                # ~5s sem pacote → marca como desconectado
                if timeout_ticks > 100:
                    for board_id in [BOARD_SIBA, BOARD_SAME]:
                        if self._connected[board_id]:
                            self._connected[board_id] = False
                            self.connection_changed.emit(board_id, False)
                    timeout_ticks = 0
            except Exception as e:
                print(f"[Ethernet] Erro: {e}")

        sock.close()

    def stop(self):
        self._running = False
        self.wait()

    # ------------------------------------------------------------------
    def _parse(self, data: bytes):
        if len(data) < HEADER_SIZE:
            return

        magic, board_id, seq = struct.unpack_from(">HHI", data, 0)
        if magic != HEADER_MAGIC:
            return

        if board_id not in (BOARD_SIBA, BOARD_SAME):
            return

        # Atualiza estado de conexão
        if not self._connected[board_id]:
            self._connected[board_id] = True
            self.connection_changed.emit(board_id, True)

        # Extrai amostras do payload
        payload = data[HEADER_SIZE:]
        num_samples = len(payload) // BYTES_PER_SAMPLE

        latest = {}
        for s in range(num_samples):
            offset = s * BYTES_PER_SAMPLE
            values = struct.unpack_from(f"<{CHANNELS}f", payload, offset)
            for i, name in enumerate(CHANNEL_NAMES):
                self.buffers[board_id][name].append(values[i])
                latest[name] = values[i]

        if latest:
            self.data_ready.emit(board_id, latest)