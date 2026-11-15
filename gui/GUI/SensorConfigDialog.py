"""SensorConfigDialog — ajusta ganho, offset e limites de todos os sensores
da SIBA e da SAME num só lugar.

Cada célula editada vira um comando `<siba|same> set <sensor> <campo>
<valor>`, e só o que foi editado é enviado ao clicar "Aplicar". Mín/máx
só disparam alarme na SIBA (Security_Manager) — a SAME apenas registra.
"""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QTableWidget,
    QTableWidgetItem, QHeaderView, QAbstractItemView,
)

# Sensores calibráveis/monitoráveis, por placa (soc e rpm não têm ganho/offset
# próprios). SAME: as 3 correntes E as 3 tensões de fase têm calibração
# própria cada uma (TARGET_CURRENT_A/B/C, TARGET_VOLTAGE_A/B/C).
_SENSORS = [
    ("SIBA", "load_cell_0"), ("SIBA", "load_cell_1"), ("SIBA", "load_cell_2"),
    ("SIBA", "load_cell_3"), ("SIBA", "load_cell_4"), ("SIBA", "load_cell_5"),
    ("SIBA", "v_batt"), ("SIBA", "i_batt"),
    ("SIBA", "temperature_battery"), ("SIBA", "air_speed"),
    ("SAME", "i_a"), ("SAME", "i_b"), ("SAME", "i_c"),
    ("SAME", "V_a"), ("SAME", "V_b"), ("SAME", "V_c"),
]

# Coluna -> (título, sub-ação do CMD)
_COLS = [
    ("Ganho (linear)", "linear"),
    ("Offset / Tara",  "offset"),
    ("Mínimo válido",  "min"),
    ("Máximo válido",  "max"),
]

_PENDING_BG = "#4a3b00"   # célula editada e ainda não enviada
_DEFAULT_FG = "#8a8a8a"   # célula mostrando o default do yaml, ainda não editada

_NO_LIMIT = 1.0e30  # == CMD_CORE_NO_LIMIT (Config/CMD_Core_prm_template.h.j2)


