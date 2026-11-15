"""Camada 4 · Apresentação — Gerenciador de Placas (popout).

Janela para PROCURAR placas, EDITAR IP/porta (em runtime, sem persistir) e ver
um LED por placa: verde = detectada / RX ativo, vermelho = ausente,
cinza = ainda não testada.

Dirigido pelo registry do LinkManager + DiscoveryService — agnóstico a placa.
"""
import threading

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import (
    QDialog, QGridLayout, QHBoxLayout, QLabel, QLineEdit, QMessageBox,
    QPushButton, QVBoxLayout, QWidget,
)

from Communication_Data import codecs
from Communication_Data.discovery import DiscoveryService
from Communication_Data.native_sim import native_sim_supported

# Cores do LED por estado.
_LED = {
    "green": "#2ecc40",
    "red": "#ff4136",
    "gray": "#555555",
}


class LedIndicator(QLabel):
    """Círculo colorido simples (LED de status)."""

    def __init__(self, diameter=16):
        super().__init__()
        self._d = diameter
        self.setFixedSize(diameter, diameter)
        self.set_state("gray")

    def set_state(self, state):
        color = _LED.get(state, _LED["gray"])
        r = self._d // 2
        self.setStyleSheet(
            f"background-color: {color}; border-radius: {r}px; border: 1px solid #222;"
        )


class _BoardRow:
    """Widgets e estado de uma linha (uma placa)."""

    def __init__(self, cfg):
        self.cfg = cfg
        self.led = LedIndicator()
        self.name = QLabel(f"<b>{cfg.id}</b> <i>({cfg.transport})</i>")
        self.ip_edit = QLineEdit(cfg.ip)
        self.ip_edit.setMinimumWidth(130)
        # Um campo por porta (pc_rx/board_rx).
        self.port_edits = {}
        self.ports_box = QWidget()
        pb = QHBoxLayout(self.ports_box)
        pb.setContentsMargins(0, 0, 0, 0)
        for key, val in cfg.ports.items():
            pb.addWidget(QLabel(key))
            e = QLineEdit(str(val))
            e.setFixedWidth(64)
            self.port_edits[key] = e
            pb.addWidget(e)
        self.status = QLabel("não testada")
        self.status.setStyleSheet("color: #888;")
        self.btn_apply = QPushButton("Aplicar")
        self.btn_detect = QPushButton("Detectar")
        self.btn_native = QPushButton("▶ Sim. Nativa")
        self.btn_reset = QPushButton("⟳ Reiniciar")
        self.btn_reset.setToolTip(
            "Reinicia o firmware da placa (FUNC_RESET). A placa cai e volta "
            "sozinha; a calibração salva no EEPROM é restaurada no boot."
        )
        self.last_present = None  # None=não testada, True/False=resultado


