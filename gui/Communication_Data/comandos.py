"""Camada 2 · Domínio — linguagem de comandos da placa (pura, sem I/O).

Uma linha de texto vira um ou mais `CMD` de 8 bytes (`<BBBfb>`), com os
índices tirados de `Config/protocolo.yaml`. É a mesma gramática no
TinyTerminal, no CLI (`placa.py`) e nos diálogos da GUI, então um comando
testado num lugar vale nos outros.

    arm                          pronto: pré-carga -> potência -> ESC armado
    off                          desarma o ESC e abre os relés
    estop                        parada de emergência
    esc <0..100>                 throttle em %
    reset                        reinicia o firmware
    force on|off                 ignora / volta a respeitar o botão físico
    set <sensor> linear|offset|min|max <valor>
    set <sensor> reset           volta a calibração ao default
    set <parâmetro> <valor>      parâmetro do motor/observador (vai ao NVM)
    set <parâmetro> reset        volta o parâmetro ao default do firmware
    bat p0..p4|temp_limit|relay <valor>
    raw <f> <i> <t> <v> <a>      CMD cru pelos índices numéricos

Sensores: nomes da telemetria (`v_batt`, `i_batt`, `v_a`..`i_c`,
`load_cell_0`..`5`, ...). Parâmetros: nome do alvo sem `TARGET_`
(`resistance`, `gamma1`, `ts_adc`, ...).
"""
import os

import yaml

from Communication_Data import codecs

PROTOCOLO_YAML = "protocolo.yaml"

# Campos de calibração/limite de sensor -> default_action.
CAMPOS_SENSOR = {
    "linear": "DEFAULT_ACTION_SENSE_LINEAR_COEF",
    "offset": "DEFAULT_ACTION_SENSE_OFFSET_COEF",
    "reset":  "DEFAULT_ACTION_SENSE_RESET_DEFAULT",
    "max":    "DEFAULT_ACTION_SENSE_MAX_VALID",
    "min":    "DEFAULT_ACTION_SENSE_MIN_VALID",
}

# Nomes de telemetria / apelidos -> alvo do protocolo. O resto resolve por
# `TARGET_<NOME>` direto (ex.: resistance -> TARGET_RESISTANCE).
APELIDOS = {
    "V_BATT": "TARGET_VOLTAGE_DIVIDOR",
    "I_BATT": "TARGET_CURRENT_SENSOR",
    "AIR_SPEED": "TARGET_PITOT_TUBE",
    "PITOT": "TARGET_PITOT_TUBE",
    "TEMPERATURE_BATTERY": "TARGET_TEMPERATURE_SENSOR",
    "TEMP_SENSOR": "TARGET_TEMPERATURE_SENSOR",
    "SOC": "TARGET_BATTERY_SOC",
    "V_A": "TARGET_PHASE_V_A", "V_B": "TARGET_PHASE_V_B", "V_C": "TARGET_PHASE_V_C",
    "I_A": "TARGET_PHASE_I_A", "I_B": "TARGET_PHASE_I_B", "I_C": "TARGET_PHASE_I_C",
    "POLE_PAIRS": "TARGET_POLES_PAIRS",
    "KV": "TARGET_KV_RATING",
    "BEMF_LPF": "TARGET_BEMF_LPF_FREQUENCY",
    "TORQUE_LPF": "TARGET_TORQUE_LPF_FREQUENCY",
}
for _i in range(6):
    APELIDOS[f"LOAD_CELL_{_i}"] = f"TARGET_LOAD_CELL_{_i}_enum"

# Parâmetros do motor/observador: faixa contígua do enum (o firmware trata
# TARGET_POLES_PAIRS..TARGET_TS_ADC como parâmetros, não sensores).
PRIMEIRO_PARAM, ULTIMO_PARAM = "TARGET_POLES_PAIRS", "TARGET_TS_ADC"

BATERIA = {
    "p0": "BAT_ACTION_P0_OVERWRITE", "p1": "BAT_ACTION_P1_OVERWRITE",
    "p2": "BAT_ACTION_P2_OVERWRITE", "p3": "BAT_ACTION_P3_OVERWRITE",
    "p4": "BAT_ACTION_P4_OVERWRITE", "temp_limit": "BAT_ACTION_TEMP_LIMIT_OVERWRITE",
    "relay": "BAT_ACTION_RELAY",
}

AJUDA = __doc__.split("\n\n")[1] + "\n\n" + __doc__.split("\n\n")[2]


class ErroComando(ValueError):
    """Linha que não forma um CMD válido (mensagem pronta para o usuário)."""


def carregar_protocolo(config_dir):
    """Lê `Config/protocolo.yaml` (dict bruto)."""
    with open(os.path.join(config_dir, PROTOCOLO_YAML), encoding="utf-8") as f:
        return yaml.safe_load(f) or {}


