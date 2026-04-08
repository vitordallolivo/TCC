import sys
import csv
import numpy as np
from datetime import datetime
from PySide6 import QtWidgets
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QGridLayout, QPushButton, QLabel, QSlider,
    QDialog, QSizePolicy, QFrame, QApplication,
    QFileDialog, QMessageBox
)
from PySide6.QtGui import QFont, QKeySequence, QShortcut
import pyqtgraph as pg

from Communication_Data.Ethernet import (
    EthernetReceiver, BOARD_SIBA, BOARD_SAME, CHANNEL_NAMES
)

# ==================================================================
#                         Estilo Global
# ==================================================================
DARK_BG      = "#0d0f14"
PANEL_BG     = "#13161e"
BORDER       = "#1e2330"
ACCENT_BLUE  = "#00aaff"
ACCENT_GREEN = "#00e676"
ACCENT_RED   = "#ff1744"
ACCENT_ORANGE= "#ff9100"
TEXT_PRIMARY = "#e8eaf0"
TEXT_MUTED   = "#5a6070"

STYLE = f"""
QMainWindow, QWidget, QDialog {{
    background-color: {DARK_BG};
    color: {TEXT_PRIMARY};
    font-family: 'Courier New', monospace;
}}
QFrame#panel {{
    background-color: {PANEL_BG};
    border: 1px solid {BORDER};
    border-radius: 6px;
}}
QPushButton {{
    background-color: {PANEL_BG};
    color: {TEXT_PRIMARY};
    border: 1px solid {BORDER};
    border-radius: 4px;
    padding: 6px 14px;
    font-family: 'Courier New', monospace;
    font-size: 11px;
    letter-spacing: 1px;
}}
QPushButton:hover   {{ border-color: {ACCENT_BLUE};   color: {ACCENT_BLUE};   }}
QPushButton#btn_ready  {{ border-color: {ACCENT_GREEN};  color: {ACCENT_GREEN};  }}
QPushButton#btn_ready:hover  {{ background-color: {ACCENT_GREEN};  color: {DARK_BG}; }}
QPushButton#btn_off    {{ border-color: {ACCENT_RED};    color: {ACCENT_RED};    }}
QPushButton#btn_off:hover    {{ background-color: {ACCENT_RED};    color: {DARK_BG}; }}
QPushButton#btn_calib  {{ border-color: {ACCENT_ORANGE}; color: {ACCENT_ORANGE}; }}
QPushButton#btn_calib:hover  {{ background-color: {ACCENT_ORANGE}; color: {DARK_BG}; }}
QSlider::groove:horizontal {{
    background: {BORDER};
    height: 6px;
    border-radius: 3px;
}}
QSlider::handle:horizontal {{
    background: {ACCENT_BLUE};
    width: 16px;
    height: 16px;
    margin: -5px 0;
    border-radius: 8px;
}}
QSlider::sub-page:horizontal {{
    background: {ACCENT_BLUE};
    border-radius: 3px;
}}
QLabel#title {{ color: {TEXT_MUTED}; font-size: 10px; letter-spacing: 2px; }}
QLabel#value {{ color: {ACCENT_BLUE}; font-size: 12px; font-weight: bold; }}
"""

# ==================================================================
#                         LED Widget
# ==================================================================
class LEDIndicator(QWidget):
    def __init__(self, label: str, parent=None):
        super().__init__(parent)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(4, 2, 4, 2)
        layout.setSpacing(6)
        self._led = QLabel("●")
        self._led.setFont(QFont("Courier New", 14))
        self._label = QLabel(label)
        self._label.setObjectName("title")
        layout.addWidget(self._led)
        layout.addWidget(self._label)
        self.set_state(False)

    def set_state(self, on: bool):
        self._led.setStyleSheet(f"color: {'#00e676' if on else '#ff1744'};")