def _numeric_default(raw):
    """Converte um valor de sensor_defaults (yaml) pra float — traduz o
    sentinela textual '(-)CMD_CORE_NO_LIMIT' pro valor real que o firmware
    usa. Retorna None se `raw` for None (sensor sem default declarado)."""
    if raw is None:
        return None
    if isinstance(raw, str):
        s = raw.strip()
        if s == "CMD_CORE_NO_LIMIT":
            return _NO_LIMIT
        if s == "-CMD_CORE_NO_LIMIT":
            return -_NO_LIMIT
        return None
    return float(raw)


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
    def _default_for(self, board, sensor):
        """Entrada de sensor_defaults (yaml) do sensor da linha, indexada
        pelo MESMO índice de target_type que o firmware usa (ver
        TinyTerminal._resolve_target_index) — ou None se não houver default
        declarado pra esse índice."""
        idx = self.gui.terminal._resolve_target_index(board, sensor)
        cfg = self.gui.eth.registry.get(board)
        defaults = (cfg.protocol_cfg.get('sensor_defaults') if cfg else None) or []
        if 0 <= idx < len(defaults):
            return defaults[idx]
        return None

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
            "Células em <span style='color:#8a8a8a;'>cinza</span> mostram o "
            "valor <b>default</b> (yaml) — só viram pendente se você editar. "
            "Clique <b>Aplicar</b> pra mandar só as células destacadas, cada "
            "uma pra placa dona daquele sensor (SIBA ou SAME).<br>"
            "⚠ <b>Células de carga</b>: <i>Ganho</i> = calibrar pelo PESO conhecido; "
            "<i>Offset</i> = TARAR (o valor é ignorado, zera pela leitura bruta)."
        )
        info.setWordWrap(True)
        root.addWidget(info)

        self.table = QTableWidget(len(_SENSORS), len(_COLS))
        self.table.setHorizontalHeaderLabels([c[0] for c in _COLS])
        self.table.setVerticalHeaderLabels([self._friendly(b, s) for b, s in _SENSORS])
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.table.setEditTriggers(QAbstractItemView.AllEditTriggers)
        for r, (board, sensor) in enumerate(_SENSORS):
            default = self._default_for(board, sensor) or {}
            for c, (_, action) in enumerate(_COLS):
                it = QTableWidgetItem("")
                it.setTextAlignment(Qt.AlignCenter)
                # Só mostra default numérico de verdade — o sentinela "sem
                # limite" (min/max) fica em branco, não ajuda o usuário a ver
                # "1e+30" na célula.
                raw = default.get(action)
                if not isinstance(raw, str):
                    val = _numeric_default(raw)
                    if val is not None:
                        it.setText(f"{val:g}")
                        it.setForeground(_qcolor(_DEFAULT_FG))
                self.table.setItem(r, c, it)
        self.table.itemChanged.connect(self._on_item_changed)
        root.addWidget(self.table, 1)

        self.lbl_status = QLabel("")
        self.lbl_status.setWordWrap(True)
        root.addWidget(self.lbl_status)

        bottom = QHBoxLayout()
        bottom.addStretch()
        self.btn_restore = QPushButton("Restaurar padrão")
        self.btn_restore.setToolTip(
            "Manda DEFAULT_ACTION_SENSE_RESET_DEFAULT (ganho+offset) e os "
            "limites do yaml (mín/máx) pra TODOS os sensores da tabela.")
        self.btn_restore.clicked.connect(self._restore_defaults)
        self.btn_clear = QPushButton("Limpar campos")
        self.btn_clear.clicked.connect(self._clear)
        self.btn_apply = QPushButton("Aplicar")
        self.btn_apply.clicked.connect(self._apply)
        bottom.addWidget(self.btn_restore)
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
        boards_pending = {_SENSORS[r][0] for (r, _) in self._pending}
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

    def _restore_defaults(self):
        """Manda TODOS os sensores da tabela de volta pro default do yaml.

        Ganho+offset: uma ação só no firmware (DEFAULT_ACTION_SENSE_RESET_
        DEFAULT restaura os dois de uma vez, não precisa saber o valor).
        Mín/máx: o firmware não tem uma ação "reset" própria pra faixa
        (só grava o que vier em `value`), então reenvia os valores default
        declarados em sensor_defaults explicitamente."""
        boards = {b for b, _ in _SENSORS}
        for board in boards:
            if board not in self.gui.eth.workers:
                self.gui.eth.start_link(board)

        sent, failed = 0, 0
        for board, sensor in _SENSORS:
            try:
                self.gui.terminal._handle_hardware_command(
                    [board.lower(), "set", sensor, "reset", "0.0"])
                sent += 1
            except Exception:
                failed += 1
                continue
            default = self._default_for(board, sensor) or {}
            for action in ("min", "max"):
                val = _numeric_default(default.get(action))
                if val is None:
                    continue
                try:
                    self.gui.terminal._handle_hardware_command(
                        [board.lower(), "set", sensor, action, f"{val:.6f}"])
                except Exception:
                    pass

        self._loading = True
        self.table.blockSignals(True)
        for r in range(len(_SENSORS)):
            for c in range(len(_COLS)):
                self.table.item(r, c).setData(Qt.BackgroundRole, None)
        self.table.blockSignals(False)
        self._pending.clear()
        self._loading = False

        self.lbl_status.setStyleSheet("color: #2ecc40;" if failed == 0 else "color: #ff4136;")
        msg = f"Padrão restaurado em {sent} sensor(es)."
        if failed:
            msg += f"  {failed} falharam."
        self.lbl_status.setText(msg)


def _qcolor(hexstr):
    from PySide6.QtGui import QColor
    return QColor(hexstr)
