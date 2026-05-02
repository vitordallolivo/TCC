import numpy as np
import pyqtgraph as pg
import time
import os
import yaml
import tempfile
import csv
import struct
import sys

from PySide6.QtWidgets import QMainWindow, QMessageBox, QFileDialog, QInputDialog
from PySide6.QtUiTools import QUiLoader
from PySide6.QtCore import QFile, Slot, QTimer, QThread, Signal

# Importações de hardware e componentes customizados
from Communication_Data.Ethernet import EthernetManager
from GUI.SAMEConfigDialog import SAMEConfigDialog
from GUI.TinyTerminal import TinyTerminal

# Mapeamento de tipos para o struct.unpack (C -> Python)
TYPE_MAP = {
    "float": "f",
    "uint32_t": "I",
    "log_status": "I",
    "uint8_t": "B"
}

class TelemetryMOCK(QThread):
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
            
            readings = {}
            if self.target == "all":
                readings = {f"Plot_{i}": (current_ts, np.sin(current_ts + i/2)*5 + i) for i in range(1, 25)}
            else:
                try:
                    num = int(self.target)
                    if 1 <= num <= 24:
                        # Gera apenas para o selecionado, o resto fica estático ou NaN
                        readings = {f"Plot_{i}": (current_ts, np.sin(current_ts)*5 if i == num else np.nan) for i in range(1, 25)}
                except:
                    pass
            
            if readings:
                self.data_ready.emit(readings)
            
            time.sleep(max(0, interval - (time.time() - loop_start)))

    def stop(self):
        self.running = False
        self.wait()

class AlbatrozGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        
        # 1. Pastas e Caminhos
        self.script_dir = os.path.dirname(os.path.abspath(__file__))
        self.root_dir = os.path.dirname(self.script_dir) if self.script_dir.endswith('GUI') else self.script_dir
        self.config_dir = os.path.join(self.root_dir, "Config")
        self.gui_dir = os.path.join(self.root_dir, "GUI")
        
        self.prefs_path = os.path.join(self.config_dir, "user_prefs.yaml")
        self.same_yaml_path = os.path.join(self.config_dir, "SAME.yaml")
        self.siba_yaml_path = os.path.join(self.config_dir, "SIBA.yaml")

        # 2. Inicializa o Terminal primeiro para capturar logs de boot
        self.terminal = TinyTerminal(self.config_dir, self)
        print(f"--- Iniciando Sistema Albatroz ---")
        print(f"Diretório Raiz: {self.root_dir}")

        # 3. Carrega as Configurações das Placas
        self.configs = {
            "SIBA": self._load_config_file(self.siba_yaml_path),
            "SAME": self._load_config_file(self.same_yaml_path)
        }
        print(f"Configs carregadas: SIBA({'OK' if self.configs['SIBA'] else 'Falha'}), SAME({'OK' if self.configs['SAME'] else 'Falha'})")

        self._load_ui() 

        # 4. Atributos de Memória e Swap
        self.ptr = 0
        self.max_ram = 5000
        self.time_buffer = np.full(self.max_ram, np.nan, dtype=np.float32)
        self.value_buffers = {f"Plot_{i}": np.full(self.max_ram, np.nan, dtype=np.float32) for i in range(1, 25)}
        
        self.temp_swap = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
        self.swap_path = self.temp_swap.name
        self.points_in_swap = 0
        print(f"Arquivo de Swap ativo: {self.swap_path}")

        # 5. Inicializa Gerenciador de Rede
        self.eth = EthernetManager(
            self.siba_yaml_path, 
            self.same_yaml_path, 
            handle_callback=self.process_network_data
        )

        self.plots = {}   
        self.curves = {}
        self._setup_plots()
        
        # 6. Aplica Preferências e Conexões
        self.prefs = self.load_user_prefs()
        self._setup_connections()
        self.apply_user_prefs()

        # 7. Threads e Timers
        self.worker = TelemetryMOCK(freq_hz=20)
        self.worker.data_ready.connect(self.on_data_received)

        self.ui_timer = QTimer()
        self.ui_timer.timeout.connect(self.update_plots_ui)
        self.ui_timer.start(50)
        print("Threads de telemetria e timer de UI prontos.")

        # 8. Inicializa o terminal  
        self.terminal.show()

    # --- Lógica de Inicialização ---
    def _load_config_file(self, path):
        if os.path.exists(path):
            with open(path, 'r') as f:
                return yaml.safe_load(f) or {}
        return {}

    def _load_ui(self):
        loader = QUiLoader()
        loader.registerCustomWidget(pg.PlotWidget)
        ui_path = os.path.join(self.gui_dir, "mainAlbatrozGUI.ui")
        ui_file = QFile(ui_path)
        if not ui_file.open(QFile.ReadOnly):
            print(f"ERRO: Não foi possível carregar {ui_path}")
            return
        self.ui = loader.load(ui_file)
        ui_file.close()
        self.setCentralWidget(self.ui)

    def _setup_plots(self):
        for i in range(1, 25):
            name = f"Plot_{i}"
            plot_widget = getattr(self.ui, name, None)
            if plot_widget:
                self.plots[name] = plot_widget
                plot_widget.setFixedSize(400, 300)
                plot_widget.setClipToView(True)
                plot_widget.setDownsampling(mode='peak')
                self.curves[name] = plot_widget.plot(pen=pg.mkPen('#0055ff', width=1.5))

    def _setup_connections(self):
        self.ui.Save_CSV.clicked.connect(self.on_save_csv)
        self.ui.Motor_off.clicked.connect(lambda: self.ui.Motor_action.setValue(0))
        self.ui.pushButton.clicked.connect(self.on_clear_all)
        self.ui.Motor_action.valueChanged.connect(self.on_slider_changed)
        self.ui.pushButton_2.clicked.connect(self.on_toggle_network)
        self.ui.Tiny_Terminal.clicked.connect(self.terminal.show)
        
        # Menu Actions
        self.ui.actionLoad.triggered.connect(self.on_load_motor)
        self.ui.actionCreate.triggered.connect(self.open_same_config)
        self.ui.actionClaro.triggered.connect(lambda: self.change_theme("claro"))
        self.ui.actionEscuro.triggered.connect(lambda: self.change_theme("escuro"))

    # --- Slots e Eventos ---
    @Slot(dict)
    def on_data_received(self, readings):
        """Recebe dados do MOCK Telemetry."""
        idx = self.ptr % self.max_ram
        self.time_buffer[idx] = readings["Plot_1"][0]
        for i in range(1, 25):
            self.value_buffers[f"Plot_{i}"][idx] = readings[f"Plot_{i}"][1]
        
        self.ptr += 1
        if self.ptr > 0 and self.ptr % self.max_ram == 0:
            self._flush_to_swap()

    @Slot(str, bytes, tuple)
    def process_network_data(self, protocol, data, addr):
        """Recebe pacotes reais da Ethernet."""
        try:
            log_config = self.configs[protocol].get("TELEMETRY_LOG", [])
            if not log_config: return

            fmt = "<" + "".join(TYPE_MAP.get(f['type'], 'f') for f in log_config)
            if len(data) != struct.calcsize(fmt): return

            values = struct.unpack(fmt, data)
            decoded = {field['name']: val for field, val in zip(log_config, values)}
            
            # Envia para o terminal (HEX ou Traduzido conforme escolha do usuário no terminal)
            if hasattr(self, 'terminal'):
                self.terminal.log_board_data(protocol, data, decoded)

            if self.ptr == 0:
                print(f"Dados recebidos de {protocol} ({addr}). Atualizando eixos.")
                self._update_plot_titles(protocol, log_config)

            self._update_internal_buffers(decoded)
        except Exception as e:
            print(f"Erro no processamento de rede [{protocol}]: {e}")

    def _update_internal_buffers(self, decoded_data):
        idx = self.ptr % self.max_ram
        self.time_buffer[idx] = decoded_data.get('timestamp', time.time())
        
        data_to_plot = {k: v for k, v in decoded_data.items() if k not in ['timestamp', 'status']}
        for i, (name, val) in enumerate(data_to_plot.items(), 1):
            if i > 24: break
            self.value_buffers[f"Plot_{i}"][idx] = val
        
        self.ptr += 1
        if self.ptr > 0 and self.ptr % self.max_ram == 0:
            self._flush_to_swap()

    def _flush_to_swap(self):
        data_to_save = np.column_stack([self.time_buffer] + [self.value_buffers[f"Plot_{i}"] for i in range(1, 25)])
        with open(self.swap_path, "ab") as f:
            f.write(data_to_save.astype(np.float32).tobytes())
        self.points_in_swap += self.max_ram
        print(f"Swap: {self.max_ram} pontos movidos para o disco. Total em swap: {self.points_in_swap}")

    def update_plots_ui(self):
        if self.ptr == 0: return
        # Lógica de buffer circular para o scroll dos gráficos
        if self.ptr < self.max_ram:
            x = self.time_buffer[:self.ptr]
            for name, curve in self.curves.items():
                curve.setData(x, self.value_buffers[name][:self.ptr])
        else:
            split = self.ptr % self.max_ram
            x = np.concatenate([self.time_buffer[split:], self.time_buffer[:split]])
            for name, curve in self.curves.items():
                y = np.concatenate([self.value_buffers[name][split:], self.value_buffers[name][:split]])
                curve.setData(x, y)

    @Slot()
    def on_toggle_network(self):
        if not self.eth.running:
            print("Iniciando modo REDE...")
            if self.worker.isRunning(): 
                self.worker.stop()
                print("Mock desativado.")
            self.on_clear_all()
            try:
                self.eth.start()
                if self.eth.running:
                    self.ui.pushButton_2.setText("Desconectar")
                    print("Escutando telemetria SIBA/SAME.")
            except Exception as e:
                print(f"Erro de rede: {e}")
        else:
            self.eth.stop()
            self.ui.pushButton_2.setText("Conectar")
            print("Rede parada.")

    @Slot()
    def on_clear_all(self):
        print("Limpando todos os buffers e dados de swap...")
        self.ptr = 0
        self.points_in_swap = 0
        self.time_buffer.fill(np.nan)
        for b in self.value_buffers.values(): b.fill(np.nan)
        if os.path.exists(self.swap_path):
            open(self.swap_path, 'wb').close()

    @Slot()
    def on_save_csv(self):
        path, _ = QFileDialog.getSaveFileName(self, "Exportar", "", "CSV (*.csv)")
        if not path: return
        print(f"Exportando CSV para {path}...")
        try:
            with open(path, 'w', newline='') as csvfile:
                writer = csv.writer(csvfile)
                writer.writerow(["Timestamp"] + [f"S_{i}" for i in range(1, 25)])
                # Lê o que está no swap
                if os.path.exists(self.swap_path) and self.points_in_swap > 0:
                    disk_data = np.fromfile(self.swap_path, dtype=np.float32).reshape(-1, 25)
                    writer.writerows(disk_data)
                # Adiciona o resto da RAM
                ram_idx = self.ptr % self.max_ram
                if ram_idx > 0:
                    current_ram = np.column_stack([self.time_buffer[:ram_idx]] + [self.value_buffers[f"Plot_{i}"][:ram_idx] for i in range(1, 25)])
                    writer.writerows(current_ram)
            print("Exportação CSV concluída com sucesso.")
        except Exception as e:
            print(f"Erro ao salvar CSV: {e}")

    def change_theme(self, theme):
        self.prefs["tema"] = theme
        print(f"Tema alterado para: {theme}")
        is_dark = (theme == "escuro")
        bg, txt = ('#1e1e1e', '#ffffff') if is_dark else ('w', 'k')
        self.ui.setStyleSheet(f"background-color: {'#2b2b2b' if is_dark else '#f0f0f0'}; color: {txt};")
        for p in self.plots.values():
            p.setBackground(bg)
            p.getAxis('left').setPen(txt); p.getAxis('bottom').setPen(txt)
        self.save_user_prefs()

    def load_user_prefs(self):
        if os.path.exists(self.prefs_path):
            with open(self.prefs_path, 'r') as f:
                return yaml.safe_load(f) or {}
        return {"tema": "claro", "motor_val": 0}

    def save_user_prefs(self):
        os.makedirs(self.config_dir, exist_ok=True)
        with open(self.prefs_path, 'w') as f:
            yaml.dump(self.prefs, f)

    def apply_user_prefs(self):
        self.change_theme(self.prefs.get("tema", "claro"))
        self.ui.Motor_action.setValue(self.prefs.get("motor_val", 0))

    def closeEvent(self, event):
        print("Finalizando recursos e removendo swap...")
        self.worker.stop()
        if os.path.exists(self.swap_path):
            try: os.remove(self.swap_path)
            except: pass
        event.accept()

    # Métodos de carregamento de menus...
    @Slot()
    def on_load_motor(self):
        print("Solicitando lista de motores do SAME.yaml")
        try:
            with open(self.same_yaml_path, 'r') as f:
                data = yaml.safe_load(f)
            motores = list(data.get('motors', {}).keys())
            choice, ok = QInputDialog.getItem(self, "Motor", "Selecione:", motores, 0, False)
            if ok and choice:
                self.prefs["last_motor_name"] = choice
                self.apply_user_prefs()
                self.save_user_prefs()

                if self.eth.running: # Tries send configuration
                    try:
                        pass
                    except Exception as e: QMessageBox.warning(self, "Erro", str(e))


        except Exception as e: QMessageBox.warning(self, "Erro", str(e))

        
    @Slot()
    def open_same_config(self):
        print("Abrindo diálogo de configuração SAME...")
        
        # 1. Pegamos as preferências (ou as configs carregadas)
        # 2. Passamos o diretório de configs (self.config_dir já existe no seu __init__)
        # 3. Passamos o objeto ethernet (self.eth)
        
        try:
            dialog = SAMEConfigDialog(
                self.prefs,           # Argumento 1: prefs
                self.config_dir,      # Argumento 2: config_dir
                self.eth,             # Argumento 3: ethernet_mgr
                self                  # Argumento 4: parent
            )
            
            if dialog.exec():
                print("Configurações aplicadas com sucesso.")
                self.save_user_prefs() # Salva o que o dialog alterou no self.prefs
                
        except Exception as e:
            print(f"Erro ao abrir o menu SAME: {e}")

    @Slot(int)
    def on_slider_changed(self, val):
        self.prefs["motor_val"] = val
        # Aqui você enviaria o comando TX de Duty Cycle/Velocidade
        print(f"Slider alterado: {val}%")
        self.save_user_prefs()

    def start_mock_with_target(self, target):
        """Configura e inicia o mock baseado no argumento do terminal."""
        if not self.worker.isRunning():
            self.worker.start()
            print(f"Mock Thread iniciada via terminal.")
        
        # Armazenamos o alvo no worker (precisamos ajustar a classe TelemetryMOCK)
        self.worker.target = target 
        print(f"Mock agora focado em: {target}")

    def clear_graph_buffers(self, target):
        """Limpa buffers de memória baseado no alvo do terminal."""
        if target == "all" or target == "all":
            self.on_clear_all() # Chama sua função existente
        else:
            try:
                num = int(target)
                if 1 <= num <= 24:
                    plot_key = f"Plot_{num}"
                    self.value_buffers[plot_key].fill(np.nan)
                    # Opcional: limpar apenas este gráfico na tela imediatamente
                    self.curves[plot_key].setData([], [])
                    print(f"Buffer do Gráfico {num} resetado.")
            except ValueError:
                print(f"Erro: Alvo de limpeza '{target}' inválido.")