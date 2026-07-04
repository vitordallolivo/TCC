"""Camada 2 · Domínio/Protocolo — codecs puros (sem I/O, sem threads).

Concentra toda a (des)serialização binária que antes vivia espalhada em
`Ethernet.py` (BoardSimulator/EthernetManager) e em
`GUI/mainAlbatrozGUI.py::process_network_data`. Funções puras => testáveis
isoladamente e reutilizáveis por qualquer camada acima.

Protocolos:
- SIBA/SAME (`udp_struct`): TX `CMD` 8 bytes `<BBBfb>`; RX `TELEMETRY_LOG`
  (layout vem do `.yaml` de cada placa em `structs.TELEMETRY_LOG`).
- SPMM (`telnet`): log binário por UDP — cabeçalho `<HH>` (count, ts_us) +
  N amostras `<Iff>` (n, omega, te). Ver `../SPMM/CLAUDE.md`.
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

# --- CMD (PC -> placa), SIBA/SAME -------------------------------------------
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


# --- TELEMETRY_LOG (placa -> PC), SIBA/SAME ---------------------------------
def board_protocol_cfg(cfg, board):
    """Normaliza o acesso ao bloco de protocolo do YAML.

    Alguns YAMLs têm a raiz aninhada sob o nome da placa (ex.: `siba:` ->
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


# --- SPMM: log binário por UDP ----------------------------------------------
SPMM_HEADER_FORMAT = "<HH"          # count, ts_us
SPMM_SAMPLE_FORMAT = "<Iff"         # n, omega, te
SPMM_HEADER_SIZE = struct.calcsize(SPMM_HEADER_FORMAT)
SPMM_SAMPLE_SIZE = struct.calcsize(SPMM_SAMPLE_FORMAT)


def decode_spmm_log(data, Ts=None):
    """Decodifica um pacote de log da SPMM.

    Layout: `<HH>` (count, ts_us) + count * `<Iff>` (n, omega, te).
    Retorna {"count", "ts_us", "samples": [ {n, omega, te, t?}, ... ]}.
    Se `Ts` for informado, calcula `t = n * Ts` por amostra (base de tempo
    do micro — NUNCA usar horário de chegada do pacote; ver ../SPMM/CLAUDE.md).
    Retorna None se o buffer for inconsistente.
    """
    if len(data) < SPMM_HEADER_SIZE:
        return None
    count, ts_us = struct.unpack(SPMM_HEADER_FORMAT, data[:SPMM_HEADER_SIZE])
    expected = SPMM_HEADER_SIZE + count * SPMM_SAMPLE_SIZE
    if len(data) < expected:
        return None

    samples = []
    off = SPMM_HEADER_SIZE
    for _ in range(count):
        n, omega, te = struct.unpack(SPMM_SAMPLE_FORMAT, data[off:off + SPMM_SAMPLE_SIZE])
        off += SPMM_SAMPLE_SIZE
        sample = {"n": n, "omega": omega, "te": te}
        if Ts is not None:
            sample["t"] = n * Ts
        samples.append(sample)

    return {"count": count, "ts_us": ts_us, "samples": samples}
