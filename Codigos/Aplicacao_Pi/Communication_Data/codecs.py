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
# Layout REAL do firmware (../SPMM/src/spmm.h, `#pragma pack(1)`):
#   header  <HH>   -> count, ts_us
#   sample  <I11f> -> n, omega, te, iq, id, theta_e, ia, ib, ic, va, vb, vc
#                     (48 bytes por amostra — "medição completa", não só n/ω/Te).
SPMM_HEADER_FORMAT = "<HH"          # count, ts_us
SPMM_SAMPLE_FORMAT = "<I11f"        # n + 11 floats = 48 bytes
SPMM_SAMPLE_FIELDS = ["n", "omega", "te", "iq", "id", "theta_e",
                      "ia", "ib", "ic", "va", "vb", "vc"]
SPMM_HEADER_SIZE = struct.calcsize(SPMM_HEADER_FORMAT)
SPMM_SAMPLE_SIZE = struct.calcsize(SPMM_SAMPLE_FORMAT)   # 48


def decode_spmm_log(data, Ts=None):
    """Decodifica um pacote de log da SPMM (ver ../SPMM/src/telemetry.c).

    Layout: `<HH>` (count, ts_us) + count * `<I11f>` (medição completa).
    Retorna {"count", "ts_us", "samples": [ {n, omega, te, iq, ..., t}, ... ]}.

    Base de tempo `t = n · Ts` (contador de amostra × período), NUNCA o horário
    de chegada do pacote. O período vem do PRÓPRIO pacote (`ts_us`, que reflete
    o `log start <hz>` usado); `Ts` só é fallback se `ts_us` vier 0.
    Retorna None se o buffer for inconsistente.
    """
    if len(data) < SPMM_HEADER_SIZE:
        return None
    count, ts_us = struct.unpack(SPMM_HEADER_FORMAT, data[:SPMM_HEADER_SIZE])
    expected = SPMM_HEADER_SIZE + count * SPMM_SAMPLE_SIZE
    if len(data) < expected:
        return None

    # Período efetivo: o do pacote (autoritativo) ou o fallback informado.
    Ts_eff = (ts_us * 1e-6) if ts_us else Ts

    samples = []
    off = SPMM_HEADER_SIZE
    for _ in range(count):
        vals = struct.unpack(SPMM_SAMPLE_FORMAT, data[off:off + SPMM_SAMPLE_SIZE])
        off += SPMM_SAMPLE_SIZE
        sample = dict(zip(SPMM_SAMPLE_FIELDS, vals))
        if Ts_eff is not None:
            sample["t"] = sample["n"] * Ts_eff
        samples.append(sample)

    return {"count": count, "ts_us": ts_us, "samples": samples}
