"""Camada 4 · Apresentação — coeficientes da hélice (CT, CQ, J) via CSV.

Consome o CSV exportado pela GUI (botão "Iniciar/Encerrar Registro" — Timestamp
do PC + telemetria SIBA/SAME). O usuário escolhe:
- qual FORÇA usar como TRAÇÃO (obrigatório — pergunta explícita);
- a coluna de TORQUE (padrão: o estimado pela SAME, `torque_eletrico`);
- a coluna de RPM (padrão: `rpm_same`) e de velocidade do ar (`air_speed`);
- diâmetro D e densidade ρ.

Roda `Scripts/helice_coeff.py --csv` e mostra a LISTA de coeficientes por ponto
de operação (RPM/V/J/T/Q/CT/CQ/CP/η) numa tabela. NÃO é um único coeficiente.
"""
import csv
import os
import time

import yaml
from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDoubleSpinBox, QFileDialog, QFormLayout,
    QGroupBox, QHBoxLayout, QLabel, QLineEdit, QMessageBox, QPushButton,
    QTableWidget, QTableWidgetItem, QVBoxLayout,
)

from Communication_Data.estimation import EstimationRunner

_TABLE_COLS = ["RPM", "V [m/s]", "J", "Tração [N]", "Torque [N·m]", "CT", "CQ", "CP", "η"]


def _parse_kv(line):
    out = {}
    for tok in line.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            try:
                out[k] = float(v)
            except ValueError:
                out[k] = v
    return out


