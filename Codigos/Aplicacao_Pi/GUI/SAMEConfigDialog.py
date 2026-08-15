import os

import yaml
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QDialog, QVBoxLayout, QFormLayout, QGroupBox,
                               QDoubleSpinBox, QComboBox, QLabel, QPushButton,
                               QHBoxLayout, QMessageBox, QInputDialog,
                               QScrollArea, QWidget)

from Communication_Data import motor_config as mc


class SAMEConfigDialog(QDialog):
    """Motor → Criar: edita/salva um perfil de motor (Config/motors/*.yaml) e
    aplica no hardware SAME. Parâmetros físicos + observador (SMO, ganhos Γ) +
    amostragem. Ke/Kt são CALCULADOS do Kv comercial (com conversão Δ→Y)."""

    def __init__(self, prefs, config_dir, ethernet_mgr, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Configuração / Criar Motor (SAME)")
        self.resize(440, 720)

        self.prefs = prefs
        self.config_dir = config_dir
        self.ethernet = ethernet_mgr
        self.same_yaml = self._load_yaml(os.path.join(config_dir, "SAME.yaml"))
        self.enums = self.same_yaml.get('enums', {})

        self.inputs = {}   # key -> QDoubleSpinBox
        self.combos = {}   # key -> QComboBox
        self.init_ui()
        self.load_current_values()
        self._recompute_derived()

    def _load_yaml(self, path):
        with open(path, 'r', encoding='utf-8') as f:
            return yaml.safe_load(f)

    # ------------------------------------------------------------------ #
    def add_spinbox(self, layout, key, label, min_val, max_val, decimals):
        spin = QDoubleSpinBox()
        spin.setRange(min_val, max_val)
        spin.setDecimals(decimals)
        self.inputs[key] = spin
        layout.addRow(label, spin)
        return spin

    def add_combo(self, layout, key, label, items):
        cb = QComboBox()
        cb.addItems(items)
        self.combos[key] = cb
        layout.addRow(label, cb)
        return cb

    def init_ui(self):
        # Conteúdo rolável (são muitos campos) + botões fixos no rodapé.
        outer = QVBoxLayout(self)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        container = QWidget()
        main = QVBoxLayout(container)

        # --- Grupo 1: Motor (físico) ---
        gm = QGroupBox("Parâmetros Físicos do Motor")
        fm = QFormLayout()
        self.add_spinbox(fm, "poles", "Número de polos (P)", 2, 200, 0)
        self.add_combo(fm, "connection", "Conexão", ["Y (Estrela)", "Delta"])
        self.add_spinbox(fm, "kv_rating", "Kv comercial (RPM/V)", 1, 10000, 1)
        self.add_spinbox(fm, "resistance", "Resistência R (Ω, em Y)", 0.0, 50.0, 5)
        self.add_spinbox(fm, "inductance", "Indutância L (H, em Y)", 0.0, 1.0, 8)
        self.add_spinbox(fm, "v_bus", "Tensão de barramento V_bus (V)", 0.0, 200.0, 2)
        gm.setLayout(fm)
        main.addWidget(gm)

        # Derivados (somente leitura) — Ke/Kt calculados do Kv (Δ→Y).
        gd = QGroupBox("Derivados (calculados do Kv, conexão Δ→Y)")
        fd = QFormLayout()
        self.lbl_pp = QLabel("—"); fd.addRow("Pares de polos (p = P/2):", self.lbl_pp)
        self.lbl_kvy = QLabel("—"); fd.addRow("Kv equivalente em Y:", self.lbl_kvy)
        self.lbl_ke = QLabel("—"); fd.addRow("Ke (V·s/rad):", self.lbl_ke)
        self.lbl_kt = QLabel("—"); fd.addRow("Kt (N·m/A):", self.lbl_kt)
        gd.setLayout(fd)
        main.addWidget(gd)

        # --- Grupo 2: Observador SMO (ganhos Γ) ---
        gs = QGroupBox("Ganhos do Observador (SMO) — forma Γ (invariante ao passo)")
        fs = QFormLayout()
        self.add_spinbox(fs, "gamma1", "Γ1 (Gamma_s1)", 0.0, 1000.0, 5)
        self.add_spinbox(fs, "gamma2", "Γ2 (Gamma_s2, < 0)", -1000.0, 0.0, 5)
        self.add_spinbox(fs, "gammat", "Γt (Gamma_t)", 0.0, 100.0, 6)
        self.add_spinbox(fs, "bl", "Camada de contorno (BL)", 0.0, 1000.0, 3)
        self.add_combo(fs, "switch_func", "Função de comutação",
                       ["tanh", "saturação", "sigmoide"])
        self.add_spinbox(fs, "pll_wn", "PLL ω_n (Hz)", 1.0, 5000.0, 1)
        # Mínimo 0: é o default do firmware (DEF_BEMF_LPF = 0), que significa
        # FILTRO DESLIGADO — a PLL usa ê direto. Com mínimo 1,0 esse estado
        # não era representável na GUI.
        sp_bemf = self.add_spinbox(fs, "bemf_lpf", "BEMF LPF (Hz, 0 = desligado)",
                                   0.0, 5000.0, 1)
        sp_bemf.setSpecialValueText("desligado")
        # Teto 50 kHz: o default do firmware é 10 kHz (DEF_TORQUE_LPF) e o
        # limite anterior, de 5 kHz, silenciosamente CORTAVA esse valor pela
        # metade ao abrir o diálogo.
        self.add_spinbox(fs, "torque_lpf", "Torque LPF (Hz)", 0.1, 50000.0, 1)
        gs.setLayout(fs)
        main.addWidget(gs)

        # --- Grupo 3: Amostragem (ADC rápido × UDP lento) ---
        ga = QGroupBox("Amostragem")
        fa = QFormLayout()
        self.add_spinbox(fa, "ts_adc_us", "Ts do ADC (µs)", 1.0, 100000.0, 2)
        self.add_spinbox(fa, "ts_udp_ms", "Ts da telemetria UDP (ms)", 1.0, 10000.0, 1)
        ga.setLayout(fa)
        main.addWidget(ga)

        # --- Grupo 4: Condições de validade do observador ---
        # Estas relações são do projeto (SAME/CLAUDE.md), e violá-las não gera
        # erro nenhum em runtime: gera estimativa errada em silêncio. Melhor
        # dizer aqui, antes de aplicar no hardware.
        gc = QGroupBox("Condições do observador")
        fc = QVBoxLayout()
        self.lbl_cond = QLabel("—")
        self.lbl_cond.setWordWrap(True)
        self.lbl_cond.setTextFormat(Qt.RichText)
        fc.addWidget(self.lbl_cond)
        gc.setLayout(fc)
        main.addWidget(gc)

        # Recalcula Ke/Kt quando poles/kv/conexão mudam.
        self.inputs["poles"].valueChanged.connect(self._recompute_derived)
        self.inputs["kv_rating"].valueChanged.connect(self._recompute_derived)
        self.combos["connection"].currentTextChanged.connect(self._recompute_derived)

        # Revalida as condições a cada mudança — o custo é um punhado de
        # contas, e o valor é ver o problema aparecer enquanto se digita.
        for spin in self.inputs.values():
            spin.valueChanged.connect(self._update_conditions)
        for cb in self.combos.values():
            cb.currentTextChanged.connect(self._update_conditions)

        main.addStretch()
        scroll.setWidget(container)
        outer.addWidget(scroll, 1)

        # Botões — FORA da área rolável, sempre visíveis no rodapé.
        btns = QHBoxLayout()
        b_save = QPushButton("Salvar .yaml"); b_save.clicked.connect(self.save_yaml)
        b_del = QPushButton("Apagar .yaml"); b_del.clicked.connect(self.delete_yaml)
        b_cancel = QPushButton("Cancelar"); b_cancel.clicked.connect(self.reject)
        b_apply = QPushButton("Aplicar no Hardware"); b_apply.clicked.connect(self.apply_and_send)
        btns.addWidget(b_save); btns.addWidget(b_del); btns.addStretch()
        btns.addWidget(b_cancel); btns.addWidget(b_apply)
        outer.addLayout(btns)

    # ------------------------------------------------------------------ #
    def _connection(self):
        return "Delta" if "Delta" in self.combos["connection"].currentText() else "Y"

    def _pole_pairs(self):
        return int(self.inputs["poles"].value()) // 2

    def _recompute_derived(self):
        pp = self._pole_pairs()
        d = mc.ke_kt_from_kv(self.inputs["kv_rating"].value(), pp, self._connection())
        self.lbl_pp.setText(str(pp))
        self.lbl_kvy.setText(f"{d['kv_y']:.1f} RPM/V")
        self.lbl_ke.setText(f"{d['ke']:.6g}  (Ke_mec={d['ke_mec']:.6g})")
        self.lbl_kt.setText(f"{d['kt']:.6g}")
        self._update_conditions()

    def _update_conditions(self):
        """Roda mc.check_conditions e mostra o resultado. Erro em vermelho,
        aviso em laranja, tudo certo em verde."""
        try:
            probs = mc.check_conditions(self._params())
        except Exception as e:                      # nunca derrubar o diálogo
            self.lbl_cond.setText(f"<span style='color:#888'>não verificado: {e}</span>")
            return
        if not probs:
            self.lbl_cond.setText(
                "<span style='color:#2ecc40'>✔ Γ1&gt;0, Γ2&lt;0, 0&lt;Γt&lt;1, "
                "Ts &lt; 2L/R e BL acima do limite de chattering.</span>")
            return
        linhas = []
        for nivel, txt in probs:
            cor = "#ff4136" if nivel == "erro" else "#ff851b"
            marca = "✖" if nivel == "erro" else "⚠"
            linhas.append(f"<span style='color:{cor}'>{marca} {txt}</span>")
        self.lbl_cond.setText("<br>".join(linhas))

    def _params(self):
        p = {k: spin.value() for k, spin in self.inputs.items()}
        p["poles"] = int(p.get("poles", 0))
        p["pole_pairs"] = self._pole_pairs()
        p["connection"] = self._connection()
        p["switch_func"] = self.combos["switch_func"].currentText().split()[0]
        return p

    # ------------------------------------------------------------------ #
    def load_current_values(self):
        """Pré-preenche do user_prefs (same_config), do motor do SAME.yaml ou,
        na falta dos dois, dos valores da bancada (mc.MOTOR_DEFAULTS)."""
        saved = self.prefs.get("same_config", {})
        if not saved:
            motores = self.same_yaml.get('motors', {}) or {}
            name = self.prefs.get("last_motor_name")
            if name not in motores and motores:
                name = list(motores.keys())[0]
            if motores.get(name):
                saved = mc.params_from_motor_dict(motores.get(name))
        # Sem nada salvo, abre com o motor da bancada em vez de tudo zerado:
        # zero em R, L ou Ts não é ponto de partida, é formulário inválido.
        if not saved:
            saved = dict(mc.MOTOR_DEFAULTS)

        for key, spin in self.inputs.items():
            v = saved.get(key, mc.MOTOR_DEFAULTS.get(key))
            spin.setValue(float(v) if v is not None else 0.0)

        conn = str(saved.get("connection", mc.MOTOR_DEFAULTS["connection"]))
        self.combos["connection"].setCurrentIndex(1 if conn.lower().startswith("d") else 0)
        sf = str(saved.get("switch_func", mc.MOTOR_DEFAULTS["switch_func"])).lower()
        idx = {"tanh": 0, "saturação": 1, "sat": 1, "sigmoide": 2}.get(sf, 0)
        self.combos["switch_func"].setCurrentIndex(idx)

    def save_yaml(self):
        name, ok = QInputDialog.getText(self, "Salvar motor", "Nome do motor:")
        name = (name or "").strip()
        if not ok or not name:
            return
        try:
            path = mc.save_motor_file(self.config_dir, name, self._params())
            QMessageBox.information(self, "Salvo", f"Motor salvo em:\n{path}")
        except Exception as e:
            QMessageBox.warning(self, "Erro ao salvar", str(e))

    def delete_yaml(self):
        files = mc.list_motor_files(self.config_dir)
        if not files:
            QMessageBox.information(self, "Apagar", "Nenhum motor .yaml em Config/motors/.")
            return
        name, ok = QInputDialog.getItem(self, "Apagar motor",
            "Selecione o motor para APAGAR:", list(files.keys()), 0, False)
        if not ok or not name:
            return
        resp = QMessageBox.question(self, "Confirmar remoção",
            f"Apagar PERMANENTEMENTE o motor '{name}'?\n{files[name]}",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
        if resp != QMessageBox.Yes:
            return
        try:
            os.remove(files[name])
            QMessageBox.information(self, "Apagado", f"Motor '{name}' removido.")
        except Exception as e:
            QMessageBox.warning(self, "Erro ao apagar", str(e))

    def apply_and_send(self):
        """Aplica no hardware SAME via o MESMO gerador de CMDs do 'Carregar'
        (motor_config.build_motor_cmds). Só os params com TARGET_* são enviados."""
        params = self._params()
        self.prefs["same_config"] = params
        same_cfg = self.same_yaml
        if "same" in same_cfg:
            same_cfg = same_cfg["same"]

        cmds = mc.build_motor_cmds(same_cfg, params)
        # build_motor_cmds só gera p/ as chaves do MOTOR_TARGET_MAP presentes em params.
        sent_keys = [k for k in mc.MOTOR_TARGET_MAP if k in params]
        for cmd_payload, gui_key in zip(cmds, sent_keys):
            print(f"[NET] CMD {mc.MOTOR_TARGET_MAP[gui_key]} = {params[gui_key]:.4f} "
                  f"-> [{cmd_payload.hex(' ').upper()}]")
            self.ethernet.send_command("SAME", cmd_payload)

        print(f"[PRNT] {len(cmds)} parâmetros aplicados na SAME.")
        self.accept()
