#!/usr/bin/env python3
"""Banco de teste de linha de comando para o firmware em `native_sim`.

Sobe o `zephyr.exe` da SIBA ou da SAME como processo no host e conversa com
ele pelo MESMO caminho que a GUI usa — UDP real em loopback, CMD de 8 bytes
montado a partir de `Config/<placa>.yaml`. Serve para exercitar o protocolo
sem abrir a interface gráfica e sem bancada física.

Por que existe: a GUI é o único cliente do protocolo, e testá-la exige
cliques. Aqui dá para roteirizar (`-c`), ver os bytes que saem e conferir o
que a placa respondeu, tudo no terminal.

Uso:

    python native_cli.py SIBA                    # sobe e abre o prompt
    python native_cli.py SAME --no-spawn         # usa um já rodando
    python native_cli.py SIBA -c arm -c tele     # roteiro e sai
    python native_cli.py SIBA -c "shell btn on" -c arm

Os comandos do prompt estão em `help`. Os que mexem na cadeia de potência
existem só na SIBA; `reset`, `tele` e `shell` valem para as duas.

Requer `make native_sim` feito no diretório do firmware, e Linux (o
`zephyr.exe` é um ELF POSIX).
"""
import argparse
import os
import socket
import sys
import threading
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
os.chdir(SCRIPT_DIR)
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from Communication_Data import codecs                      # noqa: E402
from Communication_Data.native_sim import (                # noqa: E402
    NativeSimProcess, kill_stray_native_sims, native_sim_supported,
)
from Communication_Data.registry import BoardRegistry      # noqa: E402

CONFIG_DIR = os.path.join(SCRIPT_DIR, "Config")
BOARDS = ("SIBA", "SAME")


def loopback_for(ip):
    """127.0.0.<último octeto do IP real> — mesma regra do LinkManager.

    SIBA 169.254.97.200 -> 127.0.0.200; SAME .100 -> 127.0.0.100. Tem de casar
    com CONFIG_NET_CONFIG_*_IPV4_ADDR do native_sim.conf de cada placa.
    """
    try:
        octet = int(str(ip).split(".")[-1])
        if 1 <= octet <= 254:
            return f"127.0.0.{octet}"
    except (ValueError, AttributeError):
        pass
    return "127.0.0.1"


