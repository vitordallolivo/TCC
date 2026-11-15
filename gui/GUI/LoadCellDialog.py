"""Camada 4 · Apresentação — calibração das células de carga (HX711).

Como o HX711 é um sensor bruto, a calibração é linear (y = ganho·x + offset):
- **Tara (offset):** média das últimas N (~10) medições vira o offset — o
  usuário zera o sensor sem carga.
- **Ganho:** o usuário informa o peso/momento conhecido aplicado; o ganho é
  `peso / medição_média`.

Ambos APLICAM os comandos reais na placa (`siba set <cell> offset|linear <v>`),
reaproveitando o parser/So envio de CMD do TinyTerminal. As leituras ao vivo vêm
dos buffers de telemetria da GUI (`get_recent_average`).
"""
from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (
    QComboBox, QDialog, QDoubleSpinBox, QFormLayout, QGroupBox, QHBoxLayout,
    QLabel, QLineEdit, QMessageBox, QPushButton, QSpinBox, QVBoxLayout,
)


class LoadCellDialog(QDialog):
    def __init__(self, gui, parent=None):
        super().__init__(parent)
        self.gui = gui  # AlbatrozGUI: buffers de telemetria + terminal + eth
        self.setWindowTitle("Calibrar Célula de Carga (HX711)")
        self.setWindowFlags(Qt.Window)
        self.resize(480, 360)

        self._sensor_map = self._build_sensor_map()  # display (FX/FY/...) -> var
        self._build_ui()

        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh_reading)
        self._timer.start(300)

    # ------------------------------------------------------------------ #
    def _build_sensor_map(self):
        """Mapa {nome_exibido -> variável}. APENAS células de carga (força/
        momento) — tensão/corrente NUNCA entram aqui. O nome exibido é a MESMA
        legenda dos gráficos (FX/FY/FZ/MX/MY/MZ), vinda de plots.yaml."""
        # var -> legenda dos gráficos
        legend = {}
        for info in (self.gui.plot_config.get("plots_mapping", {}) or {}).values():
            for var, leg in (info.get("vars") or {}).items():
                if leg:
                    legend[var] = leg
        live = self.gui.value_buffers
        mapping = {}
        # Só load_cell_* (as células de carga = forças e momentos).
        for var in sorted(v for v in live if v.startswith("load_cell")):
            disp = legend.get(var, var)
            mapping[disp] = var
        return mapping

    def _build_ui(self):
        root = QVBoxLayout(self)

        top = QFormLayout()
        self.cmb = QComboBox()
        self.cmb.addItems(list(self._sensor_map.keys()) or ["(sem células com leitura)"])
        self.cmb.currentTextChanged.connect(self._refresh_reading)
        top.addRow("Célula de carga:", self.cmb)
        self.sp_n = QSpinBox(); self.sp_n.setRange(1, 200); self.sp_n.setValue(10)
        top.addRow("Nº de medições (média):", self.sp_n)
        self.lbl_read = QLabel("—")
        self.lbl_read.setStyleSheet("font-weight: bold; font-size: 12pt;")
        top.addRow("Média atual:", self.lbl_read)
        self.lbl_diag = QLabel("")
        self.lbl_diag.setWordWrap(True)
        top.addRow("Diagnóstico:", self.lbl_diag)
        root.addLayout(top)

        # --- Tara (offset = raw atual, feita no firmware) ---
        g_tare = QGroupBox("Tara — zera a célula (offset = leitura bruta atual)")
        lt = QHBoxLayout(g_tare)
        self.btn_tare = QPushButton("Tarar agora")
        self.btn_tare.clicked.connect(self._do_tare)
        lt.addWidget(QLabel("Deixe a célula SEM carga e clique:"))
        lt.addStretch(); lt.addWidget(self.btn_tare)
        root.addWidget(g_tare)

        # --- Ganho por PESO CONHECIDO (modelo canônico HX711) ---
        g_gain = QGroupBox("Ganho — informe o PESO conhecido; a placa ajusta a escala")
        fg = QFormLayout(g_gain)
        self.sp_weight = QDoubleSpinBox()
        self.sp_weight.setDecimals(4); self.sp_weight.setRange(-1e6, 1e6)
        self.sp_weight.setValue(1.0)
        fg.addRow("Peso/momento conhecido aplicado:", self.sp_weight)
        self.btn_gain = QPushButton("Calibrar pelo peso")
        self.btn_gain.clicked.connect(self._do_gain)
        fg.addRow(self.btn_gain)
        root.addWidget(g_gain)

        # --- Reset + status ---
        bottom = QHBoxLayout()
        self.btn_reset = QPushButton("Resetar calibração")
        self.btn_reset.clicked.connect(self._do_reset)
        bottom.addWidget(self.btn_reset); bottom.addStretch()
        root.addLayout(bottom)

        self.lbl_status = QLabel("")
        self.lbl_status.setStyleSheet("color: #2ecc40;")
        self.lbl_status.setWordWrap(True)
        root.addWidget(self.lbl_status)

    # ------------------------------------------------------------------ #
    def _current_sensor(self):
        """Nome da variável (load_cell_N) do item selecionado (exibido como FX...)."""
        return self._sensor_map.get(self.cmb.currentText())

    def _avg(self):
        sensor = self._current_sensor()
        if sensor is None:
            return None
        return self.gui.get_recent_average(sensor, self.sp_n.value())

    # HX711 24-bit: desconectado satura em ~0xFFFFFF; bruto descalibrado fica
    # em dezenas/centenas de milhar (>> 10k). Um sensor calibrado lê newtons.
    _HX711_DISCONNECTED = 0xFF0000     # ~16,7 M — leitura saturada = sem célula
    _UNCALIB_THRESHOLD = 10000.0       # média muito acima disso = bruto/descalibrado

    def _refresh_reading(self):
        avg = self._avg()
        if avg is None:
            self.lbl_read.setText("—")
            self.lbl_diag.setText("")
            return
        self.lbl_read.setText(f"{avg:.4f}")
        if abs(avg) >= self._HX711_DISCONNECTED:
            self.lbl_diag.setText("⚠ Célula DESCONECTADA (leitura saturada ≈ 0xFFFFFF).")
            self.lbl_diag.setStyleSheet("color: #ff4136; font-weight: bold;")
        elif abs(avg) > self._UNCALIB_THRESHOLD:
            self.lbl_diag.setText(f"⚠ Provavelmente DESCALIBRADA (média {avg:.0f} ≫ 10k — valor bruto). Tare e calibre o ganho.")
            self.lbl_diag.setStyleSheet("color: #f0ad4e;")
        else:
            self.lbl_diag.setText("✓ Leitura em faixa calibrada.")
            self.lbl_diag.setStyleSheet("color: #2ecc40;")

    # ------------------------------------------------------------------ #
    def _send(self, action, value):
        """Envia `siba set <sensor> <action> <value>` via o caminho do terminal."""
        sensor = self._current_sensor()
        if sensor is None:
            self._warn("Selecione um sensor com leitura ao vivo.")
            return False
        # Garante que o link SIBA existe para o TX acontecer.
        if "SIBA" not in self.gui.eth.workers:
            self.gui.eth.start_link("SIBA")
        parts = ["siba", "set", sensor, action, f"{value:.6f}"]
        try:
            self.gui.terminal._handle_hardware_command(parts)
        except Exception as e:
            self._warn(f"Falha ao enviar comando: {e}")
            return False
        return True

    def _do_tare(self):
        # Modelo canônico HX711: a TARA é feita no firmware — ele lê o valor
        # BRUTO (raw) atual e faz tare = raw, zerando a leitura (grams =
        # (raw-raw)/scale = 0). O app só dispara; não calcula offset (o app só
        # enxerga grams calibrado, não o raw). Por isso tarar é idempotente:
        # tarar duas vezes só re-zera, não desempilha nada.
        if self._current_sensor() is None:
            return self._warn("Selecione uma célula com leitura ao vivo.")
        # `offset` (ação) na célula = TARA; o valor enviado é ignorado pelo firmware.
        if self._send("offset", 0.0):
            self._ok("Tara enviada — a placa zera pela leitura bruta atual (raw).")

    def _do_gain(self):
        # Modelo canônico HX711: a calibração de ganho é por PESO CONHECIDO.
        # Coloca-se o peso na célula, informa-se o valor, e o FIRMWARE resolve
        # scale = (raw - tare)/peso (counts/unidade) — de modo que a leitura
        # passe a valer o próprio peso. O app manda o PESO, não o ganho
        # (mandar peso/média era o inverso de counts/unidade — bug antigo).
        if self._current_sensor() is None:
            return self._warn("Selecione uma célula com leitura ao vivo.")
        weight = self.sp_weight.value()
        if abs(weight) < 1e-9:
            return self._warn("Informe o peso conhecido (≠ 0) aplicado na célula.")
        # `linear` (ação) na célula = calibrar escala pelo peso conhecido.
        if self._send("linear", weight):
            self._ok(f"Calibração enviada: peso conhecido = {weight:g}. "
                     f"A placa ajusta a escala para a leitura valer esse peso.")

    def _do_reset(self):
        if self._send("reset", 0.0):
            self._ok("Calibração resetada para o padrão de fábrica.")

    # ------------------------------------------------------------------ #
    def _ok(self, msg):
        self.lbl_status.setStyleSheet("color: #2ecc40;")
        self.lbl_status.setText(msg)

    def _warn(self, msg):
        self.lbl_status.setStyleSheet("color: #ff4136;")
        self.lbl_status.setText(msg)
