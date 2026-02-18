#=============== Libraries ==========================================
import sys
from PySide6 import QtCore, QtWidgets, QtGui
from PySide6.QtCore import Qt, QTimer
import pyqtgraph as pg
import numpy as np

# ============== Communication ==================================
from Communication_Data.SerialCommunication import SerialCommunication
# ============== GUI External ===========================
from GUI.ComandoMotor import MotorControlDialog
from GUI.CelulasCarga import LoadCellConfigDialog

#==================================================================
#                        Main Class
#==================================================================

class AlbatrozGUI(QtWidgets.QWidget):

    global serial_com

    def __init__(self):
        super().__init__()

        self.serial_com = SerialCommunication()
        
        # Initialize empty data buffers for 6 graphs
        self.graph_data = [np.array([]) for _ in range(6)]
        self.graph_timestamps = [np.array([]) for _ in range(6)]
        self.max_data_points = 200  # Default buffer size

        # ========== GLOBAL VALUES FOR DEVICE CONFIGURATION ==========
        # Load cell configuration (int64 values)
        self.loadcell_gains = {
            'cell1': 1000,  # Default gain for cell 1
            'cell2': 1000   # Default gain for cell 2
        }
        
        # Load cell tare states
        self.loadcell_tared = {
            'cell1': False,
            'cell2': False
        }
        
        # Motor control values
        self.motor_value = 0          # 0-100%
        self.motor_running = False    # Motor state
        
        # Serial commands queue
        self.command_queue = []
        
        self.init_ui()

    def init_ui(self):
        """Initialize the UI elements"""          
        # Set window properties - larger window for 6 graphs
        self.setWindowTitle("Albatroz Serial Monitor")
        self.setGeometry(100, 100, 1200, 900)
        
        # Create main layout
        main_layout = QtWidgets.QVBoxLayout()
        
        # ========== Port Control ==========
        port_group = QtWidgets.QGroupBox("Port Control")
        port_layout = QtWidgets.QGridLayout()
        
        # Port selection
        port_layout.addWidget(QtWidgets.QLabel("Port:"), 0, 0)
        self.port_combo = QtWidgets.QComboBox()
        self.port_combo.setEditable(True)
        port_layout.addWidget(self.port_combo, 0, 1)
        
        # Refresh ports button
        self.refresh_btn = QtWidgets.QPushButton("Refresh Ports")
        self.refresh_btn.clicked.connect(self.refresh_ports)
        port_layout.addWidget(self.refresh_btn, 0, 2)
        
        # Baud rate selection
        port_layout.addWidget(QtWidgets.QLabel("Baud Rate:"), 1, 0)
        self.baud_combo = QtWidgets.QComboBox()
        self.baud_combo.addItems([str(b) for b in self.serial_com.common_baudrates])
        self.baud_combo.setCurrentText("9600")
        port_layout.addWidget(self.baud_combo, 1, 1)
        
        # Connect/Disconnect button
        self.connect_btn = QtWidgets.QPushButton("Connect")
        self.connect_btn.clicked.connect(self.toggle_connection)
        port_layout.addWidget(self.connect_btn, 1, 2)
        
        # Connection status label
        self.status_label = QtWidgets.QLabel("Status: Disconnected")
        self.status_label.setStyleSheet("color: red; font-weight: bold;")
        port_layout.addWidget(self.status_label, 2, 0, 1, 3)
        
        port_group.setLayout(port_layout)
        main_layout.addWidget(port_group)
        

        # ========== Device Control Buttons ==========
        device_group = QtWidgets.QGroupBox("Device Control")
        device_layout = QtWidgets.QHBoxLayout()
        
        # Load Cell Configuration Button
        self.loadcell_config_btn = QtWidgets.QPushButton("⚙️ Configurar Célula de Carga")
        self.loadcell_config_btn.clicked.connect(self.open_loadcell_config)
        self.loadcell_config_btn.setMinimumHeight(40)
        device_layout.addWidget(self.loadcell_config_btn)
        
        # Motor Control Button
        self.motor_control_btn = QtWidgets.QPushButton("🚀 Controlar Motor")
        self.motor_control_btn.clicked.connect(self.open_motor_control)
        self.motor_control_btn.setMinimumHeight(40)
        device_layout.addWidget(self.motor_control_btn)
        
        device_group.setLayout(device_layout)
        main_layout.addWidget(device_group)

        # ========== 6 Graphs Grid ==========
        graphs_group = QtWidgets.QGroupBox("Real-Time Data Plots")
        graphs_layout = QtWidgets.QGridLayout()
        
        # Colors for the 6 graphs
        graph_colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', "#006cf8", "#ff2a00"]
        graph_titles = [
            "Channel 1", "Channel 2", "Channel 3",
            "Channel 4", "Channel 5", "Channel 6"
        ]
        graph_y_labels = [
            "Voltage (V)", "Current (A)", "Temperature (°C)",
            "Pressure (Pa)", "Frequency (Hz)", "RPM"
        ]
        
        # Create and configure 6 plot widgets (2 rows x 3 columns)
        self.plot_widgets = []
        self.plot_curves = []
        
        for i in range(6):
            row = i // 3  # 0 or 1 for 2 rows
            col = i % 3   # 0, 1, or 2 for 3 columns
            
            # Create plot widget
            plot_widget = pg.PlotWidget()
            plot_widget.setBackground('w')
            plot_widget.showGrid(x=True, y=True, alpha=0.3)
            plot_widget.setLabel('left', graph_y_labels[i])
            plot_widget.setLabel('bottom', 'Time (s)')
            plot_widget.setTitle(graph_titles[i])
            
            # Set initial Y-axis range
            plot_widget.setYRange(-1, 1)
            
            # Create plot curve with color
            curve = plot_widget.plot(pen=pg.mkPen(color=graph_colors[i], width=2))
            
            # Store references
            self.plot_widgets.append(plot_widget)
            self.plot_curves.append(curve)
            
            # Add to layout
            graphs_layout.addWidget(plot_widget, row, col)
        
        graphs_group.setLayout(graphs_layout)
        main_layout.addWidget(graphs_group)
        
        # ========== Graph Control Section ==========
        control_group = QtWidgets.QGroupBox("Graph Controls")
        control_layout = QtWidgets.QHBoxLayout()
        
        # Clear graphs button
        self.clear_graphs_btn = QtWidgets.QPushButton("🗑️ Clear All Graphs")
        self.clear_graphs_btn.clicked.connect(self.clear_all_graphs)
        control_layout.addWidget(self.clear_graphs_btn)
        
        # Pause/Resume button
        self.pause_btn = QtWidgets.QPushButton("⏸️ Pause")
        self.pause_btn.setCheckable(True)
        self.pause_btn.clicked.connect(self.toggle_pause)
        control_layout.addWidget(self.pause_btn)
        
        # Y-axis range control
        control_layout.addWidget(QtWidgets.QLabel("Y-Axis:"))
        self.y_range_combo = QtWidgets.QComboBox()
        self.y_range_combo.addItems(["Auto", "-1 to 1", "-5 to 5", "-10 to 10", "0 to 10", "0 to 100"])
        self.y_range_combo.currentTextChanged.connect(self.change_y_range)
        control_layout.addWidget(self.y_range_combo)
        
        # Buffer size control
        control_layout.addWidget(QtWidgets.QLabel("Buffer:"))
        self.buffer_combo = QtWidgets.QComboBox()
        self.buffer_combo.addItems(["100", "200", "500", "1000"])
        self.buffer_combo.setCurrentText("200")
        self.buffer_combo.currentTextChanged.connect(self.change_buffer_size)
        control_layout.addWidget(self.buffer_combo)
        
        control_group.setLayout(control_layout)
        main_layout.addWidget(control_group)
        
        # ========== Display ==========
        data_group = QtWidgets.QGroupBox("Serial Data")
        data_layout = QtWidgets.QVBoxLayout()
        
        # Text display for received data
        self.text_display = QtWidgets.QTextEdit()
        self.text_display.setReadOnly(True)
        self.text_display.setFont(QtGui.QFont("Courier", 9))
        self.text_display.setMaximumHeight(20)
        data_layout.addWidget(self.text_display)
        
        # Data info label
        self.data_info_label = QtWidgets.QLabel("No data received")
        data_layout.addWidget(self.data_info_label)
        
        # Button layout
        button_layout = QtWidgets.QHBoxLayout()
        
        # Clear button
        self.clear_btn = QtWidgets.QPushButton("Clear Display")
        self.clear_btn.clicked.connect(self.text_display.clear)
        button_layout.addWidget(self.clear_btn)
        
        # Save data button
        self.save_btn = QtWidgets.QPushButton("💾 Save Data")
        self.save_btn.clicked.connect(self.save_data)
        button_layout.addWidget(self.save_btn)
        
        data_layout.addLayout(button_layout)
        data_group.setLayout(data_layout)
        main_layout.addWidget(data_group)
        
        # Set main layout
        self.setLayout(main_layout)
        
        # Initial refresh of ports
        self.refresh_ports()
        
        # ========== Timer's ==========
        # Create the graphic update timer
        self.GraphicTimer = QtCore.QTimer(self)
        self.GraphicTimer.timeout.connect(self.update_all_plots)
        self.GraphicTimer.start(1)  # Update plots every 1ms 
        
        # Initialize serial timer (but don't start it yet)
        self.SerialTimer = QtCore.QTimer(self)
        self.SerialTimer.timeout.connect(self.read_serial_data)
        # Timer will be started when connection is established
        
        # Initialize pause state
        self.paused = False


    def refresh_ports(self):
        """Refresh the list of available serial ports"""
        # Clear current items
        self.port_combo.clear()
        
        # Get available ports
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        
        # Add ports to combo box
        for port in ports:
            self.port_combo.addItem(f"{port.device} - {port.description}")
        
        if not ports:
            self.port_combo.addItem("No ports available")
        
    def toggle_connection(self):
        """Toggle connection state"""
        if self.serial_com.is_connected:
            # ================= Disconnect =================
            if self.serial_com.disconnect():
                self.status_label.setText("Status: Disconnected")
                self.status_label.setStyleSheet("color: red; font-weight: bold;")
                self.connect_btn.setText("Connect")
                self.text_display.append("=== Disconnected ===")
                
                # Stop the serial timer when disconnecting
                if hasattr(self, 'SerialTimer') and self.SerialTimer.isActive():
                    self.SerialTimer.stop()
        else:
            # ================= Connect  =================
            # Extract port name from combo box text
            port_text = self.port_combo.currentText()
            port = port_text.split(" - ")[0] if " - " in port_text else port_text
            
            if port == "No ports available":
                QtWidgets.QMessageBox.warning(self, "Error", "No ports available!")
                return
            
            baudrate = int(self.baud_combo.currentText())
            
            if self.serial_com.connect(port, baudrate):
                self.status_label.setText(f"Status: Connected to {port}")
                self.status_label.setStyleSheet("color: green; font-weight: bold;")
                self.connect_btn.setText("Disconnect")
                self.text_display.append(f"=== Connected to {port} at {baudrate} baud ===")
                # Clear graphs when connecting
                self.clear_all_graphs()

                # ================= Create a Serial Interrupt ======================
                # Create the timer object if it doesn't exist
                if not hasattr(self, 'SerialTimer'):
                    self.SerialTimer = QtCore.QTimer(self)
                    
                # Connect interrupt function to read serial data
                self.SerialTimer.timeout.connect(self.read_serial_data)
                
                # Set timer interval (in milliseconds)
                self.SerialTimer.start(10)  # 10ms = 100Hz update rate
                
                # Enable DTR/RTS if needed (optional)
                if self.serial_com.serial_connection:
                    self.serial_com.serial_connection.dtr = True
                    self.serial_com.serial_connection.rts = True
            else:
                QtWidgets.QMessageBox.critical(self, "Error", f"Failed to connect to {port}!")

    def add_data_point(self, channel, value, timestamp=None):
        """Add a data point to a specific channel graph"""
        if channel < 0 or channel >= 6:
            return False
            
        if timestamp is None:
            timestamp = len(self.graph_timestamps[channel])
        
        # Add to data arrays
        self.graph_data[channel] = np.append(self.graph_data[channel], value)
        self.graph_timestamps[channel] = np.append(self.graph_timestamps[channel], timestamp)
        
        # Trim arrays if they exceed max size
        if len(self.graph_data[channel]) > self.max_data_points:
            self.graph_data[channel] = self.graph_data[channel][-self.max_data_points:]
            self.graph_timestamps[channel] = self.graph_timestamps[channel][-self.max_data_points:]
        
        # Update plot if not paused
        if not self.paused:
            self.update_plot(channel)
        
        return True
    
    def add_data_points(self, values, timestamp=None):
        """Add data points to all 6 channels at once"""
        if len(values) != 6:
            print(f"Error: Expected 6 values, got {len(values)}")
            return False
            
        for i in range(6):
            self.add_data_point(i, values[i], timestamp)
        
        # Update info label
        self.data_info_label.setText(f"Last update: {len(self.graph_data[0])} points | Channels: {', '.join([f'CH{i+1}: {values[i]:.3f}' for i in range(6)])}")
        
        return True
    
    def update_plot(self, channel):
        """Update a specific channel plot"""
        if channel < 0 or channel >= 6:
            return
            
        if len(self.graph_data[channel]) > 0 and len(self.graph_timestamps[channel]) > 0:
            self.plot_curves[channel].setData(self.graph_timestamps[channel], self.graph_data[channel])
    
    def update_all_plots(self):
        """Update all 6 plots"""
        for i in range(6):
            self.update_plot(i)
    
    def clear_all_graphs(self):
        """Clear all graph data"""
        for i in range(6):
            self.graph_data[i] = np.array([])
            self.graph_timestamps[i] = np.array([])
            self.plot_curves[i].setData([], [])
        
        self.text_display.append("All graphs cleared")
        self.data_info_label.setText("No data received")
    
    def toggle_pause(self):
        """Toggle pause state for graphs"""
        self.paused = not self.paused
        if self.paused:
            self.pause_btn.setText("▶️ Resume")
            self.pause_btn.setStyleSheet("background-color: #FF9999;")
            self.text_display.append("Graphs paused")
        else:
            self.pause_btn.setText("⏸️ Pause")
            self.pause_btn.setStyleSheet("")
            self.update_all_plots()
            self.text_display.append("Graphs resumed")
    
    def change_y_range(self, range_text):
        """Change Y-axis range for all graphs"""
        ranges = {
            "Auto": None,
            "-1 to 1": (-1, 1),
            "-5 to 5": (-5, 5),
            "-10 to 10": (-10, 10),
            "0 to 10": (0, 10),
            "0 to 100": (0, 100)
        }
        
        y_range = ranges.get(range_text, None)
        
        for plot_widget in self.plot_widgets:
            if y_range is None:
                plot_widget.enableAutoRange(axis='y')
            else:
                plot_widget.disableAutoRange(axis='y')
                plot_widget.setYRange(y_range[0], y_range[1])
    
    def change_buffer_size(self, size_text):
        """Change buffer size for all graphs"""
        self.max_data_points = int(size_text)
        
        # Trim existing data if necessary
        for i in range(6):
            if len(self.graph_data[i]) > self.max_data_points:
                self.graph_data[i] = self.graph_data[i][-self.max_data_points:]
                self.graph_timestamps[i] = self.graph_timestamps[i][-self.max_data_points:]
                self.update_plot(i)
        
        self.text_display.append(f"Buffer size changed to {self.max_data_points}")
    
    def save_data(self):
        """Save current graph data to file"""
        # This is just a placeholder - implement your save logic here
        self.text_display.append("Save data functionality to be implemented")
    
    def process_serial_data(self, data):
        """
        Template method for processing serial data
        Override this in your application to parse serial data and update graphs
        """
        # Example: Parse data and update graphs
        # try:
        #     # Parse your data format here
        #     # values = parse_data(data)
        #     # self.add_data_points(values)
        #     pass
        # except Exception as e:
        #     self.text_display.append(f"Error parsing data: {e}")
        pass

    def read_serial_data(self):
        """Read and process serial data from the connected port"""
        if not self.serial_com.is_connected:
            return
        
        try:
            # Read available data from serial port
            data = self.serial_com.read_data(size=1024, timeout=0.01)
            
            if data:
                # Display raw data
                self.text_display.append(data.strip())
                
                # Scroll to bottom
                self.text_display.verticalScrollBar().setValue(
                    self.text_display.verticalScrollBar().maximum()
                )
                
                # Try to parse numeric data (customize this for your protocol)
                self.parse_and_update_graphs(data)
                
        except Exception as e:
            self.text_display.append(f"Serial read error: {str(e)}")
    
    def parse_and_update_graphs(self, data):
        """
        Parse serial data and update graphs
        Customize this method based on your specific data format
        """
        try:
            # Example: Parse comma-separated values for 6 channels
            # Expected format: "CH1:value1,CH2:value2,CH3:value3,CH4:value4,CH5:value5,CH6:value6"
            
            # Remove whitespace and split
            lines = data.strip().split('\n')
            
            for line in lines:
                if line:
                    # Example parsing logic - modify based on your actual data format
                    if ',' in line:  # CSV format
                        values_str = line.split(',')
                        values = []
                        
                        for val_str in values_str[:6]:  # Take up to 6 values
                            try:
                                # Extract numeric value
                                # This handles formats like "CH1:1.23", "1.23", "Value=1.23", etc.
                                import re
                                numbers = re.findall(r"[-+]?\d*\.\d+|\d+", val_str)
                                if numbers:
                                    values.append(float(numbers[0]))
                                else:
                                    values.append(0.0)
                            except:
                                values.append(0.0)
                        
                        # Pad with zeros if less than 6 values
                        while len(values) < 6:
                            values.append(0.0)
                        
                        # Add data points with timestamp
                        timestamp = len(self.graph_data[0])  # Use length of first channel as timestamp
                        self.add_data_points(values[:6], timestamp)
                        
                    # Add more parsing formats here as needed...
                    
        except Exception as e:
            # Don't display parsing errors for every data point to avoid spam
            # Uncomment for debugging:
            # self.text_display.append(f"Parse error: {str(e)}")
            pass

    def send_serial_data(self, data):
        """Send data through the serial connection"""
        if self.serial_com.is_connected:
            success = self.serial_com.send_data(data)
            if success:
                self.text_display.append(f"Sent: {data}")
            else:
                self.text_display.append(f"Failed to send: {data}")
            return success
        else:
            self.text_display.append("Not connected to any port")
            return False
        
    def closeEvent(self, event):
        """Handle application close event"""
        # Stop all timers
        if hasattr(self, 'SerialTimer'):
            self.SerialTimer.stop()
        if hasattr(self, 'GraphicTimer'):
            self.GraphicTimer.stop()
        
        # Disconnect serial port
        if self.serial_com.is_connected:
            self.serial_com.disconnect()
        
        # Accept the close event
        event.accept()

    def open_loadcell_config(self):
        """Open the load cell configuration dialog"""
        dialog = LoadCellConfigDialog(self)
        dialog.exec()

    def open_motor_control(self):
        """Open the motor control dialog"""
        dialog = MotorControlDialog(self)
        dialog.exec()