# ==================================================================
#                     Graph helper
# ==================================================================
def make_plot(title: str, unit: str, color: str) -> tuple[pg.PlotWidget, object]:
    pw = pg.PlotWidget()
    pw.setBackground(PANEL_BG)
    pw.setTitle(
        f"<span style='color:{TEXT_MUTED};font-size:9px;letter-spacing:2px'>"
        f"{title} [{unit}]</span>"
    )
    for axis in ("left", "bottom"):
        pw.getAxis(axis).setPen(pg.mkPen(BORDER))
        pw.getAxis(axis).setTextPen(pg.mkPen(TEXT_MUTED))
    pw.showGrid(x=True, y=True, alpha=0.15)
    pw.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
    curve = pw.plot(pen=pg.mkPen(color, width=1.6))
    return pw, curve

# ==================================================================
#           Painel de Calibração — Células de Carga
# ==================================================================
class CalibracaoDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Calibração — Células de Carga")
        self.setMinimumSize(440, 300)
        self.setStyleSheet(STYLE)

        layout = QVBoxLayout(self)
        layout.setSpacing(14)

        title = QLabel("CALIBRAÇÃO CÉLULAS DE CARGA")
        title.setObjectName("title")
        title.setAlignment(Qt.AlignCenter)
        layout.addWidget(title)

        # Célula 1 e 2
        self._raw_labels = {}
        for i in range(1, 3):
            frame, raw_lbl = self._make_cell_row(f"Célula {i}")
            self._raw_labels[i] = raw_lbl
            layout.addWidget(frame)

        # Separador
        sep = QFrame()
        sep.setFrameShape(QFrame.HLine)
        sep.setStyleSheet(f"color: {BORDER};")
        layout.addWidget(sep)

        # Botões
        btn_tara = QPushButton("⚖  TARA (ZERAR)")
        btn_tara.clicked.connect(self._on_tara)

        btn_cal = QPushButton("✔  APLICAR CALIBRAÇÃO")
        btn_cal.setObjectName("btn_ready")
        btn_cal.clicked.connect(self._on_calibrate)

        layout.addWidget(btn_tara)
        layout.addWidget(btn_cal)
        layout.addStretch()

    def _make_cell_row(self, label: str) -> tuple[QFrame, QLabel]:
        frame = QFrame()
        row = QHBoxLayout(frame)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(10)

        lbl = QLabel(label)
        lbl.setFixedWidth(70)

        lbl_peso = QLabel("Ref. (kg):")
        lbl_peso.setObjectName("title")

        spinbox = QtWidgets.QDoubleSpinBox()
        spinbox.setRange(0.0, 500.0)
        spinbox.setDecimals(3)
        spinbox.setSuffix(" kg")
        spinbox.setFixedWidth(110)
        spinbox.setStyleSheet(f"""
            QDoubleSpinBox {{
                background: {PANEL_BG};
                border: 1px solid {BORDER};
                border-radius: 4px;
                color: {TEXT_PRIMARY};
                padding: 4px;
                font-family: 'Courier New', monospace;
            }}
        """)

        raw_lbl = QLabel("RAW: ---")
        raw_lbl.setObjectName("value")

        row.addWidget(lbl)
        row.addWidget(lbl_peso)
        row.addWidget(spinbox)
        row.addStretch()
        row.addWidget(raw_lbl)
        return frame, raw_lbl

    def update_raw(self, cell: int, value: float):
        """Chame para atualizar o valor RAW exibido."""
        if cell in self._raw_labels:
            self._raw_labels[cell].setText(f"RAW: {value:.4f}")

    def _on_tara(self):
        # TODO: enviar comando de tara para SIBA via Ethernet
        print("[CALIB] Tara aplicada.")

    def _on_calibrate(self):
        # TODO: enviar coeficiente de calibração para SIBA
        print("[CALIB] Calibração aplicada.")

