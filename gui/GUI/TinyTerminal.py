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
from Communication_Data.comandos import Protocolo, ErroComando, AJUDA


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
        # Telemetria das placas no terminal: começa DESLIGADA (a ~10 Hz por
        # placa ela rola a tela e esconde as respostas dos comandos).
        # Alterna com 'logs on' / 'logs off'.
        self.board_logs_enabled = False
        self.input.installEventFilter(self) 

        self.is_admin = self._check_os_permission()
        self._update_prompt() 

        if self.is_admin:
             from PySide6.QtCore import QTimer
             QTimer.singleShot(500, lambda: self.append_log("SYS", "Aplicação iniciada com privilégios de ROOT.", "#ADFF2F"))
        
        sys.stdout = self

        # Usa o gerenciador da GUI principal (evita conflito de portas).
        self.eth = parent.eth if hasattr(parent, 'eth') else EthernetManager(config_dir=self.config_dir, handle_callback=self._on_udp_receive)
        if not hasattr(parent, 'eth'): self.eth.start()

        # Mesma gramática de comandos do CLI (placa.py).
        self.proto = Protocolo(self.eth.configs.get("PLACA", {}))

        self.old_stdout = sys.stdout
        sys.stdout = self

        self.append_log("SYS", "===============================================================", "#ADFF2F")
        self.append_log("SYS", "TinyTerminal: comandos da placa direto (ex.: 'set i_a linear 0.0809', 'arm', 'esc 20'). \n Os mesmos comandos funcionam fora da GUI, em qualquer shell: python placa.py <comando>. \n A telemetria não é impressa aqui por padrão (rolaria a tela a 10 Hz): use 'logs on'. \n 'help' lista tudo; 'alvos' lista sensores e parâmetros.", "#ADFF2F")
        self.append_log("SYS", "===============================================================", "#ADFF2F")


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
            if len(parts) == 1:
                for linha in AJUDA.splitlines():
                    self.append_log("HELP", linha, "#FFFFFF")
            self._show_help(self.cmd_config, target=parts[1].lower() if len(parts) > 1 else None)
        elif cmd_root == "alvos":
            sensores, params = self.proto.alvos()
            self.append_log("HELP", "sensores: " + " ".join(sensores), "#FFFFFF")
            self.append_log("HELP", "parâmetros: " + " ".join(params), "#FFFFFF")
        elif cmd_root == "clear": self.clear_shell()
        else:
            # Despacho dinâmico para os demais comandos
            self._execute_command(parts)
            
        self.input.clear()

    def _execute_command(self, parts):
        """Despacho dinâmico: busca métodos como _cmd_native_start."""
        # Tenta do mais específico para o mais geral (ex: native start -> native)
        for i in range(len(parts), 0, -1):
            method_name = "_cmd_" + "_".join(parts[:i]).lower()
            if hasattr(self, method_name):
                getattr(self, method_name)(parts[i:])
                return

        # Comandos da placa (comandos.py): arm, off, estop, esc, reset,
        # force, set, bat, raw.
        root = parts[0].lower()
        if root in ("arm", "off", "estop", "esc", "reset", "force", "set", "bat", "raw"):
            self.enviar_comando(" ".join(parts))
        elif root in self.cmd_config:
            if self._is_complete(parts, self.cmd_config):
                self.append_log("ERR", f"Método Python para '{' '.join(parts)}' não implementado.", "#FF4500")
            else: self.append_log("ERR", "Sintaxe inválida para o comando.", "#FF4500")
        else: self.append_log("ERR", f"Comando '{root}' desconhecido.", "#FF4500")

    def enviar_comando(self, linha):
        """Interpreta uma linha da gramática da placa e envia. Também usado
        pelos diálogos (sensores, células de carga). True se enviou."""
        try:
            cmds = self.proto.interpretar(linha)
        except ErroComando as e:
            self.append_log("ERR", str(e), "#FF4500")
            return False
        for cmd in cmds:
            self.append_log("NET", f"CMD -> [{cmd.hex(' ').upper()}]  {self.proto.descrever(cmd)}", "#00FFFF")
            self.eth.send_command("PLACA", cmd)
        if linha.split()[:2] == ["force", "on"]:
            self.append_log("WARN", "Botão de emergência SOBRESCRITO: a placa ignora o "
                                    "botão físico até 'force off' ou até reiniciar.", "#FFA500")
        return True

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
            # Pega o bloco do comando específico (ex: 'native')
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
            
            # Opcional: recursividade para níveis mais profundos (ex: set i_a linear)
            self._list_subcommands(value, prefix=f"{prefix}{key} ", color=color)

    def log_board_data(self, board, data, decoded=None):
        """Exibe a telemetria das placas no console do terminal.

        DESLIGADO por padrão: a ~10 Hz por placa, isso rola a tela e esconde
        as respostas dos comandos. Liga/desliga com `logs on` / `logs off`.
        """
        if not self.board_logs_enabled:
            return
        # Se a board estiver no modo "quiet" (no-comment), ignoramos o log de BIN/LOG
        if board in self.quiet_mocks:
            return # Não loga nada se estiver no modo quiet

        if self.radio_hex.isChecked():
            msg = data.hex(' ').upper()
        else:
            # Tradução simplificada para o terminal
            msg = ", ".join([f"{k}: {v:.2f}" for k, v in decoded.items() if isinstance(v, float)])
        
        self.append_log("LOG", msg, "#32CD32")

    def closeEvent(self, e):
        # Restaura o stdout original para evitar erros de I/O após fechar
        sys.stdout = self.old_stdout
        # Garante que os mocks/processos native_sim sejam desligados ao fechar o terminal
        self.eth.set_mock_mode("PLACA", False)
        self.eth.stop_native_sim("PLACA")
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
        self.eth.set_mock_mode("PLACA", False)
        print("Todas as simulações paradas.")

    def _cmd_connect(self, args):
        """connect [IP]: troca o IP da placa (se dado) e testa o link."""
        if args:
            self.eth.ip_placa = args[0]
            self.append_log("SYS", f"IP da placa: {args[0]}", "#FFFF00")
        self._hardware_connect_flow()

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

    def _hardware_connect_flow(self):
        ip = self.eth.ip_placa
        self.append_log("NET", f"Tentando conectar com a placa ({ip})...", "#00BFFF")
        ok, local_ip = self.eth.check_subnet(ip)
        if not ok:
            self.append_log("WARN", f"Seu IP ({local_ip}) parece estar em subrede diferente de {ip}!", "#FFA500")
            self.append_log("SYS", "Dica: Configure seu IP fixo para 169.254.97.x", "#DCDCDC")
        self.eth.start_worker("PLACA")
        if self.eth.check_connectivity(ip):
            self.append_log("NET", "Link com a placa estabelecido.", "#32CD32")
        else:
            self.append_log("ERR", "Não foi possível pingar a placa. Verifique cabos.", "#FF4500")

    def _cmd_mock_start(self, args): self._mock_hardware_flow("PLACA", args)
    def _cmd_mock_stop(self, args):
        self.eth.set_mock_mode("PLACA", False)
        self.quiet_mocks.discard("PLACA")

    def _cmd_native_start(self, args): self._native_sim_flow("PLACA")
    def _cmd_native_stop(self, args): self.eth.stop_native_sim("PLACA")
    def _cmd_native_shell(self, args): self._native_shell_flow("PLACA", args)

    def _native_shell_flow(self, board, args):
        """Manda a linha crua ao shell de UART do firmware em native_sim.

        É o shell do PRÓPRIO firmware (lib/SYS/Test_Shell.c), não este
        terminal — por isso a resposta aparece como log da placa, com o
        prefixo [<placa>-native_sim]. Só existe com o firmware rodando como
        processo local; em hardware real o PC fala por Ethernet, e esse shell
        está na UART da placa.
        """
        if not args:
            self.append_log("ERR", "uso: native shell <comando>   (ex.: native shell tele)",
                            "#FF4500")
            return
        line = " ".join(args)
        if not self.eth.native_shell(board, line):
            self.append_log("ERR", "a placa não está em native_sim: use 'native start' antes.",
                            "#FF4500")
            return
        self.append_log("SHELL", f"-> {line}", "#DDA0DD")

    def _native_sim_flow(self, board):
        """Sobe o binário REAL do firmware (`make native_sim` na pasta da
        placa, gera `build_native_sim/zephyr/zephyr.exe`) como processo no
        host e conversa com ele via UDP/loopback (127.0.0.1) — mesmas portas
        do hardware físico. Diferente de 'mock', aqui não é um fake Python:
        é o firmware de verdade (System_Manager/CMD_Core/NCI reais), útil
        para validar o link UDP fim-a-fim sem placa/W-Link. Ver gui/CLAUDE.md."""
        exe_path = self.eth.native_exe_path(board)
        self.append_log("SYS", f"Procurando binário native_sim em {exe_path}...", "#00BFFF")
        if not os.path.exists(exe_path):
            self.append_log("ERR", "Binário não encontrado. Rode 'make native_sim' na raiz do repositório.", "#FF4500")
            return

        if self.eth.start_native_sim(board, exe_path):
            self.append_log("SYS", "Firmware (native_sim) rodando em loopback.", "#ADFF2F")
            self.append_log("SYS", "Esse é o FIRMWARE DE VERDADE (não um fake) — testa TX/RX UDP reais.", "#DCDCDC")
        else:
            self.append_log("ERR", "Falha ao iniciar o native_sim (ver console).", "#FF4500")

    def _cmd_logs_on(self, args):  self._set_board_logs(True)
    def _cmd_logs_off(self, args): self._set_board_logs(False)

    def _set_board_logs(self, on):
        """Liga/desliga a impressão da telemetria das placas neste terminal."""
        self.board_logs_enabled = bool(on)
        if on:
            self.append_log("SYS", "Telemetria das placas LIGADA no terminal "
                                   "(use 'logs off' para silenciar).", "#ADFF2F")
        else:
            self.append_log("SYS", "Telemetria das placas silenciada "
                                   "('logs on' para ver de novo).", "#ADFF2F")

    def _cmd_test_io(self, args): self._test_io_flow("PLACA")

    def _test_io_flow(self, board):
        """Testa RX e TX da placa de forma isolada — envia um CMD neutro e
        verifica se o socket conseguiu transmitir (TX) e se chegou algum
        pacote de telemetria dentro do timeout (RX). Funciona com mock,
        native_sim ou hardware real (eth.test_io só observa contadores do
        BoardWorker)."""
        self.append_log("SYS", f"Testando RX/TX da placa (aguarde ~2s)...", "#00BFFF")
        result = self.eth.test_io(board, timeout=2.0)
        tx_color = "#32CD32" if result["tx_ok"] else "#FF4500"
        rx_color = "#32CD32" if result["rx_ok"] else "#FF4500"
        self.append_log("TEST", f"TX (PC -> placa): {'OK' if result['tx_ok'] else 'FALHOU'}", tx_color)
        self.append_log("TEST", f"RX (placa -> PC): {'OK' if result['rx_ok'] else 'FALHOU'} "
                                 f"({result['rx_packets']} pacote(s))", rx_color)

    def _mock_hardware_flow(self, board, args):
        """Ativa o modo MOCK no gerenciador de rede."""
        if "--no-comment" in args:
            self.quiet_mocks.add(board)
            self.append_log("SYS", f"Logs de telemetria ocultados.", "#FFFF00")
        else:
            self.quiet_mocks.discard(board)

        self.eth.set_mock_mode(board, True)
        self.append_log("SYS", f"Modo MOCK ATIVADO.", "#ADFF2F")
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
        # __file__ é o caminho do TinyTerminal.py (ex: .../gui/GUI/TinyTerminal.py)
        # .parent pega a pasta GUI/
        # .parent.parent pega a raiz gui/
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