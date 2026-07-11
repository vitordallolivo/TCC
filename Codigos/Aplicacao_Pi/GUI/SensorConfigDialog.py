"""SensorConfigDialog — ajusta ganho, offset e limites (mín/máx) de TODOS os
sensores da SIBA e da SAME num só lugar.

Cada célula editável vira um comando `<siba|same> set <sensor> <linear|offset|
min|max> <valor>` (reaproveitando o parser do TinyTerminal) endereçado à placa
DONA daquele sensor. Só as células que o usuário EDITAR são enviadas ao clicar
em "Aplicar" — assim não se tara/recalibra um sensor sem querer.

Limites mín/máx: o firmware guarda em sensor_cfg[] (CMD_Core_prm.h) e os managers
(Security, só na SIBA) reagem quando o valor sai da faixa — ver
Communication_Data/errors.py. A SAME não tem Security_Manager (ver
SAME/CLAUDE.md), então lá mín/máx só ficam registrados, sem alarme.
"""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QTableWidget,
    QTableWidgetItem, QHeaderView, QAbstractItemView,
)

# Sensores calibráveis/monitoráveis, por placa (soc e rpm não têm ganho/offset
# próprios). SAME: V_c fica de fora — o ADC lê 3 fases mas só há calibração p/
# 2 tensões de linha (VOLTAGE_AB/BC); o firmware reusa a de V_b (BC) também
# p/ V_c (ver SAME/CLAUDE.md "Calibração de tensão").
_SENSORS = [
    ("SIBA", "load_cell_0"), ("SIBA", "load_cell_1"), ("SIBA", "load_cell_2"),
    ("SIBA", "load_cell_3"), ("SIBA", "load_cell_4"), ("SIBA", "load_cell_5"),
    ("SIBA", "v_batt"), ("SIBA", "i_batt"),
    ("SIBA", "temperature_battery"), ("SIBA", "air_speed"),
    ("SAME", "i_a"), ("SAME", "i_b"), ("SAME", "i_c"),
    ("SAME", "V_a"), ("SAME", "V_b"),
]

# Coluna -> (título, sub-ação do CMD)
_COLS = [
    ("Ganho (linear)", "linear"),
    ("Offset / Tara",  "offset"),
    ("Mínimo válido",  "min"),
    ("Máximo válido",  "max"),
]

_PENDING_BG = "#4a3b00"   # célula editada e ainda não enviada


