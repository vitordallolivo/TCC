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

# Ponto de partida do diálogo "Criar Motor" quando não há perfil salvo. São os
# valores REAIS da bancada — motor Scorpion A-5025-310Kv — e os defaults que o
# firmware da SAME traz compilados (DEF_* em lib/SYS/Observer_Manager.c), para
# o formulário abrir coerente com a placa em vez de tudo zerado.
#
# Procedência:
#   físicos   : P = 14 do motor; R e L do ensaio de rotor bloqueado do TCC,
#               já por fase em Y (R_Y = R_LL/2 = 8,69 mΩ; L_Y = 6,95 µH).
#               Kv 310 é usado direto, sem √3 (conexão "Y").
#   ganhos Γ  : tabela do autotune no TCC (hélice APC 15x6E). Γ carrega Ts
#               embutido: mudou Ts, roda o autotune de novo em vez de
#               reescalar.
#   amostragem: ts_adc_us = OBS_TS do firmware.
MOTOR_DEFAULTS = {
    "poles": 14,
    "pole_pairs": 7,
    "connection": "Y",         # Kv já é o valor a usar: sem conversão por √3
    "kv_rating": 310.0,
    "resistance": 8.69e-3,     # Ω por fase, em Y (TCC)
    "inductance": 6.95e-6,     # H por fase, em Y (TCC)
    "v_bus": 30.0,
    "gamma1": 31.62,
    "gamma2": -10.00,
    "gammat": 0.154,
    "bl": 18.0,
    "switch_func": "tanh",
    "pll_wn": 150.0,
    "bemf_lpf": 0.0,           # 0 = desligado (a PLL usa ê direto)
    "torque_lpf": 10000.0,
    "ts_adc_us": 10.0,
    "ts_udp_ms": 100.0,
}


def check_conditions(p):
    """Confere as condições de validade do observador sobre um dict de
    parâmetros. Devolve lista de (nivel, texto), nivel in {'erro','aviso'}.

    São as mesmas listadas em SAME/CLAUDE.md ("Limites a checar em
    Observer_Set_Param") e que até aqui não eram verificadas em lugar nenhum,
    nem na GUI nem no firmware. Violá-las não gera erro: gera estimativa
    errada em silêncio, que é pior.
    """
    out = []
    R  = float(p.get("resistance") or 0.0)
    L  = float(p.get("inductance") or 0.0)
    ts = float(p.get("ts_adc_us") or 0.0) * 1e-6
    g1 = float(p.get("gamma1") or 0.0)
    g2 = float(p.get("gamma2") or 0.0)
    gt = float(p.get("gammat") or 0.0)
    bl = float(p.get("bl") or 0.0)

    # Sinais fixos: é regra do projeto, não preferência.
    if g1 <= 0.0:
        out.append(("erro", "Γ1 tem de ser > 0."))
    if g2 >= 0.0:
        out.append(("erro", "Γ2 tem de ser < 0."))
    if gt <= 0.0:
        out.append(("erro", "Γt tem de ser > 0."))
    elif gt >= 1.0:
        # Φ_e = 1 − Γt é o polo do observador de BEMF.
        out.append(("erro", f"Γt = {gt:g} ≥ 1: o polo da BEMF Φ_e = 1−Γt "
                            f"sai do círculo unitário e o observador diverge."))

    # Euler-forward na malha de corrente exige passo bem abaixo da constante
    # elétrica; a partir de 2L/R o fator Φ_i = 1 − R·Ts/L fica ≤ −1.
    if R > 0.0 and L > 0.0 and ts > 0.0:
        ts_max = 2.0 * L / R
        if ts >= ts_max:
            out.append(("erro", f"Ts = {ts*1e6:.2f} µs ≥ 2L/R = "
                                f"{ts_max*1e6:.2f} µs: a discretização de "
                                f"Euler da corrente fica instável."))
        phi_i = 1.0 - (R * ts / L)
        # Chattering: com BL pequeno demais o termo de comutação domina e a
        # corrente estimada oscila em vez de deslizar.
        if (1.0 + phi_i) > 0.0 and g1 > 0.0:
            bl_min = g1 / (1.0 + phi_i)
            if bl > 0.0 and bl <= bl_min:
                out.append(("erro", f"BL = {bl:g} ≤ Γ1/(1+Φi) = {bl_min:.2f}: "
                                    f"o observador entra em chattering."))
            elif bl > 0.0 and bl < 1.2 * bl_min:
                out.append(("aviso", f"BL = {bl:g} com folga de só "
                                     f"{bl/bl_min:.2f}× sobre o limite de "
                                     f"chattering ({bl_min:.2f})."))

    # O observador de BEMF satura em |Γ2|/Γt: abaixo da BEMF real ele não
    # consegue representar o sinal e a PLL trava num ângulo errado.
    if gt > 0.0 and g2 != 0.0:
        bemf_max = abs(g2) / gt
        v_bus = float(p.get("v_bus") or 0.0)
        if v_bus > 0.0:
            bemf_esperada = v_bus / 2.0
            if bemf_max < bemf_esperada:
                out.append(("aviso", f"Teto de BEMF do observador "
                                     f"|Γ2|/Γt = {bemf_max:.1f} V está abaixo "
                                     f"dos ~{bemf_esperada:.1f} V esperados "
                                     f"no barramento: satura em rotação alta."))
    return out


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
