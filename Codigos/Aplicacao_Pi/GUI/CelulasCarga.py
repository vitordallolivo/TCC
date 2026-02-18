import sys
from PySide6 import QtCore, QtWidgets, QtGui
from PySide6.QtCore import Qt, QTimer
import pyqtgraph as pg
import numpy as np

class LoadCellConfigDialog(QtWidgets.QDialog):
    """Dialog for configuring load cells"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.parent = parent
        self.setWindowTitle("Configuração de Células de Carga")
        self.setGeometry(150, 150, 500, 400)
        self.setModal(True)
        
        self.init_ui()
    
    def init_ui(self):
        """Initialize the UI for load cell configuration"""
        layout = QtWidgets.QVBoxLayout()
        
        # Title
        title_label = QtWidgets.QLabel("⚙️ Configuração de Células de Carga")
        title_label.setStyleSheet("font-size: 16px; font-weight: bold;")
        title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title_label)
        
        layout.addSpacing(20)
        
        # ========== Load Cell 1 Configuration ==========
        cell1_group = QtWidgets.QGroupBox("Célula de Carga 1")
        cell1_layout = QtWidgets.QGridLayout()
        
        # Gain setting for cell 1
        cell1_layout.addWidget(QtWidgets.QLabel("Ganho (int64):"), 0, 0)
        self.gain1_spinbox = QtWidgets.QSpinBox()
        self.gain1_spinbox.setRange(1, 2**31 - 1)  # int32 max
        self.gain1_spinbox.setValue(self.parent.loadcell_gains['cell1'])
        self.gain1_spinbox.setMaximumWidth(150)
        cell1_layout.addWidget(self.gain1_spinbox, 0, 1)
        
        # Current gain display for cell 1
        cell1_layout.addWidget(QtWidgets.QLabel("Ganho Atual:"), 1, 0)
        self.current_gain1_label = QtWidgets.QLabel(str(self.parent.loadcell_gains['cell1']))
        cell1_layout.addWidget(self.current_gain1_label, 1, 1)
        
        # Tare status and button for cell 1
        self.tare1_status_label = QtWidgets.QLabel("Não tarada" if not self.parent.loadcell_tared['cell1'] else "Tarada ✓")
        self.tare1_status_label.setStyleSheet("color: red;" if not self.parent.loadcell_tared['cell1'] else "color: green;")
        cell1_layout.addWidget(self.tare1_status_label, 2, 0)
        
        self.tare1_btn = QtWidgets.QPushButton("Tarar Célula 1")
        self.tare1_btn.clicked.connect(self.tare_cell1)
        cell1_layout.addWidget(self.tare1_btn, 2, 1)
        
        cell1_group.setLayout(cell1_layout)
        layout.addWidget(cell1_group)
        
        layout.addSpacing(15)
        
        # ========== Load Cell 2 Configuration ==========
        cell2_group = QtWidgets.QGroupBox("Célula de Carga 2")
        cell2_layout = QtWidgets.QGridLayout()
        
        # Gain setting for cell 2
        cell2_layout.addWidget(QtWidgets.QLabel("Ganho (int64):"), 0, 0)
        self.gain2_spinbox = QtWidgets.QSpinBox()
        self.gain2_spinbox.setRange(1, 2**31 - 1)  # int32 max
        self.gain2_spinbox.setValue(self.parent.loadcell_gains['cell2'])
        self.gain2_spinbox.setMaximumWidth(150)
        cell2_layout.addWidget(self.gain2_spinbox, 0, 1)
        
        # Current gain display for cell 2
        cell2_layout.addWidget(QtWidgets.QLabel("Ganho Atual:"), 1, 0)
        self.current_gain2_label = QtWidgets.QLabel(str(self.parent.loadcell_gains['cell2']))
        cell2_layout.addWidget(self.current_gain2_label, 1, 1)
        
        # Tare status and button for cell 2
        self.tare2_status_label = QtWidgets.QLabel("Não tarada" if not self.parent.loadcell_tared['cell2'] else "Tarada ✓")
        self.tare2_status_label.setStyleSheet("color: red;" if not self.parent.loadcell_tared['cell2'] else "color: green;")
        cell2_layout.addWidget(self.tare2_status_label, 2, 0)
        
        self.tare2_btn = QtWidgets.QPushButton("Tarar Célula 2")
        self.tare2_btn.clicked.connect(self.tare_cell2)
        cell2_layout.addWidget(self.tare2_btn, 2, 1)
        
        cell2_group.setLayout(cell2_layout)
        layout.addWidget(cell2_group)
        
        layout.addSpacing(20)
        
        # ========== Action Buttons ==========
        button_layout = QtWidgets.QHBoxLayout()
        
        # Apply Gains Button
        self.apply_btn = QtWidgets.QPushButton("✅ Aplicar Ganhos")
        self.apply_btn.clicked.connect(self.apply_gains)
        self.apply_btn.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
        button_layout.addWidget(self.apply_btn)
        
        # Close Button
        close_btn = QtWidgets.QPushButton("Fechar")
        close_btn.clicked.connect(self.close)
        button_layout.addWidget(close_btn)
        
        layout.addLayout(button_layout)
        
        # Status Label
        self.status_label = QtWidgets.QLabel("")
        self.status_label.setStyleSheet("color: #666; font-style: italic;")
        layout.addWidget(self.status_label)
        
        # Set the layout
        self.setLayout(layout)
    
    def tare_cell1(self):
        """Tare load cell 1"""
        if self.parent and self.parent.serial_com.is_connected:
            # Update global state
            self.parent.loadcell_tared['cell1'] = True
            
            # Update UI
            self.tare1_status_label.setText("Tarada ✓")
            self.tare1_status_label.setStyleSheet("color: green;")
            
            # Add command to queue
            self.parent.command_queue.append("TARE_CELL1\n")
            
            self.status_label.setText("Célula 1 tarada - Comando na fila")
            self.status_label.setStyleSheet("color: green;")
        else:
            self.status_label.setText("Erro: Não conectado ao dispositivo")
            self.status_label.setStyleSheet("color: red;")
    
    def tare_cell2(self):
        """Tare load cell 2"""
        if self.parent and self.parent.serial_com.is_connected:
            # Update global state
            self.parent.loadcell_tared['cell2'] = True
            
            # Update UI
            self.tare2_status_label.setText("Tarada ✓")
            self.tare2_status_label.setStyleSheet("color: green;")
            
            # Add command to queue
            self.parent.command_queue.append("TARE_CELL2\n")
            
            self.status_label.setText("Célula 2 tarada - Comando na fila")
            self.status_label.setStyleSheet("color: green;")
        else:
            self.status_label.setText("Erro: Não conectado ao dispositivo")
            self.status_label.setStyleSheet("color: red;")
    
    def apply_gains(self):
        """Apply the configured gains to both load cells"""
        gain1 = self.gain1_spinbox.value()
        gain2 = self.gain2_spinbox.value()
        
        if self.parent and self.parent.serial_com.is_connected:
            # Update global values
            self.parent.loadcell_gains['cell1'] = gain1
            self.parent.loadcell_gains['cell2'] = gain2
            
            # Update UI
            self.current_gain1_label.setText(str(gain1))
            self.current_gain2_label.setText(str(gain2))
            
            # Add commands to queue
            self.parent.command_queue.append(f"SET_GAIN 1 {gain1}\n")
            self.parent.command_queue.append(f"SET_GAIN 2 {gain2}\n")
            
            self.status_label.setText(f"Ganhos na fila: Célula1={gain1}, Célula2={gain2}")
            self.status_label.setStyleSheet("color: green; font-weight: bold;")
            
        else:
            self.status_label.setText("Erro: Não conectado ao dispositivo")
            self.status_label.setStyleSheet("color: red;")

