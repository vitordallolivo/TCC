"""Camada 2 · Domínio/Protocolo — codecs puros (sem I/O, sem threads).

Concentra toda a (des)serialização binária que antes vivia espalhada em
`Ethernet.py` (BoardSimulator/EthernetManager) e em
`GUI/mainAlbatrozGUI.py::process_network_data`. Funções puras => testáveis
isoladamente e reutilizáveis por qualquer camada acima.

Protocolos:
- `udp_struct`: TX `CMD` 8 bytes `<BBBfb>`; RX `TELEMETRY_LOG`
  (layout vem do `protocolo.yaml`, em `structs.TELEMETRY_LOG`).
"""
import struct

# Mapa de tipos C -> código struct (little-endian). Mantido idêntico ao que
# estava duplicado em Ethernet.py e mainAlbatrozGUI.py.
TYPE_MAP = {
    "float": "f",
    "uint32_t": "I",
    "log_status": "I",
    "uint8_t": "B",
}

# --- CMD (PC -> placa) ------------------------------------------------------
CMD_FORMAT = "<BBBfb"  # func(B) instr(B) target(B) value(f) action(b)
CMD_SIZE = struct.calcsize(CMD_FORMAT)


def encode_cmd(func, instr, target, value, action):
    """Empacota um CMD de 8 bytes (func, instr, target, value, action)."""
    return struct.pack(CMD_FORMAT, func, instr, target, float(value), action)


def decode_cmd(raw):
    """Desempacota um CMD de 8 bytes -> (func, instr, target, value, action).

    Retorna None se o buffer for menor que 8 bytes.
    """
    if len(raw) < CMD_SIZE:
        return None
    return struct.unpack(CMD_FORMAT, raw[:CMD_SIZE])


# CMD neutro usado por test_io: FUNC_START(0)/INSTR_NONE(2)/TARGET_DEFAULT(10)
# — inofensivo para qualquer placa. Preserva o valor histórico de Ethernet.py.
NEUTRAL_CMD = encode_cmd(0, 2, 10, 0.0, 2)


# --- TELEMETRY_LOG (placa -> PC) -------------------------------------------
def board_protocol_cfg(cfg, board):
    """Normaliza o acesso ao bloco de protocolo do YAML.

    Alguns YAMLs têm a raiz aninhada sob o nome da placa (ex.: `placa:` ->
    `enums:`/`structs:`), outros não. Replica o padrão
    `if board.lower() in cfg: cfg = cfg[board.lower()]` usado em toda a app.
    """
    if not isinstance(cfg, dict):
        return {}
    key = board.lower()
    if key in cfg and isinstance(cfg[key], dict):
        return cfg[key]
    return cfg


def telemetry_layout(protocol_cfg):
    """Retorna a lista de campos de TELEMETRY_LOG (ou [] se ausente)."""
    return protocol_cfg.get("structs", {}).get("TELEMETRY_LOG", [])


def telemetry_format(log_cfg):
    """Monta a string de formato struct a partir do layout de TELEMETRY_LOG."""
    return "<" + "".join(TYPE_MAP.get(f["type"], "f") for f in log_cfg)


def decode_telemetry(log_cfg, data):
    """Decodifica um pacote TELEMETRY_LOG -> dict {campo: valor}.

    Retorna None se o layout estiver vazio ou o tamanho não bater (o pacote é
    descartado silenciosamente, como fazia process_network_data).
    """
    if not log_cfg:
        return None
    fmt = telemetry_format(log_cfg)
    if len(data) != struct.calcsize(fmt):
        return None
    values = struct.unpack(fmt, data)
    return {field["name"]: val for field, val in zip(log_cfg, values)}
