"""Camada 2/3 — perfis de motor em `.yaml` (Config/motors/) e CMDs de aplicação.

Cada motor é um `.yaml` em `Config/motors/` (salvo pelo diálogo "Criar",
listado no "Carregar", removível no "Apagar"). Formato igual ao bloco `motors`
do SAME.yaml:

    name: MeuMotor
    physical_params: { pole_pairs, resistance_ohm, inductance_henry, kv_rating }
    smo_config:      { ks1, ks2, bemf_lpf_hz, torque_filter_hz }

`build_motor_cmds` gera os CMDs (8 bytes) para aplicar os parâmetros no hardware
SAME (FUNC_SENSOR / INSTR_WRITE / DEFAULT_ACTION_SET_VALUE por target).
"""
import os
import struct

import yaml

# chave interna (usada pela GUI) -> TARGET_* do SAME.yaml
MOTOR_TARGET_MAP = {
    "pole_pairs": "TARGET_POLES_PAIRS",
    "resistance": "TARGET_RESISTANCE",
    "inductance": "TARGET_INDUCTANCE",
    "kv_rating":  "TARGET_KV_RATING",
    "ks1":        "TARGET_KS1",
    "ks2":        "TARGET_KS2",
    "bemf_lpf":   "TARGET_BEMF_LPF_FREQUENCY",
    "torque_lpf": "TARGET_TORQUE_LPF_FREQUENCY",
}


def motors_dir(config_dir):
    return os.path.join(config_dir, "motors")


def list_motor_files(config_dir):
    """{nome: caminho} dos .yaml de motor em Config/motors/."""
    d = motors_dir(config_dir)
    out = {}
    if os.path.isdir(d):
        for f in sorted(os.listdir(d)):
            if f.endswith((".yaml", ".yml")):
                out[os.path.splitext(f)[0]] = os.path.join(d, f)
    return out


def params_from_motor_dict(md):
    """Extrai o dict interno de parâmetros de um bloco de motor."""
    md = md or {}
    phys = md.get("physical_params", {}) or {}
    smo = md.get("smo_config", {}) or {}
    return {
        "pole_pairs": phys.get("pole_pairs", 0),
        "resistance": phys.get("resistance_ohm", 0.0),
        "inductance": phys.get("inductance_henry", 0.0),
        "kv_rating": phys.get("kv_rating", 0),
        "ks1": smo.get("ks1", 0.0),
        "ks2": smo.get("ks2", 0.0),
        "bemf_lpf": smo.get("bemf_lpf_hz", 0.0),
        "torque_lpf": smo.get("torque_filter_hz", 0.0),
    }


def load_motor_file(path):
    with open(path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}
    return params_from_motor_dict(data)


def motor_dict_from_params(name, params):
    return {
        "name": name,
        "physical_params": {
            "pole_pairs": params.get("pole_pairs", 0),
            "resistance_ohm": params.get("resistance", 0.0),
            "inductance_henry": params.get("inductance", 0.0),
            "kv_rating": params.get("kv_rating", 0),
        },
        "smo_config": {
            "ks1": params.get("ks1", 0.0),
            "ks2": params.get("ks2", 0.0),
            "bemf_lpf_hz": params.get("bemf_lpf", 0.0),
            "torque_filter_hz": params.get("torque_lpf", 0.0),
        },
    }


def save_motor_file(config_dir, name, params):
    """Salva o motor como Config/motors/<name>.yaml. Retorna o caminho."""
    d = motors_dir(config_dir)
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, f"{name}.yaml")
    with open(path, "w", encoding="utf-8") as f:
        yaml.safe_dump(motor_dict_from_params(name, params), f,
                       sort_keys=False, allow_unicode=True)
    return path


def build_motor_cmds(same_protocol_cfg, params):
    """Lista de CMDs (8 bytes `<BBBfb>`) p/ aplicar os params no hardware SAME."""
    enums = same_protocol_cfg.get("enums", {})

    def idx(cat, name, default=0):
        lst = enums.get(cat, [])
        return lst.index(name) if name in lst else default

    func = idx("func_type", "FUNC_SENSOR")
    instr = idx("instruct_type", "INSTR_WRITE")
    action = idx("default_action", "DEFAULT_ACTION_SET_VALUE")
    cmds = []
    for key, target in MOTOR_TARGET_MAP.items():
        if key not in params:
            continue
        target_id = idx("target_type", target)
        cmds.append(struct.pack("<BBBfb", func, instr, target_id, float(params[key]), action))
    return cmds
