"""Camada 4 · Apresentação — popout de estimação dos ganhos do SMO.

Recebe R/L/Ts/maxiter/popsize, roda Scripts/autotune_cost.py via
EstimationRunner e mostra progresso em tempo real (parseando stdout:
PROGRESS/GEN/RESULT, levado à UI por sinais Qt). Resultado final em Γ
(invariante ao passo, o que o firmware recebe) — pergunta antes de gravar
no SAME.yaml.
"""
import os
import time

import pyqtgraph as pg
import yaml
from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDoubleSpinBox, QFormLayout, QGroupBox,
    QHBoxLayout, QLabel, QLineEdit, QMessageBox, QPlainTextEdit, QPushButton,
    QSpinBox, QVBoxLayout,
)

from Communication_Data.estimation import EstimationRunner


def _parse_kv(line):
    """'RESULT Ks1=1 Ks2=2 ...' -> {'Ks1':1.0, ...} (só tokens k=v numéricos)."""
    out = {}
    for tok in line.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            try:
                out[k] = float(v)
            except ValueError:
                out[k] = v
    return out


class EstimationDialog(QDialog):
    # Sinais emitidos da thread de relay -> atualizam a UI com segurança.
    sig_progress = Signal(int, float, float)   # eval, cost, best
    sig_gen = Signal(int, float)               # iter, best
    sig_result = Signal(dict)
    sig_log = Signal(str)
    sig_done = Signal(int)

    def __init__(self, config_dir, same_yaml_path, parent=None):
        super().__init__(parent)
        self.config_dir = config_dir
        self.same_yaml_path = same_yaml_path
        self.setWindowTitle("Estimar Ganhos do SMO (autotuning)")
        self.setWindowFlags(Qt.Window)
        self.resize(1000, 780)

        self.runner = EstimationRunner.from_config_dir(
            config_dir, log_fn=lambda m: self.sig_log.emit(m)
        )
        self._result = None
        self._saved_figs = []

        self._build_ui()
        self._prefill_from_motor()

        self.sig_progress.connect(self._on_progress)
        self.sig_gen.connect(self._on_gen)
        self.sig_result.connect(self._on_result)
        self.sig_log.connect(self._append_log)
        self.sig_done.connect(self._on_done)

    # ------------------------------------------------------------------ #
    def _build_ui(self):
        root = QVBoxLayout(self)

        # --- Parâmetros da bancada (R, L usados) ---
        params = QGroupBox("Parâmetros da bancada (motor)")
        form = QFormLayout(params)
        self.ed_R = QLineEdit("0.0173")
        self.ed_L = QLineEdit("7e-6")
        self.ed_Ts = QLineEdit("10e-6")
        self.sp_maxiter = QSpinBox(); self.sp_maxiter.setRange(1, 500); self.sp_maxiter.setValue(30)
        self.sp_popsize = QSpinBox(); self.sp_popsize.setRange(3, 100); self.sp_popsize.setValue(10)
        form.addRow("Resistência R [Ω]:", self.ed_R)
        form.addRow("Indutância L [H]:", self.ed_L)
        form.addRow("Passo Ts [s]:", self.ed_Ts)
        form.addRow("Gerações (maxiter):", self.sp_maxiter)
        form.addRow("População (popsize):", self.sp_popsize)
        self.chk_plot = QCheckBox("Plotar o resultado final (gera figuras ao terminar)")
        form.addRow(self.chk_plot)

        # --- Função de custo: define CONTRA O QUE os Γ são otimizados -----
        cost = QGroupBox("Função de custo")
        cform = QFormLayout(cost)

        self.cb_metric = QComboBox()
        # (rótulo na GUI, valor passado ao script)
        for _lbl, _val in (("RMS (rmse)", "rmse"),
                           ("Média (mae)", "mae"),
                           ("Huber", "huber")):
            self.cb_metric.addItem(_lbl, _val)
        self.cb_metric.setCurrentIndex(0)      # RMS: pesos valem o que dizem
        self.cb_metric.setToolTip(
            "Como o erro no tempo vira número.\n\n"
            "RMS e Média mantêm os ganhos abaixo com o peso declarado.\n\n"
            "Huber é quadrático para resíduo pequeno e linear para grande, "
            "então ele COMPRIME o termo de resíduo menor. Medido nesta "
            "bancada, o torque cai no ramo quadrático e o rpm no linear: o "
            "rpm acaba pesando ~1,6x o torque mesmo com ganho de torque = 2x "
            "o de rpm.")

        self.sp_wtq = QDoubleSpinBox()
        self.sp_wtq.setRange(0.0, 100.0); self.sp_wtq.setSingleStep(0.1)
        self.sp_wtq.setDecimals(2); self.sp_wtq.setValue(1.0)
        self.sp_wtq.setToolTip("Peso do erro de TORQUE no custo. É a grandeza "
                               "que a bancada entrega — normalmente o maior.")

        self.sp_wrpm = QDoubleSpinBox()
        self.sp_wrpm.setRange(0.0, 100.0); self.sp_wrpm.setSingleStep(0.1)
        self.sp_wrpm.setDecimals(2); self.sp_wrpm.setValue(0.5)
        self.sp_wrpm.setToolTip("Peso do erro de ROTAÇÃO no custo.")

        cform.addRow("Métrica do erro:", self.cb_metric)
        cform.addRow("Ganho torque:", self.sp_wtq)
        cform.addRow("Ganho rpm:", self.sp_wrpm)

        self.lbl_cost_hint = QLabel()
        self.lbl_cost_hint.setWordWrap(True)
        self.lbl_cost_hint.setStyleSheet("color:#888; font-size:8pt;")
        cform.addRow(self.lbl_cost_hint)
        self.cb_metric.currentIndexChanged.connect(self._update_cost_hint)
        self._update_cost_hint()

        # Os dois grupos são formulários curtos: lado a lado em vez de
        # empilhados, para sobrar altura pra curva de custo — que é o que se
        # fica olhando enquanto a otimização roda.
        top = QHBoxLayout()
        top.addWidget(params, 1)
        top.addWidget(cost, 1)
        root.addLayout(top)

        # --- Controles ---
        ctrl = QHBoxLayout()
        self.btn_run = QPushButton("▶ Rodar Estimação")
        self.btn_run.clicked.connect(self._start)
        self.btn_stop = QPushButton("■ Parar")
        self.btn_stop.setEnabled(False)
        self.btn_stop.clicked.connect(self._stop)
        ctrl.addWidget(self.btn_run); ctrl.addWidget(self.btn_stop); ctrl.addStretch()
        root.addLayout(ctrl)

        # --- Status ao vivo ---
        self.lbl_status = QLabel("Iteração: 0 | Avaliações: 0 | Custo atual: — | Melhor: —")
        self.lbl_status.setStyleSheet("font-weight: bold;")
        root.addWidget(self.lbl_status)

        # --- Curva de custo em tempo real ---
        self.plot = pg.PlotWidget()
        self.plot.setLabel("bottom", "Avaliação")
        self.plot.setLabel("left", "Custo J")
        self.plot.setLogMode(y=True)
        self.plot.addLegend()
        self.curve_cost = self.plot.plot(pen=pg.mkPen("#1E90FF", width=1), name="Custo")
        self.curve_best = self.plot.plot(pen=pg.mkPen("#2ecc40", width=2), name="Melhor")
        # A curva de custo é o foco da tela: cresce com a janela (stretch alto)
        # e tem piso generoso. Antes estava capada em 200 px, o que a espremia
        # justamente no que se passa mais tempo olhando.
        self.plot.setMinimumHeight(300)
        root.addWidget(self.plot, 5)
        self._evals, self._costs, self._bests = [], [], []

        # --- Log ---
        self.log = QPlainTextEdit(); self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(2000)
        self.log.setStyleSheet("background:#0F0F0F; color:#DCDCDC; font-family:monospace; font-size:9pt;")
        self.log.setMinimumHeight(90)
        root.addWidget(self.log, 1)   # cede espaço para o gráfico

        # --- Resultado + aplicar ---
        self.grp_result = QGroupBox("Resultado")
        rl = QHBoxLayout(self.grp_result)
        self.lbl_result = QLabel("—")
        self.btn_apply = QPushButton("Aplicar ao SAME.yaml")
        self.btn_apply.setEnabled(False)
        self.btn_apply.clicked.connect(self._apply)
        rl.addWidget(self.lbl_result, 1); rl.addWidget(self.btn_apply)
        root.addWidget(self.grp_result)

    def _prefill_from_motor(self):
        """Pré-preenche R/L a partir do motor do SAME.yaml, se existir."""
        try:
            data = yaml.safe_load(open(self.same_yaml_path)) or {}
        except Exception:
            return
        motors = data.get("motors", {}) or {}
        for _name, mot in motors.items():
            if not isinstance(mot, dict):
                continue
            r = mot.get("resistance") or mot.get("R")
            l = mot.get("inductance") or mot.get("L")
            if r is not None: self.ed_R.setText(str(r))
            if l is not None: self.ed_L.setText(str(l))
            break

    # ------------------------------------------------------------------ #
    def _start(self):
        try:
            R = float(self.ed_R.text()); L = float(self.ed_L.text()); Ts = float(self.ed_Ts.text())
        except ValueError:
            QMessageBox.warning(self, "Entrada inválida", "R, L e Ts devem ser numéricos.")
            return
        # limpa estado
        self._evals, self._costs, self._bests = [], [], []
        self.curve_cost.setData([], []); self.curve_best.setData([], [])
        self.log.clear(); self._result = None; self._saved_figs = []
        self.btn_apply.setEnabled(False); self.lbl_result.setText("—")

        self.json_out = os.path.join(self.config_dir, "..", "Scripts", "smo_gains.json")
        metric = self.cb_metric.currentData()
        w_tq   = self.sp_wtq.value()
        w_rpm  = self.sp_wrpm.value()

        ok = self.runner.run_smo_gains(
            R=R, L=L, Ts=Ts, plot=self.chk_plot.isChecked(),
            maxiter=self.sp_maxiter.value(), popsize=self.sp_popsize.value(),
            time_metric=metric, w_tq=w_tq, w_rpm=w_rpm,
            on_line=self._on_line, on_done=lambda rc: self.sig_done.emit(rc),
        )
        if ok:
            self.btn_run.setEnabled(False); self.btn_stop.setEnabled(True)
            self._append_log(f"[GUI] Iniciado (R={R} Ω, L={L} H, Ts={Ts} s).")
            self._append_log(f"[GUI] Custo: métrica={metric} | "
                             f"ganho torque={w_tq} | ganho rpm={w_rpm}.")

    def _update_cost_hint(self):
        """Avisa quando a métrica escolhida re-pondera os ganhos por conta."""
        if self.cb_metric.currentData() == "huber":
            self.lbl_cost_hint.setText(
                "⚠ Huber comprime o termo de resíduo menor. Nesta bancada o "
                "rpm acaba pesando ~1,6× o torque mesmo com ganho de torque "
                "maior — os valores acima deixam de valer o que dizem.")
        else:
            self.lbl_cost_hint.setText(
                "Os dois termos entram com o peso declarado.")

    def _stop(self):
        self.runner.stop_all()
        self._append_log("[GUI] Estimação interrompida pelo usuário.")
        self._on_done(-1)

    # --- parsing do stdout (thread do subprocesso) ---
    def _on_line(self, line):
        if line.startswith("PROGRESS"):
            d = _parse_kv(line)
            self.sig_progress.emit(int(d.get("eval", 0)), d.get("cost", float("nan")),
                                   d.get("best", float("nan")))
        elif line.startswith("GEN"):
            d = _parse_kv(line)
            self.sig_gen.emit(int(d.get("iter", 0)), d.get("best", float("nan")))
        elif line.startswith("RESULT"):
            self.sig_result.emit(_parse_kv(line))
        elif line.startswith("salvo:"):
            self._saved_figs.append(line.split("salvo:", 1)[1].strip())

    # --- slots na UI ---
    def _on_progress(self, ev, cost, best):
        self._evals.append(ev); self._costs.append(cost); self._bests.append(best)
        self.curve_cost.setData(self._evals, self._costs)
        self.curve_best.setData(self._evals, self._bests)
        self._it = getattr(self, "_it", 0)
        self.lbl_status.setText(
            f"Iteração: {self._it} | Avaliações: {ev} | "
            f"Custo atual: {cost:.4f} | Melhor: {best:.4f}")

    def _on_gen(self, it, best):
        self._it = it

    def _append_log(self, msg):
        self.log.appendPlainText(msg)

    def _on_result(self, d):
        self._result = d
        # Mostra os ganhos na forma Γ (Gamma = K·Ts, invariante ao passo) — é o
        # que se configura no firmware da SAME. Ks* dependem de Ts (só referência).
        self.lbl_result.setText(
            f"<b>Γ1</b>={d.get('Gamma_s1', 0):.4f}  "
            f"<b>Γ2</b>={d.get('Gamma_s2', 0):.4f}  "
            f"<b>Γt</b>={d.get('Gamma_t', 0):.5f}  |  custo={d.get('cost', 0):.4f}")
        self.btn_apply.setEnabled(True)

    def _on_done(self, rc):
        self.btn_run.setEnabled(True); self.btn_stop.setEnabled(False)
        if rc == 0 and self._result is not None:
            self._append_log("[GUI] Concluído. Confira o resultado e aplique se desejar.")
            if self._saved_figs:
                QMessageBox.information(
                    self, "Figuras geradas",
                    "Figuras salvas:\n" + "\n".join(self._saved_figs))
        elif rc not in (0, -1):
            self._append_log(f"[GUI] Terminou com erro (rc={rc}). Veja o log acima.")

    # ------------------------------------------------------------------ #
    def _apply(self):
        if not self._result:
            return
        r = self._result
        resp = QMessageBox.question(
            self, "Aplicar ganhos",
            f"Gravar no SAME.yaml (bloco smo_gains)?\n\n"
            f"Ganhos Γ (invariantes ao passo — é o que o firmware recebe):\n"
            f"Γ1 (Gamma_s1) = {r.get('Gamma_s1', 0):.5f}\n"
            f"Γ2 (Gamma_s2) = {r.get('Gamma_s2', 0):.5f}\n"
            f"Γt (Gamma_t)  = {r.get('Gamma_t', 0):.6f}",
            QMessageBox.Yes | QMessageBox.No)
        if resp != QMessageBox.Yes:
            return
        try:
            data = yaml.safe_load(open(self.same_yaml_path)) or {}
            # Γ é o primário (o que vai por CMD p/ TARGET_GAMMA1/2/T). Ks* ficam
            # só como referência @ Ts_sim (dependem do passo).
            data["smo_gains"] = {
                "Gamma_s1": float(r.get("Gamma_s1", 0)), "Gamma_s2": float(r.get("Gamma_s2", 0)),
                "Gamma_t": float(r.get("Gamma_t", 0)),
                "Ks1_ref": float(r.get("Ks1", 0)), "Ks2_ref": float(r.get("Ks2", 0)),
                "Kt_ref": float(r.get("Kt", 0)),
                "Ts_sim": float(r.get("Ts", 0)), "cost": float(r.get("cost", 0)),
                "R": float(self.ed_R.text()), "L": float(self.ed_L.text()),
                "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
            }
            with open(self.same_yaml_path, "w") as f:
                yaml.safe_dump(data, f, sort_keys=False, allow_unicode=True)
            self._append_log("[GUI] Ganhos gravados em SAME.yaml (smo_gains).")
            QMessageBox.information(self, "Aplicado", "Ganhos gravados em SAME.yaml.")
        except Exception as e:
            QMessageBox.warning(self, "Erro ao gravar", str(e))
