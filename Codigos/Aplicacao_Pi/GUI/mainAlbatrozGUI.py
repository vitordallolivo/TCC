import numpy as np
import pyqtgraph as pg
from PySide6.QtWidgets import QMainWindow
from PySide6.QtUiTools import QUiLoader
from PySide6.QtCore import QFile, Slot, QTimer # Adicionado QTimer

class AlbatrozGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        
        loader = QUiLoader()
        loader.registerCustomWidget(pg.PlotWidget)
        
        ui_file = QFile("GUI/mainAlbatrozGUI.ui") 
        if not ui_file.open(QFile.ReadOnly):
            raise FileNotFoundError("Erro ao abrir .ui")

        self.ui = loader.load(ui_file) 
        ui_file.close()

        if self.ui:
            self.setCentralWidget(self.ui)
        
        self.resize(self.ui.size())
        self.setWindowTitle("Albatroz")

        self.plots = {}
        self.curves = {}
        self.data_buffers = {}
        self.buffer_size = 200 
        
        # Lista de títulos para seus 24 gráficos (Personalize conforme sua planilha de sensores)
        self.titulos = [
            "Tensão Barramento (V)", "Corrente Fase A (A)", "Corrente Fase B (A)", 
            "Corrente Fase C (A)", "RPM Motor", "Torque Estimado (Nm)",
            "Temp. ESC (°C)", "Temp. Motor (°C)", "Thrust Célula 1 (kgf)",
            "Thrust Célula 2 (kgf)", "Pitot Speed (m/s)", "SOC Bateria (%)"
            # ... adicione os outros 12 conforme necessário
        ]

        self._setup_all_plots()

        # --- MOCK DATA TIMER ---
        self.timer = QTimer()
        self.timer.timeout.connect(self._generate_mock_data)
        self.timer.start(50) # Atualiza a cada 50ms (20Hz)
        self.phase = 0

    def _setup_all_plots(self):
        for i in range(1, 25):
            plot_name = f"Plot_{i}"
            plot_widget = getattr(self.ui, plot_name, None)
            
            if plot_widget and isinstance(plot_widget, pg.PlotWidget):
                self.plots[plot_name] = plot_widget
                
                # --- CONFIGURAÇÃO DE ENGENHARIA ---
                plot_widget.setBackground('w') 
                plot_widget.showGrid(x=True, y=True, alpha=0.3)
                
                # Adiciona Título (se houver na lista, senão usa o nome do widget)
                title = self.titulos[i-1] if i-1 < len(self.titulos) else plot_name
                plot_widget.setTitle(title, color="k", size="10pt")
                
                # AUTO TRACK / AUTO RANGE
                # Habilita o ajuste automático dos eixos
                plot_widget.enableAutoRange(axis='y', enable=True)
                plot_widget.setAutoVisible(y=True) # Ajusta o Y baseado apenas no que é visível
                
                self.curves[plot_name] = plot_widget.plot(
                    pen=pg.mkPen(color='#0055ff', width=2),
                    antialias=True
                )
                self.data_buffers[plot_name] = np.zeros(self.buffer_size)

    def _generate_mock_data(self):
        """Gera dados falsos para teste de estresse da interface."""
        self.phase += 0.1
        mock_readings = {}
        
        for i in range(1, 25):
            plot_id = f"Plot_{i}"
            # Gera uma mistura de seno com ruído branco
            val = np.sin(self.phase + i*0.5) * 10 + np.random.normal(0, 0.5)
            mock_readings[plot_id] = val
            
        self.update_telemetry(mock_readings)

    def update_telemetry(self, new_readings: dict):
        for plot_id, value in new_readings.items():
            if plot_id in self.data_buffers:
                self.data_buffers[plot_id] = np.roll(self.data_buffers[plot_id], -1)
                self.data_buffers[plot_id][-1] = value
                self.curves[plot_id].setData(self.data_buffers[plot_id])