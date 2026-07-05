"""Camada 1 · Transporte — subprocesso do firmware real (modo 3 · native_sim).

Sobe o binário do firmware compilado para a board `native_sim` do Zephyr
(`make native_sim` -> build_native_sim/zephyr/zephyr.exe) como processo POSIX
no host e faz o relay do stdout para o console. Diferente do BoardSimulator
(fake Python), aqui é o firmware DE VERDADE trocando UDP real em loopback
(NSOS — native offloaded sockets, ver <board>/boards/native_sim.conf).

Só roda no **Linux**: `zephyr.exe` é um ELF POSIX de Linux.

Dois modos de comando conforme o transporte da placa:
- SIBA/SAME (udp_struct): comandos chegam por UDP (nada de stdin).
- SPMM (telnet): em native o firmware usa o **shell serial** ligado ao
  stdin/stdout (`-uart_stdinout`), pois o backend Telnet TCP fica desligado
  em native_sim (ver SPMM/boards/native_sim.conf). Por isso subimos com
  `extra_args=['-uart_stdinout']` e `pipe_stdin=True`, e os comandos de texto
  vão pelo `write_stdin()`.
"""
import os
import signal
import subprocess
import sys
import threading


def native_sim_supported():
    """native_sim (zephyr.exe ELF POSIX) só executa no Linux."""
    return sys.platform.startswith("linux")


def _pdeathsig_preexec():
    """Linux: faz o KERNEL matar este filho (zephyr.exe) quando o processo pai
    (a app Python) morrer — INCLUSIVE em segfault/kill -9, onde stop_native_sim
    não chega a rodar. Evita `zephyr.exe` órfãos acumulando e streamando UDP na
    porta 5000 (causa de tráfego 7x + dados misturados + travamentos)."""
    try:
        import ctypes
        PR_SET_PDEATHSIG = 1
        ctypes.CDLL("libc.so.6", use_errno=True).prctl(PR_SET_PDEATHSIG, signal.SIGTERM)
    except Exception:
        pass


def kill_stray_native_sims():
    """Mata `zephyr.exe` órfãos (de execuções anteriores que crasharam sem
    limpar). Chamado no boot da app. Só Linux."""
    if not native_sim_supported():
        return 0
    try:
        r = subprocess.run(["pkill", "-x", "zephyr.exe"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            print("[NATIVE_SIM] Processos zephyr.exe órfãos encerrados no boot.")
        return r.returncode
    except Exception as e:
        print(f"[NATIVE_SIM] Falha ao limpar órfãos: {e}")
        return -1


class NativeSimProcess:
    """Gerencia um processo `native_sim` (spawn + relay de log + terminate)."""

    def __init__(self, board_id, exe_path, extra_args=None, pipe_stdin=False):
        self.board_id = board_id
        self.exe_path = exe_path
        self.extra_args = list(extra_args or [])
        self.pipe_stdin = pipe_stdin
        self.proc = None
        self.log_thread = None

    @property
    def pid(self):
        return self.proc.pid if self.proc else None

    def start(self):
        """Sobe o processo. Retorna True em sucesso, False em falha."""
        if not native_sim_supported():
            print("[NATIVE_SIM] Indisponível: só é possível no Linux "
                  "(zephyr.exe é um binário POSIX de Linux).")
            return False
        if not os.path.exists(self.exe_path):
            print(f"[NATIVE_SIM] Binário não encontrado: {self.exe_path}")
            print("[NATIVE_SIM] Compile com 'make native_sim' no diretório do firmware.")
            return False
        try:
            self.proc = subprocess.Popen(
                [self.exe_path, *self.extra_args],
                stdin=subprocess.PIPE if self.pipe_stdin else None,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1,
                # Linux: o filho recebe SIGTERM se a app Python morrer (mesmo
                # em segfault) — nada de zephyr.exe órfão sobrando na porta 5000.
                preexec_fn=_pdeathsig_preexec if native_sim_supported() else None,
            )
        except Exception as e:
            print(f"[NATIVE_SIM] Falha ao iniciar {self.board_id}: {e}")
            return False

        def _relay_log():
            for line in self.proc.stdout:
                print(f"[{self.board_id}-native_sim] {line.rstrip()}")

        self.log_thread = threading.Thread(
            target=_relay_log, name=f"NativeSimLog_{self.board_id}", daemon=True
        )
        self.log_thread.start()
        print(f"[NATIVE_SIM] {self.board_id} rodando como processo nativo (PID {self.proc.pid}).")
        return True

    def write_stdin(self, text):
        """Envia um comando de texto ao shell serial do firmware (via stdin).

        Usado no modo native da SPMM (shell serial `-uart_stdinout`). Retorna
        True se escreveu."""
        if self.proc is None or self.proc.stdin is None:
            return False
        try:
            self.proc.stdin.write(str(text).rstrip() + "\n")
            self.proc.stdin.flush()
            return True
        except Exception as e:
            print(f"[NATIVE_SIM] Falha ao escrever no stdin de {self.board_id}: {e}")
            return False

    def stop(self):
        """Encerra o processo (terminate -> kill se travar)."""
        if self.proc is None:
            return
        print(f"[NATIVE_SIM] Encerrando {self.board_id} (PID {self.proc.pid})...")
        self.proc.terminate()
        try:
            self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.proc = None
