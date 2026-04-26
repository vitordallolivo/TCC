import numpy as np
import pyqtgraph as pg
import time
import threading
import pandas as pd
import yaml
import os
import csv
import tempfile
from PySide6.QtWidgets import QMainWindow, QMessageBox, QFileDialog, QInputDialog, QApplication
from PySide6.QtUiTools import QUiLoader
from PySide6.QtCore import QFile, Slot, QTimer, QThread, Signal

# --- Worker de Telemetria (Simulação de Alta Frequência) ---
class TelemetryWorker(QThread):
    data_ready = Signal(dict)

    def __init__(self, freq_hz=20):
        super().__init__()
        self.freq_hz = freq_hz
        self.running = True
        self.start_time = time.time()

    def run(self):
        interval = 1.0 / self.freq_hz
        while self.running:
            loop_start = time.time()
            current_ts = time.time() - self.start_time
            
            # Mock: 24 canais de telemetria
            readings = {f"Plot_{i}": (current_ts, np.sin(current_ts + i*0.3)*10 + i*2) for i in range(1, 25)}
            self.data_ready.emit(readings)
            
            elapsed = time.time() - loop_start
            time.sleep(max(0, interval - elapsed))

    def stop(self):
        self.running = False
        self.wait()

# --- Classe Principal com Gerenciamento Hierárquico de Memória ---
class AlbatrozGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        
        # 1. Configuração de Caminhos
        self.config_dir = "Config"
        self.prefs_path = os.path.join(self.config_dir, "user_prefs.yaml")
        self.same_path = os.path.join(self.config_dir, "SAME.yaml")
        self.siba_path = os.path.join(self.config_dir, "SIBA.yaml")
        
        # 2. Setup de Swap Binário (Prevenção de estouro de RAM)
        # delete=False para garantir que o arquivo persista até o fechamento manual
        self.temp_swap = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
        self.swap_path = self.temp_swap.name
        self.points_in_swap = 0
        
        # Configurações de Buffer
        self.max_ram_points = 5000  # Quantidade de pontos visíveis no gráfico (Leve)
        self.swap_threshold = 2500  # Quando atingir isso, movemos dados para o disco
        self.swap_chunk = 2000      # Quantidade de pontos movidos por vez
        
        # 3. Buffers de RAM (Apenas para o que está na tela)
        self.ptr = 0
        self.time_buffer = np.full(self.max_ram_points, np.nan, dtype=np.float32)
        self.value_buffers = {f"Plot_{i}": np.full(self.max_ram_points, np.nan, dtype=np.float32) for i in range(1, 25)}
        
        # 4. Carregar UI e Plots
        self._load_ui()
        self.titulos = [
            "Tensão Barramento (V)", "Corrente Fase A (A)", "Corrente Fase B (A)", 
            "Corrente Fase C (A)", "RPM Motor", "Torque Estimado (Nm)",
            "Temp. ESC (°C)", "Temp. Motor (°C)", "Thrust 1 (kgf)",
            "Thrust 2 (kgf)", "Pitot Speed (m/s)", "SOC Bateria (%)"
        ]
        self._setup_plots()
        
        # 5. Inicialização de Estado
        self.prefs = self.load_user_prefs()
        self._setup_connections()
        self.apply_user_prefs()

        # 6. Threads e Timers
        self.worker = TelemetryWorker(freq_hz=20)
        self.worker.data_ready.connect(self.on_data_received)
        self.worker.start()

        self.ui_timer = QTimer()
        self.ui_timer.timeout.connect(self.update_plots_ui)
        self.ui_timer.start(50) 

    def _load_ui(self):
        loader = QUiLoader()
        loader.registerCustomWidget(pg.PlotWidget)
        ui_file = QFile("GUI/mainAlbatrozGUI.ui")
        ui_file.open(QFile.ReadOnly)
        self.ui = loader.load(ui_file)
        ui_file.close()
        self.setCentralWidget(self.ui)

    def _setup_plots(self):
        self.plots = {}
        self.curves = {}
        for i in range(1, 25):
            name = f"Plot_{i}"
            p = getattr(self.ui, name, None)
            if p:
                self.plots[name] = p
                p.setClipToView(True)
                p.setDownsampling(mode='peak')
                title = self.titulos[i-1] if i-1 < len(self.titulos) else f"Sensor {i}"
                p.setTitle(title, size="9pt")
                self.curves[name] = p.plot(pen=pg.mkPen('#0055ff', width=1.5))

    def _setup_connections(self):
        self.ui.Save_CSV.clicked.connect(self.on_save_csv)
        self.ui.Motor_off.clicked.connect(lambda: self.ui.Motor_action.setValue(0))
        self.ui.pushButton.clicked.connect(self.on_clear_all)
        self.ui.Motor_action.valueChanged.connect(self.on_slider_changed)
        self.ui.actionLoad.triggered.connect(self.on_load_motor)
        self.ui.actionLoad_2.triggered.connect(self.on_load_battery)
        self.ui.actionClaro.triggered.connect(lambda: self.change_theme("claro"))
        self.ui.actionEscuro.triggered.connect(lambda: self.change_theme("escuro"))

    # --- Lógica de Memória e Swap ---

    @Slot(dict)
    def on_data_received(self, readings):
        """Escreve na RAM e verifica se precisa dar 'flush' para o disco."""
        self.time_buffer[self.ptr] = readings["Plot_1"][0]
        for i in range(1, 25):
            self.value_buffers[f"Plot_{i}"][self.ptr] = readings[f"Plot_{i}"][1]
        
        self.ptr += 1

        # Sistema de Swap (Gatilho de Memória)
        if self.ptr >= self.swap_threshold:
            self._flush_to_disk()

    def _flush_to_disk(self):
        """Move 40% dos dados da RAM para arquivo binário (Write-ahead)."""
        n = self.swap_chunk
        # Organiza dados: [Time, V1, V2... V24]
        data_block = np.zeros((n, 25), dtype=np.float32)
        data_block[:, 0] = self.time_buffer[:n]
        for i in range(1, 25):
            data_block[:, i] = self.value_buffers[f"Plot_{i}"][:n]
        
        # Escrita binária (Append)
        with open(self.swap_path, "ab") as f:
            f.write(data_block.tobytes())
        
        self.points_in_swap += n
        
        # Shift dos dados restantes na RAM para o início
        remaining = self.ptr - n
        self.time_buffer[:remaining] = self.time_buffer[n:self.ptr]
        for i in range(1, 25):
            p_id = f"Plot_{i}"
            self.value_buffers[p_id][:remaining] = self.value_buffers[p_id][n:self.ptr]
            
        self.ptr = remaining
        self.statusBar().showMessage(f"Auto-Swap: {self.points_in_swap} pontos em disco | RAM otimizada.", 2000)

    def update_plots_ui(self):
        """Renderiza apenas o que está na RAM (Gráfico sempre rápido)."""
        # Como o buffer é linear e fazemos shift no swap, não precisamos de concatenate complexo
        x = self.time_buffer[:self.ptr]
        for p_id, curve in self.curves.items():
            curve.setData(x, self.value_buffers[p_id][:self.ptr])

    # --- Exportação Profissional (Stitching) ---

    @Slot()
    def on_save_csv(self):
        path, _ = QFileDialog.getSaveFileName(self, "Exportar Ensaio Completo", "", "CSV (*.csv)")
        if not path: return

        self.statusBar().showMessage("Costurando dados Disco + RAM...")

        def run_stitching():
            try:
                with open(path, 'w', newline='') as f_out:
                    writer = csv.writer(f_out)
                    # Headers
                    headers = ["Time(s)"] + [self.titulos[i] if i < len(self.titulos) else f"S{i+1}" for i in range(24)]
                    writer.writerow(headers)

                    # 1. Recuperar do Disco (Swap Binário) em pedaços para não travar
                    if self.points_in_swap > 0:
                        with open(self.swap_path, "rb") as f_bin:
                            chunk_size = 1000 # linhas
                            row_size = 25 * 4 # 25 colunas * 4 bytes (float32)
                            while True:
                                chunk_raw = f_bin.read(chunk_size * row_size)
                                if not chunk_raw: break
                                data_chunk = np.frombuffer(chunk_raw, dtype=np.float32).reshape(-1, 25)
                                writer.writerows(data_chunk)

                    # 2. Adicionar o que restou na RAM
                    ram_block = np.zeros((self.ptr, 25), dtype=np.float32)
                    ram_block[:, 0] = self.time_buffer[:self.ptr]
                    for i in range(1, 25):
                        ram_block[:, i] = self.value_buffers[f"Plot_{i}"][:self.ptr]
                    
                    writer.writerows(ram_block)
                
                self.statusBar().showMessage("CSV Gerado com Sucesso!", 5000)
            except Exception as e:
                print(f"Erro no stitching: {e}")

        threading.Thread(target=run_stitching, daemon=True).start()

    # --- Auxiliares e Menus ---

    def load_user_prefs(self):
        default = {"tema": "claro", "motor_val": 0, "last_motor_name": "Nenhum", "last_battery_name": "Nenhum"}
        if os.path.exists(self.prefs_path):
            with open(self.prefs_path, 'r') as f:
                return yaml.safe_load(f) or default
        return default

    def save_user_prefs(self):
        os.makedirs(self.config_dir, exist_ok=True)
        with open(self.prefs_path, 'w') as f:
            yaml.dump(self.prefs, f)

    def apply_user_prefs(self):
        self.change_theme(self.prefs.get("tema", "claro"))
        self.ui.Motor_action.setValue(self.prefs.get("motor_val", 0))
        # self.statusBar().showMessage(f"Motor: {self.prefs['last_motor_name']} | Bateria: {self.prefs['last_battery_name']}")

    @Slot()
    def on_load_motor(self):
        try:
            with open(self.same_path, 'r') as f:
                data = yaml.safe_load(f)
            motores = list(data.get('motors', {}).keys())
            choice, ok = QInputDialog.getItem(self, "Motor", "Selecione:", motores, 0, False)
            if ok and choice:
                self.prefs["last_motor_name"] = choice
                self.apply_user_prefs()
                self.save_user_prefs()
        except Exception as e: QMessageBox.warning(self, "Erro", str(e))

    @Slot()
    def on_load_battery(self):
        try:
            with open(self.siba_path, 'r') as f:
                data = yaml.safe_load(f)
            baterias = [b['name'] for b in data.get('Battery', []) if isinstance(b, dict) and 'name' in b]
            choice, ok = QInputDialog.getItem(self, "Bateria", "Selecione:", baterias, 0, False)
            if ok and choice:
                self.prefs["last_battery_name"] = choice
                self.apply_user_prefs()
                self.save_user_prefs()
        except Exception as e: QMessageBox.warning(self, "Erro", str(e))

    def change_theme(self, theme):
        self.prefs["tema"] = theme
        is_dark = (theme == "escuro")
        bg, txt = ('#1e1e1e', '#ffffff') if is_dark else ('w', 'k')
        self.ui.setStyleSheet(f"background-color: {'#2b2b2b' if is_dark else '#f0f0f0'}; color: {txt};")
        for p in self.plots.values():
            p.setBackground(bg)
            for ax in ['left', 'bottom']:
                p.getAxis(ax).setPen(txt)
                p.getAxis(ax).setTextPen(txt)
        self.save_user_prefs()

    @Slot(int)
    def on_slider_changed(self, val):
        self.prefs["motor_val"] = val
        self.save_user_prefs()

    @Slot()
    def on_clear_all(self):
        self.ptr = 0
        self.points_in_swap = 0
        if os.path.exists(self.swap_path):
            open(self.swap_path, 'wb').close()
        self.worker.start_time = time.time()
        self.statusBar().showMessage("Buffers e Swap limpos.")

    def closeEvent(self, event):
        self.save_user_prefs()
        self.worker.stop()
        if os.path.exists(self.swap_path):
            try: os.remove(self.swap_path)
            except: pass
        event.accept()