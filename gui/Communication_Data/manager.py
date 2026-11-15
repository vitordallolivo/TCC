"""Camada 3 · Serviço — LinkManager (ex-EthernetManager, genérico).

Orquestra UM link por placa, dirigido pelo `BoardRegistry` (sem hardcode de
SIBA/SAME). Mantém a API pública que a GUI/TinyTerminal já usam (start_worker,
set_mock_mode, start/stop_native_sim, test_io, send_command, check_*, stop),
para migração sem quebra — o antigo `EthernetManager` vira um alias em
`Ethernet.py`.
"""
import os
import platform
import socket
import subprocess
import sys
import time

from Communication_Data import codecs
from Communication_Data.registry import BoardRegistry
from Communication_Data.links.udp_struct_link import UdpStructLink
from Communication_Data.native_sim import NativeSimProcess, native_sim_supported
from Communication_Data.simulators.board_simulator import BoardSimulator



# Pasta do firmware de cada placa, relativa à raiz do repositório.
FIRMWARE_DIRS = {"SIBA": "firmware"}

class LinkManager:
    def __init__(self, config_dir=None, handle_callback=None,
                 registry=None, board_ids=("SIBA", "SAME")):
        self.running = False
        self.config_dir = config_dir
        self.callback = handle_callback
        self.registry = registry or BoardRegistry.load(config_dir, board_ids)

        self.workers = {}          # id -> BoardLink
        self.simulators = {}       # id -> BoardSimulator (modo 2)
        self.native = {}           # id -> NativeSimProcess (modo 3)
        self.mock_enabled = {bid: False for bid in self.registry.ids()}
        self.native_enabled = {bid: False for bid in self.registry.ids()}

        # `configs` mantido por compat: GUI/TinyTerminal acessam
        # eth.configs["SIBA"] esperando o YAML bruto completo.
        self.configs = {bid: self.registry.get(bid).raw_yaml for bid in self.registry.ids()}

    # ------------------------------------------------------------------ #
    # Factory de link por transporte
    # ------------------------------------------------------------------ #
    def _make_link(self, cfg):
        if cfg.transport == "udp_struct":
            return UdpStructLink(cfg.id, cfg.ip, cfg.pc_rx_port, cfg.board_rx_port, self.callback)
        print(f"[NET] Transporte desconhecido '{cfg.transport}' para {cfg.id}.")
        return None

    # ------------------------------------------------------------------ #
    # Ciclo de vida dos links
    # ------------------------------------------------------------------ #
    def start_link(self, board):
        """Cria e inicia o link da placa se ainda não existir.

        Um link já registrado mas com a thread MORTA é descartado e recriado:
        `threading.Thread` não reinicia depois de terminar, então reaproveitar
        o objeto deixaria a placa sem RX (era o bug de reconectar — ver
        stop()).
        """
        board = board.upper()
        existing = self.workers.get(board)
        if existing is not None:
            alive = getattr(existing, "is_alive", None)
            if alive is None or alive():
                return
            print(f"[NET] Link antigo de {board} estava morto — recriando.")
            self.workers.pop(board, None)
        cfg = self.registry.get(board)
        if cfg is None:
            print(f"[NET] Placa {board} não registrada.")
            return
        link = self._make_link(cfg)
        if link is None:
            return
        self.workers[board] = link
        link.start()
        print(f"[NET] Listener para {board} ativado.")

    # Alias de compat com o código antigo.
    start_worker = start_link

    def send_command(self, protocol, raw_bytes):
        """Enfileira um comando para TX na placa."""
        p = protocol.upper()
        if p in self.workers:
            self.workers[p].send(raw_bytes)
            return True
        return False

    def update_link(self, board, ip=None, ports=None):
        """Atualiza IP/portas em RUNTIME (Board Manager). Não persiste no YAML."""
        board = board.upper()
        cfg = self.registry.get(board)
        if cfg is None:
            return
        if ip:
            cfg.ip = ip
        if ports:
            cfg.ports.update(ports)
        # Reflete no link ativo (se houver e for UDP).
        worker = self.workers.get(board)
        if worker is not None and hasattr(worker, "update_ip") and ip:
            worker.update_ip(ip)

    @staticmethod
    def _native_loopback(cfg):
        """IP de loopback DISTINTO por placa para mock/native_sim.

        Mapeia o último octeto do IP real da placa para 127.0.0.<octeto>
        (SIBA 169.254.97.200 -> 127.0.0.200, SAME .100 -> 127.0.0.100), para
        as placas não colidirem no mesmo 127.0.0.1 ao rodar juntas. Precisa
        casar com CONFIG_NET_CONFIG_*_IPV4_ADDR de cada <board>/boards/
        native_sim.conf. Cai em 127.0.0.1 se o IP não puder ser derivado."""
        try:
            octet = int(str(cfg.ip).split(".")[-1])
            if 1 <= octet <= 254:
                return f"127.0.0.{octet}"
        except (ValueError, AttributeError):
            pass
        return "127.0.0.1"

    # ------------------------------------------------------------------ #
    # Modo 2 — simulador Python (fake)
    # ------------------------------------------------------------------ #
    def set_mock_mode(self, board, enabled):
        """Liga/desliga o BoardSimulator para uma placa udp_struct."""
        board = board.upper()
        cfg = self.registry.get(board)
        if cfg is None or cfg.transport != "udp_struct":
            return

        if board not in self.workers:
            self.start_link(board)
        self.mock_enabled[board] = enabled
        worker = self.workers[board]

        if enabled:
            print(f"[MOCK] Ativando simulador para {board}...")
            worker.original_ip = worker.ip
            worker.ip = '127.0.0.1'
            sim = BoardSimulator(board, cfg.protocol_cfg, worker.rx_port, worker.tx_port)
            self.simulators[board] = sim
            sim.start()
        else:
            print(f"[MOCK] Desativando simulador para {board}...")
            if board in self.simulators:
                self.simulators[board].running = False
                self.simulators[board].join()
                del self.simulators[board]
            worker.ip = getattr(worker, 'original_ip', worker.ip)

    # ------------------------------------------------------------------ #
    # Modo 3 — firmware real via native_sim (subprocesso)
    # ------------------------------------------------------------------ #
    def native_exe_path(self, board):
        """Resolve o binário native_sim da placa.

        Em dev, as placas (SIBA/SAME) são pastas irmãs de gui
        dentro do repo git: ../<board>/build_native_sim/zephyr/zephyr.exe.
        Num build empacotado (PyInstaller) esse repo não existe na máquina
        de destino — os `zephyr.exe` precisam ter sido copiados PRA DENTRO
        do próprio bundle em build-time (--add-binary, ver build/pyinstaller),
        então aí a busca é relativa ao executável, não ao git.
        """
        board = board.upper()
        if getattr(sys, "frozen", False):
            base = os.path.dirname(os.path.abspath(sys.executable))
            return os.path.join(base, "native_sim_bin", board, "zephyr.exe")
        base = self.config_dir or "."
        # config_dir = gui/Config -> sobe 2 níveis até a raiz do repositório
        board_dir = os.path.normpath(os.path.join(base, "..", "..", FIRMWARE_DIRS.get(board, board)))
        return os.path.normpath(os.path.join(board_dir, "build_native_sim", "zephyr", "zephyr.exe"))

    def start_native_sim(self, board, exe_path=None):
        """Sobe o firmware real (native_sim) e aponta o link para loopback.

        Os comandos continuam indo por UDP; só o IP do link troca para o
        loopback da placa (127.0.0.<último octeto do IP real>).

        Só Linux (zephyr.exe é ELF POSIX). Se exe_path for None, resolve
        automaticamente via native_exe_path().
        """
        board = board.upper()
        cfg = self.registry.get(board)
        if cfg is None:
            return False
        if not native_sim_supported():
            print("[NATIVE_SIM] Indisponível fora do Linux.")
            return False
        if board in self.native:
            print(f"[NATIVE_SIM] {board} já está rodando (PID {self.native[board].pid}).")
            return False

        if exe_path is None:
            exe_path = self.native_exe_path(board)

        if board not in self.workers:
            self.start_link(board)
        worker = self.workers.get(board)
        if worker is None:
            return False

        # Cada placa ganha um loopback distinto (SIBA .200, SAME .100), para
        # as duas poderem rodar juntas sem misturar pacotes.
        worker.original_ip = worker.ip
        loopback = self._native_loopback(cfg)
        if hasattr(worker, "update_ip"):
            worker.update_ip(loopback)
        else:
            worker.ip = loopback

        # -uart_stdinout dá acesso ao shell de diagnóstico do firmware da SIBA
        # por `siba native shell <comando>`. É um canal EXTRA — o comando
        # normal continua indo por UDP.
        proc = NativeSimProcess(
            board, exe_path,
            extra_args=['-uart_stdinout'],
            pipe_stdin=True,
        )
        if not proc.start():
            worker.ip = getattr(worker, 'original_ip', worker.ip)
            return False

        self.native[board] = proc
        self.native_enabled[board] = True
        print(f"[NATIVE_SIM] {board} ({cfg.transport}) — comunicação real via {loopback}.")
        return True

    def native_shell(self, board, line):
        """Encaminha uma linha ao shell de UART do firmware em native_sim.

        Só faz sentido com o firmware rodando como processo local: é o stdin
        dele. Em hardware real não existe esse caminho (o PC fala por
        Ethernet). Retorna False se a placa não estiver em native_sim.
        """
        proc = self.native.get(board.upper())
        if proc is None:
            return False
        return proc.write_stdin(line)

    def start_native_sim_auto(self, board):
        """Conveniência p/ a GUI: resolve o path e sobe o native_sim."""
        return self.start_native_sim(board, self.native_exe_path(board))

    def stop_native_sim(self, board):
        """Encerra o firmware nativo e restaura o IP/roteamento do link."""
        board = board.upper()
        proc = self.native.pop(board, None)
        self.native_enabled[board] = False
        worker = self.workers.get(board)
        if proc is not None:
            proc.stop()
        if worker is not None:
            worker.ip = getattr(worker, 'original_ip', worker.ip)

    # ------------------------------------------------------------------ #
    # Diagnóstico e conectividade
    # ------------------------------------------------------------------ #
    def check_connectivity(self, ip):
        """Ping ICMP — verifica se o hardware responde no barramento.

        Flags do `ping` divergem por SO: Windows usa `-n`/`-w` (timeout em ms);
        Linux/macOS usam `-c`/`-W` (timeout em segundos no Linux; macOS aceita
        `-W` em ms, mas 1000 também funciona como "não trava por muito tempo").
        """
        print(f"[NET] Verificando link com {ip}...")
        if platform.system() == "Windows":
            cmd = ["ping", "-n", "1", "-w", "1000", ip]
        else:
            cmd = ["ping", "-c", "1", "-W", "1", ip]
        try:
            res = subprocess.run(
                cmd,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            return res.returncode == 0
        except Exception as e:
            print(f"[NET] Falha ao executar ping: {e}")
            return False

    def check_subnet(self, target_ip):
        """Verifica se o PC está na mesma subrede que o alvo."""
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.connect((target_ip, 80))
                local_ip = s.getsockname()[0]
            target_prefix = ".".join(target_ip.split(".")[:3])
            local_prefix = ".".join(local_ip.split(".")[:3])
            return (target_prefix == local_prefix), local_ip
        except Exception:
            return False, "0.0.0.0"

    def test_io(self, board, timeout=2.0):
        """Testa RX e TX isoladamente via contadores do link (independe da GUI).

        Retorna {"tx_ok", "rx_ok", "rx_packets"}. Funciona com mock,
        native_sim ou hardware real.
        """
        board = board.upper()
        if board not in self.workers:
            self.start_link(board)
        worker = self.workers.get(board)
        if worker is None:
            return {"tx_ok": False, "rx_ok": False, "rx_packets": 0}

        rx_before = worker.rx_count
        tx_before = worker.tx_count

        sent = self.send_command(board, codecs.NEUTRAL_CMD)

        deadline = time.time() + timeout
        while time.time() < deadline:
            if worker.tx_count > tx_before and worker.rx_count > rx_before:
                break
            time.sleep(0.05)

        result = {
            "tx_ok": bool(sent and worker.tx_count > tx_before),
            "rx_ok": worker.rx_count > rx_before,
            "rx_packets": worker.rx_count - rx_before,
        }
        print(f"[TEST_IO] {board}: TX {'OK' if result['tx_ok'] else 'FALHOU'}, "
              f"RX {'OK' if result['rx_ok'] else 'FALHOU'} "
              f"({result['rx_packets']} pacote(s) em {timeout:.1f}s)")
        return result

    # ------------------------------------------------------------------ #
    # IP conveniências (compat: eth.ip_siba / eth.ip_same, leitura e escrita)
    # ------------------------------------------------------------------ #
    def _board_ip(self, board):
        cfg = self.registry.get(board)
        return cfg.ip if cfg else None

    @property
    def ip_siba(self):
        return self._board_ip("SIBA")

    @ip_siba.setter
    def ip_siba(self, value):
        self.update_link("SIBA", ip=value)

    @property
    def ip_same(self):
        return self._board_ip("SAME")

    @ip_same.setter
    def ip_same(self, value):
        self.update_link("SAME", ip=value)

    # ------------------------------------------------------------------ #
    def start(self):
        """Marca o gerenciador como ativo (workers sobem sob demanda)."""
        if self.running:
            return
        self.running = True

    def stop(self):
        self.running = False
        for board in list(self.native.keys()):
            self.stop_native_sim(board)
        for board in list(self.simulators.keys()):
            self.simulators[board].running = False
        for w in self.workers.values():
            w.stop()
            w.join(timeout=1.0)
        # ESVAZIAR é obrigatório: as threads já terminaram e não reiniciam, e
        # start_link() pula placas que ainda constam aqui. Sem isto, reconectar
        # reaproveitava o link morto — a placa nunca mais entregava telemetria
        # e os gráficos ficavam parados até reiniciar a aplicação.
        self.workers.clear()
        print("[NET] Interface de rede encerrada.")
