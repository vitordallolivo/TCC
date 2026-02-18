import sys
from PySide6 import QtCore, QtWidgets, QtGui
from PySide6.QtCore import Qt, QTimer
import pyqtgraph as pg
import numpy as np

class MotorControlDialog(QtWidgets.QDialog):
    """Dialog for motor control"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.parent = parent
        self.setWindowTitle("Controle do Motor")
        self.setGeometry(150, 150, 500, 400)
        self.setModal(True)
        
        self.init_ui()
    
    def init_ui(self):
        """Initialize the UI for motor control"""
        layout = QtWidgets.QVBoxLayout()
        
        # Title
        title_label = QtWidgets.QLabel("🚀 Controle do Motor")
        title_label.setStyleSheet("font-size: 16px; font-weight: bold;")
        title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title_label)
        
        layout.addSpacing(20)
        
        # ========== Motor Power Control ==========
        power_group = QtWidgets.QGroupBox("Controle de Potência")
        power_layout = QtWidgets.QVBoxLayout()
        
        # Value display - use global motor value
        self.value_display = QtWidgets.QLabel(f"{self.parent.motor_value}%")
        self.value_display.setStyleSheet("font-size: 48px; font-weight: bold; color: #2196F3;")
        self.value_display.setAlignment(Qt.AlignmentFlag.AlignCenter)
        power_layout.addWidget(self.value_display)
        
        # Slider for motor control - use global motor value
        self.motor_slider = QtWidgets.QSlider(Qt.Orientation.Horizontal)
        self.motor_slider.setRange(0, 100)
        self.motor_slider.setValue(self.parent.motor_value)
        self.motor_slider.setTickPosition(QtWidgets.QSlider.TickPosition.TicksBelow)
        self.motor_slider.setTickInterval(10)
        self.motor_slider.valueChanged.connect(self.slider_changed)
        power_layout.addWidget(self.motor_slider)
        
        # Value label
        value_label = QtWidgets.QLabel("Potência do Motor (0-100%)")
        value_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        power_layout.addWidget(value_label)
        
        power_group.setLayout(power_layout)
        layout.addWidget(power_group)
        
        layout.addSpacing(20)
        
        # ========== Preset Buttons ==========
        preset_group = QtWidgets.QGroupBox("Predefinições Rápidas")
        preset_layout = QtWidgets.QGridLayout()
        
        # Create preset buttons
        presets = [0, 25, 50, 75, 100]
        colors = ["#f44336", "#FF9800", "#4CAF50", "#2196F3", "#9C27B0"]
        
        for i, value in enumerate(presets):
            btn = QtWidgets.QPushButton(f"{value}%")
            btn.setMinimumHeight(40)
            btn.setStyleSheet(f"background-color: {colors[i]}; color: white; font-weight: bold;")
            btn.clicked.connect(lambda checked, v=value: self.set_motor_value(v))
            preset_layout.addWidget(btn, 0, i)
        
        preset_group.setLayout(preset_layout)
        layout.addWidget(preset_group)
        
        layout.addSpacing(20)
        
        # ========== Manual Input ==========
        manual_group = QtWidgets.QGroupBox("Entrada Manual")
        manual_layout = QtWidgets.QHBoxLayout()
        
        manual_layout.addWidget(QtWidgets.QLabel("Valor (%):"))
        
        self.value_spinbox = QtWidgets.QSpinBox()
        self.value_spinbox.setRange(0, 100)
        self.value_spinbox.setValue(self.parent.motor_value)
        self.value_spinbox.valueChanged.connect(self.spinbox_changed)
        manual_layout.addWidget(self.value_spinbox)
        
        apply_btn = QtWidgets.QPushButton("Aplicar")
        apply_btn.clicked.connect(self.apply_manual_value)
        manual_layout.addWidget(apply_btn)
        
        manual_group.setLayout(manual_layout)
        layout.addWidget(manual_group)
        
        layout.addSpacing(20)
        
        # ========== Control Buttons ==========
        control_layout = QtWidgets.QHBoxLayout()
        
        # Start/Stop button - use global motor state
        self.start_stop_btn = QtWidgets.QPushButton("▶️ Iniciar Motor")
        self.start_stop_btn.setCheckable(True)
        self.start_stop_btn.setChecked(self.parent.motor_running)
        self.start_stop_btn.setStyleSheet("""
            QPushButton:checked {
                background-color: #f44336;
                color: white;
            }
            QPushButton:!checked {
                background-color: #4CAF50;
                color: white;
            }
        """)
        self.start_stop_btn.clicked.connect(self.toggle_motor)
        control_layout.addWidget(self.start_stop_btn)
        
        # Emergency Stop button
        self.emergency_stop_btn = QtWidgets.QPushButton("🛑 Parada de Emergência")
        self.emergency_stop_btn.clicked.connect(self.emergency_stop)
        self.emergency_stop_btn.setStyleSheet("background-color: #f44336; color: white; font-weight: bold;")
        control_layout.addWidget(self.emergency_stop_btn)
        
        layout.addLayout(control_layout)
        
        layout.addSpacing(15)
        
        # ========== Status and Close ==========
        bottom_layout = QtWidgets.QHBoxLayout()
        
        # Status label - use global motor state
        status_text = "Motor: LIGADO" if self.parent.motor_running else "Motor: DESLIGADO"
        color = "green" if self.parent.motor_running else "#666"
        self.status_label = QtWidgets.QLabel(status_text)
        self.status_label.setStyleSheet(f"color: {color}; font-weight: bold;")
        bottom_layout.addWidget(self.status_label)
        
        # Close button
        close_btn = QtWidgets.QPushButton("Fechar")
        close_btn.clicked.connect(self.close)
        bottom_layout.addWidget(close_btn)
        
        layout.addLayout(bottom_layout)
        
        # Set the layout
        self.setLayout(layout)
        
        # Update button text based on initial state
        self.update_button_text()
    
    def update_button_text(self):
        """Update button text based on motor state"""
        if self.parent.motor_running:
            self.start_stop_btn.setText("⏸️ Parar Motor")
        else:
            self.start_stop_btn.setText("▶️ Iniciar Motor")
    
    def update_display_color(self):
        """Update the display color based on motor value"""
        value = self.parent.motor_value
        
        if value == 0:
            color = "#666666"
        elif value <= 25:
            color = "#4CAF50"  # Green
        elif value <= 50:
            color = "#2196F3"  # Blue
        elif value <= 75:
            color = "#FF9800"  # Orange
        else:
            color = "#f44336"  # Red
        
        self.value_display.setStyleSheet(f"font-size: 48px; font-weight: bold; color: {color};")
    
    def slider_changed(self, value):
        """Handle slider value change"""
        # Update global value
        self.parent.motor_value = value
        
        # Update UI
        self.value_display.setText(f"{value}%")
        self.value_spinbox.setValue(value)
        self.update_display_color()
        
        # If motor is running, add command to queue
        if self.parent.motor_running and self.parent.serial_com.is_connected:
            self.parent.command_queue.append(f"MOTOR_SET {value}\n")
    
    def spinbox_changed(self, value):
        """Handle spinbox value change"""
        # Update global value
        self.parent.motor_value = value
        
        # Update UI
        self.value_display.setText(f"{value}%")
        self.motor_slider.setValue(value)
        self.update_display_color()
    
    def apply_manual_value(self):
        """Apply manual value from spinbox"""
        value = self.value_spinbox.value()
        
        # Update global value
        self.parent.motor_value = value
        
        # Update UI
        self.value_display.setText(f"{value}%")
        self.motor_slider.setValue(value)
        self.update_display_color()
        
        # If motor is running, add command to queue
        if self.parent.motor_running and self.parent.serial_com.is_connected:
            self.parent.command_queue.append(f"MOTOR_SET {value}\n")
    
    def set_motor_value(self, value):
        """Set motor to a specific value"""
        # Update global value
        self.parent.motor_value = value
        
        # Update UI
        self.value_display.setText(f"{value}%")
        self.motor_slider.setValue(value)
        self.value_spinbox.setValue(value)
        self.update_display_color()
        
        # If motor is running, add command to queue
        if self.parent.motor_running and self.parent.serial_com.is_connected:
            self.parent.command_queue.append(f"MOTOR_SET {value}\n")
    
    def toggle_motor(self, checked):
        """Toggle motor on/off"""
        # Update global state
        self.parent.motor_running = checked
        
        # Update UI
        if self.parent.motor_running:
            self.start_stop_btn.setText("⏸️ Parar Motor")
            self.status_label.setText("Motor: LIGADO")
            self.status_label.setStyleSheet("color: green; font-weight: bold;")
            
            # Add start command to queue
            if self.parent.serial_com.is_connected:
                self.parent.command_queue.append(f"MOTOR_START {self.parent.motor_value}\n")
        else:
            self.start_stop_btn.setText("▶️ Iniciar Motor")
            self.status_label.setText("Motor: DESLIGADO")
            self.status_label.setStyleSheet("color: #666; font-weight: bold;")
            
            # Add stop command to queue
            if self.parent.serial_com.is_connected:
                self.parent.command_queue.append("MOTOR_STOP\n")
    
    def emergency_stop(self):
        """Emergency stop - immediately stop motor and set to 0%"""
        # Update global values
        self.parent.motor_running = False
        self.parent.motor_value = 0
        
        # Update UI
        self.start_stop_btn.setChecked(False)
        self.start_stop_btn.setText("▶️ Iniciar Motor")
        
        self.value_display.setText("0%")
        self.motor_slider.setValue(0)
        self.value_spinbox.setValue(0)
        self.update_display_color()
        
        self.status_label.setText("Motor: PARADA DE EMERGÊNCIA")
        self.status_label.setStyleSheet("color: red; font-weight: bold;")
        
        # Add emergency stop command to queue
        if self.parent.serial_com.is_connected:
            self.parent.command_queue.append("EMERGENCY_STOP\n")