class HeliceDialog(QDialog):
    sig_log = Signal(str)
    sig_point = Signal(dict)
    sig_result = Signal(dict)
    sig_done = Signal(int)

    def __init__(self, config_dir, same_yaml_path, parent=None):
        super().__init__(parent)
        self.config_dir = config_dir
        self.same_yaml_path = same_yaml_path
        self.gui = parent
        self.setWindowTitle("Coeficientes da Hélice (CT, CQ, J)")
        self.setWindowFlags(Qt.Window)
        self.resize(820, 560)

        self.runner = EstimationRunner.from_config_dir(
            config_dir, log_fn=lambda m: self.sig_log.emit(m))
        self._csv_path = None
        self._points = []
        self._summary = None
        self._saved = []

        self._build_ui()
        self.sig_log.connect(self._append_log)
        self.sig_point.connect(self._add_point)
        self.sig_result.connect(self._on_result)
        self.sig_done.connect(self._on_done)

    # ------------------------------------------------------------------ #
    def _legend_map(self):
        """var -> legenda dos gráficos (FX/FY/...), p/ exibir a força."""
        leg = {}
        try:
            for info in (self.gui.plot_config.get("plots_mapping", {}) or {}).values():
                for var, l in (info.get("vars") or {}).items():
                    if l:
                        leg[var] = l
        except Exception:
            pass
        return leg

    def _build_ui(self):
        root = QVBoxLayout(self)

        src = QGroupBox("Fonte: CSV exportado (Timestamp + telemetria SIBA/SAME)")
        form = QFormLayout(src)
        pick = QHBoxLayout()
        self.lbl_csv = QLineEdit(); self.lbl_csv.setReadOnly(True)
        self.lbl_csv.setPlaceholderText("Escolha o .csv salvo pela GUI...")
        btn = QPushButton("Escolher CSV..."); btn.clicked.connect(self._pick_csv)
        pick.addWidget(self.lbl_csv, 1); pick.addWidget(btn)
        form.addRow("Arquivo:", self._wrap(pick))

        # Escolha das colunas (populadas ao abrir o CSV).
        self.cmb_thrust = QComboBox()   # qual FORÇA usar como tração
        self.cmb_torque = QComboBox()
        self.cmb_rpm = QComboBox()
        self.cmb_v = QComboBox()
        form.addRow("Tração (qual força?):", self.cmb_thrust)
        form.addRow("Torque:", self.cmb_torque)
        form.addRow("RPM:", self.cmb_rpm)
        form.addRow("Velocidade do ar (coluna):", self.cmb_v)

        # Velocidade fixa para TODOS os pontos (se desmarcado, usa a do CSV).
        vfix = QHBoxLayout()
        self.chk_vfixed = QCheckBox("Usar velocidade do ar fixa (todos os pontos):")
        self.chk_vfixed.toggled.connect(lambda on: (self.sp_vfixed.setEnabled(on),
                                                    self.cmb_v.setEnabled(not on)))
        self.sp_vfixed = QDoubleSpinBox(); self.sp_vfixed.setDecimals(2)
        self.sp_vfixed.setRange(0.0, 200.0); self.sp_vfixed.setValue(0.0)
        self.sp_vfixed.setSuffix(" m/s"); self.sp_vfixed.setEnabled(False)
        vfix.addWidget(self.chk_vfixed); vfix.addWidget(self.sp_vfixed); vfix.addStretch()
        form.addRow(self._wrap(vfix))

        self.sp_D = QDoubleSpinBox(); self.sp_D.setDecimals(3); self.sp_D.setRange(0.01, 5.0)
        self.sp_D.setValue(0.30); self.sp_D.setSuffix(" m")
        self.sp_rho = QDoubleSpinBox(); self.sp_rho.setDecimals(3); self.sp_rho.setRange(0.1, 2.0)
        self.sp_rho.setValue(1.225); self.sp_rho.setSuffix(" kg/m³")
        form.addRow("Diâmetro D:", self.sp_D)
        form.addRow("Densidade do ar ρ:", self.sp_rho)
        root.addWidget(src)

        ctrl = QHBoxLayout()
        self.btn_run = QPushButton("▶ Calcular coeficientes")
        self.btn_run.clicked.connect(self._start)
        ctrl.addWidget(self.btn_run); ctrl.addStretch()
        root.addLayout(ctrl)

        self.table = QTableWidget(0, len(_TABLE_COLS))
        self.table.setHorizontalHeaderLabels(_TABLE_COLS)
        root.addWidget(self.table, 1)

        self.lbl_summary = QLabel("—")
        root.addWidget(self.lbl_summary)

        bottom = QHBoxLayout()
        self.btn_save_csv = QPushButton("Salvar resultados (CSV)...")
        self.btn_save_csv.setEnabled(False); self.btn_save_csv.clicked.connect(self._save_csv)
        self.btn_apply = QPushButton("Aplicar ao SAME.yaml")
        self.btn_apply.setEnabled(False); self.btn_apply.clicked.connect(self._apply)
        bottom.addStretch(); bottom.addWidget(self.btn_save_csv); bottom.addWidget(self.btn_apply)
        root.addLayout(bottom)

    @staticmethod
    def _wrap(layout):
        from PySide6.QtWidgets import QWidget
        w = QWidget(); w.setLayout(layout); return w

    # ------------------------------------------------------------------ #
    def _pick_csv(self):
        path, _ = QFileDialog.getOpenFileName(self, "CSV exportado", "", "CSV (*.csv)")
        if not path:
            return
        self._csv_path = path
        self.lbl_csv.setText(path)
        self._populate_columns(path)

    def _populate_columns(self, path):
        """Lê o cabeçalho e popula os combos de coluna com defaults sensatos."""
        try:
            with open(path, newline="") as f:
                header = next(csv.reader(f))
        except Exception as e:
            QMessageBox.warning(self, "Erro ao ler CSV", str(e))
            return
        header = [h.strip() for h in header]
        leg = self._legend_map()

        # Tração: só as forças (load_cell_*), exibidas pela legenda (FX/FY/...).
        forces = [c for c in header if c.startswith("load_cell")]
        self.cmb_thrust.clear()
        for c in (forces or header):
            self.cmb_thrust.addItem(leg.get(c, c), c)

        def fill(combo, cols, default):
            combo.clear()
            present = [c for c in cols if c in header]
            for c in present:
                combo.addItem(c, c)
            if default in header and default not in present:
                combo.insertItem(0, default, default)
            i = combo.findData(default)
            if i >= 0:
                combo.setCurrentIndex(i)

        # Torque: padrão = estimado pela SAME (torque_eletrico).
        fill(self.cmb_torque, ["torque_eletrico", "torque_helice", "te"], "torque_eletrico")
        # RPM: padrão = estimado pela SAME (rpm_same).
        fill(self.cmb_rpm, ["rpm_same", "rpm"], "rpm_same")
        # Velocidade do ar.
        fill(self.cmb_v, ["air_speed"], "air_speed")

    # ------------------------------------------------------------------ #
    def _start(self):
        if not self._csv_path:
            QMessageBox.warning(self, "CSV faltando", "Escolha o .csv exportado.")
            return
        if self.cmb_thrust.count() == 0:
            QMessageBox.warning(self, "Tração", "Escolha qual força usar como tração.")
            return
        self.table.setRowCount(0); self._points = []; self._summary = None
        self._saved = []; self.btn_apply.setEnabled(False); self.lbl_summary.setText("—")
        v_fixed = self.sp_vfixed.value() if self.chk_vfixed.isChecked() else None
        ok = self.runner.run_helice_coeff(
            csv_path=self._csv_path,
            thrust_col=self.cmb_thrust.currentData(),
            torque_col=self.cmb_torque.currentData(),
            rpm_col=self.cmb_rpm.currentData(),
            v_col=self.cmb_v.currentData(),
            v_fixed=v_fixed,
            D=self.sp_D.value(), rho=self.sp_rho.value(), plot=True,
            outdir=os.path.dirname(self._csv_path) or ".",
            on_line=self._on_line, on_done=lambda rc: self.sig_done.emit(rc))
        if ok:
            self.btn_run.setEnabled(False)
            self._append_log(f"[GUI] Calculando de {os.path.basename(self._csv_path)}...")

    def _on_line(self, line):
        if line.startswith("POINT"):
            self.sig_point.emit(_parse_kv(line))
        elif line.startswith("RESULT"):
            self.sig_result.emit(_parse_kv(line))
        elif line.startswith("salvo:"):
            self._saved.append(line.split("salvo:", 1)[1].strip())

    def _add_point(self, d):
        self._points.append(d)
        r = self.table.rowCount(); self.table.insertRow(r)
        vals = [d.get("rpm"), d.get("V"), d.get("J"), d.get("thrust"),
                d.get("torque"), d.get("CT"), d.get("CQ"), d.get("CP"), d.get("eta")]
        fmt = ["{:.0f}", "{:.2f}", "{:.4f}", "{:.4f}", "{:.5f}", "{:.5f}", "{:.6f}", "{:.5f}", "{:.4f}"]
        for c, (v, f) in enumerate(zip(vals, fmt)):
            try:
                txt = f.format(v)
            except (TypeError, ValueError):
                txt = str(v)
            self.table.setItem(r, c, QTableWidgetItem(txt))

    def _append_log(self, msg):
        pass  # log detalhado vai para o console; a tabela é o resultado

    def _on_result(self, d):
        self._summary = d
        self.lbl_summary.setText(
            f"<b>{int(d.get('n_points', 0))} pontos</b> — "
            f"CT médio = {d.get('CT_mean', 0):.5f}  |  CQ médio = {d.get('CQ_mean', 0):.6f}")
        self.btn_apply.setEnabled(True)
        self.btn_save_csv.setEnabled(bool(self._points))

    def _save_csv(self):
        """Salva a LISTA de coeficientes (a tabela) num CSV escolhido — arquivo
        diferente do CSV de entrada (que é a telemetria bruta)."""
        if not self._points:
            return
        path, _ = QFileDialog.getSaveFileName(
            self, "Salvar coeficientes da hélice", "coeficientes_helice.csv", "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", newline="") as f:
                wr = csv.writer(f)
                wr.writerow(["RPM", "V", "J", "thrust_N", "torque_Nm", "CT", "CQ", "CP", "eta"])
                for p in self._points:
                    wr.writerow([f"{p['rpm']:.1f}", f"{p['V']:.3f}", f"{p['J']:.4f}",
                                 f"{p['thrust']:.4f}", f"{p['torque']:.5f}", f"{p['CT']:.5f}",
                                 f"{p['CQ']:.6f}", f"{p['CP']:.5f}", f"{p['eta']:.4f}"])
            QMessageBox.information(self, "Salvo", f"Coeficientes salvos em:\n{path}")
        except Exception as e:
            QMessageBox.warning(self, "Erro ao salvar", str(e))

    def _on_done(self, rc):
        self.btn_run.setEnabled(True)
        if rc == 0 and self._saved:
            QMessageBox.information(self, "Saídas geradas", "Salvo:\n" + "\n".join(self._saved))
        elif rc not in (0,):
            QMessageBox.warning(self, "Erro", f"Terminou com erro (rc={rc}). Veja o console.")

    # ------------------------------------------------------------------ #
    def _apply(self):
        if not self._points:
            return
        resp = QMessageBox.question(
            self, "Aplicar coeficientes",
            f"Gravar a lista de {len(self._points)} pontos (CT/CQ/J) no SAME.yaml?",
            QMessageBox.Yes | QMessageBox.No)
        if resp != QMessageBox.Yes:
            return
        try:
            data = yaml.safe_load(open(self.same_yaml_path)) or {}
            data["helice"] = {
                "D": self.sp_D.value(), "rho": self.sp_rho.value(),
                "thrust_col": self.cmb_thrust.currentData(),
                "torque_col": self.cmb_torque.currentData(),
                "CT_mean": float((self._summary or {}).get("CT_mean", 0)),
                "CQ_mean": float((self._summary or {}).get("CQ_mean", 0)),
                "csv_lista": next((s for s in self._saved if s.endswith(".csv")), None),
                "pontos": [{k: round(float(p[k]), 6) for k in
                            ("rpm", "V", "J", "thrust", "torque", "CT", "CQ", "CP", "eta")}
                           for p in self._points],
                "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
            }
            with open(self.same_yaml_path, "w") as f:
                yaml.safe_dump(data, f, sort_keys=False, allow_unicode=True)
            QMessageBox.information(self, "Aplicado", "Coeficientes gravados em SAME.yaml (bloco helice).")
        except Exception as e:
            QMessageBox.warning(self, "Erro ao gravar", str(e))
