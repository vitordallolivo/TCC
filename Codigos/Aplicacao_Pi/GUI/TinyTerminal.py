import ctypes
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
                             QInputDialog) # <--- Adicione este cara
from PySide6.QtGui import QTextCursor
from PySide6.QtCore import QEvent, Slot, Qt
from Communication_Data.Ethernet import EthernetManager


import struct

# Mapeamento conforme sua lógica de firmware
FUNC_TYPES = {"SAME": 1, "SIBA": 2, "SYSTEM": 3}

INSTRUCT_TYPES = {
    "SET": 1, "GET": 2, "START": 3, "STOP": 4, "RESET": 5, "MOCK": 6
}

# IDs de Alvos (target_id) - Diferenciados por Board
TARGET_IDS = {
    "SAME": {
        "MOTOR": 10, "RESISTANCE": 20, "INDUCTANCE": 21, 
        "KS1": 22, "KS2": 23, "BEMF_LPF": 24
    },
    "SIBA": {
        "BATTERY": 10, "RELAY": 11, "TEMP_LIMIT": 12,
        "P0": 30, "P1": 31, "P2": 32, "P3": 33, "P4": 34
    }
}

class TinyTerminal(QWidget):
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
        
        self.history = []
        self.history_index = -1
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
        
        # 2. Inicialização do Gerente de Ethernet
        self.eth = EthernetManager(yaml_siba, yaml_same, handle_callback=self._on_udp_receive)
        self.eth.start()

        # 3. Mapeamento Dinâmico de Targets (Extração segura das configs carregadas)
        self.siba_targets = self._extract_targets_from_config("SIBA")
        self.same_targets = self._extract_targets_from_config("SAME")

        self.old_stdout = sys.stdout
        sys.stdout = self

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
        if text.strip(): self.append_log("PRNT", text.strip(), "#FFD700")
    
    def flush(self): pass

    def append_log(self, pre, msg, col):
        self.output.appendHtml(f'<span style="color:{col};"><b>[{pre}]</b> {msg}</span>')
        self.output.moveCursor(QTextCursor.End)

    def clear_shell(self):
        self.output.clear()

    def _handle_input(self):
        raw = self.input.text().strip()
        if not raw: return
        
        # 1. Histórico
        if not self.history or raw != self.history[0]:
            self.history.insert(0, raw)
        self.history_index = -1
        
        # 2. PROMPT DINÂMICO (CORREÇÃO AQUI)
        # Usa self.current_prompt que é atualizado pelo _update_prompt()
        color = "#FF0000" if self.is_admin else "#00FFFF"
        self.append_log(self.current_prompt, raw, color)
        
        parts = raw.split()
        cmd_root = parts[0].lower()
        
        # --- LÓGICA DE COMANDOS ---
        if cmd_root == "sudo":
            self._logic_sudo()
            self.input.clear() # Limpa e sai para não processar 'sudo' como comando YAML
            return
        elif cmd_root == "exit":
            # Checa o argumento 'all' sem depender da validação do YAML
            if len(parts) > 1 and parts[1].lower() == "all":
                self.append_log("SYS", "Encerrando sistema Albatroz...", "#FF4500")
                QApplication.quit()
            else:
                self.close()
            self.input.clear()
            return
        
        elif cmd_root == "help":
            target = parts[1].lower() if len(parts) > 1 else None
            self._show_help(self.cmd_config, target=target)
        elif cmd_root == "clear":
            self.clear_shell()
        elif cmd_root == "tcpdump":
            # Agora ele passa pela checagem de admin_only automaticamente
            if self.is_admin:
                if len(parts) >= 3:
                    self._execute_tcpdump(parts[1], int(parts[2]))
                else:
                    self.append_log("ERR", "Uso: tcpdump [IP] [tempo]", "#FF4500")
            else:
                self.append_log("ERR", "Este comando exige 'sudo'.", "#FF4500")
        elif cmd_root in self.cmd_config:
            if self._is_complete(parts, self.cmd_config):
                self._execute_command(parts)
            else:
                self.append_log("ERR", f"Comando incompleto. Use 'help {cmd_root}'", "#FF4500")
        else:
            self.append_log("ERR", f"Comando '{cmd_root}' desconhecido.", "#FF4500")
            
        self.input.clear()

    def _is_complete(self, parts, config):
        """Valida comandos baseados no YAML, permitindo argumentos no final (leaf nodes)."""
        current_level = config
        
        for p in parts:
            if isinstance(current_level, dict) and p in current_level:
                current_level = current_level[p]
            else:
                # Chegamos em algo que não está no YAML (ex: o valor '15.5' ou o 'id' do sensor).
                # Se o 'current_level' anterior era um "nó final" (só tem 'help', 'type', etc), é válido!
                if isinstance(current_level, dict):
                    # Filtra as chaves de metadados. Se sobrar algo, é porque faltou subcomando.
                    keys = set(current_level.keys()) - {'help', 'type', 'admin_only', 'args'}
                    if len(keys) == 0:
                        return True 
                return False

        # Se terminamos de ler todas as partes (ex: digitou 'same set ks1')
        # Verifica se 'ks1' é um nó final ou se ainda exige mais coisas.
        if isinstance(current_level, dict):
            keys = set(current_level.keys()) - {'help', 'type', 'admin_only', 'args'}
            if len(keys) > 0:
                return False # O dicionário tem mais filhos, faltou digitar o resto
            
        return True

    def _execute_command(self, parts):
        """Executa a ação baseada no comando validado."""
        root = parts[0].lower()
        
        # --- Lógica do Grupo GRAPH ---
        if root == "graph":
            action = parts[1].lower()
            
            if action == "clear":
                # graph(0) clear(1) [all/1-24](2)
                target = parts[2] if len(parts) > 2 else "all"
                self.parent().clear_graph_buffers(target)
                
            elif action == "mock_data":
                # graph(0) mock_data(1) [start/stop](2) [target](3)
                sub_action = parts[2].lower() if len(parts) > 2 else ""
                
                if sub_action == "start":
                    target = parts[3] if len(parts) > 3 else "all"
                    self.parent().start_mock_with_target(target)
                elif sub_action == "stop":
                    self.parent().worker.stop()
                    print("Mock Simulation parada.")
            return
        
        if root == "connect":
            ip = parts[1] if len(parts) > 1 else "127.0.0.1"
            self._cmd_connect(ip)

        elif root == "ping":
            ip = parts[1] if len(parts) > 1 else None
            self._cmd_ping(ip)

        elif root == "ipconfig":
            self._cmd_ipconfig()

        elif root == "listing":
            port = int(parts[1]) if len(parts) > 1 else 5555
            self._cmd_listing(port)

        elif root == "tcpdump":
            if len(parts) >= 3:
                self._execute_tcpdump(parts[1], int(parts[2]))
            else:
                self.append_log("ERR", "Uso: tcpdump [IP] [tempo]", "#FF4500")


        if root in ["same", "siba"]:
            cmd_struct = self._parse_to_cmd_struct(parts)
            
            if cmd_struct:
                hex_dump = cmd_struct.hex(' ').upper()
                self.append_log("NET", f"CMD -> [{hex_dump}]", "#00FFFF")

                # --- MUDANÇA AQUI: Passamos 'root' explicitamente ---
                traducao = self.decode_hex_to_cmd(
                        hex_dump, 
                        self.eth.configs["SAME"], 
                        self.eth.configs["SIBA"],
                        origin=root.upper() 
                    )

                self.append_log("DECODE", traducao, "#DCDCDC")
                
                if hasattr(self, 'eth'):
                    self.eth.send_command(root.upper(), cmd_struct)

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
        if self.radio_hex.isChecked():
            msg = data.hex(' ').upper()
        else:
            msg = str(decoded)
        
        col = "#32CD32" if board == "SIBA" else "#1E90FF"
        self.append_log("LOG", f"({board}) {msg}", col)

    def closeEvent(self, e):
        # Restaura o stdout original para evitar erros de I/O após fechar
        sys.stdout = self.old_stdout
        e.accept()

    def _cmd_connect(self, ip):
        self.append_log("SYS", f"Alterando alvo de conexão para: {ip}", "#FFFF00")
        # Atualiza os IPs nos gerentes de rede da MainGUI
        self.parent().eth.ip_siba = ip
        self.parent().eth.ip_same = ip
        print(f"IP de destino global definido como {ip}")

    def _cmd_ping(self, ip):
        """Ping 'Lightweight' usando tentativa de conexão TCP (Porta 80 ou 7)."""
        if not ip:
            self.append_log("ERR", "Uso: ping [IP/URL]", "#FF4500")
            return
        
        self.append_log("NET", f"Testando conectividade com {ip}...", "#00FF7F")
        try:
            # Tenta abrir um socket simples para verificar se o host responde
            socket.setdefaulttimeout(2)
            socket.socket(socket.AF_INET, socket.SOCK_STREAM).connect((ip, 80))
            self.append_log("NET", f"Resposta de {ip}: Conexão TCP aceita.", "#32CD32")
        except Exception as e:
            self.append_log("ERR", f"Falha no ping: {e}", "#FF4500")

    def _cmd_ipconfig(self):
        """Versão portátil do ipconfig usando psutil."""
        self.append_log("SYS", "--- Interfaces de Rede Detectadas ---", "#ADFF2F")
        interfaces = psutil.net_if_addrs()
        for name, snics in interfaces.items():
            for snic in snics:
                if snic.family == socket.AF_INET: # Filtra apenas IPv4
                    self.append_log("NET", f"{name}: {snic.address}", "#DCDCDC")

    def _cmd_listing(self, port):
        """Verifica se uma porta local está ocupada ou livre."""
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            result = s.connect_ex(('127.0.0.1', port))
            if result == 0:
                self.append_log("NET", f"Porta {port}: OCUPADA (Listening)", "#FFD700")
            else:
                self.append_log("NET", f"Porta {port}: LIVRE", "#32CD32")

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
        try:
            # Tenta ler como texto (útil para logs de debug vindos da placa)
            mensagem = data.decode('utf-8').strip()
            self.append_log(f"RX-{board}", mensagem, "#00FF00")
        except UnicodeDecodeError:
            # Se não for texto (ex: pacote binário de telemetria bruta), mostra em HEX
            hex_data = data.hex(' ').upper()
            self.append_log(f"RX-{board}", f"BIN: {hex_data}", "#8A2BE2")

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
                    # Detecta se o alvo pertence aos parâmetros de bateria
                    bat_keywords = ["BATT", "TEMP", "P0", "P1", "P2", "P3", "P4", "RELAY", "SOC"]
                    if any(x in target_raw for x in bat_keywords):
                        search_func = "FUNC_BATTERY"
                    else:
                        search_func = "FUNC_SENSOR"
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
            target_val = 0
            targets_dict = self.siba_targets if root == "SIBA" else self.same_targets
            
            # Busca exata primeiro, depois substring
            exact_key = f"TARGET_{target_raw}"
            if exact_key in targets_dict:
                target_val = targets_dict[exact_key]
            else:
                for name, idx in targets_dict.items():
                    if target_raw in name:
                        target_val = idx
                        break
                else:
                    target_val = targets_dict.get("TARGET_DEFAULT", 10)

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