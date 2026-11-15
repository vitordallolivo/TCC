import sys
import socket
import struct
import numpy as np
from PyQt5 import QtWidgets, QtCore
import pyqtgraph as pg
from collections import deque

# --- Configurações de Rede e Protocolo ---
UDP_IP = "0.0.0.0"
UDP_PORT = 9999
ADC_CHANNELS = 8
SAMPLES_PER_PKT = 84
BYTES_PER_SAMPLE = ADC_CHANNELS * 2  # 16 bytes
MAX_POINTS = 5000  # Quantidade de pontos visíveis por canal (~70ms de dados a 36kHz)

class BLDCDashboard(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        
        # 1. Configuração da Janela Principal
        self.setWindowTitle("Monitor de Aquisição")
        self.resize(1200, 900)
        self.setStyleSheet("background-color: #121212; color: white;")

        # Widget Central
        central_widget = QtWidgets.QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QtWidgets.QVBoxLayout(central_widget)

        # 2. Configuração do PyQtGraph
        # Usamos um GraphicsLayoutWidget para gerenciar os múltiplos subplots
        self.view = pg.GraphicsLayoutWidget()
        main_layout.addWidget(self.view)

        # 3. Inicialização dos Sockets
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.sock.bind((UDP_IP, UDP_PORT))
            self.sock.setblocking(False)
            print(f"Socket UDP ativo na porta {UDP_PORT}")
        except Exception as e:
            print(f"Erro ao abrir socket: {e}")

        # 4. Criação dos Canais
        self.data_buffers = [deque([0.0] * MAX_POINTS, maxlen=MAX_POINTS) for _ in range(ADC_CHANNELS)]
        self.curves = []
        self.plots = []

        # Cores para facilitar a distinção das fases do motor
        colors = ['#FF3333', '#33FF33', '#3333FF', '#FFFF33', '#FF33FF', '#33FFFF', '#FFA500', '#FFFFFF']

        for i in range(ADC_CHANNELS):
            # Organiza em 4 linhas e 2 colunas
            if i % 2 == 0:
                self.view.nextRow()
            
            p = self.view.addPlot(title=f"Canal {i}")
            p.setYRange(0, 3.4)
            p.showGrid(x=True, y=True, alpha=0.3)
            p.setLabel('left', 'Tensão', units='V')
            
            # Cria a linha (pen) com cor específica
            curve = p.plot(pen=pg.mkPen(color=colors[i], width=1.5))
            self.curves.append(curve)
            self.plots.append(p)

        # 5. Timer para atualização da Interface (60 FPS)
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.update_data)
        self.timer.start(1) 

    def update_data(self):
        has_new_data = False
        
        # Esvazia o buffer de rede antes de desenhar
        while True:
            try:
                data, addr = self.sock.recvfrom(2048)
                if len(data) < 8: continue
                
                # Validação do Header 0xAA55
                header = struct.unpack(">H", data[0:2])[0]
                if header != 0xAA55: continue

                # Extração dos Samples (Little Endian do novo código C)
                num_samples = (len(data) - 8) // BYTES_PER_SAMPLE
                offset = 8
                
                for _ in range(num_samples):
                    # Desempacota os 8 canais de uma vez
                    raw_values = struct.unpack("<8H", data[offset:offset+16])
                    
                    for i in range(ADC_CHANNELS):
                        voltage = (raw_values[i] * 3.3) / 4095
                        self.data_buffers[i].append(voltage)
                    offset += 16
                
                has_new_data = True
            except BlockingIOError:
                break
            except Exception as e:
                print(f"Erro no processamento: {e}")
                break

        # Se chegaram novos dados, atualiza as curvas na tela
        if has_new_data:
            for i in range(ADC_CHANNELS):
                # Convertemos deque para numpy array para renderização ultra-rápida
                self.curves[i].setData(np.array(self.data_buffers[i]))

if __name__ == '__main__':
    # Otimização opcional para PyQtGraph
    pg.setConfigOptions(antialias=False, useOpenGL=True)
    
    app = QtWidgets.QApplication(sys.argv)
    window = BLDCDashboard()
    window.show()
    sys.exit(app.exec_())