# ==================================================================
#               Painel Secundário — Inversor
# ==================================================================
class InversorPanel(QDialog):
    CHANNELS = [
        ("Va",     "V",   "#00aaff"),
        ("Vb",     "V",   "#ff9100"),
        ("Vc",     "V",   "#00e676"),
        ("ia",     "A",   "#e040fb"),
        ("ib",     "A",   "#ff6d00"),
        ("ic",     "A",   "#00e5ff"),
        ("RPM",    "rpm", "#00aaff"),
        ("Torque", "Nm",  "#00e676"),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Inversor — Grandezas Elétricas")
        self.setMinimumSize(900, 480)
        self.setStyleSheet(STYLE)

        layout = QGridLayout(self)
        layout.setSpacing(4)

        self._curves: dict[str, object] = {}
        self._data:   dict[str, np.ndarray] = {}

        for idx, (name, unit, color) in enumerate(self.CHANNELS):
            r, c = divmod(idx, 4)
            pw, curve = make_plot(name, unit, color)
            self._curves[name] = curve
            self._data[name]   = np.array([])
            layout.addWidget(pw, r, c)

    def push_sample(self, samples: dict):
        for name, value in samples.items():
            if name in self._curves:
                self._data[name] = np.append(self._data[name], value)
                self._curves[name].setData(self._data[name])

# ==================================================================
#                         Main GUI
# ==================================================================
class AlbatrozGUI(QMainWindow):
    MAIN_CHANNELS = [
        ("Tração",          "N",   "#00aaff", (0,0)),
        ("Momento Bancada", "Nm",  "#ff9100", (0,1)),
        ("RPM Sensor",      "rpm", "#00e676", (0,2)),
        ("Vel. Vento",      "m/s", "#00e5ff", (0,3)),
        ("Tensão Bat.",     "V",   "#e040fb", (1,0)),
        ("Corrente",        "A",   "#ff6d00", (1,1)),
        ("Bateria",         "%",   "#00e676", (1,2)),
    ]

    def __init__(self):
        super().__init__()
        self.setWindowTitle("ALBATROZ — Monitoramento")
        self.setMinimumSize(1280, 720)
        self.setStyleSheet(STYLE)

        # Buffers para exportação CSV
        self._csv_siba: list[dict] = []
        self._csv_same: list[dict] = []

        self._inversor_panel  = InversorPanel(self)
        self._calibracao_dial = CalibracaoDialog(self)

        self._build_ui()
        self._start_ethernet()

        # F11 fullscreen
        fs = QShortcut(QKeySequence("F11"), self)
        fs.activated.connect(self._toggle_fullscreen)

    # ------------------------------------------------------------------
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(8, 8, 8, 8)
        root.setSpacing(6)

        # Header
        header = QHBoxLayout()
        lbl = QLabel("ALBATROZ")
        lbl.setFont(QFont("Courier New", 16, QFont.Bold))
        lbl.setStyleSheet(f"color:{ACCENT_BLUE}; letter-spacing:6px;")
        sub = QLabel("SISTEMA DE MONITORAMENTO EM TEMPO REAL")
        sub.setObjectName("title")
        btn_inv = QPushButton("▶  INVERSOR")
        btn_inv.clicked.connect(self._inversor_panel.show)
        header.addWidget(lbl)
        header.addSpacing(10)
        header.addWidget(sub)
        header.addStretch()
        header.addWidget(btn_inv)
        root.addLayout(header)

        # Gráficos principais
        grid = QGridLayout()
        grid.setSpacing(4)
        self._curves: dict[str, object] = {}
        self._data:   dict[str, np.ndarray] = {}

        for name, unit, color, (r, c) in self.MAIN_CHANNELS:
            pw, curve = make_plot(name, unit, color)
            self._curves[name] = curve
            self._data[name]   = np.array([])
            grid.addWidget(pw, r, c)

        root.addLayout(grid, stretch=1)

        # ── Status bar ─────────────────────────────────────────────
        bar = QFrame()
        bar.setObjectName("panel")
        bl = QHBoxLayout(bar)
        bl.setContentsMargins(10, 6, 10, 6)
        bl.setSpacing(14)

        # LEDs
        self._led_siba = LEDIndicator("SIBA")
        self._led_same = LEDIndicator("SAME")

        # Slider throttle
        thr_lbl = QLabel("THROTTLE")
        thr_lbl.setObjectName("title")

        self._throttle = QSlider(Qt.Horizontal)
        self._throttle.setRange(0, 100)
        self._throttle.setValue(0)
        self._throttle.setFixedWidth(180)
        self._throttle.setTickInterval(10)

        self._throttle_val = QLabel("0%")
        self._throttle_val.setObjectName("value")
        self._throttle_val.setFixedWidth(36)
        self._throttle.valueChanged.connect(
            lambda v: self._throttle_val.setText(f"{v}%")
        )

        # Botões
        btn_calib = QPushButton("⚙  CALIBRAÇÃO")
        btn_calib.setObjectName("btn_calib")
        btn_calib.clicked.connect(self._calibracao_dial.show)

        btn_save  = QPushButton("💾  SALVAR CSV")
        btn_ready = QPushButton("✔  READY")
        btn_ready.setObjectName("btn_ready")
        btn_off   = QPushButton("⏻  DESLIGAR")
        btn_off.setObjectName("btn_off")

        btn_save.clicked.connect(self._on_save)
        btn_ready.clicked.connect(self._on_ready)
        btn_off.clicked.connect(self._on_off)

        bl.addWidget(self._led_siba)
        bl.addWidget(self._led_same)
        bl.addStretch()
        bl.addWidget(thr_lbl)
        bl.addWidget(self._throttle)
        bl.addWidget(self._throttle_val)
        bl.addStretch()
        bl.addWidget(btn_calib)
        bl.addWidget(btn_save)
        bl.addWidget(btn_ready)
        bl.addWidget(btn_off)
        root.addWidget(bar)

    # ------------------------------------------------------------------
    def _start_ethernet(self):
        self._eth = EthernetReceiver(ip="0.0.0.0", port=9999)
        self._eth.data_ready.connect(self._on_data)
        self._eth.connection_changed.connect(self._on_connection)
        self._eth.start()

    # ------------------------------------------------------------------
    def _on_data(self, board_id: int, samples: dict):
        ts = {"timestamp": datetime.now().isoformat()}

        if board_id == BOARD_SIBA:
            self._csv_siba.append({**ts, **samples})
            for name, value in samples.items():
                if name in self._curves:
                    self._data[name] = np.append(self._data[name], value)
                    self._curves[name].setData(self._data[name])

        elif board_id == BOARD_SAME:
            self._csv_same.append({**ts, **samples})
            self._inversor_panel.push_sample(samples)

    def _on_connection(self, board_id: int, connected: bool):
        if board_id == BOARD_SIBA:
            self._led_siba.set_state(connected)
        elif board_id == BOARD_SAME:
            self._led_same.set_state(connected)

    # ------------------------------------------------------------------
    def _on_save(self):
        if not self._csv_siba and not self._csv_same:
            QMessageBox.warning(self, "Salvar", "Nenhum dado recebido ainda.")
            return

        path, _ = QFileDialog.getSaveFileName(
            self, "Salvar dados",
            f"albatros_{datetime.now().strftime('%Y%m%d_%H%M%S')}",
            "CSV (*.csv)"
        )
        if not path:
            return

        base = path.replace(".csv", "")

        def write_csv(filepath: str, rows: list[dict]):
            if not rows:
                return
            with open(filepath, "w", newline="") as f:
                writer = csv.DictWriter(f, fieldnames=rows[0].keys())
                writer.writeheader()
                writer.writerows(rows)

        write_csv(f"{base}_SIBA.csv", self._csv_siba)
        write_csv(f"{base}_SAME.csv", self._csv_same)

        QMessageBox.information(
            self, "Salvar",
            f"Arquivos salvos:\n{base}_SIBA.csv\n{base}_SAME.csv"
        )

    def _on_ready(self):
        print("[INFO] READY enviado.")

    def _on_off(self):
        reply = QMessageBox.question(
            self, "Desligar", "Confirmar desligamento?",
            QMessageBox.Yes | QMessageBox.No
        )
        if reply == QMessageBox.Yes:
            self._eth.stop()
            print("[INFO] Sistema desligado.")

    def _toggle_fullscreen(self):
        if self.isFullScreen():
            self.showNormal()
        else:
            self.showFullScreen()

    def closeEvent(self, event):
        self._eth.stop()
        super().closeEvent(event)