class Protocolo:
    """Enums do protocolo + montagem e tradução de CMDs."""

    def __init__(self, raw_yaml):
        self.raw = raw_yaml or {}
        self.enums = self.raw.get("enums", {})

    @classmethod
    def de_pasta(cls, config_dir):
        return cls(carregar_protocolo(config_dir))

    # ---------------------------------------------------------------- enums
    def idx(self, categoria, nome):
        lst = self.enums.get(categoria, [])
        if nome not in lst:
            raise ErroComando(f"{nome} não existe em {categoria} do "
                              f"{PROTOCOLO_YAML} (regere o protocolo?)")
        return lst.index(nome)

    def nome(self, categoria, indice):
        lst = self.enums.get(categoria, [])
        return lst[indice] if 0 <= indice < len(lst) else f"?{indice}"

    def alvo(self, nome):
        """Nome digitado (sensor ou parâmetro) -> índice de target_type."""
        chave = nome.upper()
        alvos = self.enums.get("target_type", [])
        for candidato in (APELIDOS.get(chave), f"TARGET_{chave}", chave):
            if candidato and candidato in alvos:
                return alvos.index(candidato)
        raise ErroComando(f"alvo desconhecido: {nome}  (use 'alvos')")

    def e_parametro(self, indice):
        return self.idx("target_type", PRIMEIRO_PARAM) <= indice <= \
            self.idx("target_type", ULTIMO_PARAM)

    def alvos(self):
        """(sensores, parâmetros) em nomes curtos, para ajuda/autocompletar."""
        inverso = {}
        for k, v in APELIDOS.items():
            inverso.setdefault(v, k.lower())
        sensores, params = [], []
        for i, t in enumerate(self.enums.get("target_type", [])):
            if t == "TARGET_DEFAULT":
                continue
            curto = inverso.get(t, t.removeprefix("TARGET_").lower())
            (params if self.e_parametro(i) else sensores).append(curto)
        return sensores, params

    # ------------------------------------------------------------- montagem
    def cmd(self, func, instr="INSTR_WRITE", alvo="TARGET_DEFAULT", valor=0.0,
            acao=None, categoria_acao="status_type"):
        f = self.idx("func_type", func)
        i = self.idx("instruct_type", instr)
        t = alvo if isinstance(alvo, int) else self.idx("target_type", alvo)
        a = self.idx(categoria_acao, acao) if acao else 0
        return codecs.encode_cmd(f, i, t, valor, a)

    def interpretar(self, linha):
        """Linha de texto -> lista de CMDs (bytes). Levanta ErroComando."""
        p = linha.split()
        if not p:
            return []
        c, a = p[0].lower(), p[1:]

        if c == "arm":
            return [self.cmd("FUNC_START", valor=1.0, acao="STATUS_OK")]
        if c == "off":
            return [self.cmd("FUNC_START", valor=0.0, acao="STATUS_ERROR")]
        if c == "estop":
            return [self.cmd("FUNC_STOP")]
        if c == "reset":
            return [self.cmd("FUNC_RESET", valor=1.0)]
        if c == "esc":
            v = _numero(a, 0, "esc <0..100>")
            if not 0.0 <= v <= 100.0:
                raise ErroComando("throttle fora de 0..100 %")
            return [self.cmd("FUNC_ESC", valor=v)]
        if c == "force":
            if not a or a[0].lower() not in ("on", "off"):
                raise ErroComando("uso: force on|off")
            st = "STATUS_EMERGENCY_OVERRIDE" if a[0].lower() == "on" else "STATUS_EMERGENCY_RESTORE"
            return [self.cmd("FUNC_START", acao=st)]
        if c == "set":
            return [self._set(a)]
        if c == "bat":
            if len(a) != 2 or a[0].lower() not in BATERIA:
                raise ErroComando("uso: bat p0..p4|temp_limit|relay <valor>")
            return [self.cmd("FUNC_BATTERY", valor=_numero(a, 1, "bat"),
                             acao=BATERIA[a[0].lower()], categoria_acao="battery_action")]
        if c == "raw":
            if len(a) != 5:
                raise ErroComando("uso: raw <func> <instr> <target> <valor> <acao>")
            try:
                return [codecs.encode_cmd(int(a[0]), int(a[1]), int(a[2]),
                                          float(a[3]), int(a[4]))]
            except ValueError:
                raise ErroComando("raw: índices inteiros e valor numérico")
        raise ErroComando(f"comando desconhecido: {c}  (use 'help')")

    def _set(self, a):
        uso = ("uso: set <sensor> linear|offset|min|max <valor> | "
               "set <sensor> reset | set <parâmetro> <valor>")
        if len(a) < 2:
            raise ErroComando(uso)
        t = self.alvo(a[0])
        campo = a[1].lower()
        if self.e_parametro(t):
            if campo == "reset":
                return self.cmd("FUNC_SENSOR", alvo=t, acao=CAMPOS_SENSOR["reset"],
                                categoria_acao="default_action")
            return self.cmd("FUNC_SENSOR", alvo=t, valor=_numero(a, 1, uso),
                            acao="DEFAULT_ACTION_NONE", categoria_acao="default_action")
        if campo not in CAMPOS_SENSOR:
            raise ErroComando(uso)
        valor = 0.0 if campo == "reset" else _numero(a, 2, uso)
        return self.cmd("FUNC_SENSOR", alvo=t, valor=valor,
                        acao=CAMPOS_SENSOR[campo], categoria_acao="default_action")

    # ------------------------------------------------------------- tradução
    def descrever(self, cmd):
        """CMD (bytes) -> texto legível com o nome de cada campo."""
        d = codecs.decode_cmd(cmd)
        if d is None:
            return "CMD curto"
        f, i, t, v, a = d
        fn = self.nome("func_type", f)
        cat = {"FUNC_SENSOR": "default_action",
               "FUNC_BATTERY": "battery_action"}.get(fn, "status_type")
        return (f"{fn} | {self.nome('instruct_type', i)} | "
                f"{self.nome('target_type', t)} | {v:g} | {self.nome(cat, a)}")


def _numero(args, pos, uso):
    try:
        return float(args[pos])
    except (IndexError, ValueError):
        raise ErroComando(uso)
