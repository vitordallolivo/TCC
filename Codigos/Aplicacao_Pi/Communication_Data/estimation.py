"""Camada 3 · Serviço — runner de scripts de estimação (Aplicacao_Pi/Scripts).

Roda os scripts de estimação como subprocesso (mesmo padrão do native_sim:
Popen + relay de stdout), sem travar a GUI. Quem decide *aplicar* os resultados
é a camada de apresentação (EstimationDialog) — este runner só executa, faz o
stream das linhas de progresso (`on_line`) e devolve o código de saída.

Scripts (em Aplicacao_Pi/Scripts/, trazidos de ../Equations):
- autotune_cost.py  -> ganhos do SMO (recebe --R/--L/--Ts; gera smo_gains.json)
- test_coeff_auto.py -> simulação do SMO (dependência do autotune/hélice)
- plot_pi.py        -> figuras do controle (dependência opcional)
- helice_coeff.py   -> coeficiente kQ da hélice
"""
import os
import subprocess
import sys
import threading


class EstimationRunner:
    def __init__(self, equations_dir, python_exe=None, log_fn=print):
        # ABSOLUTO sempre: run() passa o caminho do script + cwd=equations_dir;
        # se fosse relativo, o cwd duplicaria o caminho (Scripts/Scripts/...).
        self.equations_dir = os.path.abspath(equations_dir)
        self.python_exe = python_exe or sys.executable
        self.log_fn = log_fn
        self._procs = {}

    @classmethod
    def from_config_dir(cls, config_dir, **kwargs):
        """scripts_dir = Aplicacao_Pi/Scripts (irmã de Config/)."""
        sd = os.path.normpath(os.path.join(config_dir, "..", "Scripts"))
        return cls(sd, **kwargs)

    def script_path(self, name):
        return os.path.join(self.equations_dir, name)

    def run(self, script_name, args=None, on_line=None, on_done=None):
        """Roda um script em subprocesso. Relay de stdout via thread; chama
        `on_line(raw_line)` por linha (p/ a GUI parsear PROGRESS/RESULT) e
        `on_done(returncode)` ao terminar. Retorna True se iniciou."""
        args = args or []
        path = self.script_path(script_name)
        if not os.path.exists(path):
            self.log_fn(f"[ESTIM] Script não encontrado: {path}")
            return False
        if script_name in self._procs:
            self.log_fn(f"[ESTIM] {script_name} já está rodando.")
            return False

        try:
            proc = subprocess.Popen(
                [self.python_exe, path, *map(str, args)],
                cwd=self.equations_dir,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1,
            )
        except Exception as e:
            self.log_fn(f"[ESTIM] Falha ao iniciar {script_name}: {e}")
            return False

        self._procs[script_name] = proc
        self.log_fn(f"[ESTIM] Rodando {script_name} (PID {proc.pid})...")

        def _relay():
            for line in proc.stdout:
                raw = line.rstrip()
                if on_line:
                    on_line(raw)
                self.log_fn(f"[{script_name}] {raw}")
            rc = proc.wait()
            self._procs.pop(script_name, None)
            self.log_fn(f"[ESTIM] {script_name} terminou (rc={rc}).")
            if on_done:
                on_done(rc)

        threading.Thread(target=_relay, name=f"Estim_{script_name}", daemon=True).start()
        return True

    # Conveniências nomeadas -------------------------------------------------
    def run_smo_gains(self, R=None, L=None, Ts=None, plot=False, maxiter=None,
                      popsize=None, json_path=None, on_line=None, on_done=None):
        """Ganhos do SMO -> autotune_cost.py. Passa R/L/Ts da bancada."""
        args = []
        if R is not None: args += ["--R", R]
        if L is not None: args += ["--L", L]
        if Ts is not None: args += ["--Ts", Ts]
        if maxiter is not None: args += ["--maxiter", maxiter]
        if popsize is not None: args += ["--popsize", popsize]
        if plot: args += ["--plot"]
        if json_path: args += ["--json", json_path]
        return self.run("autotune_cost.py", args=args, on_line=on_line, on_done=on_done)

    def run_helice_coeff(self, csv_path=None, thrust_col=None, torque_col=None,
                         rpm_col=None, v_col=None, v_fixed=None, D=None, rho=None,
                         plot=False, outdir=None, on_line=None, on_done=None):
        """Coeficientes da hélice (CT, CQ, J) -> helice_coeff.py --csv.

        Consome o CSV completo de telemetria; `thrust_col` (tração) é a força
        escolhida, `torque_col` por padrão o estimado pela SAME. Sem csv_path,
        roda o modo simulação embutido do script."""
        args = []
        if csv_path: args += ["--csv", csv_path]
        if thrust_col: args += ["--thrust-col", thrust_col]
        if torque_col: args += ["--torque-col", torque_col]
        if rpm_col: args += ["--rpm-col", rpm_col]
        if v_col: args += ["--v-col", v_col]
        if v_fixed is not None: args += ["--v-fixed", v_fixed]
        if D is not None: args += ["--D", D]
        if rho is not None: args += ["--rho", rho]
        if plot: args += ["--plot"]
        if outdir: args += ["--outdir", outdir]
        return self.run("helice_coeff.py", args=args, on_line=on_line, on_done=on_done)

    def smo_gains_path(self):
        return self.script_path("smo_gains.json")

    def is_running(self, script_name=None):
        if script_name:
            return script_name in self._procs
        return bool(self._procs)

    def stop_all(self):
        for name, proc in list(self._procs.items()):
            try:
                proc.terminate()
            except Exception:
                pass
            self._procs.pop(name, None)
