"""Camada 1 · Transporte — TelnetLink (SPMM).

Protocolo da SPMM (diferente de SIBA/SAME):
- Comandos de bancada por TCP (shell Telnet do Zephyr) em texto:
  arm/disarm/throttle/ramp/patamar/coast/log ...
- Log rápido por UDP binário: `<HH>` (count, ts_us) + N×`<Iff>` (n, omega, te),
  decodificado por `codecs.decode_spmm_log` com `t = n*Ts`.

`telnetlib` foi REMOVIDO no Python 3.13 — usamos socket TCP puro (o shell do
Zephyr aceita linhas cruas terminadas em CRLF).

SEGURANÇA (crítico): ao parar, envia SEMPRE `disarm` + `throttle 1000` para o
motor nunca ficar girando se a GUI cair (replica a filosofia do script Python
da bancada — ver ../SPMM/CLAUDE.md).
"""
import math
import socket
import threading
import time

from Communication_Data import codecs
from Communication_Data.links.base import BoardLink

RAD_S_TO_RPM = 60.0 / (2.0 * math.pi)


class TelnetLink(BoardLink):
    """Link da SPMM: sessão TCP (comandos) + socket UDP (log binário)."""

    def __init__(self, cfg, callback, sample_callback=None):
        super().__init__(cfg.id, callback)
        self.cfg = cfg
        self.ip = cfg.ip
        self.telnet_port = cfg.telnet_port
        self.udp_log_port = cfg.udp_log_port
        self.Ts = cfg.Ts
        # Callback de amostras já decodificadas (board, dict). A GUI usa isso
        # para alimentar os buffers, sem precisar decodificar struct binário.
        self.sample_callback = sample_callback

        self._tcp = None
        self._tcp_lock = threading.Lock()
        # Sink alternativo de comandos: no modo native_sim o shell do firmware
        # é serial (stdin), não Telnet TCP. O LinkManager seta isso para
        # NativeSimProcess.write_stdin; se estiver setado, send() usa ele em
        # vez do socket TCP (ver Aplicacao_Pi/CLAUDE.md "native_sim da SPMM").
        self.command_sink = None

    # ------------------------------------------------------------------ #
    # TX — comandos de texto por TCP (Telnet)
    # ------------------------------------------------------------------ #
    def _ensure_telnet(self, timeout=2.0):
        """Abre (ou reaproveita) a conexão TCP com o shell. Retorna o socket
        ou None se a placa não responder."""
        with self._tcp_lock:
            if self._tcp is not None:
                return self._tcp
            try:
                s = socket.create_connection((self.ip, self.telnet_port), timeout=timeout)
                s.settimeout(timeout)
                self._tcp = s
                print(f"[SPMM] Sessão Telnet aberta com {self.ip}:{self.telnet_port}")
                return s
            except Exception as e:
                print(f"[SPMM] Falha ao abrir Telnet {self.ip}:{self.telnet_port}: {e}")
                return None

    def send(self, raw_bytes):
        """Envia um comando de texto ao shell. Aceita str ou bytes.

        No modo native_sim (command_sink setado) vai pelo stdin do processo;
        caso contrário, pela sessão Telnet TCP (hardware real)."""
        if isinstance(raw_bytes, bytes):
            text = raw_bytes.decode(errors="ignore").strip()
        else:
            text = str(raw_bytes).strip()

        # native_sim: shell serial via stdin do subprocesso.
        if self.command_sink is not None:
            ok = bool(self.command_sink(text))
            if ok:
                self.tx_count += 1
            return ok

        s = self._ensure_telnet()
        if s is None:
            return False
        try:
            with self._tcp_lock:
                s.sendall((text + "\r\n").encode())
            self.tx_count += 1
            return True
        except Exception as e:
            print(f"[SPMM] Erro TX '{text}': {e}")
            with self._tcp_lock:
                self._tcp = None  # força reconexão na próxima
            return False

    def read_reply(self, timeout=1.0):
        """Lê uma resposta curta do shell (usado por descoberta/ping-pong)."""
        s = self._ensure_telnet()
        if s is None:
            return b""
        try:
            s.settimeout(timeout)
            return s.recv(1024)
        except Exception:
            return b""

    # ------------------------------------------------------------------ #
    # RX — log binário por UDP
    # ------------------------------------------------------------------ #
    def run(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                sock.bind(('', self.udp_log_port))
            except Exception as e:
                print(f"[ERRO] Bind SPMM na porta UDP {self.udp_log_port}: {e}")
                return
            sock.settimeout(0.1)

            try:
                while self.running:
                    try:
                        data, addr = sock.recvfrom(4096)
                    except (socket.timeout, OSError):
                        continue
                    if self.ip not in (addr[0], '127.0.0.1'):
                        continue
                    self.rx_count += 1
                    self.last_rx_time = time.time()
                    # Callback bruto (p/ HEX no terminal, se assinado)
                    if self.callback:
                        self.callback(self.board_id, data, addr)
                    self._dispatch_samples(data)
            finally:
                self._safe_shutdown()

    def _dispatch_samples(self, data):
        """Decodifica o pacote e publica cada amostra normalizada."""
        pkt = codecs.decode_spmm_log(data, self.Ts)
        if pkt is None or self.sample_callback is None:
            return
        for s in pkt["samples"]:
            decoded = {
                "n": s["n"],
                "omega": s["omega"],
                "rpm": s["omega"] * RAD_S_TO_RPM,
                "te": s["te"],
                "iq": s.get("iq"),
                "id": s.get("id"),
            }
            if "t" in s:
                decoded["timestamp"] = s["t"] * 1000.0  # ms, p/ base de tempo da GUI
            self.sample_callback(self.board_id, decoded)

    # ------------------------------------------------------------------ #
    # Parada segura — motor NUNCA fica girando
    # ------------------------------------------------------------------ #
    def _safe_shutdown(self):
        """Envia disarm + throttle 1000 e fecha a sessão TCP.

        Funciona nos dois modos: native (stdin, via command_sink) ou hardware
        (Telnet TCP). O motor NUNCA pode ficar girando se a GUI cair."""
        try:
            if self.command_sink is not None:
                for cmd in ("disarm", "throttle 1000"):
                    try:
                        self.command_sink(cmd)
                    except Exception:
                        pass
                print("[SPMM] Parada segura (native/stdin): disarm + throttle 1000.")
            elif self._tcp is not None:
                for cmd in ("disarm", "throttle 1000"):
                    try:
                        self._tcp.sendall((cmd + "\r\n").encode())
                    except Exception:
                        pass
                print("[SPMM] Parada segura: disarm + throttle 1000 enviados.")
        finally:
            with self._tcp_lock:
                if self._tcp is not None:
                    try:
                        self._tcp.close()
                    except Exception:
                        pass
                    self._tcp = None

    def update_ip(self, new_ip):
        self.ip = new_ip
        with self._tcp_lock:
            if self._tcp is not None:
                try:
                    self._tcp.close()
                except Exception:
                    pass
                self._tcp = None
