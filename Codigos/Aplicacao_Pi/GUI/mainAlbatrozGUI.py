import numpy as np
import pyqtgraph as pg
import threading
import time
import os

# Mitiga o crash em QFontEngineFT::loadGlyphSet (segfault na engine de fontes do
# Qt6/FreeType renderizando os rótulos ROTACIONADOS dos eixos em alta taxa):
# - antialias=False: menos glifos com transformação (o caminho que quebra);
# - useOpenGL=False: força rasterização (evita o cache de glifos via OpenGL).
pg.setConfigOptions(antialias=False, useOpenGL=False)
import yaml
import tempfile
import csv
import struct
import sys
import shutil

from PySide6.QtWidgets import (QMainWindow, QMessageBox, QFileDialog, QInputDialog,
                               QMenu, QComboBox, QLabel, QWidget, QHBoxLayout,
                               QGridLayout)
from PySide6.QtUiTools import QUiLoader
from PySide6.QtCore import QFile, Slot, QTimer, QThread, Signal, Qt
from PySide6.QtGui import QAction

from Communication_Data import codecs

# Importações de hardware e componentes customizados
from Communication_Data.Ethernet import EthernetManager
from Communication_Data.discovery import DiscoveryService
from GUI.SAMEConfigDialog import SAMEConfigDialog
from GUI.TinyTerminal import TinyTerminal
from GUI.BoardManagerDialog import BoardManagerDialog
from GUI.EstimationDialog import EstimationDialog
from GUI.HeliceDialog import HeliceDialog
from GUI.LoadCellDialog import LoadCellDialog
from GUI.SPMMEnsaioDialog import SPMMEnsaioDialog
from GUI.ErrorPanelDialog import ErrorPanelDialog
from GUI.SensorConfigDialog import SensorConfigDialog
from GUI.InfoDialog import InfoDialog

# Mapeamento de tipos para o struct.unpack (C -> Python)
TYPE_MAP = {
    "float": "f",
    "uint32_t": "I",
    "log_status": "I",
    "uint8_t": "B"
}