class BoardManagerDialog(QDialog):
    # Emitido pela thread de descoberta -> atualiza LEDs na thread da UI.
    _discovery_done = Signal(dict)

    def __init__(self, manager, parent=None):
        super().__init__(parent)
        self.manager = manager
        self.discovery = DiscoveryService(manager)
        self.setWindowTitle("Gerenciador de Placas")
        self.setWindowFlags(Qt.Window)
        self.resize(640, 260)

        self.rows = {}
        self._build_ui()

        self._discovery_done.connect(self._apply_discovery)

        # Timer de LED ao vivo: promove a verde quando o link tem RX recente.
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh_live)
        self._timer.start(1000)

    # ------------------------------------------------------------------ #
    def _build_ui(self):
        root = QVBoxLayout(self)

        grid = QGridLayout()
        headers = ["", "Placa", "IP", "Portas", "Status", "", "", "", ""]
        for c, h in enumerate(headers):
            lbl = QLabel(f"<b>{h}</b>")
            grid.addWidget(lbl, 0, c)

        linux_ok = native_sim_supported()
        for r, cfg in enumerate(self.manager.registry.all(), start=1):
            row = _BoardRow(cfg)
            self.rows[cfg.id] = row
            grid.addWidget(row.led, r, 0, alignment=Qt.AlignCenter)
            grid.addWidget(row.name, r, 1)
            grid.addWidget(row.ip_edit, r, 2)
            grid.addWidget(row.ports_box, r, 3)
            grid.addWidget(row.status, r, 4)
            grid.addWidget(row.btn_apply, r, 5)
            grid.addWidget(row.btn_detect, r, 6)
            grid.addWidget(row.btn_native, r, 7)
            grid.addWidget(row.btn_reset, r, 8)
            row.btn_apply.clicked.connect(lambda _=False, b=cfg.id: self._apply_row(b))
            row.btn_detect.clicked.connect(lambda _=False, b=cfg.id: self._detect_one(b))
            row.btn_native.clicked.connect(lambda _=False, b=cfg.id: self._toggle_native(b))
            row.btn_reset.clicked.connect(lambda _=False, b=cfg.id: self._reset_board(b))
            if not linux_ok:
                # native_sim (zephyr.exe) é um binário POSIX de Linux.
                row.btn_native.setEnabled(False)
                row.btn_native.setToolTip("Simulação nativa (native_sim) só é possível no Linux.")
        root.addLayout(grid)

        bottom = QHBoxLayout()
        self.btn_scan = QPushButton("🔍 Procurar placas")
        self.btn_scan.clicked.connect(self._scan_all)
        bottom.addStretch()
        bottom.addWidget(self.btn_scan)
        root.addLayout(bottom)

        note = QLabel("As edições de IP/porta valem apenas nesta sessão "
                      "(não são gravadas no YAML).")
        note.setStyleSheet("color: #888; font-size: 9pt;")
        root.addWidget(note)

        linux_note = QLabel(
            "⚠ Simulação Nativa (native_sim) roda o firmware real como processo "
            "e só é possível no <b>Linux</b>" +
            ("." if native_sim_supported() else " — <b>desabilitada neste sistema</b>."))
        linux_note.setStyleSheet("color: #f0ad4e; font-size: 9pt;")
        linux_note.setWordWrap(True)
        root.addWidget(linux_note)

    # ------------------------------------------------------------------ #
    # Aplicar IP/portas em runtime
    # ------------------------------------------------------------------ #
    def _apply_row(self, board_id):
        row = self.rows[board_id]
        ip = row.ip_edit.text().strip()
        ports = {}
        for key, edit in row.port_edits.items():
            try:
                ports[key] = int(edit.text().strip())
            except ValueError:
                row.status.setText(f"porta '{key}' inválida")
                row.status.setStyleSheet("color: #ff4136;")
                return
        self.manager.update_link(board_id, ip=ip, ports=ports)
        row.status.setText("aplicado (runtime)")
        row.status.setStyleSheet("color: #2ecc40;")

    # ------------------------------------------------------------------ #
    # Descoberta
    # ------------------------------------------------------------------ #
    def _reset_board(self, board_id):
        """Envia FUNC_RESET para a placa (reboot frio do firmware).

        Confirmação explícita: a placa cai, o link fica mudo por alguns
        segundos e qualquer ensaio em curso é interrompido.
        """
        if QMessageBox.question(
            self, "Reiniciar placa",
            f"Reiniciar o firmware da {board_id}?\n\n"
            "A placa vai cair e voltar sozinha. Um ensaio em curso é "
            "interrompido, e o ESC e os relés voltam ao estado de boot "
            "(desarmado e abertos).",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No,
        ) != QMessageBox.Yes:
            return

        cfg = self.manager.registry.get(board_id)
        raw = self.manager.configs.get(board_id, {})
        enums = codecs.board_protocol_cfg(raw, board_id).get("enums", {})

        def idx(cat, name, default=0):
            lst = enums.get(cat, [])
            return lst.index(name) if name in lst else default

        cmd = codecs.encode_cmd(
            idx("func_type", "FUNC_RESET"),
            idx("instruct_type", "INSTR_WRITE", 1),
            idx("target_type", "TARGET_DEFAULT"),
            1.0, 0,
        )
        if self.manager.send_command(board_id, cmd):
            self._append_note(f"{board_id}: FUNC_RESET enviado — aguardando o boot.")
        else:
            self._append_note(f"{board_id}: falha ao enviar FUNC_RESET (link fechado?).")

    def _detect_one(self, board_id):
        self.rows[board_id].status.setText("testando...")
        self.rows[board_id].status.setStyleSheet("color: #f0ad4e;")

        def work():
            status = self.discovery.probe(board_id)
            self._discovery_done.emit({board_id: status})

        threading.Thread(target=work, daemon=True).start()

    def _scan_all(self):
        self.btn_scan.setEnabled(False)
        self.btn_scan.setText("Procurando...")
        for row in self.rows.values():
            row.status.setText("testando...")
            row.status.setStyleSheet("color: #f0ad4e;")

        def work():
            results = self.discovery.probe_all()
            self._discovery_done.emit(results)

        threading.Thread(target=work, daemon=True).start()

    def _apply_discovery(self, results):
        for board_id, status in results.items():
            row = self.rows.get(board_id)
            if row is None:
                continue
            row.last_present = status.present
            row.led.set_state("green" if status.present else "red")
            row.status.setText(status.detail or ("presente" if status.present else "ausente"))
            row.status.setStyleSheet("color: #2ecc40;" if status.present else "color: #ff4136;")
        self.btn_scan.setEnabled(True)
        self.btn_scan.setText("🔍 Procurar placas")

    # ------------------------------------------------------------------ #
    def _toggle_native(self, board_id):
        """Inicia/para a simulação nativa (firmware real via native_sim).

        Só Linux (o botão já vem desabilitado fora do Linux)."""
        row = self.rows[board_id]
        if board_id in self.manager.native:
            self.manager.stop_native_sim(board_id)
            row.btn_native.setText("▶ Sim. Nativa")
            row.status.setText("native_sim parado")
            row.status.setStyleSheet("color: #888;")
            return
        row.status.setText("subindo native_sim...")
        row.status.setStyleSheet("color: #f0ad4e;")
        ok = self.manager.start_native_sim_auto(board_id)
        if ok:
            row.btn_native.setText("■ Parar Nativa")
            row.status.setText("native_sim rodando (127.0.0.1)")
            row.status.setStyleSheet("color: #2ecc40;")
        else:
            exe = self.manager.native_exe_path(board_id)
            row.status.setText("falhou — compile 'make native_sim'")
            row.status.setStyleSheet("color: #ff4136;")
            self._append_note(f"native_sim {board_id}: binário ausente/erro ({exe})")

    def _append_note(self, msg):
        print(f"[BoardManager] {msg}")

    def _refresh_live(self):
        """Promove o LED a verde quando o link tem RX recente (ao vivo)."""
        for board_id, row in self.rows.items():
            worker = self.manager.workers.get(board_id)
            if worker is not None and worker.is_alive_link():
                row.led.set_state("green")
                if row.last_present is not True:
                    row.status.setText("RX ativo")
                    row.status.setStyleSheet("color: #2ecc40;")
                    row.last_present = True
            elif row.last_present is None:
                row.led.set_state("gray")
