"""Camada 4 · Apresentação — Ensaios da SPMM (atrito B/τc e inércia J).

Porta o cliente de bancada de ../SPMM/CLAUDE.md para a GUI:
- **Atrito (patamares):** para cada throttle, estabiliza e mede (ω, Te) em
  regime permanente → regressão Te = B·ω + τc.
- **Inércia (coast-down):** sobe a rotação, `coast` corta o throttle, coleta
  ω(t) (t = n·Ts, do micro) → J = -(B·ω+τc)/(dω/dt) em janelas.

Orquestra pelo link Telnet da SPMM (comandos de texto) e coleta as amostras
UDP pelo gancho `gui._spmm_sample_sink`. Roda numa thread para não travar a UI.

SEGURANÇA (crítico): `disarm` + `throttle 1000` rodam SEMPRE no `finally` —
o motor nunca pode ficar girando se algo falhar (Ctrl+C do script vira o botão
"Parar" + o fechamento do diálogo aqui).
"""
import csv
import threading
import time

import numpy as np
from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QButtonGroup, QDialog, QDoubleSpinBox, QFileDialog, QFormLayout, QGroupBox,
    QHBoxLayout, QLabel, QLineEdit, QMessageBox, QPlainTextEdit, QPushButton,
    QRadioButton, QSpinBox, QVBoxLayout,
)

from Communication_Data import spmm_analysis as sa


