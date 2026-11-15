#!/usr/bin/env python3
"""Comandos da placa de medição pela linha de comando (Linux ou Windows).

Usa a mesma gramática do TinyTerminal (`Communication_Data/comandos.py`) e o
mesmo protocolo da GUI: CMD de 8 bytes por UDP e telemetria por broadcast.

    python placa.py set i_a linear 0.0809     # um comando e sai
    python placa.py tele                      # última telemetria
    python placa.py -c arm -c "esc 20" -c "watch 5" -c off
    python placa.py                           # prompt interativo
    python placa.py --native                  # sobe o firmware em native_sim

Comandos que só enviam (set, arm, esc, ...) funcionam com a GUI aberta.
Os que leem telemetria (tele, watch, errs) precisam da porta 5555 livre,
ou seja, com a GUI fechada.
"""
import argparse
import os
import socket
import sys
import threading
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
if AQUI not in sys.path:
    sys.path.insert(0, AQUI)

from Communication_Data import codecs, errors                 # noqa: E402
from Communication_Data import motor_config as mc             # noqa: E402
from Communication_Data.comandos import (                     # noqa: E402
    AJUDA, ErroComando, Protocolo,
)
from Communication_Data.native_sim import (                   # noqa: E402
    NativeSimProcess, kill_stray_native_sims, native_sim_supported,
)

CONFIG_DIR = os.path.join(AQUI, "Config")
NATIVE_EXE = os.path.join(AQUI, "..", "firmware", "build_native_sim",
                          "zephyr", "zephyr.exe")

AJUDA_LOCAL = """
  tele [campo ...]             última telemetria (todos os campos ou os pedidos)
  watch [n] [campo ...]        telemetria n vezes, 1 por segundo (padrão 5)
  errs                         erros ativos, decodificados
  motor [perfil]               aplica um perfil de Config/motors/ (sem nome: lista)
  alvos                        sensores e parâmetros aceitos pelo 'set'
  shell <linha>                shell do firmware (só com --native)
  sleep <s>                    pausa (útil em roteiro com -c)
  help | quit
"""


def loopback(ip):
    """127.0.0.<último octeto>: endereço do native_sim (native_sim.conf)."""
    try:
        o = int(str(ip).split(".")[-1])
        if 1 <= o <= 254:
            return f"127.0.0.{o}"
    except ValueError:
        pass
    return "127.0.0.1"


class Placa:
    """Link UDP com a placa (e, opcionalmente, o firmware em native_sim)."""

    def __init__(self, ip=None, native=False, quiet=False):
        self.proto = Protocolo.de_pasta(CONFIG_DIR)
        link = self.proto.raw.get("link", {})
        portas = link.get("ports", {})
        self.board_rx = portas.get("board_rx", 5000)
        self.pc_rx = portas.get("pc_rx", 5555)
        self.ip = ip or link.get("ip", "169.254.97.200")
        self.quiet = quiet
        self.layout = codecs.telemetry_layout(self.proto.raw)
        errors.ensure_registered("PLACA", self.proto.raw)

        self.proc = None
        if native:
            self.ip = loopback(self.ip)
            self._subir_native()

        self.tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx = None
        self.last_tele = None
        self.tele_count = 0
        self.erros = 0              # comandos rejeitados (código de saída)
        self._parar = threading.Event()

    # --------------------------------------------------------------- native
    def _subir_native(self):
        if not native_sim_supported():
            raise SystemExit("native_sim só roda no Linux.")
        exe = os.path.normpath(NATIVE_EXE)
        if not os.path.exists(exe):
            raise SystemExit(f"Binário não encontrado: {exe}\n"
                             "Compile com 'make native_sim' na raiz do repositório.")
        kill_stray_native_sims()
        # -uart_stdinout: shell do firmware na stdin (comando 'shell').
        self.proc = NativeSimProcess("PLACA", exe, extra_args=["-uart_stdinout"],
                                     pipe_stdin=True)
        if not self.proc.start():
            raise SystemExit("Falha ao subir o native_sim.")
        time.sleep(1.5)   # deixa o firmware chegar no bind do socket

    # ------------------------------------------------------------ telemetria
    def escutar(self):
        """Abre a escuta da telemetria (uma vez). False se a porta estiver ocupada."""
        if self.rx is not None:
            return True
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind(("", self.pc_rx))
        except OSError as e:
            print(f"[ERRO] porta {self.pc_rx} ocupada ({e}). A GUI está aberta?")
            s.close()
            return False
        s.settimeout(0.3)
        self.rx = s
        threading.Thread(target=self._rx_loop, daemon=True).start()
        return True

    def _rx_loop(self):
        while not self._parar.is_set():
            try:
                data, _ = self.rx.recvfrom(2048)
            except (socket.timeout, OSError):
                continue
            t = codecs.decode_telemetry(self.layout, data)
            if t is not None:
                self.last_tele = t
                self.tele_count += 1

    def esperar_tele(self, timeout=2.0):
        """Garante a escuta e espera um pacote novo. True se chegou."""
        if not self.escutar():
            return False
        visto = self.tele_count
        fim = time.monotonic() + timeout
        while time.monotonic() < fim:
            if self.tele_count != visto:
                return True
            time.sleep(0.02)
        print(f"[ERRO] sem telemetria em {timeout:.0f} s na porta {self.pc_rx} "
              f"(placa em {self.ip}).")
        return False

    # ------------------------------------------------------------------- tx
    def enviar(self, cmd):
        if not self.quiet:
            print(f"[TX] {cmd.hex(' ').upper()}  {self.proto.descrever(cmd)}")
        self.tx.sendto(cmd, (self.ip, self.board_rx))

    def send(self, func, status=None, value=0.0, label="", instr="INSTR_WRITE",
             target="TARGET_DEFAULT"):
        """Atalho por nomes de enum (usado pelo test_emergencia)."""
        self.enviar(self.proto.cmd(func, instr=instr, alvo=target, valor=value,
                                   acao=status))
        return True

    def shell(self, linha):
        if self.proc is None:
            print("[ERRO] 'shell' só existe com --native.")
            return
        print(f"[SHELL] {linha}")
        self.proc.write_stdin(linha)

    def close(self):
        self._parar.set()
        for s in (self.rx, self.tx):
            if s:
                s.close()
        if self.proc:
            self.proc.stop()