class NativeTester:
    """Link UDP + processo do firmware de uma placa."""

    def __init__(self, board, spawn=True, quiet=False):
        self.board = board.upper()
        self.quiet = quiet

        registry = BoardRegistry.load(CONFIG_DIR, board_ids=BOARDS)
        self.cfg = registry.get(self.board)
        if self.cfg is None:
            raise SystemExit(f"Placa desconhecida: {self.board}")
        if self.cfg.transport != "udp_struct":
            raise SystemExit(
                f"{self.board} usa transporte '{self.cfg.transport}'; "
                "este script cobre só as placas de protocolo CMD (SIBA/SAME)."
            )

        self.proto = codecs.board_protocol_cfg(self.cfg.raw_yaml, self.board)
        self.enums = self.proto.get("enums", {})
        self.log_cfg = codecs.telemetry_layout(self.proto)

        self.ip = loopback_for(self.cfg.ip)
        self.board_rx = self.cfg.board_rx_port
        self.pc_rx = self.cfg.pc_rx_port

        self.proc = None
        self.sock = None
        self.last_tele = None
        self.tele_count = 0
        self._stop = threading.Event()

        if spawn:
            self._spawn()
        self._open_socket()

    # ---------------------------------------------------------------- setup
    def _exe_path(self):
        return os.path.join(
            SCRIPT_DIR, "..", self.board, "build_native_sim", "zephyr", "zephyr.exe"
        )

    def _spawn(self):
        if not native_sim_supported():
            raise SystemExit("native_sim só roda no Linux (zephyr.exe é ELF POSIX).")
        kill_stray_native_sims()
        exe = self._exe_path()
        if not os.path.exists(exe):
            raise SystemExit(
                f"Binário não encontrado: {exe}\n"
                f"Compile com 'make native_sim' em ../{self.board}/."
            )
        # -uart_stdinout deixa o shell do firmware no stdin/stdout do processo,
        # que é o que o comando `shell` daqui usa. O relay de log do
        # NativeSimProcess já imprime tudo que sai.
        self.proc = NativeSimProcess(
            self.board, exe, extra_args=["-uart_stdinout"], pipe_stdin=True
        )
        if not self.proc.start():
            raise SystemExit("Falha ao subir o native_sim.")
        time.sleep(1.5)   # deixa o firmware chegar no bind do socket

    def _open_socket(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.sock.bind(("0.0.0.0", self.pc_rx))
        except OSError as e:
            print(f"[AVISO] não consegui escutar a telemetria na porta "
                  f"{self.pc_rx}: {e}")
        self.sock.settimeout(0.3)
        threading.Thread(target=self._rx_loop, daemon=True).start()

    def _rx_loop(self):
        while not self._stop.is_set():
            try:
                data, _ = self.sock.recvfrom(2048)
            except (socket.timeout, OSError):
                continue
            tele = codecs.decode_telemetry(self.log_cfg, data)
            if tele is not None:
                self.last_tele = tele
                self.tele_count += 1

    # ------------------------------------------------------------- protocolo
    def idx(self, category, name, default=0):
        lst = self.enums.get(category, [])
        return lst.index(name) if name in lst else default

    def has(self, category, name):
        return name in self.enums.get(category, [])

    def send(self, func, instr="INSTR_WRITE", target="TARGET_DEFAULT",
             value=1.0, status=None, label=""):
        """Monta o CMD a partir dos enums do YAML e envia. Mostra os bytes."""
        if not self.has("func_type", func):
            print(f"[ERRO] {func} não existe no {self.board}.yaml.")
            return False
        if status and not self.has("status_type", status):
            print(f"[ERRO] {status} não existe no {self.board}.yaml — "
                  "regere o protocolo (Config/generate.py).")
            return False

        fi = self.idx("func_type", func)
        ii = self.idx("instruct_type", instr, 1)
        ti = self.idx("target_type", target)
        ai = self.idx("status_type", status) if status else 0
        cmd = codecs.encode_cmd(fi, ii, ti, value, ai)

        extra = f" {status}({ai})" if status else ""
        print(f"[TX] {label or func}  ->  {cmd.hex(' ').upper()}   "
              f"[{func}({fi}) {instr}({ii}) {target}({ti}) val={value:g}{extra}]")
        self.sock.sendto(cmd, (self.ip, self.board_rx))
        return True

    def shell(self, line):
        """Manda uma linha para o shell do próprio firmware (via stdin)."""
        if self.proc is None:
            print("[ERRO] sem processo local — `shell` exige subir o native_sim "
                  "por aqui (não use --no-spawn).")
            return
        print(f"[SHELL] {line}")
        self.proc.write_stdin(line)

    def print_tele(self):
        if self.last_tele is None:
            print(f"[TELE] nada recebido ainda na porta {self.pc_rx} "
                  f"({self.tele_count} pacotes).")
            return
        print(f"[TELE] pacote #{self.tele_count}:")
        for k, v in self.last_tele.items():
            if isinstance(v, float):
                print(f"    {k:24s} {v:12.4f}")
            else:
                print(f"    {k:24s} {v}")

    def close(self):
        self._stop.set()
        if self.sock:
            self.sock.close()
        if self.proc:
            self.proc.stop()


# --------------------------------------------------------------------- REPL
HELP = """
Comandos (os de bancada existem só na SIBA):

  arm              FUNC_START/STATUS_OK — energiza (pré-carga -> potência) e arma o ESC
  off              FUNC_START/STATUS_ERROR — desarma o ESC e abre os dois relés
  force on|off     sobrescreve / restaura o botão físico de emergência
  estop            FUNC_STOP — parada de emergência
  esc <us>         FUNC_ESC com o pulso em microssegundos (1000..2000)

  reset            FUNC_RESET — reinicia o firmware
  tele             imprime o último TELEMETRY_LOG recebido
  watch [n]        imprime a telemetria n vezes (padrão 5, ~1 s entre elas)
  shell <linha>    manda a linha ao shell do próprio firmware (ex.: shell btn on)
  raw f i t v a    CMD cru pelos ÍNDICES numéricos dos enums
  sleep <s>        pausa (útil em roteiro com -c)
  help             esta lista
  quit             sai (encerra o native_sim se foi subido aqui)
"""


def run_command(t, line):
    """Executa uma linha do prompt. Retorna False para encerrar."""
    line = line.strip()
    if not line or line.startswith("#"):
        return True
    parts = line.split()
    cmd, args = parts[0].lower(), parts[1:]

    if cmd in ("quit", "exit", "q"):
        return False
    if cmd == "help":
        print(HELP)
    elif cmd == "arm":
        t.send("FUNC_START", status="STATUS_OK", value=1.0, label="armar bancada")
    elif cmd == "off":
        t.send("FUNC_START", status="STATUS_ERROR", value=0.0, label="desligar bancada")
    elif cmd == "force":
        if not args or args[0] not in ("on", "off"):
            print("uso: force on|off")
        else:
            on = args[0] == "on"
            t.send("FUNC_START", value=0.0,
                   status="STATUS_EMERGENCY_OVERRIDE" if on else "STATUS_EMERGENCY_RESTORE",
                   label=f"sobrescrita do botao {'ON' if on else 'OFF'}")
    elif cmd == "estop":
        t.send("FUNC_STOP", value=0.0, label="parada de emergencia")
    elif cmd == "esc":
        if not args:
            print("uso: esc <us>   (1000..2000)")
        else:
            t.send("FUNC_ESC", value=float(args[0]), label=f"throttle {args[0]}us")
    elif cmd == "reset":
        t.send("FUNC_RESET", value=1.0, label="reiniciar placa")
    elif cmd == "tele":
        t.print_tele()
    elif cmd == "watch":
        n = int(args[0]) if args else 5
        for _ in range(n):
            t.print_tele()
            time.sleep(1.0)
    elif cmd == "shell":
        t.shell(" ".join(args))
    elif cmd == "raw":
        if len(args) != 5:
            print("uso: raw <func> <instr> <target> <value> <action>  (índices)")
        else:
            f, i, tg, a = (int(args[0]), int(args[1]), int(args[2]), int(args[4]))
            v = float(args[3])
            raw = codecs.encode_cmd(f, i, tg, v, a)
            print(f"[TX] raw  ->  {raw.hex(' ').upper()}")
            t.sock.sendto(raw, (t.ip, t.board_rx))
    elif cmd == "sleep":
        time.sleep(float(args[0]) if args else 1.0)
    else:
        print(f"comando desconhecido: {cmd}  (use 'help')")
    return True


def main():
    ap = argparse.ArgumentParser(
        description="Exercita o firmware em native_sim pela linha de comando.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=HELP,
    )
    ap.add_argument("board", nargs="?", default="SIBA", choices=BOARDS + tuple(b.lower() for b in BOARDS),
                    help="placa a testar (padrão: SIBA)")
    ap.add_argument("--no-spawn", action="store_true",
                    help="não sobe o native_sim; usa um já em execução")
    ap.add_argument("-c", "--command", action="append", default=[], metavar="CMD",
                    help="executa o comando e segue (repetível); sem prompt no fim")
    args = ap.parse_args()

    t = NativeTester(args.board.upper(), spawn=not args.no_spawn)
    print(f"\n=== {t.board} em native_sim — CMD para {t.ip}:{t.board_rx}, "
          f"telemetria na :{t.pc_rx} ===")
    print("'help' lista os comandos.\n")

    try:
        for line in args.command:
            if not run_command(t, line):
                return
        if args.command:
            time.sleep(0.5)
            return
        while True:
            try:
                line = input(f"{t.board.lower()}> ")
            except (EOFError, KeyboardInterrupt):
                print()
                break
            if not run_command(t, line):
                break
    finally:
        t.close()


if __name__ == "__main__":
    main()
