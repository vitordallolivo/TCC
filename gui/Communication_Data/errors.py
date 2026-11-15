"""errors.py — decodificação do `error_code` (bitfield) da telemetria.

FONTE ÚNICA: a seção `error_bits` do `protocolo.yaml`, a MESMA que gera o enum C
de bits de erro (lib/SYS/Error_Codes.h, via Config/generate.py). O firmware
agrega 1 bit por condição e envia o `uint32_t error_code` no TELEMETRY_LOG; aqui
montamos a tabela bit→(mensagem pt-BR, grupo) a partir do mesmo yaml.

A tabela é registrada uma vez (register_error_bits/ensure_registered) sob um
nome (`protocol`), que as funções de decode recebem.

As mensagens são texto claro pro usuário (o que houve + o que fazer), em pt-BR —
mantenha-as no yaml, não aqui.
"""

# protocol (str) -> { bitmask(int) : (mensagem_pt, grupo) }
_TABLES = {}


def _extract_error_bits(cfg):
    """Extrai a lista `error_bits` de um cfg de placa (yaml já carregado),
    lidando com a raiz aninhada sob uma chave (ex.: {'placa': {...}})."""
    if not isinstance(cfg, dict):
        return []
    if "error_bits" in cfg:
        return cfg.get("error_bits") or []
    for v in cfg.values():
        if isinstance(v, dict) and "error_bits" in v:
            return v.get("error_bits") or []
    return []


def register_error_bits(protocol, cfg):
    """Monta e memoiza a tabela de erros da placa `protocol` a partir do seu
    yaml (`cfg`). Sobrescreve qualquer tabela anterior. Retorna a tabela."""
    table = {}
    for e in _extract_error_bits(cfg):
        try:
            mask = 1 << int(e["bit"])
        except (KeyError, TypeError, ValueError):
            continue
        msg = e.get("msg") or e.get("name") or f"bit {e.get('bit')}"
        group = e.get("group") or "Sistema"
        table[mask] = (msg, group)
    _TABLES[protocol] = table
    return table


def ensure_registered(protocol, cfg):
    """Registra a tabela da placa se ainda não houver uma. Retorna a tabela."""
    if protocol not in _TABLES:
        register_error_bits(protocol, cfg)
    return _TABLES.get(protocol, {})


def decode_error_code(code, protocol=None):
    """`code` (int) -> lista de (bitmask, mensagem_pt, grupo) das condições
    ATIVAS, usando a tabela da placa `protocol`.

    Bits não mapeados aparecem com uma mensagem genérica em português para não
    passarem despercebidos se o firmware ganhar um erro novo antes do yaml."""
    if not code:
        return []
    table = _TABLES.get(protocol, {})
    active = []
    for bit in range(32):
        mask = 1 << bit
        if not (code & mask):
            continue
        if mask in table:
            msg, group = table[mask]
        else:
            msg, group = (f"Erro desconhecido (bit {bit}) — atualize error_bits no yaml", "Sistema")
        active.append((mask, msg, group))
    return active


def format_error_code(code, protocol=None):
    """Uma linha resumida para a barra de status, em português.

    'Tudo OK' quando não há erro; senão a contagem + as mensagens dos erros
    ativos separadas por ' • '."""
    active = decode_error_code(code, protocol)
    if not active:
        return "Tudo OK"
    n = len(active)
    msgs = " • ".join(msg for _mask, msg, _grp in active)
    return f"{n} problema(s): {msgs}"


def format_error_detail(code, protocol=None):
    """Texto multi-linha agrupado por área — bom para tooltip / caixa de detalhes."""
    active = decode_error_code(code, protocol)
    if not active:
        return "Nenhum erro ativo."
    por_grupo = {}
    for _mask, msg, grp in active:
        por_grupo.setdefault(grp, []).append(msg)
    linhas = []
    for grp, msgs in por_grupo.items():
        linhas.append(f"[{grp}]")
        linhas.extend(f"  • {m}" for m in msgs)
    return "\n".join(linhas)