# ----------------------------------------------------------------- comandos
def imprimir_tele(t, campos):
    if campos:
        faltam = [c for c in campos if c not in t]
        if faltam:
            print(f"[ERRO] campo(s) inexistente(s): {', '.join(faltam)}")
        linha = "  ".join(f"{c}={t[c]:.4g}" if isinstance(t[c], float) else f"{c}={t[c]}"
                          for c in campos if c in t)
        print(linha)
        return
    for k, v in t.items():
        print(f"    {k:24s} {v:12.4f}" if isinstance(v, float) else f"    {k:24s} {v}")


def aplicar_motor(p, args):
    perfis = mc.list_motor_files(CONFIG_DIR)
    if not args:
        print("Perfis em Config/motors/:", ", ".join(perfis) or "(nenhum)")
        return
    nome = " ".join(args)
    if nome not in perfis:
        print(f"[ERRO] perfil '{nome}' não encontrado.")
        return
    params = mc.load_motor_file(perfis[nome])
    cmds = mc.build_motor_cmds(p.proto.raw, params, skip_keys=mc.board_skip_keys("PLACA"))
    for c in cmds:
        p.enviar(c)
        time.sleep(0.05)   # cada parâmetro grava no NVM
    print(f"{len(cmds)} parâmetros de '{nome}' enviados.")


def executar(p, linha):
    """Executa uma linha. Retorna False para encerrar."""
    linha = linha.strip()
    if not linha or linha.startswith("#"):
        return True
    partes = linha.split()
    c, a = partes[0].lower(), partes[1:]

    if c in ("quit", "exit", "q"):
        return False
    if c == "help":
        print(AJUDA + "\n" + AJUDA_LOCAL)
    elif c == "tele":
        if p.esperar_tele():
            imprimir_tele(p.last_tele, a)
    elif c == "watch":
        n = int(a[0]) if a and a[0].isdigit() else 5
        campos = a[1:] if a and a[0].isdigit() else a
        for i in range(n):
            if not p.esperar_tele():
                break
            imprimir_tele(p.last_tele, campos)
            if i < n - 1:
                time.sleep(1.0)
    elif c == "errs":
        if p.esperar_tele():
            code = int(p.last_tele.get("error_code", 0))
            ativos = errors.decode_error_code(code, "PLACA")
            print(f"error_code = 0x{code:08X}")
            for mask, msg, grupo in ativos:
                print(f"    bit {mask.bit_length() - 1:2d}  [{grupo}] {msg}")
            if not ativos:
                print("    nenhum erro ativo")
    elif c == "alvos":
        sensores, params = p.proto.alvos()
        print("sensores:   ", " ".join(sensores))
        print("parâmetros: ", " ".join(params))
    elif c == "motor":
        aplicar_motor(p, a)
    elif c == "shell":
        p.shell(" ".join(a))
    elif c == "sleep":
        time.sleep(float(a[0]) if a else 1.0)
    else:
        try:
            for cmd in p.proto.interpretar(linha):
                p.enviar(cmd)
        except ErroComando as e:
            print(f"[ERRO] {e}")
            p.erros += 1
    return True


def main():
    ap = argparse.ArgumentParser(
        description="Comandos da placa de medição pela rede.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=AJUDA + "\n" + AJUDA_LOCAL,
    )
    ap.add_argument("comando", nargs=argparse.REMAINDER,
                    help="um comando para executar e sair (sem nada: prompt)")
    ap.add_argument("-c", action="append", default=[], metavar="CMD",
                    help="executa o comando (repetível) e sai")
    ap.add_argument("--ip", help="IP da placa (padrão: o do protocolo.yaml)")
    ap.add_argument("--native", action="store_true",
                    help="sobe o firmware em native_sim (Linux) e fala com ele")
    args = ap.parse_args()

    p = Placa(ip=args.ip, native=args.native)
    roteiro = list(args.c)
    if args.comando:
        roteiro.append(" ".join(args.comando))
    try:
        if roteiro:
            for linha in roteiro:
                if not executar(p, linha):
                    break
            time.sleep(0.2)   # deixa o último datagrama sair
            sys.exit(1 if p.erros else 0)
        print(f"Placa em {p.ip}:{p.board_rx}, telemetria na :{p.pc_rx}. "
              "'help' lista os comandos.")
        while True:
            try:
                linha = input("placa> ")
            except (EOFError, KeyboardInterrupt):
                print()
                break
            if not executar(p, linha):
                break
    finally:
        p.close()


if __name__ == "__main__":
    main()
