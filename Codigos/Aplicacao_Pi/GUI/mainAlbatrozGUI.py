import numpy as np
import pyqtgraph as pg
import time
import os
import yaml
import tempfile
import csv
import struct
import sys

from PySide6.QtWidgets import (QMainWindow, QMessageBox, QFileDialog, QInputDialog,
                               QMenu, QComboBox, QLabel, QWidget, QHBoxLayout)
from PySide6.QtUiTools import QUiLoader
from PySide6.QtCore import QFile, Slot, QTimer, QThread, Signal, Qt
from PySide6.QtGui import QAction

from Communication_Data import codecs

# Importações de hardware e componentes customizados
from Communication_Data.Ethernet import EthernetManager
from GUI.SAMEConfigDialog import SAMEConfigDialog
from GUI.TinyTerminal import TinyTerminal
from GUI.BoardManagerDialog import BoardManagerDialog
from GUI.EstimationDialog import EstimationDialog
from GUI.HeliceDialog import HeliceDialog
from GUI.LoadCellDialog import LoadCellDialog
from GUI.SPMMEnsaioDialog import SPMMEnsaioDialog

# Mapeamento de tipos para o struct.unpack (C -> Python)
TYPE_MAP = {
    "float": "f",
    "uint32_t": "I",
    "log_status": "I",
    "uint8_t": "B"
}

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

        self.plots_yaml_path = os.path.join(self.config_dir, "plots.yaml")
        
        # 2. Carrega as Configurações das Placas (Necessário para EthernetManager e Buffers)
        self.configs = {
            "SIBA": self._load_config_file(self.siba_yaml_path),
            "SAME": self._load_config_file(self.same_yaml_path)
        }
        self.plot_config = self._load_config_file(self.plots_yaml_path)

        print(f"Configs carregadas: SIBA({'OK' if self.configs['SIBA'] else 'Falha'}), SAME({'OK' if self.configs['SAME'] else 'Falha'})")

        # 3. Atributos de Memória e Swap (Devem existir antes de qualquer dado chegar via rede)
        self.ptr = 0
        self.max_ram = 5000
        self.time_buffer = np.full(self.max_ram, np.nan, dtype=np.float32)
        self.value_buffers = {} 
        self.variable_order = [] # Mantém a ordem para o CSV/Swap
        
        self.temp_swap = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
        self.swap_path = self.temp_swap.name
        self.points_in_swap = 0
        print(f"Arquivo de Swap ativo: {self.swap_path}")

        # 4. Inicializa Gerenciador de Rede (Agora com o callback central process_network_data)
        self.eth = EthernetManager(
            self.siba_yaml_path,
            self.same_yaml_path,
            handle_callback=self.process_network_data,
            sample_callback=self.on_decoded_sample  # amostras já decodificadas (ex.: SPMM)
        )

        # 5. Inicializa o Terminal (Agora ele herdará self.eth corretamente do parent)
        self.terminal = TinyTerminal(self.config_dir, self)
        print(f"--- Iniciando Sistema Albatroz ---")
        print(f"Diretório Raiz: {self.root_dir}")

        self._load_ui() 

        self.plots = {}   
        self.curves = {}
        self._setup_plots()
        
        # 6. Aplica Preferências e Conexões
        self._spmm_sample_sink = None   # gancho de amostras p/ os ensaios SPMM
        self.prefs = self.load_user_prefs()
        self._setup_connections()
        self.apply_user_prefs()
        # Filtra os gráficos pelo modo inicial.
        self._build_var_board_map()
        self._apply_mode_plots(self.op_mode)

        # 7. Timer de UI
        self.ui_timer = QTimer()
        self.ui_timer.timeout.connect(self.update_plots_ui)
        self.ui_timer.start(50)
        print("Sistema de atualização de gráficos e timer de UI prontos.")
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
        """Configura os gráficos e curvas dinamicamente conforme o plots.yaml."""
        mapping = self.plot_config.get('plots_mapping', {})
        
        for widget_id, info in mapping.items():
            plot_widget = getattr(self.ui, widget_id, None)
            if not plot_widget: continue
            
            self.plots[widget_id] = plot_widget
            plot_widget.setTitle(info.get('title', ''))
            plot_widget.setLabel('left', info.get('ylabel', ''))
            plot_widget.setLabel('bottom', info.get('xlabel', ''))
            plot_widget.addLegend()
            
            self.curves[widget_id] = []
            for var_name, legend in info.get('vars', {}).items():
                # Inicializa buffer para a variável se não existir
                if var_name not in self.value_buffers:
                    self.value_buffers[var_name] = np.full(self.max_ram, np.nan, dtype=np.float32)
                    if var_name not in self.variable_order:
                        self.variable_order.append(var_name)
                
                # Cria a curva com cor automática
                pen = pg.mkPen(pg.intColor(len(self.curves[widget_id])), width=1.5)
                curve = plot_widget.plot(pen=pen, name=legend if legend else None)
                self.curves[widget_id].append((var_name, curve))

    def _setup_connections(self):
        self.ui.Save_CSV.clicked.connect(self.on_save_csv)
        self.ui.pushButton.clicked.connect(self.on_clear_all)
        self.ui.pushButton_2.clicked.connect(self.on_toggle_network)
        self.ui.Tiny_Terminal.clicked.connect(self.terminal.show)

        # --- Controle do motor (slider = throttle) ---
        # valueChanged: atualiza o rótulo enquanto arrasta (não envia).
        # sliderReleased / "Atualizar (Motor)": ENVIA o throttle atual.
        # "Desligar (Motor)": para o motor (disarm/stop) e zera o slider.
        self.ui.Motor_action.valueChanged.connect(self.on_slider_changed)
        self.ui.Motor_action.sliderReleased.connect(self.apply_throttle)
        self.ui.Motor_update.clicked.connect(self.apply_throttle)
        self.ui.Motor_off.clicked.connect(self.motor_stop)
        
        # Menu Actions
        self.ui.actionLoad.triggered.connect(self.on_load_motor)
        self.ui.actionCreate.triggered.connect(self.open_same_config)
        self.ui.actionClaro.triggered.connect(lambda: self.change_theme("claro"))
        self.ui.actionEscuro.triggered.connect(lambda: self.change_theme("escuro"))

        # Reorganiza a barra de menus (tudo em português, menus de topo).
        self._build_menus()
        # Seletor "Modo de Operação" (canto direito da barra de menus).
        self._build_mode_selector()

    # ================================================================== #
    # Modo de Operação — define quais placas sobem e para qual o throttle vai
    # ================================================================== #
    def _build_mode_selector(self):
        self.op_mode = "SPMM"
        # Guarda em self (posse) — widget local seria coletado pelo GC e sumiria.
        self._mode_box = QWidget(self.ui.menubar)
        lay = QHBoxLayout(self._mode_box); lay.setContentsMargins(4, 0, 8, 0)
        lay.addWidget(QLabel("Modo:"))
        self.cmb_mode = QComboBox()
        self.cmb_mode.addItems(["SPMM", "SIBA", "SAME", "SIBA+SAME"])
        self.cmb_mode.currentTextChanged.connect(self.on_mode_changed)
        lay.addWidget(self.cmb_mode)
        self.ui.menubar.setCornerWidget(self._mode_box, Qt.TopRightCorner)

    def _boards_in_mode(self, mode):
        return {"SPMM": ["SPMM"], "SIBA": ["SIBA"], "SAME": ["SAME"],
                "SIBA+SAME": ["SIBA", "SAME"]}.get(mode, [])

    def _motor_board(self, mode):
        """Placa que recebe o throttle no modo atual (SPMM ou SAME; SIBA não)."""
        if mode == "SPMM":
            return "SPMM"
        if mode in ("SAME", "SIBA+SAME"):
            return "SAME"
        return None

    @Slot(str)
    def on_mode_changed(self, mode):
        self.op_mode = mode
        print(f"Modo de Operação: {mode}")
        # Sobe os links das placas do modo (sob demanda).
        for b in self._boards_in_mode(mode):
            self.eth.start_link(b)
        mb = self._motor_board(mode)
        self.ui.Motor_update.setEnabled(mb is not None)
        self.ui.Motor_action.setEnabled(mb is not None)
        self._apply_mode_plots(mode)

    def _build_var_board_map(self):
        """var de telemetria -> conjunto de placas que a produzem (p/ filtrar
        os gráficos por modo)."""
        self._var_boards = {}

        def add(var, board):
            if var:
                self._var_boards.setdefault(var, set()).add(board)

        for board in ("SIBA", "SAME"):
            cfg = codecs.board_protocol_cfg(self.configs.get(board, {}), board)
            for f in codecs.telemetry_layout(cfg):
                add(f.get("name"), board)
        spmm = self.eth.registry.get("SPMM")
        if spmm:
            for f in spmm.raw_yaml.get("telemetry_fields", []):
                add(f.get("name"), "SPMM")
        # A SPMM também mede rotação/torque:
        for v in ("rpm", "omega", "te", "iq"):
            add(v, "SPMM")

    def _apply_mode_plots(self, mode):
        """Mostra só os gráficos cujas variáveis pertencem às placas do modo."""
        if not hasattr(self, "_var_boards"):
            self._build_var_board_map()
        boards = set(self._boards_in_mode(mode))
        for widget_id, info in self.plot_config.get("plots_mapping", {}).items():
            widget = self.plots.get(widget_id)
            if widget is None:
                continue
            wboards = set()
            for var in (info.get("vars") or {}):
                wboards |= self._var_boards.get(var, set())
            # visível se alguma variável é de uma placa do modo (ou sem vínculo)
            widget.setVisible((not wboards) or bool(wboards & boards))

    # ================================================================== #
    def _build_menus(self):
        """Barra de menus em português com Motor, Bateria, Células de Carga e
        Gerenciador de Placas como menus de TOPO (fora do antigo 'Config')."""
        mb = self.ui.menubar

        # Ações criadas em código.
        self.action_board_manager = QAction("Abrir Gerenciador", self)
        self.action_board_manager.triggered.connect(self.open_board_manager)
        self.action_estimate_smo = QAction("Estimar Ganhos (SMO)", self)
        self.action_estimate_smo.triggered.connect(self.open_estimation)
        self.action_estimate_helice = QAction("Estimar Coeficiente da Hélice", self)
        self.action_estimate_helice.triggered.connect(self.open_helice)
        self.action_calib_load_cell = QAction("Calibrar Célula de Carga", self)
        self.action_calib_load_cell.triggered.connect(self.open_load_cell_calib)
        self.action_spmm_ensaio = QAction("Ensaios SPMM (B, τc, J)", self)
        self.action_spmm_ensaio.triggered.connect(self.open_spmm_ensaio)

        # Traduz as ações existentes do .ui para português.
        for attr, txt in [("actionLoad", "Carregar"), ("actionCreate", "Criar"),
                          ("actionLoad_2", "Carregar"), ("actionCreate_2", "Criar"),
                          ("actionLoadCells", "Configurar Células")]:
            act = getattr(self.ui, attr, None)
            if act is not None:
                act.setText(txt)

        # Desanexa Motor/Bateria do antigo 'Config' ANTES de removê-lo — senão
        # eles são deletados junto (são filhos do menuConfig no .ui). Depois
        # removemos os menus de topo com removeAction (NÃO clear(), que deleta).
        cfg = getattr(self.ui, "menuConfig", None)
        if cfg is not None:
            for m in (self.ui.menuMotor, getattr(self.ui, "menuBattery", None)):
                if m is not None:
                    cfg.removeAction(m.menuAction())
        for act in list(mb.actions()):
            mb.removeAction(act)

        # --- Motor (topo) ---
        self.ui.menuMotor.setTitle("Motor")
        self.ui.menuMotor.addSeparator()
        self.ui.menuMotor.addAction(self.action_estimate_smo)
        self.ui.menuMotor.addAction(self.action_estimate_helice)
        self.ui.menuMotor.addAction(self.action_spmm_ensaio)
        mb.addMenu(self.ui.menuMotor)

        # --- Bateria (topo) ---
        if getattr(self.ui, "menuBattery", None) is not None:
            self.ui.menuBattery.setTitle("Bateria")
            mb.addMenu(self.ui.menuBattery)

        # --- Células de Carga (topo) ---
        # Cria o QMenu com o menubar como PARENT (posse em C++, não é coletado
        # pelo GC) e guarda a referência — addMenu(str) tem posse ambígua e o
        # menu somia da barra.
        self.menu_cells = QMenu("Células de Carga", mb)
        self.menu_cells.addAction(self.action_calib_load_cell)
        if getattr(self.ui, "actionLoadCells", None) is not None:
            self.menu_cells.addAction(self.ui.actionLoadCells)
        mb.addMenu(self.menu_cells)

        # --- Gerenciador de Placas (topo) ---
        self.menu_boards = QMenu("Gerenciador de Placas", mb)
        self.menu_boards.addAction(self.action_board_manager)
        mb.addMenu(self.menu_boards)

        # --- Ajuda (topo, mantém tema/infos) ---
        if getattr(self.ui, "menuHelp", None) is not None:
            self.ui.menuHelp.setTitle("Ajuda")
            mb.addMenu(self.ui.menuHelp)

    @Slot(str, bytes, tuple)
    def process_network_data(self, protocol, data, addr):
        """Recebe pacotes reais da Ethernet."""
        try:
            # Placas sem struct TELEMETRY_LOG (ex.: SPMM) chegam decodificadas
            # por on_decoded_sample — aqui só tratamos SIBA/SAME.
            if protocol not in self.configs:
                return
            # Acessa a estrutura correta no YAML (structs -> TELEMETRY_LOG)
            cfg = self.configs[protocol]
            if protocol.lower() in cfg: cfg = cfg[protocol.lower()]
            
            log_config = cfg.get("structs", {}).get("TELEMETRY_LOG", [])
            if not log_config: return

            fmt = "<" + "".join(TYPE_MAP.get(f['type'], 'f') for f in log_config)
            expected_size = struct.calcsize(fmt)
            
            if len(data) != expected_size:
                # Silencioso para não inundar o terminal, mas evita o crash
                return

            values = struct.unpack(fmt, data)
            decoded = {field['name']: val for field, val in zip(log_config, values)}
            
            # Agora que o terminal é thread-safe, podemos chamar isso com segurança
            if hasattr(self, 'terminal'):
                self.terminal.log_board_data(protocol, data, decoded)

            self._update_internal_buffers(decoded)
        except Exception as e:
            print(f"Erro no processamento de rede [{protocol}]: {e}")

    @Slot(str, dict)
    def on_decoded_sample(self, protocol, decoded):
        """Recebe amostras JÁ decodificadas de links que decodificam no
        transporte (ex.: SPMM/TelnetLink). Alimenta os mesmos buffers/gráficos
        que a telemetria struct de SIBA/SAME."""
        try:
            # Gancho para os ensaios da SPMM: recebe as amostras cruas
            # (com t = n·Ts em decoded['timestamp']) sem passar pela base de
            # tempo do PC dos buffers. Usado pelo SPMMEnsaioDialog (coast-down).
            sink = getattr(self, "_spmm_sample_sink", None)
            if sink is not None and protocol == "SPMM":
                sink(decoded)
            if hasattr(self, 'terminal'):
                self.terminal.log_board_data(protocol, b"", decoded)
            self._update_internal_buffers(decoded)
        except Exception as e:
            print(f"Erro ao processar amostra decodificada [{protocol}]: {e}")

    def get_recent_average(self, var_name, n=10):
        """Média das últimas `n` medições válidas (não-NaN) de uma variável de
        telemetria — usado pela calibração da célula de carga (HX711)."""
        buf = self.value_buffers.get(var_name)
        if buf is None or self.ptr == 0:
            return None
        count = min(n, self.ptr, self.max_ram)
        idxs = [(self.ptr - 1 - k) % self.max_ram for k in range(count)]
        vals = buf[idxs]
        vals = vals[~np.isnan(vals)]
        if len(vals) == 0:
            return None
        return float(np.mean(vals))

    def _update_internal_buffers(self, decoded_data):
        idx = self.ptr % self.max_ram

        # Base de tempo = relógio do PC (Python), não o timestamp do firmware.
        # Marca t=0 na primeira amostra; grava segundos decorridos. É esse tempo
        # que vai para o CSV exportado (usado depois pela identificação da hélice).
        if getattr(self, "_t0", None) is None:
            self._t0 = time.time()
        self.time_buffer[idx] = time.time() - self._t0
        
        for var_name, val in decoded_data.items():
            if var_name in self.value_buffers:
                self.value_buffers[var_name][idx] = val
        
        self.ptr += 1
        if self.ptr > 0 and self.ptr % self.max_ram == 0:
            self._flush_to_swap()

    def _flush_to_swap(self):
        data_to_save = np.column_stack([self.time_buffer] + [self.value_buffers[v] for v in self.variable_order])
        with open(self.swap_path, "ab") as f:
            f.write(data_to_save.astype(np.float32).tobytes())
        self.points_in_swap += self.max_ram
        print(f"Swap: {self.max_ram} pontos movidos para o disco. Total em swap: {self.points_in_swap}")

    def update_plots_ui(self):
        if self.ptr == 0: return
        
        range_slice = slice(0, self.ptr) if self.ptr < self.max_ram else slice(None)
        x = self.time_buffer[range_slice]
        
        if self.ptr >= self.max_ram:
            split = self.ptr % self.max_ram
            x = np.concatenate([self.time_buffer[split:], self.time_buffer[:split]])

        for widget_id, curve_data in self.curves.items():
            for var_name, curve in curve_data:
                y = self.value_buffers[var_name][range_slice]
                if self.ptr >= self.max_ram:
                    y = np.concatenate([self.value_buffers[var_name][split:], self.value_buffers[var_name][:split]])
                curve.setData(x, y)

    @Slot()
    def on_toggle_network(self):
        if not self.eth.running:
            boards = self._boards_in_mode(getattr(self, "op_mode", "SPMM"))
            print(f"Conectando placas do modo {self.op_mode}: {boards}")
            self.on_clear_all()
            try:
                self.eth.start()
                for b in boards:
                    self.eth.start_link(b)
            except Exception as e:
                print(f"Erro de rede: {e}")
            self.ui.pushButton_2.setText("Desconectar")
        else:
            self.eth.stop()
            self._spmm_armed = False
            self.ui.pushButton_2.setText("Conectar")
            print("Rede parada.")

    @Slot()
    def on_clear_all(self):
        print("Limpando todos os buffers e dados de swap...")
        self.ptr = 0
        self._t0 = None  # reinicia a base de tempo do PC
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
                writer.writerow(["Timestamp"] + self.variable_order)
                # Lê o que está no swap
                num_cols = len(self.variable_order) + 1
                if os.path.exists(self.swap_path) and self.points_in_swap > 0:
                    disk_data = np.fromfile(self.swap_path, dtype=np.float32).reshape(-1, num_cols)
                    writer.writerows(disk_data)
                # Adiciona o resto da RAM
                ram_idx = self.ptr % self.max_ram
                if ram_idx > 0:
                    current_ram = np.column_stack([self.time_buffer[:ram_idx]] + [self.value_buffers[v][:ram_idx] for v in self.variable_order])
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
        if hasattr(self, 'eth'):
            self.eth.stop()
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
    def open_board_manager(self):
        """Abre o Gerenciador de Placas (popout)."""
        print("Abrindo Gerenciador de Placas...")
        if not getattr(self, "_board_mgr", None):
            self._board_mgr = BoardManagerDialog(self.eth, self)
        self._board_mgr.show()
        self._board_mgr.raise_()
        self._board_mgr.activateWindow()

    @Slot()
    def open_estimation(self):
        """Abre o popout de estimação dos ganhos do SMO (menu Motor)."""
        print("Abrindo Estimador de Ganhos (SMO)...")
        if not getattr(self, "_estim_dlg", None):
            self._estim_dlg = EstimationDialog(self.config_dir, self.same_yaml_path, self)
        self._estim_dlg.show()
        self._estim_dlg.raise_()
        self._estim_dlg.activateWindow()

    @Slot()
    def open_load_cell_calib(self):
        """Abre o popout de calibração das células de carga (HX711)."""
        print("Abrindo Calibração de Célula de Carga...")
        if not getattr(self, "_loadcell_dlg", None):
            self._loadcell_dlg = LoadCellDialog(self, self)
        self._loadcell_dlg.show()
        self._loadcell_dlg.raise_()
        self._loadcell_dlg.activateWindow()

    @Slot()
    def open_spmm_ensaio(self):
        """Abre o popout de ensaios da SPMM (atrito B/τc e inércia J)."""
        print("Abrindo Ensaios SPMM...")
        if "SPMM" not in self.eth.workers:
            self.eth.start_link("SPMM")
        if not getattr(self, "_spmm_ensaio_dlg", None):
            self._spmm_ensaio_dlg = SPMMEnsaioDialog(self, self)
        self._spmm_ensaio_dlg.show()
        self._spmm_ensaio_dlg.raise_()
        self._spmm_ensaio_dlg.activateWindow()

    @Slot()
    def open_helice(self):
        """Abre o popout de estimação do coeficiente da hélice (kQ via CSV)."""
        print("Abrindo Estimador de Coeficiente da Hélice...")
        if not getattr(self, "_helice_dlg", None):
            self._helice_dlg = HeliceDialog(self.config_dir, self.same_yaml_path, self)
        self._helice_dlg.show()
        self._helice_dlg.raise_()
        self._helice_dlg.activateWindow()

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
        """Só atualiza o estado/rótulo enquanto arrasta — o envio é no
        sliderReleased / 'Atualizar (Motor)' (evita inundar a rede)."""
        self.prefs["motor_val"] = val
        board = self._motor_board(self.op_mode)
        if board == "SPMM":
            us = 1000 + int(val * 10)   # 0%->1000us, 100%->2000us
            self.ui.Motor_update.setText(f"Atualizar (throttle {us}us)")
        else:
            self.ui.Motor_update.setText(f"Atualizar (Motor {val}%)")
        self.save_user_prefs()

    @Slot()
    def apply_throttle(self):
        """Envia o throttle atual para a placa de motor do modo de operação.

        SPMM: arma (uma vez) e manda `throttle <us>` (1000–2000).
        SAME: manda um CMD FUNC_START com value = duty (0–1)."""
        board = self._motor_board(self.op_mode)
        val = self.ui.Motor_action.value()
        if board == "SPMM":
            if "SPMM" not in self.eth.workers:
                self.eth.start_link("SPMM")
            if not getattr(self, "_spmm_armed", False):
                self.eth.send_command("SPMM", "arm")
                self._spmm_armed = True
            us = 1000 + int(val * 10)
            self.eth.send_command("SPMM", f"throttle {us}")
            print(f"[Motor] SPMM throttle {us}us ({val}%)")
        elif board == "SAME":
            if "SAME" not in self.eth.workers:
                self.eth.start_link("SAME")
            self._send_same_duty(val / 100.0)
            print(f"[Motor] SAME duty {val}%")
        else:
            print("[Motor] Modo SIBA não controla motor.")

    @Slot()
    def motor_stop(self):
        """Para o motor com segurança conforme o modo e zera o slider."""
        board = self._motor_board(self.op_mode)
        if board == "SPMM":
            self.eth.send_command("SPMM", "throttle 1000")
            self.eth.send_command("SPMM", "disarm")
            self._spmm_armed = False
            print("[Motor] SPMM parado (throttle 1000 + disarm).")
        elif board == "SAME":
            # FUNC_STOP no alvo padrão.
            self._send_same_cmd("FUNC_STOP", 0.0)
            print("[Motor] SAME parado (stop).")
        self.ui.Motor_action.setValue(0)

    def _send_same_cmd(self, func_name, value):
        """Monta e envia um CMD para a SAME usando os enums do SAME.yaml."""
        enums = codecs.board_protocol_cfg(self.configs["SAME"], "SAME").get("enums", {})

        def idx(cat, name, default=0):
            lst = enums.get(cat, [])
            return lst.index(name) if name in lst else default

        cmd = codecs.encode_cmd(
            idx("func_type", func_name, 0),
            idx("instruct_type", "INSTR_NONE", 2),
            idx("target_type", "TARGET_DEFAULT", 0),
            value, 0)
        self.eth.send_command("SAME", cmd)

    def _send_same_duty(self, duty):
        self._send_same_cmd("FUNC_START", duty)

    def start_mock_with_target(self, target, quiet=False):
        """Configura e inicia o mock baseado no argumento do terminal."""
        args = ["--no-comment"] if quiet else []
        
        # Ativa os mocks de hardware através do terminal
        if target == "all" or "siba" in target.lower():
            self.terminal._cmd_siba_mock_start(args)
        if target == "all" or "same" in target.lower():
            self.terminal._cmd_same_mock_start(args)

    def clear_graph_buffers(self, target_str):
        """Limpa buffers de memória para um gráfico específico ou todos."""
        self.on_clear_all() # Por simplicidade, clear all sempre