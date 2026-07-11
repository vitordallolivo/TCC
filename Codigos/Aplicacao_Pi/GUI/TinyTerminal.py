import ctypes
from pathlib import Path
import sys
import os
import time
import getpass
import socket
import platform

import psutil  
import yaml
from PySide6.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout, QPlainTextEdit, 
                             QLineEdit, QPushButton, QGroupBox, QRadioButton, QCompleter, 
                             QInputDialog)
from PySide6.QtGui import QTextCursor
from PySide6.QtCore import QEvent, Slot, Qt, Signal
from Communication_Data.Ethernet import EthernetManager


import struct


class TinyTerminal(QWidget):
    # Sinal para permitir que threads enviem logs com segurança para a UI
    log_signal = Signal(str, str, str)

    def __init__(self, config_dir, parent=None):
        super().__init__(parent)

        self.old_stdout = None
        self.setWindowTitle("Albatroz TinyTerminal")
        self.resize(800, 450)
        self.setWindowFlags(Qt.Window)
        
        self.config_dir = config_dir
        self.cmd_config = self._load_commands()
        self.cmd_list = self._generate_autocomplete_list(self.cmd_config)
        
        self._setup_ui()
        
        # Conecta o sinal de log à função que realmente escreve na tela
        self.log_signal.connect(self._safe_append_log)

        self.history = []
        self.history_index = -1
        self.quiet_mocks = set() # Armazena quais boards não devem logar BIN
        self.input.installEventFilter(self) 

        self.is_admin = self._check_os_permission()
        self._update_prompt() 

        if self.is_admin:
             from PySide6.QtCore import QTimer
             QTimer.singleShot(500, lambda: self.append_log("SYS", "Aplicação iniciada com privilégios de ROOT.", "#ADFF2F"))
        
        sys.stdout = self

        # 1. Caminhos corrigidos conforme a tree (Case Sensitive e nomes reais)
        yaml_siba = os.path.join(self.config_dir, "SIBA.yaml")
        yaml_same = os.path.join(self.config_dir, "SAME.yaml")
        
        # 2. USA O GERENCIADOR DA PARENT (GUI Principal) para evitar conflito de portas
        self.eth = parent.eth if hasattr(parent, 'eth') else EthernetManager(yaml_siba, yaml_same, handle_callback=self._on_udp_receive)
        if not hasattr(parent, 'eth'): self.eth.start()

        # 3. Mapeamento Dinâmico de Targets (Extração segura das configs carregadas)
        self.siba_targets = self._extract_targets_from_config("SIBA")
        self.same_targets = self._extract_targets_from_config("SAME")

        self.old_stdout = sys.stdout
        sys.stdout = self

        self.append_log("SYS", "===============================================================", "#ADFF2F")
        self.append_log("SYS", "Esse é o Tiny Terminal ele não é um terminal shell é completamente auto contido, feito para facilitar mudanças de parametros online. \n Como ele funciona? Bem ele usa de arquivos .yaml para conseguir configurar as placas (SIBA e SAME) e o próprio terminal, ou seja os codigos são padronizados. \n Além disso ele conta com funções de MOCK, para testar features da própria. \n Para mais informações use o comando help", "#ADFF2F")
        self.append_log("SYS", "===============================================================", "#ADFF2F")


    def _extract_targets_from_config(self, board_key):
        """Helper para mapear target_type para índices sem quebrar por KeyError."""
        cfg = self.eth.configs.get(board_key, {})
        
        # Se o YAML tiver uma chave raiz (ex: 'siba: enums: ...'), desce um nível
        if board_key.lower() in cfg:
            cfg = cfg[board_key.lower()]
            
        enums = cfg.get('enums', {})
        target_list = enums.get('target_type', [])
        
        return {name: i for i, name in enumerate(target_list)}
    def _load_commands(self):
        """Carrega o YAML de comandos."""
        path = os.path.join(self.config_dir, "commands.yaml")
        if os.path.exists(path):
            with open(path, 'r', encoding='utf-8') as f:
                content = yaml.safe_load(f)
                return content.get('commands', {}) if content else {}
        return {}

    def _is_group(self, node):
        """Identifica se o item no YAML é um grupo (dicionário com filhos) ou um comando final."""
        if not isinstance(node, dict):
            return False
        # Se houver chaves que não sejam metadados (help/type), é um grupo/subcomando
        keys = set(node.keys()) - {'help', 'type', 'subcommands'}
        return len(keys) > 0

    def _generate_autocomplete_list(self, config, prefix=""):
        """Gera lista de strings para o QCompleter percorrendo a árvore do YAML."""
        items = []
        for name, details in config.items():
            if name in ['help', 'type', 'subcommands']: continue
            
            path = f"{prefix} {name}".strip()
            items.append(path)
            
            # Se for um grupo (como 'graph' ou 'mock_data'), continua descendo
            if self._is_group(details):
                items.extend(self._generate_autocomplete_list(details, path))
            # Suporte legado para a chave 'subcommands' caso exista
            elif isinstance(details, dict) and "subcommands" in details:
                items.extend(self._generate_autocomplete_list(details["subcommands"], path))
        return items

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        
        # Toolbar
        tbar = QHBoxLayout()
        self.group = QGroupBox("View Mode")
        v_lay = QHBoxLayout()
        self.radio_hex = QRadioButton("HEX")
        self.radio_trad = QRadioButton("Traduzido")
        self.radio_trad.setChecked(True)
        v_lay.addWidget(self.radio_trad); v_lay.addWidget(self.radio_hex)
        self.group.setLayout(v_lay)
        
        btn_cls = QPushButton("Clear Console")
        btn_cls.clicked.connect(self.clear_shell)
        
        tbar.addWidget(self.group); tbar.addStretch(); tbar.addWidget(btn_cls)
        
        # Terminal Output
        self.output = QPlainTextEdit()
        self.output.setReadOnly(True)
        self.output.setMaximumBlockCount(1500)
        self.output.setStyleSheet("background: #0F0F0F; color: #DCDCDC; font-family: monospace; font-size: 10pt;")
        
        # Command Input
        self.input = QLineEdit()
        self.input.returnPressed.connect(self._handle_input)
        self.completer = QCompleter(self.cmd_list, self)
        self.completer.setCaseSensitivity(Qt.CaseInsensitive)
        # Permite completar caminhos longos (ex: graph mock_data start)
        self.completer.setCompletionMode(QCompleter.PopupCompletion)
        self.input.setCompleter(self.completer)
        
        layout.addLayout(tbar); layout.addWidget(self.output); layout.addWidget(self.input)

    def write(self, text):
        """Captura prints do sistema para o terminal."""
        if text.strip(): self.log_signal.emit("PRNT", text.strip(), "#FFD700")
    
    def flush(self): pass

    def append_log(self, pre, msg, col):
        """Método público para ser usado por outras classes."""
        self.log_signal.emit(pre, msg, col)

    @Slot(str, str, str)
    def _safe_append_log(self, pre, msg, col):
        """Executado SEMPRE na thread principal do Qt."""
        self.output.appendHtml(f'<span style="color:{col};"><b>[{pre}]</b> {msg}</span>')
        self.output.moveCursor(QTextCursor.End)
        # Espelha em session.log (texto puro) — para revisar a sequência de
        # eventos depois de um crash. Não deixa erro de I/O derrubar a UI.
        try:
            f = getattr(self, "_session_f", None)
            if f is None:
                import debug_log
                p = debug_log.session_log_path()
                f = self._session_f = open(p, "a", buffering=1, encoding="utf-8") if p else False
            if f:
                f.write(f"[{pre}] {msg}\n")
        except Exception:
            self._session_f = False

    def clear_shell(self):
        self.output.clear()

    def _handle_input(self):
        raw = self.input.text().strip()
        if not raw: return
        
        # 1. Histórico
        if not self.history or raw != self.history[0]:
            self.history.insert(0, raw)
        self.history_index = -1
        
    def _handle_input(self):
        raw = self.input.text().strip()
        if not raw: return
        if not self.history or raw != self.history[0]:
            self.history.insert(0, raw)
        self.history_index = -1
        
        color = "#FF0000" if self.is_admin else "#00FFFF"
        self.append_log(self.current_prompt, raw, color)
        
        parts = raw.split()
        cmd_root = parts[0].lower()
        
        # --- Comandos do Sistema (Não-YAML ou Fixos) ---
        if cmd_root == "sudo": self._logic_sudo()
        elif cmd_root == "restart":
            self.append_log("SYS", "Reiniciando...", "#FFFF00")
            QApplication.processEvents()
            self.restart_program(self.eth) 
            return
        elif cmd_root == "exit":
            if len(parts) > 1 and parts[1].lower() == "all": QApplication.quit()
            else: self.close()
        elif cmd_root == "help":
            self._show_help(self.cmd_config, target=parts[1].lower() if len(parts) > 1 else None)
        elif cmd_root == "clear": self.clear_shell()
        else:
            # Despacho dinâmico para os demais comandos
            self._execute_command(parts)
            
        self.input.clear()

    def _execute_command(self, parts):
        """Despacho dinâmico: busca métodos como _cmd_siba_start_mock."""
        # Tenta do mais específico para o mais geral (ex: siba start mock -> siba start -> siba)
        for i in range(len(parts), 0, -1):
            method_name = "_cmd_" + "_".join(parts[:i]).lower()
            if hasattr(self, method_name):
                getattr(self, method_name)(parts[i:])
                return

        # Fallback para comandos de hardware ou validação via YAML
        root = parts[0].lower()
        if root in ["same", "siba"]:
            if self._is_complete(parts, self.cmd_config):
                self._handle_hardware_command(parts)
            else: self.append_log("ERR", f"Comando incompleto. Use 'help {root}'", "#FF4500")
        elif root in self.cmd_config:
            if self._is_complete(parts, self.cmd_config):
                self.append_log("ERR", f"Método Python para '{' '.join(parts)}' não implementado.", "#FF4500")
            else: self.append_log("ERR", "Sintaxe inválida para o comando.", "#FF4500")
        else: self.append_log("ERR", f"Comando '{root}' desconhecido.", "#FF4500")

    def _handle_hardware_command(self, parts):
        """Trata o envio de comandos reais (binários) para as placas."""
        root = parts[0].upper()
        cmd_struct = self._parse_to_cmd_struct(parts)
        if cmd_struct:
            hex_dump = cmd_struct.hex(' ').upper()
            self.append_log("NET", f"CMD -> [{hex_dump}]", "#00FFFF")
            traducao = self.decode_hex_to_cmd(
                hex_dump, self.eth.configs["SAME"], self.eth.configs["SIBA"], origin=root
            )
            self.append_log("DECODE", traducao, "#DCDCDC")
            if hasattr(self, 'eth'): self.eth.send_command(root, cmd_struct)

    def _is_complete(self, parts, config):
        """Valida se o comando segue a estrutura do YAML."""
        current_level = config
        for p in parts:
            if isinstance(current_level, dict) and p in current_level:
                current_level = current_level[p]
            else:
                if isinstance(current_level, dict):
                    keys = set(current_level.keys()) - {'help', 'type', 'admin_only', 'args'}
                    if not keys: return True 
                return False
        if isinstance(current_level, dict):
            keys = set(current_level.keys()) - {'help', 'type', 'admin_only', 'args'}
            if keys: return False
        return True

    def _show_help(self, config, target=None):
        """
        Exibe a ajuda em Branco (#FFFFFF). 
        Se um target for especificado, detalha seus subcomandos.
        """
        self.append_log("SYS", "--- AJUDA DO SISTEMA ---", "#ADFF2F")
        help_txt_color = "#FFFFFF"

        if target and target in config:
            # Pega o bloco do comando específico (ex: 'siba')
            info = config[target]
            main_help = info.get('help', 'Sem descrição')
            self.append_log("HELP", f"<b>{target.upper()}</b>: {main_help}", help_txt_color)
            
            # Percorre os subcomandos se existirem
            self._list_subcommands(info, prefix=f"{target} ", color=help_txt_color)
        else:
            # Listagem geral (apenas comandos raiz)
            for cmd, info in config.items():
                desc = info.get('help', '---')
                self.append_log("HELP", f"<b>{cmd:12}</b> | {desc}", help_txt_color)
                
        self.append_log("SYS", "-------------------------", "#ADFF2F")

    def _list_subcommands(self, dictionary, prefix="", color="#FFFFFF"):
        """Função auxiliar para listar sub-chaves que possuam o campo 'help'."""
        for key, value in dictionary.items():
            # Ignora a própria chave 'help' e foca em sub-dicionários
            if key == "help" or not isinstance(value, dict):
                continue
            
            sub_help = value.get("help", "")
            # Se tem ajuda, é um subcomando válido para o terminal
            if sub_help:
                self.append_log("HELP", f"  -> <b>{prefix}{key}</b>: {sub_help}", color)
            
            # Opcional: recursividade para níveis mais profundos (ex: siba sensor linear)
            self._list_subcommands(value, prefix=f"{prefix}{key} ", color=color)

    def log_board_data(self, board, data, decoded=None):
        """Exibe dados de rede no console do terminal."""
        # Se a board estiver no modo "quiet" (no-comment), ignoramos o log de BIN/LOG
        if board in self.quiet_mocks:
            return # Não loga nada se estiver no modo quiet

        if self.radio_hex.isChecked():
            msg = data.hex(' ').upper()
        else:
            # Tradução simplificada para o terminal
            msg = ", ".join([f"{k}: {v:.2f}" for k, v in decoded.items() if isinstance(v, float)])
        
        col = "#32CD32" if board == "SIBA" else "#1E90FF"
        self.append_log("LOG", f"({board}) {msg}", col)

    def closeEvent(self, e):
        # Restaura o stdout original para evitar erros de I/O após fechar
        sys.stdout = self.old_stdout
        # Garante que os mocks/processos native_sim sejam desligados ao fechar o terminal
        self.eth.set_mock_mode("SIBA", False)
        self.eth.set_mock_mode("SAME", False)
        self.eth.stop_native_sim("SIBA")
        self.eth.stop_native_sim("SAME")
        # Encerra o link da SPMM com parada segura (disarm) se estiver ativo.
        spmm = self.eth.workers.pop("SPMM", None)
        if spmm:
            spmm.stop(); spmm.join(timeout=1.0)
        e.accept()

    # --- Implementações de Comandos Específicos ---

    def _cmd_graph_clear(self, args):
        # O clear_graph_buffers na GUI agora sempre limpa tudo
        self.parent().clear_graph_buffers("all") 

    def _cmd_graph_mock_data_start(self, args):
        target = args[0] if args and not args[0].startswith("--") else "all"
        quiet = "--no-comment" in args
        self.parent().start_mock_with_target(target, quiet)

    def _cmd_graph_mock_data_stop(self, args):
        # Para o mock interno da UI (se existir) e os mocks de hardware
        if hasattr(self.parent(), 'worker'): 
            self.parent().worker.stop()
        self.eth.set_mock_mode("SIBA", False)
        self.eth.set_mock_mode("SAME", False)
        print("Todas as simulações paradas.")

    def _cmd_connect(self, args):
        ip = args[0] if args else "127.0.0.1"
        self.append_log("SYS", f"IP Global alterado: {ip}", "#FFFF00")
        self.parent().eth.ip_siba = self.parent().eth.ip_same = ip

    def _cmd_ping(self, args):
        if not args: return self.append_log("ERR", "Uso: ping [IP]", "#FF4500")
        ip = args[0]
        self.append_log("NET", f"Pingando {ip}...", "#00FF7F")
        try:
            socket.setdefaulttimeout(2)
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s: s.connect((ip, 80))
            self.append_log("NET", f"Link OK com {ip}", "#32CD32")
        except Exception as e: self.append_log("ERR", f"Timeout link {ip}: {e}", "#FF4500")

    def _cmd_ipconfig(self, args):
        self.append_log("SYS", "--- Interfaces ---", "#ADFF2F")
        for name, snics in psutil.net_if_addrs().items():
            for snic in snics:
                if snic.family == socket.AF_INET: self.append_log("NET", f"{name}: {snic.address}", "#DCDCDC")

    def _cmd_listing(self, args):
        port = int(args[0]) if args else 5555
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            res = s.connect_ex(('127.0.0.1', port))
            self.append_log("NET", f"Porta {port}: {'OCUPADA' if res == 0 else 'LIVRE'}", "#32CD32")

    def _cmd_tcpdump(self, args):
        if not self.is_admin: return self.append_log("ERR", "Requer 'sudo'", "#FF4500")
        if len(args) >= 2: self._execute_tcpdump(args[0], int(args[1]))

    def _cmd_siba_connect(self, args): self._hardware_connect_flow("SIBA")
    def _cmd_same_connect(self, args): self._hardware_connect_flow("SAME")

    def _hardware_connect_flow(self, board):
        ip = self.eth.ip_siba if board == "SIBA" else self.eth.ip_same
        self.append_log("NET", f"Tentando conectar com {board} ({ip})...", "#00BFFF")
        
        # 1. Verifica Subnet
        ok, local_ip = self.eth.check_subnet(ip)
        if not ok:
            self.append_log("WARN", f"Seu IP ({local_ip}) parece estar em subrede diferente de {ip}!", "#FFA500")
            self.append_log("SYS", "Dica: Configure seu IP fixo para 169.254.97.x", "#DCDCDC")
        
        # 2. Inicia o Worker
        self.eth.start_worker(board)
        
        # 3. Ping de Teste
        if self.eth.check_connectivity(ip):
            self.append_log("NET", f"Link com {board} estabelecido com sucesso!", "#32CD32")
        else:
            self.append_log("ERR", f"Não foi possível pingar a board {board}. Verifique cabos.", "#FF4500")

    def _cmd_siba_mock_start(self, args): self._mock_hardware_flow("SIBA", args)
    def _cmd_siba_mock_stop(self, args): 
        self.eth.set_mock_mode("SIBA", False)
        self.quiet_mocks.discard("SIBA")

    def _cmd_same_mock_start(self, args): self._mock_hardware_flow("SAME", args)
    def _cmd_same_mock_stop(self, args):
        self.eth.set_mock_mode("SAME", False)
        self.quiet_mocks.discard("SAME")

    def _cmd_siba_native_start(self, args): self._native_sim_flow("SIBA")
    def _cmd_siba_native_stop(self, args): self.eth.stop_native_sim("SIBA")
    def _cmd_same_native_start(self, args): self._native_sim_flow("SAME")
    def _cmd_same_native_stop(self, args): self.eth.stop_native_sim("SAME")

    def _native_sim_flow(self, board):
        """Sobe o binário REAL do firmware (`make native_sim` na pasta da
        placa, gera `build_native_sim/zephyr/zephyr.exe`) como processo no
        host e conversa com ele via UDP/loopback (127.0.0.1) — mesmas portas
        do hardware físico. Diferente de 'mock', aqui não é um fake Python:
        é o firmware de verdade (System_Manager/CMD_Core/NCI reais), útil
        para validar o link UDP fim-a-fim sem placa/W-Link. Ver
        SIBA/CLAUDE.md "Modo Mock (native_sim)" e Aplicacao_Pi/CLAUDE.md."""
        # config_dir = .../Aplicacao_Pi/Config -> sobe 2 níveis para chegar em
        # Codigos/, onde SIBA/ e SAME/ são pastas irmãs de Aplicacao_Pi/
        board_dir = os.path.normpath(os.path.join(self.config_dir, "..", "..", board))
        exe_path = os.path.normpath(os.path.join(board_dir, "build_native_sim", "zephyr", "zephyr.exe"))

        self.append_log("SYS", f"Procurando binário native_sim de {board} em {exe_path}...", "#00BFFF")
        if not os.path.exists(exe_path):
            self.append_log("ERR", f"Binário não encontrado. Rode 'make native_sim' em {board}/.", "#FF4500")
            return

        if self.eth.start_native_sim(board, exe_path):
            self.append_log("SYS", f"Firmware {board} (native_sim) rodando em 127.0.0.1.", "#ADFF2F")
            self.append_log("SYS", "Esse é o FIRMWARE DE VERDADE (não um fake) — testa TX/RX UDP reais.", "#DCDCDC")
        else:
            self.append_log("ERR", f"Falha ao iniciar native_sim para {board} (ver console).", "#FF4500")

    def _cmd_siba_test_io(self, args): self._test_io_flow("SIBA")
    def _cmd_same_test_io(self, args): self._test_io_flow("SAME")

    def _cmd_spmm(self, args):
        """SPMM (Telnet + UDP): connect/disconnect/detect ou encaminha o texto
        restante como comando de bancada ao shell do Zephyr."""
        if not args:
            return self.append_log("ERR", "Uso: spmm [connect|disconnect|detect|arm|throttle <us>|coast|log start <hz>|...]", "#FF4500")
        sub = args[0].lower()

        if sub == "connect":
            self.eth.start_link("SPMM")
            self.append_log("SYS", "Link SPMM iniciado (Telnet TCP + UDP log).", "#ADFF2F")
            return
        if sub == "disconnect":
            w = self.eth.workers.pop("SPMM", None)
            if w:
                w.stop(); w.join(timeout=1.0)
            self.append_log("SYS", "Link SPMM encerrado (disarm + throttle 1000 enviados).", "#FFFF00")
            return
        if sub == "detect":
            from Communication_Data.discovery import DiscoveryService
            st = DiscoveryService(self.eth).probe("SPMM")
            col = "#32CD32" if st.present else "#FF4500"
            self.append_log("SPMM", f"{'PRESENTE' if st.present else 'AUSENTE'} — {st.detail}", col)
            return
        if sub == "native":
            action = args[1].lower() if len(args) > 1 else "start"
            if action == "stop":
                self.eth.stop_native_sim("SPMM")
                self.append_log("SYS", "native_sim SPMM encerrado.", "#FFFF00")
            else:
                from Communication_Data.native_sim import native_sim_supported
                if not native_sim_supported():
                    return self.append_log("ERR", "native_sim só é possível no Linux.", "#FF4500")
                if self.eth.start_native_sim_auto("SPMM"):
                    self.append_log("SYS", "Firmware SPMM (native_sim) rodando — shell via stdin, log UDP em 127.0.0.1.", "#ADFF2F")
                else:
                    self.append_log("ERR", "Falha: rode 'make native_sim' em ../SPMM/.", "#FF4500")
            return

        # Demais: encaminha como texto ao shell (garante o link ativo antes).
        if "SPMM" not in self.eth.workers:
            self.eth.start_link("SPMM")
        text = " ".join(args)
        ok = self.eth.send_command("SPMM", text)
        col = "#00FFFF" if ok else "#FF4500"
        self.append_log("SPMM", f"CMD -> '{text}'{'' if ok else '  (FALHOU — placa conectada?)'}", col)

    def _test_io_flow(self, board):
        """Testa RX e TX da placa de forma isolada — envia um CMD neutro e
        verifica se o socket conseguiu transmitir (TX) e se chegou algum
        pacote de telemetria dentro do timeout (RX). Funciona com mock,
        native_sim ou hardware real (eth.test_io só observa contadores do
        BoardWorker)."""
        self.append_log("SYS", f"Testando RX/TX de {board} (aguarde ~2s)...", "#00BFFF")
        result = self.eth.test_io(board, timeout=2.0)
        tx_color = "#32CD32" if result["tx_ok"] else "#FF4500"
        rx_color = "#32CD32" if result["rx_ok"] else "#FF4500"
        self.append_log("TEST", f"TX (PC -> {board}): {'OK' if result['tx_ok'] else 'FALHOU'}", tx_color)
        self.append_log("TEST", f"RX ({board} -> PC): {'OK' if result['rx_ok'] else 'FALHOU'} "
                                 f"({result['rx_packets']} pacote(s))", rx_color)

    def _mock_hardware_flow(self, board, args):
        """Ativa o modo MOCK no gerenciador de rede."""
        if "--no-comment" in args:
            self.quiet_mocks.add(board)
            self.append_log("SYS", f"Logs de telemetria ocultados para {board}.", "#FFFF00")
        else:
            self.quiet_mocks.discard(board)

        self.eth.set_mock_mode(board, True)
        self.append_log("SYS", f"Modo MOCK para {board} ATIVADO.", "#ADFF2F")
        self.append_log("SYS", "A placa agora responde em 127.0.0.1 e simula Telemetria.", "#DCDCDC")

    def eventFilter(self, obj, event):
        if obj == self.input and event.type() == QEvent.KeyPress:
            # --- Lógica de Histórico (Setas para Cima/Baixo) ---
            if event.key() == Qt.Key_Up:
                if self.history and self.history_index < len(self.history) - 1:
                    self.history_index += 1
                    self.input.setText(self.history[self.history_index])
                return True
            
            elif event.key() == Qt.Key_Down:
                if self.history_index > 0:
                    self.history_index -= 1
                    self.input.setText(self.history[self.history_index])
                else:
                    self.history_index = -1
                    self.input.clear()
                return True

            # --- Lógica de Autocomplete (TAB) ---
            elif event.key() == Qt.Key_Tab:
                self._handle_autocomplete()
                return True

        return super().eventFilter(obj, event)

    def _handle_autocomplete(self):
        text = self.input.text().lower()
        parts = text.split()
        
        # Se vazio, não faz nada
        if not text: return

        # Busca opções baseadas na hierarquia do YAML
        options = self._get_completions(parts)
        
        if len(options) == 1:
            # Apenas uma opção: completa automaticamente
            new_text = " ".join(parts[:-1] + [options[0]]) + " "
            self.input.setText(new_text.lstrip())
        elif len(options) > 1:
            # Múltiplas opções: mostra no terminal
            self.append_log("AUTO", f"Opções: {', '.join(options)}", "#FFFF00")

    def _get_completions(self, parts):
        """Navega no YAML para encontrar sugestões válidas."""
        current_level = self.cmd_config
        
        # Tenta navegar até o nível atual do comando
        try:
            for p in parts[:-1]:
                if p in current_level:
                    current_level = current_level[p]
        except:
            return []

        # Filtra as chaves que começam com o que o usuário digitou
        last_part = parts[-1]
        return [key for key in current_level.keys() if key.startswith(last_part) and key != "help"]
    
    def _update_prompt(self):
        """Atualiza o prompt baseado no status atual de admin."""
        user = "root" if self.is_admin else getpass.getuser()
        hostname = socket.gethostname()
        char = "#" if self.is_admin else "$"
        self.current_prompt = f"{user}@{hostname}:~{char}"

    def _check_os_permission(self):
        """
        Checa se o processo atual tem permissão administrativa no SO.
        """
        system = platform.system()
        try:
            if system == "Windows":
                # Verifica se o processo atual tem o token de Admin do Windows
                return ctypes.windll.shell32.IsUserAnAdmin() != 0
            else:
                # No Linux/Unix, verifica se o Effective User ID é 0 (root)
                return os.geteuid() == 0
        except AttributeError:
            # Caso algum SO não suporte essas chamadas específicas
            return False

    def _logic_sudo(self):
        if self._check_os_permission():
            self.is_admin = True
            self._update_prompt()
            return

        self.append_log("SYS", "Relançando aplicação como ROOT...", "#FFFF00")
        
        script = os.path.abspath(sys.argv[0])
        python_exe = sys.executable # Garante que usa o Python do seu .venv
        system = platform.system()

        try:
            if system == "Windows":
                import ctypes
                ctypes.windll.shell32.ShellExecuteW(None, "runas", python_exe, script, None, 1)
            else:
                # --- CORREÇÃO PARA LINUX (X11/Wayland) ---
                # Pegamos as variáveis do display do usuário atual
                display = os.environ.get('DISPLAY', ':0')
                xauth = os.environ.get('XAUTHORITY', os.path.expanduser('~/.Xauthority'))
                
                # O comando agora exporta as variáveis antes de rodar o python
                # Usamos 'sudo' em vez de 'pkexec' para manter o terminal se necessário, 
                # ou 'pkexec' com env setup.
                cmd = f'pkexec env DISPLAY={display} XAUTHORITY={xauth} {python_exe} {script}'
                
                # Rodamos de forma assíncrona para não travar
                os.popen(cmd)
            
            sys.exit(0)
            
        except Exception as e:
            self.append_log("ERR", f"Falha ao elevar: {e}", "#FF4500")

    
    def _execute_tcpdump(self, target_ip, duration):
        if not self.is_admin:
            self.append_log("ERR", "Privilégios insuficientes.", "#FF4500")
            return

        system = platform.system()
        self.append_log("NET", f"Sniffer {system} ativo em {target_ip}...", "#00BFFF")

        try:
            if system == "Windows":
                sniffer = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_IP)
                local_ip = socket.gethostbyname(socket.gethostname())
                sniffer.bind((local_ip, 0))
                sniffer.ioctl(socket.SIO_RCVALL, socket.RCVALL_ON)
            else:
                # No Linux, o 0x0003 (ETH_P_ALL) pega TUDO
                sniffer = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.ntohs(3))

            sniffer.settimeout(0.5)
            # Converte IP para bytes para busca rápida no binário
            try:
                target_bytes = socket.inet_aton(target_ip)
            except:
                self.append_log("ERR", "IP Inválido.", "#FF4500")
                return

            start_time = time.time()
            packets_count = 0

            while time.time() - start_time < duration:
                try:
                    raw_data, addr = sniffer.recvfrom(65535)
                    
                    # Checa se o IP alvo aparece em qualquer lugar do pacote (Header ou Payload)
                    if target_bytes in raw_data:
                        packets_count += 1
                        ts = time.strftime("%H:%M:%S")
                        
                        # Mostra os primeiros 32 bytes para diagnóstico
                        hex_view = raw_data[:32].hex(' ').upper()
                        self.append_log("DUMP", f"[{ts}] PKT <- {raw_data[42:].hex(' ').upper()}", "#DCDCDC")
                    
                    QApplication.processEvents()
                except socket.timeout:
                    continue

            if system == "Windows":
                sniffer.ioctl(socket.SIO_RCVALL, socket.RCVALL_OFF)
            sniffer.close()
            self.append_log("NET", f"Fim. {packets_count} pacotes capturados.", "#00FF7F")

        except Exception as e:
            self.append_log("ERR", f"Erro no Sniffer: {e}", "#FF4500")

    def _on_udp_receive(self, board, data, addr):
        """
        Callback disparado pelas threads do EthernetManager.
        Decodifica o pacote e joga no terminal de forma segura.
        """
        # Este callback é apenas para o EthernetManager passar os dados.
        # O logging para o terminal é feito por mainAlbatrozGUI.process_network_data
        # que chama self.terminal.log_board_data, que já respeita quiet_mocks.
        pass

    # Aliases: o nome que o usuário digita (e o nome da variável de
    # telemetria) nem sempre bate por substring com o enum target_type.
    # Ex.: `v_batt`/`i_batt` NÃO contêm "VOLTAGE_DIVIDOR"/"CURRENT_SENSOR",
    # então sem isso caíam em TARGET_DEFAULT e o firmware rejeitava a
    # calibração (era o "não funciona para o resto"). load_cell_* e
    # pitot_tube já batem por substring e não precisam de alias.
    TARGET_ALIASES = {
        "V_BATT": "TARGET_VOLTAGE_DIVIDOR",
        "I_BATT": "TARGET_CURRENT_SENSOR",
        "AIR_SPEED": "TARGET_PITOT_TUBE",
        "PITOT": "TARGET_PITOT_TUBE",
        "TEMPERATURE_BATTERY": "TARGET_TEMPERATURE_SENSOR",
        "TEMP_SENSOR": "TARGET_TEMPERATURE_SENSOR",
        # SAME: telemetria usa i_a/i_b/i_c e V_a/V_b/V_c; os nomes de
        # calibração (TARGET_CURRENT_A/B/C, TARGET_VOLTAGE_A/B/C) são
        # 1:1 com a telemetria mas não batem por substring ("I_A" não
        # está contido em "CURRENT_A", etc.), daí o alias.
        "I_A": "TARGET_CURRENT_A",
        "I_B": "TARGET_CURRENT_B",
        "I_C": "TARGET_CURRENT_C",
        "V_A": "TARGET_VOLTAGE_A",
        "V_B": "TARGET_VOLTAGE_B",
        "V_C": "TARGET_VOLTAGE_C",
    }

    def _resolve_target_index(self, root, target_raw):
        """Resolve um nome de sensor/target digitado (ou vindo da GUI) pro
        índice numérico de target_type da placa `root` (SIBA/SAME). Usado
        tanto pelo parser de CMD quanto pelo SensorConfigDialog (pra achar o
        default em sensor_defaults, que é indexado na MESMA ordem)."""
        targets_dict = self.siba_targets if root == "SIBA" else self.same_targets
        target_raw = target_raw.upper()
        exact_key = f"TARGET_{target_raw}"
        if target_raw in self.TARGET_ALIASES and self.TARGET_ALIASES[target_raw] in targets_dict:
            return targets_dict[self.TARGET_ALIASES[target_raw]]
        if exact_key in targets_dict:
            return targets_dict[exact_key]
        for name, idx in targets_dict.items():
            if target_raw in name:
                return idx
        return targets_dict.get("TARGET_DEFAULT", 10)

    def _parse_to_cmd_struct(self, parts):
        """
        Parser Final: Mapeamento dinâmico SAME/SIBA baseado nos YAMLs.
        """
        try:
            root = parts[0].upper()      # SAME ou SIBA
            action = parts[1].upper()    # SET, START, STOP, RESET
            target_raw = parts[2].upper() if len(parts) > 2 else "DEFAULT"
            
            cfg = self.eth.configs.get(root, {})
            if root.lower() in cfg: 
                cfg = cfg[root.lower()]
            
            enums = cfg.get('enums', {})

            # --- 1. FUNCTION ID ---
            func_val = 0
            search_func = ""
            if action in ["SET", "GET"]:
                if root == "SIBA":
                    # Calibração de SENSOR (offset/linear/reset) é SEMPRE
                    # FUNC_SENSOR — inclui v_batt/i_batt, que contêm "BATT" mas
                    # são SENSORES (não parâmetros de bateria). Só os parâmetros
                    # de bateria (SOC poly/temp/relé), que NÃO usam offset/linear,
                    # vão para FUNC_BATTERY.
                    sub = parts[3].upper() if len(parts) > 3 else ""
                    if sub in ("LINEAR", "OFFSET", "RESET", "MAX", "MIN"):
                        search_func = "FUNC_SENSOR"
                    else:
                        bat_keywords = ["TEMP", "RELAY", "SOC", "P0", "P1", "P2", "P3", "P4"]
                        search_func = ("FUNC_BATTERY"
                                       if any(x in target_raw for x in bat_keywords)
                                       else "FUNC_SENSOR")
                else:
                    search_func = "FUNC_SENSOR"
            else:
                search_func = f"FUNC_{action}" # FUNC_START, FUNC_STOP, FUNC_RESET

            f_list = enums.get('func_type', [])
            for i, name in enumerate(f_list):
                if search_func == name:
                    func_val = i
                    break

            # --- 2. INSTRUCTION ID ---
            inst_val = 0
            search_inst = "INSTR_WRITE" if action == "SET" else "INSTR_READ" if action == "GET" else "INSTR_DEFAULT"
            
            i_list = enums.get('instruct_type', [])
            for i, name in enumerate(i_list):
                if search_inst == name:
                    inst_val = i
                    break

            # --- 3. TARGET ID ---
            target_val = self._resolve_target_index(root, target_raw)

            # --- 4. VALUE ---
            try:
                if action == "STOP": value_val = 0.0
                elif action in ["START", "RESET"]: value_val = 1.0
                else: value_val = float(parts[-1])
            except:
                value_val = 0.0

            # --- 5. ACTION / STATUS (CORREÇÃO DO TEMP_LIMIT) ---
            status_val = 0
            if root == "SIBA" and search_func == "FUNC_BATTERY":
                bat_actions = enums.get('battery_action', [])
                # Se target_raw for TEMP_LIMIT, busca a action que tenha TEMP e LIMIT
                # Se for SOC_P2, busca a que tenha P2
                clean_target = target_raw.replace("SOC_", "")
                
                for i, name in enumerate(bat_actions):
                    # Verifica se todas as partes da palavra chave estão no enum
                    # Ex: 'TEMP_LIMIT' dá match em 'BAT_ACTION_TEMP_LIMIT_OVERWRITE'
                    keywords = clean_target.split('_')
                    if all(k in name for k in keywords):
                        status_val = i
                        break
            elif len(parts) > 3 and action == "SET":
                sub_cmd = parts[3].upper()
                def_actions = enums.get('default_action', [])
                for i, name in enumerate(def_actions):
                    if sub_cmd in name:
                        status_val = i
                        break

            return struct.pack("<BBBfb", func_val, inst_val, target_val, value_val, status_val)

        except Exception as e:
            self.append_log("ERR", f"Erro Parser: {e}", "#FF4500")
            return None


    def decode_hex_to_cmd(self, hex_str, same_config, siba_config, origin=None):
        import struct
        try:
            raw_bytes = bytes.fromhex(hex_str.replace(" ", ""))
            # B, B, B, f, b
            f_idx, i_idx, t_idx, val, a_idx = struct.unpack('<BBBfb', raw_bytes)

            # 1. Seleção Forçada pela Origem
            if origin == "SIBA":
                cfg = siba_config
                # Se o YAML tiver a chave raiz 'siba', acessamos ela
                if "siba" in cfg: cfg = cfg["siba"]
            else:
                cfg = same_config
                if "same" in cfg: cfg = cfg["same"]

            enums = cfg.get('enums', {})
            
            # 2. Tradução de Categorias Principais
            def get_enum_name(category, index):
                lst = enums.get(category, [])
                return lst[index] if index < len(lst) else f"ID:{index}"

            f_name = get_enum_name('func_type', f_idx)
            i_name = get_enum_name('instruct_type', i_idx)
            t_name = get_enum_name('target_type', t_idx)
            
            # 3. Lógica de Action/Status (Contextual)
            # Se a função for Bateria na SIBA, usa battery_action, senão default_action
            if origin == "SIBA" and "BATTERY" in f_name:
                a_list = enums.get('battery_action', [])
            else:
                a_list = enums.get('default_action', [])
            
            a_name = a_list[a_idx] if a_idx < len(a_list) else f"ACT:{a_idx}"

            return f"[{origin}] {f_name} | {i_name} | {t_name} | VAL: {val:.4f} | ACT: {a_name}"

        except Exception as e:
            return f"ERRO DECODE ({origin}): {e}"
        
    def restart_program(self, ethernet_manager=None):
        """Fecha o programa e reinicia especificamente a mainApp.py na raiz."""
        print("\n[SYS] Preparando reinicialização do sistema Albatroz...")
        
        # 1. Liberação de Hardware/Rede
        if ethernet_manager:
            try:
                ethernet_manager.stop()
                time.sleep(0.8) # Tempo para o Kernel liberar o bind das portas
            except Exception as e:
                print(f"[ERR] Falha ao parar rede: {e}")

        # 2. Localização dinâmica da mainApp.py
        # __file__ é o caminho do TinyTerminal.py (ex: .../Aplicacao_Pi/GUI/TinyTerminal.py)
        # .parent pega a pasta GUI/
        # .parent.parent pega a raiz Aplicacao_Pi/
        current_file = Path(__file__).resolve()
        root_dir = current_file.parent.parent # Sobe um nível para sair de GUI/
        main_script = root_dir / "mainApp.py"

        if not main_script.exists():
            print(f"[ERR] mainApp.py não encontrada em: {main_script}")
            # Tenta um fallback se estiver em outra estrutura
            main_script = root_dir.parent / "mainApp.py"

        python = sys.executable

        print(f"[SYS] Executando: {python} {main_script}")
        
        # 3. Substituição do Processo
        try:
            # os.execv exige uma lista de strings
            os.execv(python, [python, str(main_script)] + sys.argv[1:])
        except Exception as e:
            print(f"[ERR] Falha crítica no execv: {e}")