class AlbatrozGUI(QMainWindow):
    # Resultado do ping disparado pelo botão Conectar — emitido da thread de
    # sondagem, consumido na thread da UI (mesmo padrão do Gerenciador).
    _probe_done = Signal(dict)

    def __init__(self):
        super().__init__()
        
        # 1. Pastas e Caminhos
        if getattr(sys, "frozen", False):
            # Congelado (PyInstaller): __file__ apontaria pro _MEIPASS (pasta
            # temp apagada ao fechar) — usa a pasta do executável de verdade,
            # senão perfis de motor/SAME.yaml/preferências não persistem.
            self.root_dir = os.path.dirname(os.path.abspath(sys.executable))
            self.script_dir = os.path.join(self.root_dir, "GUI")
        else:
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
        # Lock que serializa TODO acesso aos buffers de plot: a thread de rede
        # ESCREVE (via _update_internal_buffers) enquanto a thread de UI LÊ
        # (update_plots_ui/pyqtgraph). Sem isso, o acesso concorrente aos mesmos
        # arrays numpy corrompe memória e causa Falha de segmentação (visto em
        # alta taxa, ex.: SPMM a 200 Hz no ensaio).
        self._buf_lock = threading.Lock()
        self.time_buffer = np.full(self.max_ram, np.nan, dtype=np.float32)
        self.value_buffers = {}
        self.variable_order = [] # Mantém a ordem para o CSV/Swap
        
        self.temp_swap = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
        self.swap_path = self.temp_swap.name
        self.points_in_swap = 0
        print(f"Arquivo de Swap ativo: {self.swap_path}")

        # --- Registro sob demanda (botão Iniciar/Encerrar Registro) ---------
        # A telemetria é gravada num CSV temporário AO VIVO (mesma ideia do swap
        # dos gráficos); ao encerrar, o usuário escolhe onde salvar o arquivo
        # final. Estado protegido por self._buf_lock (thread de rede escreve as
        # linhas; a thread de UI abre/fecha no start/stop).
        self.recording = False
        self._record_fp = None       # handle do CSV temporário aberto
        self._record_path = None     # caminho do CSV temporário
        self._record_cols = None     # colunas fixadas na 1ª amostra gravada
        self._record_count = 0       # nº de amostras já gravadas na sessão

        # 4. Inicializa Gerenciador de Rede (Agora com o callback central process_network_data)
        self.eth = EthernetManager(
            self.siba_yaml_path,
            self.same_yaml_path,
            handle_callback=self.process_network_data,
            sample_callback=self.on_decoded_sample  # amostras já decodificadas (ex.: SPMM)
        )

        # Ping antes de conectar + controle do aviso de "supervisório pronto"
        # (uma vez por conexão, no primeiro pacote de cada placa).
        self.discovery = DiscoveryService(self.eth)
        self._ready_announced = set()
        self._probe_done.connect(self._connect_probed)

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

        # 7. Timer de UI — 100ms (10 Hz). Taxa mais baixa reduz a re-renderização
        # dos rótulos de eixo (pressão na engine de fontes que causava o crash),
        # e ainda é suave para monitoramento.
        self.ui_timer = QTimer()
        self.ui_timer.timeout.connect(self.update_plots_ui)
        self.ui_timer.start(100)
        print("Sistema de atualização de gráficos e timer de UI prontos.")

        # --- Erros/alarmes da telemetria (error_code, bitfield do Security_Manager) ---
        # A thread de rede só ATUALIZA estes dois; o texto do rótulo é escrito na
        # main thread (update_plots_ui). Ver Communication_Data/errors.py.
        self._last_error_code = {}          # protocol -> último error_code visto
        self._error_status_text = "Erros: aguardando telemetria…"
        self._error_status_detail = ""
        self._error_status_lbl = QLabel("Erros: aguardando telemetria…")
        self._error_status_lbl.setStyleSheet("color: #2ecc40; font-weight: bold; padding: 0 8px;")
        # Clicar no rótulo abre o painel completo de erros (lâmpadas).
        self._error_status_lbl.setCursor(Qt.PointingHandCursor)
        self._error_status_lbl.setToolTip("Clique para ver o painel de erros")
        self._error_status_lbl.mousePressEvent = lambda _ev: self.open_error_panel()
        self.statusBar().addPermanentWidget(self._error_status_lbl)
        # 8. Terminal fica DISPONÍVEL mas NÃO abre no startup — o usuário abre
        #    pelo botão "Tiny_Terminal" quando quiser (menos poluição inicial).
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
        # Botão de registro (antigo "Salvar CSV") vira um toggle:
        # verde "Iniciar Registro" → vermelho "Encerrar Registro".
        self.ui.Save_CSV.clicked.connect(self.on_toggle_recording)
        self._update_record_button()
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
        self.op_mode = "SIBA+SAME"   # modo padrão
        # Guarda em self (posse) — widget local seria coletado pelo GC e sumiria.
        self._mode_box = QWidget(self.ui.menubar)
        lay = QHBoxLayout(self._mode_box); lay.setContentsMargins(4, 0, 8, 0)
        lay.addWidget(QLabel("Modo:"))
        self.cmb_mode = QComboBox()
        self.cmb_mode.addItems(["SIBA+SAME", "SIBA", "SAME", "SPMM"])
        self.cmb_mode.currentTextChanged.connect(self.on_mode_changed)
        lay.addWidget(self.cmb_mode)
        self.ui.menubar.setCornerWidget(self._mode_box, Qt.TopRightCorner)

    def _boards_in_mode(self, mode):
        return {"SPMM": ["SPMM"], "SIBA": ["SIBA"], "SAME": ["SAME"],
                "SIBA+SAME": ["SIBA", "SAME"]}.get(mode, [])

    def _motor_board(self, mode):
        """Placa que ACIONA o ESC no modo atual.

        - SPMM: a bancada comanda o throttle do ESC (comandos de texto).
        - SIBA: a SIBA controla o ESC (inclusive no modo SIBA+SAME).
        - SAME: NÃO aciona — a SAME é só LEITURA das correntes/tensões a,b,c
          do ESC + estimativa (SMO). Por isso 'SAME' sozinho não tem throttle.
        """
        if mode == "SPMM":
            return "SPMM"
        if mode in ("SIBA", "SIBA+SAME"):
            return "SIBA"
        return None

    @Slot(str)
    def on_mode_changed(self, mode):
        self.op_mode = mode
        print(f"Modo de Operação: {mode}")
        # Sobe os links das placas do modo (sob demanda).
        for b in self._boards_in_mode(mode):
            self.eth.start_link(b)
        self._refresh_motor_controls()
        self._apply_mode_plots(mode)

    def _refresh_motor_controls(self):
        """Habilita slider/Atualizar só se o modo aciona um ESC E não há ensaio
        travando os controles."""
        en = (self._motor_board(self.op_mode) is not None) and not getattr(self, "_motor_locked", False)
        self.ui.Motor_action.setEnabled(en)
        self.ui.Motor_update.setEnabled(en)

    def set_motor_locked(self, locked):
        """Trava/destrava os controles de motor da tela principal (usado pelos
        ensaios SPMM para não conflitar com os comandos do ensaio)."""
        self._motor_locked = bool(locked)
        self._refresh_motor_controls()

    def _require_board_mode(self, mode, boards):
        """Troca para `mode` (mostra os plots certos) e AVISA se as `boards` não
        estão recebendo dados. Retorna True se todas estão conectadas."""
        if getattr(self, "cmb_mode", None) is not None and self.op_mode != mode:
            self.cmb_mode.setCurrentText(mode)   # troca modo + sobe links + plots
        for b in boards:
            self.eth.start_link(b)
        # Dá um instante para a telemetria (contínua) chegar.
        from PySide6.QtCore import QCoreApplication
        t0 = time.time()
        while time.time() - t0 < 0.6:
            QCoreApplication.processEvents()
            time.sleep(0.02)
        faltando = [b for b in boards
                    if not (self.eth.workers.get(b) and self.eth.workers[b].is_alive_link())]
        if faltando:
            QMessageBox.warning(self, "Placa não conectada",
                f"A(s) placa(s) {', '.join(faltando)} não está(ão) recebendo dados.\n\n"
                f"Conecte/detecte no Gerenciador de Placas — sem isso o ensaio "
                f"não recebe amostras.")
        return not faltando

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
        """REORDENA os gráficos do modo: os relevantes (variáveis das placas do
        modo) vão para o topo (visíveis); os demais/placeholders vão para o fim
        (ocultos). Refaz o layout do grid de 3 colunas."""
        if not hasattr(self, "_var_boards"):
            self._build_var_board_map()
        boards = set(self._boards_in_mode(mode))
        mapping = self.plot_config.get("plots_mapping", {})

        def is_relevant(widget_id):
            info = mapping.get(widget_id)
            if not info:
                return False   # placeholder sem mapeamento
            wboards = set()
            for var in (info.get("vars") or {}):
                wboards |= self._var_boards.get(var, set())
            return (not wboards) or bool(wboards & boards)

        # Todos os widgets Plot_N do .ui, na ordem numérica.
        all_ids = []
        for i in range(1, 25):
            w = getattr(self.ui, f"Plot_{i}", None)
            if w is not None:
                all_ids.append((f"Plot_{i}", w))

        relevantes = [(wid, w) for wid, w in all_ids if is_relevant(wid)]
        outros = [(wid, w) for wid, w in all_ids if not is_relevant(wid)]

        # Grid REAL dos plots = layout do widget-pai de um Plot (dentro da
        # scroll area). Evita pegar o layout externo por nome errado.
        grid = None
        if all_ids:
            parent = all_ids[0][1].parentWidget()
            lay = parent.layout() if parent is not None else None
            if isinstance(lay, QGridLayout):
                grid = lay
        if grid is None:   # fallback: só visibilidade
            for wid, w in relevantes:
                w.setVisible(True)
            for wid, w in outros:
                w.setVisible(False)
            return

        for _wid, w in all_ids:
            grid.removeWidget(w)

        NC = 3
        cell_w, cell_h = self._plot_cell_size(NC)
        for i, (_wid, w) in enumerate(relevantes):
            w.setVisible(True)
            w.setMinimumSize(cell_w, cell_h)
            w.setMaximumHeight(cell_h)   # NÃO esticar além da célula
            grid.addWidget(w, i // NC, i % NC)
        base = (len(relevantes) + NC - 1) // NC
        for j, (_wid, w) in enumerate(outros):
            w.setVisible(False)
            grid.addWidget(w, base + j // NC, j % NC)
        # Colunas com stretch igual (espaços iguais); linha-sentinela lá embaixo
        # absorve o espaço vertical extra p/ os gráficos NÃO esticarem.
        for c in range(NC):
            grid.setColumnStretch(c, 1)
        for r in range(grid.rowCount() + 1):
            grid.setRowStretch(r, 0)
        grid.setRowStretch(999, 1)

    def _plot_cell_size(self, ncols=3):
        """Tamanho mínimo/máximo de cada gráfico baseado na ÁREA DE ROLAGEM
        (Graficos_Scroll), não na tela: largura = viewport/ncols (cabem `ncols`
        por linha) e altura = viewport/2 (≈2 linhas visíveis; o resto rola).
        A altura da viewport é o teto por gráfico."""
        sa = getattr(self.ui, "Graficos_Scroll", None)
        vw = vh = 0
        if sa is not None and sa.viewport() is not None:
            vw = sa.viewport().width()
            vh = sa.viewport().height()
        if vw < 200 or vh < 150:   # ainda não dimensionado (antes de show/resize)
            from PySide6.QtWidgets import QApplication
            scr = QApplication.primaryScreen()
            geo = scr.availableGeometry() if scr else None
            vw = geo.width() if geo else 1280
            vh = int((geo.height() if geo else 800) * 0.55)
        cell_w = max(280, vw // ncols - 18)
        cell_h = max(150, vh // 2 - 12)
        return cell_w, cell_h

    def resizeEvent(self, event):
        """Ao redimensionar a janela, reajusta a altura/largura dos gráficos
        visíveis à nova viewport da scroll (sem refazer o layout todo)."""
        super().resizeEvent(event)
        if not hasattr(self, "op_mode"):
            return
        cell_w, cell_h = self._plot_cell_size(3)
        for i in range(1, 25):
            w = getattr(self.ui, f"Plot_{i}", None)
            if w is not None and not w.isHidden():
                w.setMinimumSize(cell_w, cell_h)
                w.setMaximumHeight(cell_h)

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

        # --- Motor (topo) — Carregar/Criar (a fonte SIBA+SAME do ensaio foi para
        #     o diálogo de Ensaios SPMM, onde se escolhe a fonte de dados). ---
        self.ui.menuMotor.setTitle("Motor")
        mb.addMenu(self.ui.menuMotor)

        # --- Estimativas (topo, menu próprio) ---
        self.menu_estim = QMenu("Estimativas", mb)
        self.menu_estim.addAction(self.action_estimate_smo)
        self.menu_estim.addAction(self.action_estimate_helice)
        self.menu_estim.addAction(self.action_spmm_ensaio)
        mb.addMenu(self.menu_estim)

        # --- Bateria (topo) ---
        if getattr(self.ui, "menuBattery", None) is not None:
            self.ui.menuBattery.setTitle("Bateria")
            mb.addMenu(self.ui.menuBattery)

        # --- Células de Carga (topo) — só a calibração ---
        # QMenu com o menubar como PARENT (posse em C++) e ref em self (senão
        # o GC coleta e some da barra).
        self.menu_cells = QMenu("Células de Carga", mb)
        self.menu_cells.addAction(self.action_calib_load_cell)
        mb.addMenu(self.menu_cells)

        # --- Opções (topo) — agrupa Configuração de Sensores, Gerenciador de
        #     Placas, Painel de Erros e a Ajuda num único menu. ---
        self.action_sensor_config = QAction("Configuração de Sensores", self)
        self.action_sensor_config.triggered.connect(self.open_sensor_config)
        self.action_error_panel = QAction("Painel de Erros e Alarmes", self)
        self.action_error_panel.triggered.connect(self.open_error_panel)
        self.action_info = QAction("Guia do Usuário (Sobre)", self)
        self.action_info.triggered.connect(self.open_info)

        self.menu_options = QMenu("Opções", mb)
        self.menu_options.addAction(self.action_sensor_config)
        self.menu_options.addAction(self.action_board_manager)
        self.menu_options.addAction(self.action_error_panel)
        self.menu_options.addSeparator()
        # A Ajuda do .ui (tema/infos) vira SUBMENU de Opções + o guia do usuário.
        help_menu = getattr(self.ui, "menuHelp", None)
        if help_menu is not None:
            help_menu.setTitle("Ajuda")
            help_menu.addSeparator()
            help_menu.addAction(self.action_info)
            self.menu_options.addMenu(help_menu)
        else:
            self.menu_options.addAction(self.action_info)
        mb.addMenu(self.menu_options)

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

            self._announce_ready(protocol)
            self._check_error_code(protocol, decoded)
            self._update_internal_buffers(decoded)
        except Exception as e:
            print(f"Erro no processamento de rede [{protocol}]: {e}")

    def current_error_code(self):
        """error_code agregado (união dos bits) de todas as placas — usado pelo
        rótulo da barra de status. NB: bits iguais têm significado diferente
        entre placas; para o painel detalhado use current_error_codes()."""
        merged = 0
        for c in getattr(self, "_last_error_code", {}).values():
            merged |= int(c)
        return merged

    def current_error_codes(self):
        """Dict {protocolo: error_code} por placa — cada um decodificado com a
        SUA tabela (error_bits do yaml). Usado pelo Painel de Erros."""
        return dict(getattr(self, "_last_error_code", {}))

    def open_error_panel(self):
        """Abre (ou traz à frente) o painel completo de erros com lâmpadas."""
        if not hasattr(self, "_error_panel") or self._error_panel is None:
            self._error_panel = ErrorPanelDialog(self, self)
        self._error_panel.show()
        self._error_panel.raise_()
        self._error_panel.activateWindow()

    def open_sensor_config(self):
        """Abre o diálogo de configuração de sensores (ganho/offset/mín/máx)."""
        if not hasattr(self, "_sensor_cfg_dlg") or self._sensor_cfg_dlg is None:
            self._sensor_cfg_dlg = SensorConfigDialog(self, self)
        self._sensor_cfg_dlg.show()
        self._sensor_cfg_dlg.raise_()
        self._sensor_cfg_dlg.activateWindow()

    def open_info(self):
        """Abre o guia do usuário (Sobre/Ajuda)."""
        if not hasattr(self, "_info_dlg") or self._info_dlg is None:
            self._info_dlg = InfoDialog(self)
        self._info_dlg.show()
        self._info_dlg.raise_()
        self._info_dlg.activateWindow()

    def _check_error_code(self, protocol, decoded):
        """Decodifica o `error_code` (bitfield) da telemetria e reporta as
        transições no terminal (só na borda, como o firmware). Roda na thread de
        rede — usa só `append_log` (thread-safe via signal) e escreve o texto do
        rótulo persistente numa string (atualizada na main thread por
        update_plots_ui). Ver Communication_Data/errors.py."""
        if "error_code" not in decoded:
            return
        try:
            code = int(decoded["error_code"]) & 0xFFFFFFFF
        except (TypeError, ValueError):
            return
        prev = self._last_error_code.get(protocol, 0)
        if code == prev:
            return
        self._last_error_code[protocol] = code

        from Communication_Data import errors
        # Cada placa decodifica com a SUA tabela (bits iguais significam coisas
        # diferentes entre placas) — montada da seção error_bits do yaml da placa.
        errors.ensure_registered(protocol, self.configs.get(protocol, {}))
        if hasattr(self, 'terminal'):
            for _m, msg, grp in errors.decode_error_code(code & ~prev, protocol):   # novos
                self.terminal.append_log(protocol, f"⚠ PROBLEMA ({grp}): {msg}", "#ff4136")
            for _m, msg, grp in errors.decode_error_code(prev & ~code, protocol):   # resolvidos
                self.terminal.append_log(protocol, f"✓ Resolvido ({grp}): {msg}", "#2ecc40")

        # Rótulo persistente = erros ativos de TODAS as placas, cada uma
        # decodificada com a sua tabela (em pt-BR).
        texts, details = [], []
        for proto, c in self._last_error_code.items():
            if not c:
                continue
            errors.ensure_registered(proto, self.configs.get(proto, {}))
            texts.append(f"{proto}: {errors.format_error_code(c, proto)}")
            details.append(f"=== {proto} ===\n{errors.format_error_detail(c, proto)}")
        if not texts:
            self._error_status_text = "✓ Tudo OK"
            self._error_status_detail = "Nenhum erro ativo."
        else:
            self._error_status_text = "⚠ " + " | ".join(texts)
            self._error_status_detail = "\n\n".join(details)

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
            # Throttle do log no terminal (a SPMM streama ~100-200 Hz; logar tudo
            # com appendHtml sobrecarrega a main thread). Buffers seguem em taxa
            # cheia; só o texto do terminal é limitado a ~10 Hz.
            if hasattr(self, 'terminal'):
                now = time.time()
                if now - getattr(self, "_last_sample_log", 0.0) > 0.1:
                    self._last_sample_log = now
                    self.terminal.log_board_data(protocol, b"", decoded)
            self._update_internal_buffers(decoded)
        except Exception as e:
            print(f"Erro ao processar amostra decodificada [{protocol}]: {e}")

    def now_buffer_time(self):
        """Tempo atual na base do buffer/CSV (segundos desde a 1ª amostra).
        Os ensaios usam isto para MARCAR janelas e depois filtrar os dados
        já capturados — sem coleta paralela."""
        t0 = getattr(self, "_t0", None)
        return (time.time() - t0) if t0 is not None else 0.0

    def snapshot_series(self, var_names):
        """Foto (cópias, sob o lock) do histórico em RAM: (t, {var: y}).
        Reaproveita os mesmos buffers do CSV; o ensaio filtra por timestamp."""
        with self._buf_lock:
            ptr = self.ptr
            if ptr == 0:
                return np.array([]), {v: np.array([]) for v in var_names}
            if ptr < self.max_ram:
                t = self.time_buffer[:ptr].copy()
                ys = {v: self.value_buffers[v][:ptr].copy()
                      for v in var_names if v in self.value_buffers}
            else:
                split = ptr % self.max_ram
                t = np.concatenate([self.time_buffer[split:], self.time_buffer[:split]])
                ys = {v: np.concatenate([self.value_buffers[v][split:], self.value_buffers[v][:split]])
                      for v in var_names if v in self.value_buffers}
        return t, ys

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
        # Escrita dos buffers sob o lock (roda na thread de rede) — ver
        # comentário em __init__ (self._buf_lock).
        with self._buf_lock:
            idx = self.ptr % self.max_ram
            # Base de tempo = relógio do PC (Python), não o timestamp do firmware.
            if getattr(self, "_t0", None) is None:
                self._t0 = time.time()
            self.time_buffer[idx] = time.time() - self._t0

            # Sample-and-hold (forward-fill): DUAS placas (SIBA+SAME) partilham
            # ESTE mesmo buffer/ptr/base-de-tempo. Um pacote só traz as variáveis
            # da SUA placa; sem isto, as variáveis da OUTRA placa ficavam NaN
            # neste índice, e o pacote seguinte da outra placa preenchia o índice
            # seguinte — resultado: cada variável ficava real/NaN/real/NaN
            # intercalado, e o pyqtgraph desenha NaN como buraco → as curvas
            # "somem" ao plotar as duas ao mesmo tempo (bug em native_sim).
            # Carregando o último valor conhecido, todo índice tem valor para
            # toda variável e as curvas ficam contínuas (degrau na mais lenta).
            if self.ptr > 0:
                prev = (self.ptr - 1) % self.max_ram
                for buf in self.value_buffers.values():
                    buf[idx] = buf[prev]

            for var_name, val in decoded_data.items():
                if var_name in self.value_buffers:
                    self.value_buffers[var_name][idx] = val

            # Registro ao vivo: grava esta amostra (já com forward-fill) no CSV
            # temporário da sessão. Linha curta a ~10-20 Hz → custo desprezível
            # mesmo sob o lock.
            if self.recording and self._record_fp is not None:
                self._record_write_row_locked(idx)

            self.ptr += 1
            snapshot = None
            if self.ptr > 0 and self.ptr % self.max_ram == 0:
                # Tira a FOTO (cópia) sob o lock, mas grava no disco FORA do lock
                # (o I/O é lento e não pode segurar o lock — isso travava a UI/
                # pyqtgraph no meio do ensaio, exatamente quando dava swap).
                snapshot = np.column_stack(
                    [self.time_buffer.copy()] +
                    [self.value_buffers[v].copy() for v in self.variable_order]
                ).astype(np.float32)

        # I/O de disco fora do lock:
        if snapshot is not None:
            self._write_swap(snapshot)

    def _write_swap(self, snapshot):
        try:
            with open(self.swap_path, "ab") as f:
                f.write(snapshot.tobytes())
            self.points_in_swap += self.max_ram
            print(f"Swap: {self.max_ram} pontos no disco. Total em swap: {self.points_in_swap}")
        except Exception as e:
            print(f"Erro no swap: {e}")

    #: nº de pontos exibidos por gráfico (janela rolante). Limita o trabalho do
    #: pyqtgraph — o histórico completo continua indo p/ o swap/CSV.
    PLOT_WINDOW = 2000

    def update_plots_ui(self):
        # Slot do QTimer — nunca deixar uma exceção escapar para o C++ do Qt
        # (boa prática PyQt: exceção em slot pode corromper estado/crashar).
        try:
            self._refresh_error_label()   # main thread: escreve o rótulo de erros
        except Exception as e:
            print(f"[error_label] erro ignorado: {e}")
        try:
            self._update_plots_ui_impl()
        except Exception as e:
            print(f"[update_plots_ui] erro ignorado: {e}")

    def _refresh_error_label(self):
        """Atualiza o rótulo persistente de erros na barra de status (main thread)."""
        lbl = getattr(self, "_error_status_lbl", None)
        if lbl is None:
            return
        txt = getattr(self, "_error_status_text", "Erros: —")
        if lbl.text() != txt:
            lbl.setText(txt)
            ok = ("Tudo OK" in txt) or txt.endswith("—")
            lbl.setStyleSheet("color: %s; font-weight: bold; padding: 0 8px;"
                              % ("#2ecc40" if ok else "#ff4136"))
            # detalhe agrupado por área + dica de clique ao passar o mouse
            det = getattr(self, "_error_status_detail", "")
            lbl.setToolTip((det + "\n\n" if det else "") + "(clique para abrir o painel de erros)")

    def _update_plots_ui_impl(self):
        if self.ptr == 0: return

        # 1) Sob o lock, tira uma FOTO (cópias) só dos ÚLTIMOS PLOT_WINDOW pontos
        #    (em ordem cronológica), tratando o buffer circular. Assim o pyqtgraph
        #    nunca recebe array gigante nem lê os arrays vivos.
        with self._buf_lock:
            ptr, mr = self.ptr, self.max_ram
            win = min(self.PLOT_WINDOW, ptr, mr)
            if ptr <= mr:
                sl = slice(ptr - win, ptr)
                x = self.time_buffer[sl].copy()
                ys = {v: b[sl].copy() for v, b in self.value_buffers.items()}
            else:
                start = (ptr - win) % mr
                end = start + win
                if end <= mr:
                    x = self.time_buffer[start:end].copy()
                    ys = {v: b[start:end].copy() for v, b in self.value_buffers.items()}
                else:                       # a janela cruza a borda do circular
                    tail = end - mr
                    x = np.concatenate([self.time_buffer[start:], self.time_buffer[:tail]])
                    ys = {v: np.concatenate([b[start:], b[:tail]])
                          for v, b in self.value_buffers.items()}

        # 2) Fora do lock, plota a partir das cópias.
        for widget_id, curve_data in self.curves.items():
            for var_name, curve in curve_data:
                y = ys.get(var_name)
                if y is not None:
                    curve.setData(x, y)

    @Slot()
    def on_toggle_network(self):
        """Conecta/desconecta. Ao conectar, PINGA antes — mesma rotina do
        Gerenciador de Placas — e só sobe o link das placas que responderam."""
        if self.eth.running:
            self.eth.stop()
            self._spmm_armed = False
            self._ready_announced.clear()
            self.ui.pushButton_2.setText("Conectar")
            print("Rede parada.")
            return

        boards = self._boards_in_mode(getattr(self, "op_mode", "SPMM"))
        print(f"Procurando placas do modo {self.op_mode}: {boards}")
        self.on_clear_all()
        self._ready_announced.clear()
        self.ui.pushButton_2.setEnabled(False)
        self.ui.pushButton_2.setText("Procurando…")

        def work():
            found = {}
            for b in boards:
                try:
                    found[b] = self.discovery.probe(b)
                except Exception as e:
                    print(f"[NET] Falha ao sondar {b}: {e}")
                    found[b] = False
            self._probe_done.emit(found)

        threading.Thread(target=work, name="ProbeOnConnect", daemon=True).start()

    @Slot(dict)
    def _connect_probed(self, found):
        """Resultado do ping (thread da UI): conecta quem respondeu."""
        self.ui.pushButton_2.setEnabled(True)
        presentes = [b for b, ok in found.items() if ok]
        ausentes = [b for b, ok in found.items() if not ok]

        if ausentes:
            print(f"[NET] Sem resposta: {', '.join(ausentes)}.")
        if not presentes:
            self.ui.pushButton_2.setText("Conectar")
            QMessageBox.warning(
                self, "Nenhuma placa encontrada",
                "Nenhuma das placas do modo atual respondeu ao ping.\n\n"
                "Confira o cabo e o IP/porta no Gerenciador de Placas, ou "
                "suba o firmware em simulação nativa.")
            return

        print(f"[NET] Respondeu(ram): {', '.join(presentes)} — conectando.")
        try:
            self.eth.start()
            for b in presentes:
                self.eth.start_link(b)
        except Exception as e:
            print(f"Erro de rede: {e}")
        self.ui.pushButton_2.setText("Desconectar")

    def _announce_ready(self, board):
        """Declara à placa que o supervisório está pronto, uma vez por conexão.

        É uma das DUAS condições que a SIBA exige para armar a bancada (a
        outra é o botão de emergência liberado) — por isso vai sozinho,
        assim que o primeiro pacote de telemetria chega, sem botão na tela.
        Só a SIBA trata este comando: é ela que tem a cadeia de potência.
        """
        if board != "SIBA" or board in self._ready_announced:
            return
        self._ready_announced.add(board)
        print(f"[NET] {board}: primeiro log recebido — avisando que o "
              f"supervisorio esta pronto.")
        self._send_board_cmd(board, "FUNC_START", 1.0,
                             instr="INSTR_WRITE", status="STATUS_OK")

    @Slot()
    def on_clear_all(self):
        print("Limpando todos os buffers e dados de swap...")
        with self._buf_lock:
            self.ptr = 0
            self._t0 = None  # reinicia a base de tempo do PC
            self.points_in_swap = 0
            self.time_buffer.fill(np.nan)
            for b in self.value_buffers.values(): b.fill(np.nan)
        if os.path.exists(self.swap_path):
            open(self.swap_path, 'wb').close()
        # Limpa as CURVAS de fato — senão continuam mostrando os dados velhos
        # (update_plots_ui retorna cedo com ptr=0 e não redesenha vazio).
        for curve_data in self.curves.values():
            for _var, curve in curve_data:
                curve.setData([], [])

    @Slot()
    # ============================ REGISTRO ================================ #
    # O botão "Save_CSV" agora é um toggle Iniciar/Encerrar Registro. Durante o
    # registro, cada amostra é gravada AO VIVO num CSV temporário (mesma técnica
    # do swap dos gráficos); ao encerrar, o usuário escolhe onde salvar o final.

    def _update_record_button(self):
        """Reflete o estado (verde 'Iniciar' / vermelho 'Encerrar') no botão."""
        btn = getattr(self.ui, "Save_CSV", None)
        if btn is None:
            return
        if self.recording:
            btn.setText("⏹ Encerrar Registro")
            btn.setStyleSheet("background-color: #e74c3c; color: white; font-weight: bold;")
        else:
            btn.setText("⏺ Iniciar Registro")
            btn.setStyleSheet("background-color: #2ecc40; color: white; font-weight: bold;")

    def on_toggle_recording(self):
        """Clique do botão: alterna entre iniciar e encerrar o registro."""
        if not self.recording:
            self._start_recording()
        else:
            self._stop_recording()

    def _start_recording(self):
        """Abre um CSV temporário e passa a gravar cada amostra ao vivo."""
        try:
            with self._buf_lock:
                tmp = tempfile.NamedTemporaryFile(
                    mode="w", delete=False, suffix=".csv", newline="",
                    prefix="registro_", encoding="utf-8")
                self._record_fp = tmp
                self._record_path = tmp.name
                self._record_cols = None    # fixadas na 1ª amostra gravada
                self._record_count = 0
                self.recording = True
            print(f"[Registro] iniciado → temporário: {self._record_path}")
        except Exception as e:
            self.recording = False
            QMessageBox.warning(self, "Registro", f"Não foi possível iniciar: {e}")
        self._update_record_button()

    def _record_write_row_locked(self, idx):
        """Grava a amostra do índice `idx` no CSV temporário. Chamado com o
        self._buf_lock TRAVADO (thread de rede). Fixa as colunas + cabeçalho na
        primeira amostra em que já há variáveis conhecidas."""
        fp = self._record_fp
        if fp is None:
            return
        if self._record_cols is None:
            cols = list(self.variable_order)
            if not cols:
                return   # ainda sem variáveis; espera a próxima amostra
            self._record_cols = cols
            fp.write("Timestamp," + ",".join(cols) + "\n")
        row = ["%.6f" % float(self.time_buffer[idx])]
        for c in self._record_cols:
            b = self.value_buffers.get(c)
            v = b[idx] if b is not None else np.nan
            row.append("" if (v is None or np.isnan(v)) else "%.6f" % float(v))
        fp.write(",".join(row) + "\n")
        self._record_count += 1

    def _stop_recording(self):
        """Encerra a gravação e pergunta onde salvar o arquivo final."""
        with self._buf_lock:
            self.recording = False
            fp, path, count = self._record_fp, self._record_path, self._record_count
            self._record_fp = None
        self._update_record_button()

        if fp is not None:
            try:
                fp.flush(); fp.close()
            except Exception:
                pass

        if not path or count == 0:
            QMessageBox.information(self, "Registro",
                                    "Nenhuma amostra foi gravada — registro descartado.")
            self._discard_temp(path)
            return

        print(f"[Registro] encerrado: {count} amostras em {path}")
        dest, sel = QFileDialog.getSaveFileName(
            self, "Salvar Registro", "registro.csv",
            "CSV (*.csv);;Texto (*.txt)")
        if not dest:
            # Usuário cancelou: mantém não salvo → descarta o temporário.
            self._discard_temp(path)
            print("[Registro] salvamento cancelado — temporário descartado.")
            return
        try:
            shutil.copyfile(path, dest)
            self._discard_temp(path)
            QMessageBox.information(self, "Registro salvo",
                                    f"{count} amostras salvas em:\n{dest}")
            print(f"[Registro] salvo em {dest}")
        except Exception as e:
            QMessageBox.warning(self, "Erro ao salvar",
                                f"{e}\n\nO registro segue no temporário:\n{path}")

    def _discard_temp(self, path):
        """Remove um CSV temporário de registro (se existir)."""
        if path and os.path.exists(path):
            try:
                os.remove(path)
            except OSError:
                pass

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
        # Encerra um registro em andamento (fecha o handle) e descarta o temp.
        if getattr(self, "recording", False) or getattr(self, "_record_fp", None):
            fp = self._record_fp
            self._record_fp = None
            self.recording = False
            if fp is not None:
                try: fp.flush(); fp.close()
                except Exception: pass
            self._discard_temp(getattr(self, "_record_path", None))
        if os.path.exists(self.swap_path):
            try: os.remove(self.swap_path)
            except: pass
        event.accept()

    # Métodos de carregamento de menus...
    @Slot()
    def on_load_motor(self):
        """Carrega um motor de um .yaml (Config/motors/ ou embutido no SAME.yaml)
        e pergunta se aplica os parâmetros no hardware (envia os CMDs)."""
        from Communication_Data import motor_config as mc
        try:
            files = mc.list_motor_files(self.config_dir)       # {nome: caminho}
            same = self._load_config_file(self.same_yaml_path)
            embedded = list((same.get("motors") or {}).keys())
            options = list(files.keys()) + [f"{m}  (SAME.yaml)" for m in embedded]
            if not options:
                QMessageBox.information(self, "Carregar Motor",
                    "Nenhum motor salvo. Use 'Criar' para salvar um .yaml em Config/motors/.")
                return
            choice, ok = QInputDialog.getItem(self, "Carregar Motor",
                                              "Selecione o motor:", options, 0, False)
            if not ok or not choice:
                return

            if choice in files:
                params = mc.load_motor_file(files[choice])
            else:
                name = choice.replace("  (SAME.yaml)", "")
                params = mc.params_from_motor_dict((same.get("motors") or {}).get(name, {}))

            self.prefs["last_motor_name"] = choice
            self.save_user_prefs()

            resp = QMessageBox.question(
                self, "Aplicar no hardware",
                f"Aplicar os parâmetros de '{choice}' no hardware (SAME) agora?",
                QMessageBox.Yes | QMessageBox.No)
            if resp == QMessageBox.Yes:
                if "SAME" not in self.eth.workers:
                    self.eth.start_link("SAME")
                same_cfg = codecs.board_protocol_cfg(self.configs["SAME"], "SAME")
                cmds = mc.build_motor_cmds(same_cfg, params)
                # Loga CADA comando no terminal (hex + decode), igual ao 'Criar'
                # -> o usuário vê exatamente o que foi mandado para a SAME.
                print(f"[Motor] Aplicando '{choice}' na SAME ({len(cmds)} parâmetros):")
                for cmd, (gui_key, target_str) in zip(cmds, mc.MOTOR_TARGET_MAP.items()):
                    print(f"[NET] CMD -> [{cmd.hex(' ').upper()}]")
                    print(f"[DECODE] [SAME] FUNC_SENSOR | INSTR_WRITE | {target_str} | "
                          f"VAL: {params[gui_key]:.4f} | ACT: DEFAULT_ACTION_SET_VALUE")
                    self.eth.send_command("SAME", cmd)
                print(f"[Motor] {len(cmds)} parâmetros de '{choice}' enviados à SAME.")
                QMessageBox.information(self, "Aplicado",
                    f"{len(cmds)} parâmetros de '{choice}' aplicados no hardware.")
        except Exception as e:
            QMessageBox.warning(self, "Erro", str(e))

        
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
        """Abre o popout de ensaios da SPMM (atrito B/τc e inércia J).
        Troca automaticamente para o modo SPMM (plots certos) e avisa se a SPMM
        não estiver conectada."""
        print("Abrindo Ensaios SPMM...")
        self._require_board_mode("SPMM", ["SPMM"])
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
        # SPMM e SIBA acionam o ESC em µs (servo PWM); rótulo mostra os µs.
        if board in ("SPMM", "SIBA"):
            us = 1000 + int(val * 10)   # 0%->1000us, 100%->2000us
            self.ui.Motor_update.setText(f"Atualizar (throttle {us}us)")
        else:
            self.ui.Motor_update.setText(f"Atualizar (Motor {val}%)")
        self.save_user_prefs()

    @Slot()
    def apply_throttle(self):
        """Envia o throttle atual para quem ACIONA o ESC no modo atual.

        SPMM: arma (uma vez) e manda `throttle <us>` (1000–2000).
        SIBA: manda um CMD FUNC_ESC com value = <us> (1000–2000) — a SIBA
              controla o ESC (servo PWM). A SAME NÃO aciona (só lê/estima)."""
        board = self._motor_board(self.op_mode)
        val = self.ui.Motor_action.value()
        us = 1000 + int(val * 10)   # 0%->1000us, 100%->2000us
        if board == "SPMM":
            if "SPMM" not in self.eth.workers:
                self.eth.start_link("SPMM")
            if not getattr(self, "_spmm_armed", False):
                print("[NET] (SPMM) -> 'arm'")
                self.eth.send_command("SPMM", "arm")
                self._spmm_armed = True
            print(f"[NET] (SPMM) -> 'throttle {us}'")
            self.eth.send_command("SPMM", f"throttle {us}")
            print(f"[Motor] SPMM throttle {us}us ({val}%)")
        elif board == "SIBA":
            if "SIBA" not in self.eth.workers:
                self.eth.start_link("SIBA")
            self._send_board_cmd("SIBA", "FUNC_ESC", float(us))
            print(f"[Motor] SIBA ESC throttle {us}us ({val}%)")
        else:
            print("[Motor] Modo atual não aciona o ESC (SAME é só leitura/estimativa).")

    @Slot()
    def motor_stop(self):
        """Para o motor com segurança conforme o modo e zera o slider."""
        board = self._motor_board(self.op_mode)
        if board == "SPMM":
            self.eth.send_command("SPMM", "throttle 1000")
            self.eth.send_command("SPMM", "disarm")
            self._spmm_armed = False
            print("[Motor] SPMM parado (throttle 1000 + disarm).")
        elif board == "SIBA":
            # ESC em idle (1000us) + corte imediato de potência (FUNC_STOP).
            self._send_board_cmd("SIBA", "FUNC_ESC", 1000.0)
            self._send_board_cmd("SIBA", "FUNC_STOP", 0.0)
            print("[Motor] SIBA ESC parado (throttle 1000 + stop).")
        self.ui.Motor_action.setValue(0)

    def _send_board_cmd(self, board, func_name, value, target="TARGET_DEFAULT",
                        instr="INSTR_NONE", status=None):
        """Monta e envia um CMD para `board` usando os enums do <board>.yaml, e
        LOGA a quebra no terminal (hex + decode) — o usuário vê exatamente os 8
        bytes e o que cada campo significa."""
        enums = codecs.board_protocol_cfg(self.configs[board], board).get("enums", {})

        def idx(cat, name, default=0):
            lst = enums.get(cat, [])
            return lst.index(name) if name in lst else default

        fi, ii, ti = idx("func_type", func_name, 0), idx("instruct_type", instr, 2), idx("target_type", target, 0)
        ai = idx("status_type", status, 0) if status else 0
        cmd = codecs.encode_cmd(fi, ii, ti, value, ai)
        # Log da quebra: hex dos 8 bytes + tradução dos campos.
        print(f"[NET] CMD -> [{cmd.hex(' ').upper()}]")
        print(f"[DECODE] [{board}] {func_name}(f={fi}) | {instr}(i={ii}) | "
              f"{target}(t={ti}) | VAL: {value:.4f} | ACT: {ai}"
              f"{f' ({status})' if status else ''}")
        self.eth.send_command(board, cmd)

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