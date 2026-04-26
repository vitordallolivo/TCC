from PySide6.QtWidgets import (QDialog, QVBoxLayout, QFormLayout, QGroupBox, 
                               QDoubleSpinBox, QPushButton, QHBoxLayout, QMessageBox)
import yaml
import os

from Communication_Data.MOCK_Ethernet import pack_same_cmd

class SAMEConfigDialog(QDialog):
    def __init__(self, prefs, config_dir, ethernet_mgr, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Configuração SAME (Inversor & Observador)")
        self.resize(400, 600)
        
        self.prefs = prefs
        self.ethernet = ethernet_mgr
        self.same_yaml = self._load_yaml(os.path.join(config_dir, "SAME.yaml"))
        self.enums = self.same_yaml.get('enums', {})
        
        self.inputs = {} # Dicionário para armazenar as referências dos SpinBoxes
        self.init_ui()
        self.load_current_values()

    def _load_yaml(self, path):
        with open(path, 'r', encoding='utf-8') as f:
            return yaml.safe_load(f)

    def init_ui(self):
        main_layout = QVBoxLayout(self)

        # --- Grupo 1: Parâmetros do Motor ---
        motor_group = QGroupBox("Parâmetros Físicos do Motor")
        motor_layout = QFormLayout()
        self.add_spinbox(motor_layout, "pole_pairs", "Pares de Polos", 1, 100, 0)
        self.add_spinbox(motor_layout, "resistance", "Resistência (Ohms)", 0.001, 5.0, 4)
        self.add_spinbox(motor_layout, "inductance", "Indutância (H)", 0.000001, 1.0, 6)
        self.add_spinbox(motor_layout, "kv_rating", "KV Rating", 10, 5000, 0)
        motor_group.setLayout(motor_layout)
        main_layout.addWidget(motor_group)

        # --- Grupo 2: Observador (SMO) ---
        smo_group = QGroupBox("Parâmetros do Observador (SMO)")
        smo_layout = QFormLayout()
        self.add_spinbox(smo_layout, "ks1", "Ganho KS1", 0, 10000, 1)
        self.add_spinbox(smo_layout, "ks2", "Ganho KS2", 0, 10000, 1)
        self.add_spinbox(smo_layout, "bemf_lpf", "BEMF LPF (Hz)", 1, 1000, 1)
        self.add_spinbox(smo_layout, "torque_lpf", "Torque LPF (Hz)", 0.1, 500, 1)
        smo_group.setLayout(smo_layout)
        main_layout.addWidget(smo_group)

        # Botões
        btn_layout = QHBoxLayout()
        btn_apply = QPushButton("Aplicar no Hardware")
        btn_apply.clicked.connect(self.apply_and_send)
        btn_cancel = QPushButton("Cancelar")
        btn_cancel.clicked.connect(self.reject)
        btn_layout.addWidget(btn_cancel)
        btn_layout.addWidget(btn_apply)
        main_layout.addLayout(btn_layout)

    def add_spinbox(self, layout, key, label, min_val, max_val, decimals):
        spin = QDoubleSpinBox()
        spin.setRange(min_val, max_val)
        spin.setDecimals(decimals)
        self.inputs[key] = spin
        layout.addRow(label, spin)

    def load_current_values(self):
        """Carrega os dados das preferências, ou usa o SAME.yaml dinamicamente sem hardcode."""
        saved = self.prefs.get("same_config", {})
        
        # Se não houver configuração salva no user_prefs, busca do SAME.yaml
        if not saved:
            motor_name = self.prefs.get("last_motor_name")
            motores_yaml = self.same_yaml.get('motors', {})
            
            # Se o motor não existir ou for "Nenhum", pega o primeiro motor do YAML como fallback (Default)
            if motor_name not in motores_yaml and motores_yaml:
                motor_name = list(motores_yaml.keys())[0]
                print(f"Aviso: Motor não selecionado. Usando {motor_name} como padrão.")

            motor_data = motores_yaml.get(motor_name, {})
            
            if motor_data:
                # Extrai os sub-blocos (se não existirem, retorna dict vazio)
                phys = motor_data.get('physical_params', {})
                smo = motor_data.get('smo_config', {})
                
                # Lê os valores estritamente do YAML. Se faltar a chave no YAML, assume 0.
                saved = {
                    "pole_pairs": phys.get('pole_pairs', 0),
                    "resistance": phys.get('resistance_ohm', 0.0),
                    "inductance": phys.get('inductance_henry', 0.0),
                    "kv_rating": phys.get('kv_rating', 0),
                    "ks1": smo.get('ks1', 0.0),
                    "ks2": smo.get('ks2', 0.0),
                    "bemf_lpf": smo.get('bemf_lpf_hz', 0.0),
                    "torque_lpf": smo.get('torque_filter_hz', 0.0)
                }

        # Preenche a UI
        for key, spin in self.inputs.items():
            # Pega o valor salvo/lido, ou o mínimo do spinbox se o YAML estiver mal formatado
            valor = saved.get(key)
            if valor is not None and valor > 0:
                spin.setValue(valor)
            else:
                spin.setValue(spin.minimum())

    def get_enum_val(self, enum_category, enum_string):
        """Busca o valor inteiro correspondente ao Enum do YAML."""
        try:
            return self.enums[enum_category].index(enum_string)
        except ValueError:
            print(f"Erro: Enum {enum_string} não encontrado em {enum_category}.")
            return 0

    def apply_and_send(self):
        """Gera os CMDs e despacha via Ethernet com log detalhado para o TinyTerminal."""
        # 1. Salvar no user_prefs
        new_config = {key: spin.value() for key, spin in self.inputs.items()}
        self.prefs["same_config"] = new_config
        
        # 2. Resgatar valores base dos enums
        func_name = "FUNC_SENSOR"
        instr_name = "INSTR_WRITE"
        act_name = "DEFAULT_ACTION_SET_VALUE"

        func = self.get_enum_val("func_type", func_name)
        instr = self.get_enum_val("instruct_type", instr_name)
        action = self.get_enum_val("default_action", act_name)

        # 3. Mapeamento dos inputs da GUI para os TARGETS do YAML
        mapping = {
            "pole_pairs": "TARGET_POLES_PAIRS",
            "resistance": "TARGET_RESISTANCE",
            "inductance": "TARGET_INDUCTANCE",
            "kv_rating":  "TARGET_KV_RATING",
            "ks1":        "TARGET_KS1",
            "ks2":        "TARGET_KS2",
            "bemf_lpf":   "TARGET_BEMF_LPF_FREQUENCY",
            "torque_lpf": "TARGET_TORQUE_LPF_FREQUENCY"
        }

        print("\n" + "="*50)
        print(f"--- TRANSMISSÃO DE CONFIGURAÇÃO: SAME ---")
        print(f"Modo: {func_name} | Instrução: {instr_name}")
        print(f"Ação Base: {act_name}")
        print("-" * 50)

        # 4. Geração e envio do lote de comandos
        success_count = 0
        for gui_key, target_str in mapping.items():
            try:
                target_id = self.get_enum_val("target_type", target_str)
                value = new_config[gui_key]
                
                # Print de depuração por parâmetro
                print(f"[TX] {target_str:<30} | ID: {target_id:<3} | Val: {value:.6f}")
                
                # Empacota no formato binário C-compatible
                cmd_payload = pack_same_cmd(func, instr, target_id, value, action)
                
                # Log do payload binário (útil para ver se o Endianness está correto)
                # print(f"      Hex: {cmd_payload.hex(' ').upper()}")

                # Envia via Ethernet
                # Nota: Certifique-se que self.ethernet.SAME existe ou use self.ethernet.send_command("SAME", ...)
                self.ethernet.send_command("SAME", cmd_payload)
                success_count += 1
                
            except Exception as e:
                print(f"[ERRO] Falha ao enviar {gui_key}: {e}")

        print("-" * 50)
        print(f"Sucesso: {success_count}/{len(mapping)} parâmetros enviados.")
        print("="*50 + "\n")
        
        self.accept()