class SPMMEnsaioDialog(QDialog):
    sig_log = Signal(str)
    sig_result = Signal(str)
    sig_done = Signal()

    def __init__(self, gui, parent=None):
        super().__init__(parent)
        self.gui = gui
        self.setWindowTitle("Ensaios SPMM — Atrito (B, τc) e Inércia (J)")
        self.setWindowFlags(Qt.Window)
        self.resize(620, 560)

        self._samples = []          # amostras coletadas (dict) durante o ensaio
        self._thread = None
        self._running = False
        self._rep = 0

        self._build_ui()
        self.sig_log.connect(lambda m: self.log.appendPlainText(m))
        self.sig_result.connect(self._show_result)
        self.sig_done.connect(self._on_done)

    # ------------------------------------------------------------------ #
    def _build_ui(self):
        root = QVBoxLayout(self)

        modo = QGroupBox("Tipo de ensaio")
        ml = QHBoxLayout(modo)
        self.rb_atrito = QRadioButton("Atrito (B, τc) — patamares")
        self.rb_inercia = QRadioButton("Inércia (J) — coast-down")
        self.rb_atrito.setChecked(True)
        grp = QButtonGroup(self); grp.addButton(self.rb_atrito); grp.addButton(self.rb_inercia)
        self.rb_atrito.toggled.connect(self._toggle_params)
        ml.addWidget(self.rb_atrito); ml.addWidget(self.rb_inercia)
        root.addWidget(modo)

        # Parâmetros — Atrito
        self.g_atrito = QGroupBox("Parâmetros — Atrito")
        fa = QFormLayout(self.g_atrito)
        self.ed_throttles = QLineEdit("1200,1300,1400,1500,1600,1700")
        self.sp_settle = QDoubleSpinBox(); self.sp_settle.setRange(0.2, 60); self.sp_settle.setValue(3.0); self.sp_settle.setSuffix(" s")
        fa.addRow("Throttles [µs] (vírgula):", self.ed_throttles)
        fa.addRow("Estabilização por patamar:", self.sp_settle)
        root.addWidget(self.g_atrito)

        # Parâmetros — Inércia
        self.g_inercia = QGroupBox("Parâmetros — Inércia (coast-down)")
        fi = QFormLayout(self.g_inercia)
        self.sp_thr_alto = QSpinBox(); self.sp_thr_alto.setRange(1000, 2000); self.sp_thr_alto.setValue(1800); self.sp_thr_alto.setSuffix(" µs")
        self.sp_estab = QDoubleSpinBox(); self.sp_estab.setRange(0.5, 60); self.sp_estab.setValue(4.0); self.sp_estab.setSuffix(" s")
        self.sp_loghz = QSpinBox(); self.sp_loghz.setRange(10, 5000); self.sp_loghz.setValue(1000); self.sp_loghz.setSuffix(" Hz")
        self.sp_dur = QDoubleSpinBox(); self.sp_dur.setRange(1, 120); self.sp_dur.setValue(8.0); self.sp_dur.setSuffix(" s")
        fi.addRow("Throttle alto:", self.sp_thr_alto)
        fi.addRow("Estabilização:", self.sp_estab)
        fi.addRow("Taxa de log:", self.sp_loghz)
        fi.addRow("Duração da captura:", self.sp_dur)
        root.addWidget(self.g_inercia)

        # B/τc conhecidos (para o cálculo de J)
        self.g_btc = QGroupBox("B, τc (do ensaio de atrito — usados no cálculo de J)")
        fb = QFormLayout(self.g_btc)
        self.sp_B = QDoubleSpinBox(); self.sp_B.setDecimals(6); self.sp_B.setRange(0, 10); self.sp_B.setValue(0.001)
        self.sp_tc = QDoubleSpinBox(); self.sp_tc.setDecimals(5); self.sp_tc.setRange(-10, 10); self.sp_tc.setValue(0.05)
        fb.addRow("B [N·m·s/rad]:", self.sp_B)
        fb.addRow("τc [N·m]:", self.sp_tc)
        root.addWidget(self.g_btc)

        # CSV + controles
        arq = QHBoxLayout()
        self.ed_csv = QLineEdit()
        self.ed_csv.setPlaceholderText("nome do arquivo .csv (obrigatório)...")
        btn_csv = QPushButton("..."); btn_csv.clicked.connect(self._pick_csv)
        arq.addWidget(QLabel("CSV:")); arq.addWidget(self.ed_csv, 1); arq.addWidget(btn_csv)
        root.addLayout(arq)

        ctrl = QHBoxLayout()
        self.btn_run = QPushButton("▶ Rodar Ensaio"); self.btn_run.clicked.connect(self._start)
        self.btn_stop = QPushButton("■ Parar (seguro)"); self.btn_stop.setEnabled(False); self.btn_stop.clicked.connect(self._stop)
        self.lbl_rep = QLabel("Repetições: 0")
        ctrl.addWidget(self.btn_run); ctrl.addWidget(self.btn_stop); ctrl.addStretch(); ctrl.addWidget(self.lbl_rep)
        root.addLayout(ctrl)

        self.log = QPlainTextEdit(); self.log.setReadOnly(True); self.log.setMaximumBlockCount(2000)
        self.log.setStyleSheet("background:#0F0F0F; color:#DCDCDC; font-family:monospace; font-size:9pt;")
        root.addWidget(self.log, 1)
        self.lbl_result = QLabel("—"); self.lbl_result.setStyleSheet("font-weight:bold;")
        self.lbl_result.setWordWrap(True)
        root.addWidget(self.lbl_result)

        self._toggle_params()

    def _toggle_params(self):
        atrito = self.rb_atrito.isChecked()
        self.g_atrito.setVisible(atrito)
        self.g_inercia.setVisible(not atrito)
        self.g_btc.setVisible(not atrito)

    def _pick_csv(self):
        path, _ = QFileDialog.getSaveFileName(self, "CSV do ensaio", "ensaio_spmm.csv", "CSV (*.csv)")
        if path:
            self.ed_csv.setText(path)

    # ------------------------------------------------------------------ #
    def _spmm(self, cmd):
        """Envia um comando de texto ao shell da SPMM."""
        self.gui.eth.send_command("SPMM", cmd)

    def _start(self):
        if self._running:
            return
        if not self.ed_csv.text().strip():
            QMessageBox.warning(self, "CSV", "Defina o nome do arquivo .csv.")
            return
        if "SPMM" not in self.gui.eth.workers:
            self.gui.eth.start_link("SPMM")
        self._running = True
        self.btn_run.setEnabled(False); self.btn_stop.setEnabled(True)
        # Instala o gancho de coleta de amostras (t = n·Ts).
        self._samples = []
        self.gui._spmm_sample_sink = self._collect
        atrito = self.rb_atrito.isChecked()
        self._thread = threading.Thread(
            target=self._run_atrito if atrito else self._run_inercia, daemon=True)
        self._thread.start()

    def _stop(self):
        self._running = False   # o loop do ensaio checa isso e sai pelo finally

    def _collect(self, decoded):
        # t = n·Ts vem em decoded['timestamp'] (ms) — base de tempo do micro.
        self._samples.append({
            "n": decoded.get("n"),
            "t": decoded.get("timestamp", 0.0) / 1000.0,
            "omega": decoded.get("omega"),
            "te": decoded.get("te"),
        })

    def _sleep(self, s):
        """Espera respeitando o botão Parar."""
        t0 = time.time()
        while time.time() - t0 < s and self._running:
            time.sleep(0.02)

    # ------------------------------------------------------------------ #
    def _run_atrito(self):
        try:
            throttles = [int(x) for x in self.ed_throttles.text().replace(";", ",").split(",") if x.strip()]
            settle = self.sp_settle.value()
            self.sig_log.emit(f"[Atrito] arm + {len(throttles)} patamares...")
            self._spmm("arm"); self._sleep(2.0)
            pares = []
            for us in throttles:
                if not self._running:
                    break
                self._spmm(f"throttle {us}")
                start = len(self._samples)
                self._sleep(settle)
                seg = [s for s in self._samples[start:] if s["omega"] is not None]
                if not seg:
                    self.sig_log.emit(f"  throttle {us}: sem amostras (SPMM enviando log?)")
                    continue
                omega = float(np.mean([s["omega"] for s in seg]))
                te = float(np.mean([s["te"] for s in seg]))
                pares.append((us, omega, te))
                self.sig_log.emit(f"  throttle {us}: ω={omega:.2f} rad/s  Te={te:.4f} N·m  ({len(seg)} amostras)")
            self._spmm("throttle 1000")
            if len(pares) >= 2:
                B, tc, r2 = sa.fit_atrito([p[1] for p in pares], [p[2] for p in pares])
                self._save_csv("atrito", [{"patamar_us": p[0], "omega": p[1], "te": p[2]} for p in pares])
                alerta = "  ⚠ R²<0,98" if r2 < 0.98 else ""
                alerta += "  ⚠ τc<0" if tc < 0 else ""
                self.sig_result.emit(f"ATRITO: B = {B:.5g} N·m·s/rad | τc = {tc:.5g} N·m | R² = {r2:.4f}{alerta}")
            else:
                self.sig_result.emit("ATRITO: dados insuficientes (nenhum patamar com amostras).")
        except Exception as e:
            self.sig_log.emit(f"[ERRO Atrito] {e}")
        finally:
            self._safe_shutdown()

    def _run_inercia(self):
        try:
            thr = self.sp_thr_alto.value(); estab = self.sp_estab.value()
            hz = self.sp_loghz.value(); dur = self.sp_dur.value()
            B = self.sp_B.value(); tc = self.sp_tc.value()
            self.sig_log.emit(f"[Inércia] arm, throttle {thr}, estabiliza {estab}s...")
            self._spmm("arm"); self._sleep(2.0)
            self._spmm(f"throttle {thr}"); self._sleep(estab)
            self._spmm(f"log start {hz}")
            start = len(self._samples)
            self.sig_log.emit("[Inércia] coast! coletando ω(t)...")
            self._spmm("coast")
            self._sleep(dur)
            self._spmm("log stop"); self._spmm("throttle 1000")
            seg = [s for s in self._samples[start:] if s["omega"] is not None]
            if len(seg) < 10:
                self.sig_result.emit(f"INÉRCIA: coast-down com poucas amostras ({len(seg)}).")
            else:
                t = [s["t"] for s in seg]; w = [s["omega"] for s in seg]
                res = sa.compute_J(t, w, B, tc, n_windows=5)
                self._save_csv("coast", [{"n": s["n"], "t": s["t"], "omega": s["omega"], "te": s["te"]} for s in seg])
                jw = res["windows"]
                disp = f" (±{res['J_std']:.2e}, {len(jw)} janelas)" if jw else ""
                self.sig_result.emit(f"INÉRCIA: J = {res['J_mean']:.4e} kg·m²{disp}")
        except Exception as e:
            self.sig_log.emit(f"[ERRO Inércia] {e}")
        finally:
            self._safe_shutdown()

    def _safe_shutdown(self):
        """SEMPRE: desliga o motor e remove o gancho de coleta."""
        try:
            self._spmm("disarm")
            self._spmm("throttle 1000")
            self.sig_log.emit("[SEGURANÇA] disarm + throttle 1000 enviados.")
        finally:
            if getattr(self.gui, "_spmm_sample_sink", None) is self._collect:
                self.gui._spmm_sample_sink = None
            self.sig_done.emit()

    # ------------------------------------------------------------------ #
    def _save_csv(self, tipo, rows):
        """Salva incrementalmente (append) com índice de repetição."""
        self._rep += 1
        path = self.ed_csv.text().strip()
        write_header = not _exists(path)
        try:
            with open(path, "a", newline="") as f:
                if tipo == "atrito":
                    fields = ["rep", "tipo", "patamar_us", "omega", "te"]
                else:
                    fields = ["rep", "tipo", "n", "t", "omega", "te"]
                wr = csv.DictWriter(f, fieldnames=fields)
                if write_header:
                    wr.writeheader()
                for r in rows:
                    r = dict(r); r["rep"] = self._rep; r["tipo"] = tipo
                    wr.writerow(r)
            self.sig_log.emit(f"[CSV] {len(rows)} linhas salvas em {path} (rep {self._rep}).")
        except Exception as e:
            self.sig_log.emit(f"[ERRO CSV] {e}")

    def _show_result(self, txt):
        self.lbl_result.setText(txt)

    def _on_done(self):
        self._running = False
        self.btn_run.setEnabled(True); self.btn_stop.setEnabled(False)
        self.lbl_rep.setText(f"Repetições: {self._rep}")

    def closeEvent(self, e):
        # Fechar o diálogo também garante o desligamento seguro.
        self._running = False
        try:
            self._spmm("disarm"); self._spmm("throttle 1000")
        except Exception:
            pass
        if getattr(self.gui, "_spmm_sample_sink", None) is self._collect:
            self.gui._spmm_sample_sink = None
        e.accept()


def _exists(path):
    import os
    return os.path.exists(path) and os.path.getsize(path) > 0