class SensorConfigDialog(QDialog):
    def __init__(self, gui, parent=None):
        super().__init__(parent)
        self.gui = gui
        self.setWindowTitle("Configuração de Sensores — Ganho / Offset / Limites")
        self.setWindowFlags(Qt.Window)
        self.resize(640, 460)
        self._pending = set()      # (row, col) editados
        self._loading = True
        self._build_ui()
        self._loading = False

    # ------------------------------------------------------------------ #
    def _friendly(self, board, var):
        """Legenda dos gráficos (FX/FY/…) se existir; senão o próprio nome,
        prefixado com a placa dona do sensor (SIBA/SAME têm sensores distintos,
        mas o prefixo deixa claro pra onde cada comando vai)."""
        for info in (self.gui.plot_config.get("plots_mapping", {}) or {}).values():
            for v, leg in (info.get("vars") or {}).items():
                if v == var and leg:
                    return f"{board}: {leg}  ({var})"
        return f"{board}: {var}"

    def _build_ui(self):
        root = QVBoxLayout(self)

        info = QLabel(
            "Digite um valor numa célula e clique <b>Aplicar</b> — só as células "
            "editadas (destacadas) são enviadas, cada uma pra placa dona daquele "
            "sensor (SIBA ou SAME).<br>"
            "⚠ <b>Células de carga</b>: <i>Ganho</i> = calibrar pelo PESO conhecido; "
            "<i>Offset</i> = TARAR (o valor é ignorado, zera pela leitura bruta).<br>"
            "⚠ <b>SAME — V_c</b>: não aparece porque o firmware reusa a calibração "
            "de V_b (só há 2 alvos de tensão de linha pras 3 fases medidas)."
        )
        info.setWordWrap(True)
        root.addWidget(info)

        self.table = QTableWidget(len(_SENSORS), len(_COLS))
        self.table.setHorizontalHeaderLabels([c[0] for c in _COLS])
        self.table.setVerticalHeaderLabels([self._friendly(b, s) for b, s in _SENSORS])
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.table.setEditTriggers(QAbstractItemView.AllEditTriggers)
        for r in range(len(_SENSORS)):
            for c in range(len(_COLS)):
                it = QTableWidgetItem("")
                it.setTextAlignment(Qt.AlignCenter)
                self.table.setItem(r, c, it)
        self.table.itemChanged.connect(self._on_item_changed)
        root.addWidget(self.table, 1)

        self.lbl_status = QLabel("")
        self.lbl_status.setWordWrap(True)
        root.addWidget(self.lbl_status)

        bottom = QHBoxLayout()
        bottom.addStretch()
        self.btn_clear = QPushButton("Limpar campos")
        self.btn_clear.clicked.connect(self._clear)
        self.btn_apply = QPushButton("Aplicar")
        self.btn_apply.clicked.connect(self._apply)
        bottom.addWidget(self.btn_clear)
        bottom.addWidget(self.btn_apply)
        root.addLayout(bottom)

    # ------------------------------------------------------------------ #
    def _on_item_changed(self, item):
        if self._loading:
            return
        key = (item.row(), item.column())
        # setBackground/setData também emitem itemChanged — bloqueia p/ não
        # reentrar neste slot.
        self.table.blockSignals(True)
        if item.text().strip():
            self._pending.add(key)
            item.setBackground(_qcolor(_PENDING_BG))
        else:
            self._pending.discard(key)
            item.setData(Qt.BackgroundRole, None)
        self.table.blockSignals(False)

    def _clear(self):
        self._loading = True
        for r in range(len(_SENSORS)):
            for c in range(len(_COLS)):
                it = self.table.item(r, c)
                it.setText("")
                it.setData(Qt.BackgroundRole, None)
        self._pending.clear()
        self._loading = False
        self.lbl_status.setText("")

    def _apply(self):
        if not self._pending:
            self.lbl_status.setStyleSheet("color: #f0ad4e;")
            self.lbl_status.setText("Nada para aplicar — edite alguma célula primeiro.")
            return
        # Garante o link de cada placa envolvida pra o TX acontecer (só sobe
        # a(s) que realmente tiver(em) alguma célula pendente).
        boards_pending = {_SENSORS[r][0] for (r, _c) in self._pending}
        for board in boards_pending:
            if board not in self.gui.eth.workers:
                self.gui.eth.start_link(board)

        sent, failed = 0, 0
        for (r, c) in sorted(self._pending):
            board, sensor = _SENSORS[r]
            action = _COLS[c][1]
            try:
                value = float(self.table.item(r, c).text().replace(",", "."))
            except ValueError:
                failed += 1
                continue
            parts = [board.lower(), "set", sensor, action, f"{value:.6f}"]
            try:
                self.gui.terminal._handle_hardware_command(parts)
                sent += 1
                self.table.blockSignals(True)
                self.table.item(r, c).setData(Qt.BackgroundRole, None)
                self.table.blockSignals(False)
            except Exception:
                failed += 1

        self._pending.clear()
        self.lbl_status.setStyleSheet("color: #2ecc40;" if failed == 0 else "color: #ff4136;")
        msg = f"{sent} comando(s) enviado(s)."
        if failed:
            msg += f"  {failed} falharam (valor inválido?)."
        self.lbl_status.setText(msg)


def _qcolor(hexstr):
    from PySide6.QtGui import QColor
    return QColor(hexstr)
