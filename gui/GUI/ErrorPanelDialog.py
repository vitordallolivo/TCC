"""ErrorPanelDialog — painel completo de erros/alarmes com "lâmpadas".

Mostra TODAS as condições possíveis da placa, lidas da
seção `error_bits` do protocolo.yaml (via Communication_Data/errors.py), uma
por linha com uma lâmpada: acesa (vermelha) quando a condição está ativa na
telemetria, apagada (cinza) quando não. Agrupado por área, em pt-BR.

O código vem de
`AlbatrozGUI.current_error_codes()`, atualizado a cada ~300 ms.
"""
from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QGroupBox, QScrollArea, QWidget,
    QPushButton,
)

from Communication_Data import errors


_LAMP_ON = "#ff4136"     # vermelho — condição ATIVA
_LAMP_OFF = "#3a3a3a"    # apagada
_LAMP_EDGE = "#222"


class _Lamp(QLabel):
    """Lâmpada circular simples (acesa/apagada)."""

    def __init__(self, diameter=18):
        super().__init__()
        self._d = diameter
        self.setFixedSize(diameter, diameter)
        self.set_on(False)

    def set_on(self, on):
        r = self._d // 2
        color = _LAMP_ON if on else _LAMP_OFF
        glow = f"border: 2px solid {_LAMP_ON};" if on else f"border: 1px solid {_LAMP_EDGE};"
        self.setStyleSheet(
            f"background-color: {color}; border-radius: {r}px; {glow}"
        )


class _ErrorRow:
    def __init__(self, board, mask, message):
        self.board = board
        self.mask = mask
        self.lamp = _Lamp()
        self.label = QLabel(message)
        self.label.setWordWrap(True)

    def update(self, codes):
        code = int(codes.get(self.board, 0) or 0)
        on = bool(code & self.mask)
        self.lamp.set_on(on)
        self.label.setStyleSheet(
            "color: #ff6b61; font-weight: bold;" if on else "color: #9aa0a6;"
        )


class ErrorPanelDialog(QDialog):
    def __init__(self, gui, parent=None):
        super().__init__(parent)
        self.gui = gui
        self.setWindowTitle("Painel de Erros e Alarmes")
        self.setWindowFlags(Qt.Window)
        self.resize(520, 560)
        self._rows = []
        self._build_ui()

        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh)
        self._timer.start(300)
        self._refresh()

    # ------------------------------------------------------------------ #
    def _build_ui(self):
        root = QVBoxLayout(self)

        self.lbl_header = QLabel("—")
        self.lbl_header.setStyleSheet("font-size: 13pt; font-weight: bold; padding: 4px;")
        self.lbl_header.setWordWrap(True)
        root.addWidget(self.lbl_header)

        # Área rolável com os grupos.
        inner = QWidget()
        vbox = QVBoxLayout(inner)

        # Uma seção para a placa, subdividida por área. A tabela vem da
        # seção error_bits do yaml (errors.py).
        configs = getattr(self.gui, "configs", {}) or {}
        for board, cfg in configs.items():
            table = errors.ensure_registered(board, cfg or {})
            if not table:
                continue  # placa sem error_bits

            # Agrupa por área, preservando a ordem dos bits.
            por_grupo = {}
            for mask, (msg, grp) in table.items():
                por_grupo.setdefault(grp, []).append((mask, msg))

            board_box = QGroupBox("● Placa de medição")
            board_box.setStyleSheet("QGroupBox { font-weight: bold; }")
            bl = QVBoxLayout(board_box)
            for grp, items in por_grupo.items():
                box = QGroupBox(grp)
                gl = QVBoxLayout(box)
                for mask, msg in items:
                    row = _ErrorRow(board, mask, msg)
                    self._rows.append(row)
                    h = QHBoxLayout()
                    h.addWidget(row.lamp, 0, Qt.AlignTop)
                    h.addWidget(row.label, 1)
                    gl.addLayout(h)
                bl.addWidget(box)
            vbox.addWidget(board_box)
        vbox.addStretch()

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(inner)
        root.addWidget(scroll, 1)

        bottom = QHBoxLayout()
        bottom.addStretch()
        btn = QPushButton("Fechar")
        btn.clicked.connect(self.close)
        bottom.addWidget(btn)
        root.addLayout(bottom)

    # ------------------------------------------------------------------ #
    def _refresh(self):
        codes = {}
        getter = getattr(self.gui, "current_error_codes", None)
        if callable(getter):
            codes = getter() or {}
        for row in self._rows:
            row.update(codes)

        n = sum(bin(int(c) & 0xFFFFFFFF).count("1") for c in codes.values())
        code = 0
        for c in codes.values():
            code |= int(c) & 0xFFFFFFFF
        if n == 0:
            self.lbl_header.setText("✓ Nenhum erro ativo — tudo OK")
            self.lbl_header.setStyleSheet(
                "font-size: 13pt; font-weight: bold; color: #2ecc40; padding: 4px;")
        else:
            self.lbl_header.setText(f"⚠ {n} problema(s) ativo(s)  (código 0x{code:08X})")
            self.lbl_header.setStyleSheet(
                "font-size: 13pt; font-weight: bold; color: #ff4136; padding: 4px;")

    def closeEvent(self, event):
        self._timer.stop()
        super().closeEvent(event)
