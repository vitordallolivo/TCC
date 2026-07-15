"""Camada 4 · Apresentação — Ensaios da SPMM (atrito B/τc e inércia J).

Atrito: por patamar de throttle, estabiliza e mede (ω, Te) em regime ->
regressão Te = B·ω + τc. Inércia: sobe a rotação, `coast` corta o throttle,
coleta ω(t) -> J = -(B·ω+τc)/(dω/dt) em janelas.

Orquestra via Telnet da SPMM, roda numa thread pra não travar a UI.
Segurança: disarm + throttle 1000 rodam sempre no finally.
"""
import csv
import threading
import time

import numpy as np
from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QButtonGroup, QDialog, QDoubleSpinBox, QFileDialog, QFormLayout, QGroupBox,
    QHBoxLayout, QLabel, QLineEdit, QMessageBox, QPlainTextEdit, QPushButton,
    QRadioButton, QScrollArea, QSpinBox, QVBoxLayout, QWidget,
)

from Communication_Data import spmm_analysis as sa
from Communication_Data import motor_math as mm
from Communication_Data import motor_config as mc


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
        outer = QVBoxLayout(self)
        # Parâmetros vão numa área ROLÁVEL (são muitos grupos); CSV/controles/log
        # ficam fixos no rodapé.
        scroll = QScrollArea(); scroll.setWidgetResizable(True)
        container = QWidget(); root = QVBoxLayout(container)

        # Fonte dos dados do ensaio: a bancada SPMM (Telnet+UDP) ou SIBA (RPM) +
        # SAME (correntes→torque). Substitui o antigo diálogo "Configuração
        # SIBA+SAME" (agora integrado aqui, no fluxo do SPMM).
        fonte = QGroupBox("Fonte dos dados")
        fl = QVBoxLayout(fonte)
        rbrow = QHBoxLayout()
        self.rb_src_spmm = QRadioButton("Bancada SPMM")
        self.rb_src_ss = QRadioButton("SIBA (RPM) + SAME (correntes)")
        src = ((self.gui.prefs.get("motor_source") or {}).get("source"))
        (self.rb_src_ss if src == "SIBA_SAME" else self.rb_src_spmm).setChecked(True)
        self.rb_src_spmm.toggled.connect(self._on_source_changed)
        rbrow.addWidget(self.rb_src_spmm); rbrow.addWidget(self.rb_src_ss)
        fl.addLayout(rbrow)
        # Com SIBA+SAME o torque do ensaio é Kt·Iq calculado das correntes de fase
        # (não o torque estimado da SAME, que depende de Kt). Isso é feito
        # AUTOMATICAMENTE ao "Rodar Ensaio" — o usuário não precisa saber.
        fl.addWidget(QLabel("<i>SIBA+SAME: o torque é calculado das correntes "
                            "automaticamente ao rodar.</i>"))
        root.addWidget(fonte)

        modo = QGroupBox("Tipo de ensaio")
        ml = QHBoxLayout(modo)
        self.rb_atrito = QRadioButton("Atrito (B, τc) — patamares")
        self.rb_inercia = QRadioButton("Inércia (J) — coast-down")
        self.rb_atrito.setChecked(True)
        grp = QButtonGroup(self); grp.addButton(self.rb_atrito); grp.addButton(self.rb_inercia)
        self.rb_atrito.toggled.connect(self._toggle_params)
        ml.addWidget(self.rb_atrito); ml.addWidget(self.rb_inercia)
        root.addWidget(modo)

        # Espaço para figura/diagrama do que cada ensaio faz.
        self.diagrama = QLabel()
        self.diagrama.setWordWrap(True)
        self.diagrama.setTextFormat(Qt.RichText)
        self.diagrama.setMinimumHeight(120)
        self.diagrama.setStyleSheet(
            "background:#101418; color:#DCDCDC; border:1px solid #2a2f36;"
            "border-radius:6px; padding:10px; font-size:10pt;")
        root.addWidget(self.diagrama)

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

        root.addStretch()
        scroll.setWidget(container)
        outer.addWidget(scroll, 1)

        # CSV + controles — FORA da área rolável, sempre visíveis.
        arq = QHBoxLayout()
        self.ed_csv = QLineEdit()
        self.ed_csv.setPlaceholderText("nome do arquivo .csv (obrigatório)...")
        btn_csv = QPushButton("..."); btn_csv.clicked.connect(self._pick_csv)
        arq.addWidget(QLabel("CSV:")); arq.addWidget(self.ed_csv, 1); arq.addWidget(btn_csv)
        outer.addLayout(arq)

        ctrl = QHBoxLayout()
        self.btn_run = QPushButton("▶ Rodar Ensaio"); self.btn_run.clicked.connect(self._start)
        self.btn_stop = QPushButton("■ Parar (seguro)"); self.btn_stop.setEnabled(False); self.btn_stop.clicked.connect(self._stop)
        self.lbl_rep = QLabel("Repetições: 0")
        ctrl.addWidget(self.btn_run); ctrl.addWidget(self.btn_stop); ctrl.addStretch(); ctrl.addWidget(self.lbl_rep)
        outer.addLayout(ctrl)

        self.log = QPlainTextEdit(); self.log.setReadOnly(True); self.log.setMaximumBlockCount(2000)
        self.log.setStyleSheet("background:#0F0F0F; color:#DCDCDC; font-family:monospace; font-size:9pt;")
        outer.addWidget(self.log, 1)
        self.lbl_result = QLabel("—"); self.lbl_result.setStyleSheet("font-weight:bold;")
        self.lbl_result.setWordWrap(True)
        outer.addWidget(self.lbl_result)

        self._toggle_params()

    def _atrito_series(self):
        """(t, rpm[rpm], te[N·m]) para o ensaio de atrito, conforme a fonte:
        - SPMM: ω do `rpm` e Te do `te` (torque da bancada).
        - SIBA+SAME: ω do RPM da SIBA e **Te = Kt·Iq calculado das correntes de
          fase** (não do torque estimado da SAME, que depende de Kt do motor).
        Roda na thread do ensaio — só funções puras + buffers (thread-safe)."""
        if self._cfg.get("source") == "SIBA_SAME":
            t, ys = self.gui.snapshot_series(["rpm_same", "rpm", "i_a", "i_b", "i_c"])
            rpm = ys.get("rpm_same")
            if rpm is None or rpm.size == 0 or np.all(np.isnan(rpm)):
                rpm = ys.get("rpm")
            ia, ib, ic = ys.get("i_a"), ys.get("i_b"), ys.get("i_c")
            if rpm is None or ia is None or ib is None or ic is None or not t.size:
                return t, rpm, None
            mot = self._cfg.get("motor", {})
            P = int(mot.get("pole_pairs") or (int(mot.get("poles", 0)) // 2) or 7)
            kt = mc.ke_kt_from_kv(mot.get("kv_rating", 0), P, mot.get("connection", "Y"))["kt"]
            theta = mm.electrical_angle_from_speed(t, np.nan_to_num(rpm), P)
            _id, iq, *_ = mm.dq_from_phases(np.nan_to_num(ia), np.nan_to_num(ib),
                                            np.nan_to_num(ic), theta)
            return t, rpm, kt * iq
        # padrão: bancada SPMM
        t, ys = self.gui.snapshot_series(["rpm", "te"])
        return t, ys.get("rpm"), ys.get("te")

    def _on_source_changed(self, *_):
        """Persiste a fonte de dados escolhida (SPMM × SIBA+SAME) no user_prefs.
        O ensaio lê isso para decidir de onde vêm ω (RPM) e o torque (Iq da SAME
        vs. torque da bancada)."""
        ms = dict(self.gui.prefs.get("motor_source") or {})
        ms["source"] = "SPMM" if self.rb_src_spmm.isChecked() else "SIBA_SAME"
        self.gui.prefs["motor_source"] = ms
        try:
            self.gui.save_user_prefs()
        except Exception:
            pass

    _DIAG_ATRITO = (
        "<b>Atrito (B, τc) — patamares em regime permanente</b><br>"
        "Para cada throttle, o motor estabiliza e mede-se (ω, Te). Em regime "
        "(dω/dt=0): <b>Te = B·ω + τc</b>.<br>"
        "<pre>Te │        •<br>"
        "   │     •     ← reta ajustada (mín. quadrados)<br>"
        "   │  •        inclinação = B ; intercepto = τc<br>"
        "   └───────── ω</pre>"
        "throttle 1200→1700 (patamares) · o PC faz a regressão · alerta se R²&lt;0,98 ou τc&lt;0.")

    _DIAG_INERCIA = (
        "<b>Inércia (J) — coast-down</b><br>"
        "Sobe a rotação, <b>coast</b> corta o throttle e coleta-se ω(t) "
        "(t = n·Ts, do micro). Sem torque: <b>J·(dω/dt) = -(B·ω+τc)</b> → "
        "<b>J = -(B·ω+τc)/(dω/dt)</b>.<br>"
        "<pre>ω │•<br>"
        "  │ •._<br>"
        "  │    '•._      decaimento livre; derivada dω/dt&lt;0<br>"
        "  │        '•.__ J calculado em várias janelas (validação)<br>"
        "  └──────────── t = n·Ts</pre>"
        "usa B e τc do ensaio de atrito · J deve ficar ~constante entre janelas.")

    def _toggle_params(self):
        atrito = self.rb_atrito.isChecked()
        self.g_atrito.setVisible(atrito)
        self.g_inercia.setVisible(not atrito)
        self.g_btc.setVisible(not atrito)
        self.diagrama.setText(self._DIAG_ATRITO if atrito else self._DIAG_INERCIA)

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
        # === TUDO que toca WIDGET é feito AQUI, na MAIN thread ===
        # A thread do ensaio NUNCA acessa widget (nem leitura .value()/.text()
        # nem escrita .setText()) — só computa e emite sinais. Isso evita o
        # crash de "atualizar a GUI fora da main thread" (que ocorria no fim do
        # ensaio, quando o _save_csv fazia ed_csv.setText()).
        csv_path = self._normalize_csv_path()   # pode fazer setText (main thread)
        if not csv_path:
            QMessageBox.warning(self, "CSV", "Defina o nome do arquivo .csv.")
            return
        if "SPMM" not in self.gui.eth.workers:
            self.gui.eth.start_link("SPMM")

        # Fotografa TODOS os parâmetros dos widgets num dict simples.
        try:
            throttles = [int(x) for x in self.ed_throttles.text().replace(";", ",").split(",") if x.strip()]
        except ValueError:
            QMessageBox.warning(self, "Throttles", "Lista de throttles inválida.")
            return
        self._cfg = {
            "csv": csv_path,
            "atrito": self.rb_atrito.isChecked(),
            "throttles": throttles,
            "settle": self.sp_settle.value(),
            "thr_alto": self.sp_thr_alto.value(),
            "estab": self.sp_estab.value(),
            "loghz": self.sp_loghz.value(),
            "dur": self.sp_dur.value(),
            "B": self.sp_B.value(),
            "tc": self.sp_tc.value(),
            # Fonte dos dados + perfil do motor (lidos na main thread). Com
            # SIBA+SAME o ensaio calcula o torque de Iq internamente — o usuário
            # não precisa saber, só clicar "Rodar".
            "source": (self.gui.prefs.get("motor_source") or {}).get("source"),
            "motor": dict(self.gui.prefs.get("same_config") or {}),
        }

        self._running = True
        self.btn_run.setEnabled(False); self.btn_stop.setEnabled(True)
        self.gui.set_motor_locked(True)
        self._thread = threading.Thread(
            target=self._run_atrito if self._cfg["atrito"] else self._run_inercia, daemon=True)
        self._thread.start()

    def _normalize_csv_path(self):
        """Garante a extensão .csv — SÓ na main thread (faz setText)."""
        p = self.ed_csv.text().strip()
        if p and not p.lower().endswith(".csv"):
            p += ".csv"
            self.ed_csv.setText(p)
        return p

    def _stop(self):
        self._running = False   # o loop do ensaio checa isso e sai pelo finally

    def _sleep(self, s):
        """Espera respeitando o botão Parar."""
        t0 = time.time()
        while time.time() - t0 < s and self._running:
            time.sleep(0.02)

    # ------------------------------------------------------------------ #
    # NOVO approach: o ensaio só COMANDA o motor e MARCA janelas de tempo (na
    # base do buffer/CSV). Ao final, lê os dados JÁ capturados (rpm/te dos
    # buffers da GUI — os mesmos do CSV) filtrados por essas janelas. Sem coleta
    # paralela (evita a corrida que causava o segfault). ω = rpm·2π/60.
    # ------------------------------------------------------------------ #
    def _run_atrito(self):
        try:
            throttles = self._cfg["throttles"]      # já lido na main thread
            settle = self._cfg["settle"]
            self.sig_log.emit(f"[Atrito] arm + {len(throttles)} patamares (marcando janelas)...")
            self._spmm("arm"); self._spmm("log start 200"); self._sleep(2.0)
            janelas = []   # (us, t0, t1) na base do buffer/CSV
            for us in throttles:
                if not self._running:
                    break
                self._spmm(f"throttle {us}")
                t0 = self.gui.now_buffer_time()
                self._sleep(settle)
                t1 = self.gui.now_buffer_time()
                janelas.append((us, t0, t1))
                self.sig_log.emit(f"  throttle {us}: janela [{t0:.1f}, {t1:.1f}] s marcada")
            self._spmm("throttle 1000")

            # Fim: lê os buffers (mesmos do CSV) e usa só o que está nas janelas.
            # A fonte (SPMM ou SIBA+SAME) define de onde vêm ω e Te — com
            # SIBA+SAME o Te é Kt·Iq calculado das correntes (transparente ao usuário).
            t, rpm, te = self._atrito_series()
            pares, rows = [], []
            if rpm is not None and te is not None and t.size:
                for us, t0, t1 in janelas:
                    m = (np.isfinite(t) & (t >= t0 + 0.4 * (t1 - t0)) & (t <= t1)
                         & np.isfinite(rpm) & np.isfinite(te))
                    if not np.any(m):
                        self.sig_log.emit(f"  throttle {us}: sem dados na janela (SPMM enviando?)")
                        continue
                    omega = float(np.mean(rpm[m])) * (2.0 * np.pi / 60.0)
                    te_m = float(np.mean(te[m]))
                    pares.append((omega, te_m))
                    rows.append({"patamar_us": us, "omega": omega, "te": te_m})
                    self.sig_log.emit(f"  throttle {us}: ω={omega:.2f} rad/s  Te={te_m:.4f} N·m  ({int(m.sum())} amostras)")

            if len(pares) >= 2:
                B, tc, r2 = sa.fit_atrito([p[0] for p in pares], [p[1] for p in pares])
                self._save_csv("atrito", rows)
                if not np.isfinite(B):
                    self.sig_result.emit("ATRITO: dados degenerados (ω ~constante) — a SPMM está girando?")
                else:
                    alerta = "  ⚠ R²<0,98" if r2 < 0.98 else ""
                    alerta += "  ⚠ τc<0" if tc < 0 else ""
                    self.sig_result.emit(f"ATRITO: B = {B:.5g} N·m·s/rad | τc = {tc:.5g} N·m | R² = {r2:.4f}{alerta}")
            else:
                self.sig_result.emit("ATRITO: dados insuficientes nas janelas (a SPMM está conectada/girando?).")
        except Exception as e:
            self.sig_log.emit(f"[ERRO Atrito] {e}")
        finally:
            self._safe_shutdown()

    def _run_inercia(self):
        try:
            c = self._cfg                            # já lido na main thread
            thr = c["thr_alto"]; estab = c["estab"]
            hz = c["loghz"]; dur = c["dur"]
            B = c["B"]; tc = c["tc"]
            self.sig_log.emit(f"[Inércia] arm, throttle {thr}, estabiliza {estab}s...")
            self._spmm("arm"); self._sleep(2.0)
            self._spmm(f"throttle {thr}"); self._sleep(estab)
            self._spmm(f"log start {hz}")
            t_coast = self.gui.now_buffer_time()   # marca o instante do coast
            self.sig_log.emit("[Inércia] coast! (marcando janela do coast-down)")
            self._spmm("coast")
            self._sleep(dur)
            t_end = self.gui.now_buffer_time()
            self._spmm("log start 100"); self._spmm("throttle 1000")

            # Fim: lê o rpm(t) já capturado na janela do coast e calcula J.
            t, ys = self.gui.snapshot_series(["rpm"])
            rpm = ys.get("rpm")
            if rpm is None or not t.size:
                self.sig_result.emit("INÉRCIA: sem dados de rpm no buffer (SPMM conectada?).")
                return
            m = np.isfinite(t) & (t >= t_coast) & (t <= t_end) & np.isfinite(rpm)
            tt = t[m]; ww = rpm[m] * (2.0 * np.pi / 60.0)   # ω(t) do coast-down
            if tt.size < 10:
                self.sig_result.emit(f"INÉRCIA: coast-down com poucas amostras ({int(tt.size)}).")
                return
            res = sa.compute_J(tt, ww, B, tc, n_windows=5)
            self._save_csv("coast", [{"t": float(a), "omega": float(b)} for a, b in zip(tt, ww)])
            jw = res["windows"]
            if not jw or not np.isfinite(res["J_mean"]):
                self.sig_result.emit("INÉRCIA: coast-down sem desaceleração válida (ω não caiu).")
            else:
                self.sig_result.emit(f"INÉRCIA: J = {res['J_mean']:.4e} kg·m² "
                                     f"(±{res['J_std']:.2e}, {len(jw)} janelas)")
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
            self.sig_done.emit()

    # ------------------------------------------------------------------ #
    def _save_csv(self, tipo, rows):
        """Salva incrementalmente (append) com índice de repetição.
        Roda na thread do ensaio — usa o path JÁ normalizado no _start (não toca
        em widget)."""
        self._rep += 1
        path = self._cfg["csv"]
        write_header = not _exists(path)
        try:
            with open(path, "a", newline="") as f:
                if tipo == "atrito":
                    fields = ["rep", "tipo", "patamar_us", "omega", "te"]
                else:
                    fields = ["rep", "tipo", "t", "omega"]
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
        # Destrava os controles de motor da tela principal.
        if hasattr(self.gui, "set_motor_locked"):
            self.gui.set_motor_locked(False)

    def closeEvent(self, e):
        # Fechar o diálogo também garante o desligamento seguro.
        self._running = False
        try:
            self._spmm("disarm"); self._spmm("throttle 1000")
        except Exception:
            pass
        e.accept()


def _exists(path):
    import os
    return os.path.exists(path) and os.path.getsize(path) > 0
