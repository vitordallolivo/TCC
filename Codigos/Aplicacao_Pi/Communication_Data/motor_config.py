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
import math
import os
import struct

import yaml


def ke_kt_from_kv(kv, pole_pairs, connection="Y"):
    """Deriva Ke/Kt do Kv COMERCIAL do motor.

    O modelo/firmware trabalha em ESTRELA (Y). Se o motor é Delta, converte o Kv
    para o equivalente Y (Y tem Ke maior e Kv menor): `Kv_Y = Kv_delta / √3`.
    Depois: `Ke_mec = 60/(2π·Kv_Y)` [V·s/rad] — numericamente igual a
    `Kt` [N·m/A]. `Ke` por par de polos = `Ke_mec / p`.
    Retorna dict {kv_y, ke_mec, kt, ke}."""
    conn = (connection or "Y").strip().lower()
    kv = float(kv or 0.0)
    kv_y = kv / math.sqrt(3.0) if conn.startswith("d") else kv
    ke_mec = 60.0 / (2.0 * math.pi * kv_y) if kv_y > 0 else 0.0
    ke = ke_mec / pole_pairs if pole_pairs else 0.0
    return {"kv_y": kv_y, "ke_mec": ke_mec, "kt": ke_mec, "ke": ke}

# chave interna (usada pela GUI) -> TARGET_* do SAME.yaml
# Ganhos do SMO na forma Gamma (Γ = K·Ts, invariante ao passo) — NÃO Ks* (que
# dependem de Ts). Ver ../../SAME/CLAUDE.md.
MOTOR_TARGET_MAP = {
    "pole_pairs": "TARGET_POLES_PAIRS",
    "resistance": "TARGET_RESISTANCE",
    "inductance": "TARGET_INDUCTANCE",
    "kv_rating":  "TARGET_KV_RATING",
    "gamma1":     "TARGET_GAMMA1",
    "gamma2":     "TARGET_GAMMA2",
    "gammat":     "TARGET_GAMMAT",
    "bemf_lpf":   "TARGET_BEMF_LPF_FREQUENCY",
    "torque_lpf": "TARGET_TORQUE_LPF_FREQUENCY",
    "ts_adc_us":  "TARGET_TS_ADC",
    "ts_udp_ms":  "TARGET_TS_UDP",
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
    samp = md.get("sampling", {}) or {}
    pp = phys.get("pole_pairs", 0)
    poles = phys.get("poles", (pp * 2 if pp else 0))
    pole_pairs = pp or (poles // 2 if poles else 0)
    return {
        "poles": poles,
        "pole_pairs": pole_pairs,
        "connection": phys.get("connection", "Y"),   # "Y" (estrela) ou "Delta"
        "kv_rating": phys.get("kv_rating", 0),
        # R e L já em Y (estrela) por padrão — ver ../SAME/CLAUDE.md.
        "resistance": phys.get("resistance_ohm", 0.0),
        "inductance": phys.get("inductance_henry", 0.0),
        "v_bus": phys.get("v_bus", 0.0),
        # Ganhos do SMO em Gamma (Γ = K·Ts). Aceita ks1/ks2 antigos por retrocompat.
        "gamma1": smo.get("gamma1", smo.get("ks1", 0.0)),
        "gamma2": smo.get("gamma2", smo.get("ks2", 0.0)),
        "gammat": smo.get("gammat", 0.0),
        "bl": smo.get("bl", 0.0),                     # camada de contorno
        "switch_func": smo.get("switch_func", "tanh"),
        "pll_wn": smo.get("pll_wn_hz", 0.0),
        "bemf_lpf": smo.get("bemf_lpf_hz", 0.0),
        "torque_lpf": smo.get("torque_filter_hz", 0.0),
        # Amostragem: ADC rápido (µs) e telemetria UDP (ms) desacoplados p/ o UDP
        # não travar a operação do ADC.
        "ts_adc_us": samp.get("ts_adc_us", 0.0),
        "ts_udp_ms": samp.get("ts_udp_ms", 0.0),
    }


def load_motor_file(path):
    with open(path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}
    return params_from_motor_dict(data)


def motor_dict_from_params(name, params):
    return {
        "name": name,
        "physical_params": {
            "poles": params.get("poles", 0),
            "pole_pairs": params.get("pole_pairs", 0),
            "connection": params.get("connection", "Y"),
            "kv_rating": params.get("kv_rating", 0),
            "resistance_ohm": params.get("resistance", 0.0),
            "inductance_henry": params.get("inductance", 0.0),
            "v_bus": params.get("v_bus", 0.0),
        },
        "smo_config": {
            "gamma1": params.get("gamma1", 0.0),
            "gamma2": params.get("gamma2", 0.0),
            "gammat": params.get("gammat", 0.0),
            "bl": params.get("bl", 0.0),
            "switch_func": params.get("switch_func", "tanh"),
            "pll_wn_hz": params.get("pll_wn", 0.0),
            "bemf_lpf_hz": params.get("bemf_lpf", 0.0),
            "torque_filter_hz": params.get("torque_lpf", 0.0),
        },
        "sampling": {
            "ts_adc_us": params.get("ts_adc_us", 0.0),
            "ts_udp_ms": params.get("ts_udp_ms", 0.0),
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
