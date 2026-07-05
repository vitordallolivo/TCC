"""errors.py — decodificação do `error_code` (bitfield) da telemetria SIBA.

Espelha `siba_error_bit_t` (../SIBA/lib/SYS/Security_Manager.h): o firmware
agrega 1 bit por condição de erro/alarme e envia o `uint32_t error_code` no
TELEMETRY_LOG. A GUI usa `decode_error_code()` para mostrar, EM PORTUGUÊS e de
forma clara, o que está errado — mantenha esta tabela em sincronia com o enum C.

Cada entrada é uma MENSAGEM que o usuário entende (o que houve + o que fazer),
não um código técnico. Nada de siglas em inglês na interface.
"""

# bit (valor) -> (mensagem clara em pt-BR, grupo)
SIBA_ERROR_BITS = {
    # --- Bateria ---
    1 << 0:  ("Bateria com carga baixa (nível abaixo do limite de alerta)", "Bateria"),
    1 << 1:  ("Cálculo de carga da bateria mal configurado (revise o polinômio de SOC)", "Bateria"),
    1 << 2:  ("Tensão da bateria baixa — alguma célula perto do limite mínimo", "Bateria"),
    1 << 3:  ("Passando corrente com o relé desligado — possível falha no relé", "Bateria"),
    1 << 4:  ("Bateria superaquecendo (temperatura acima do limite de alarme)", "Bateria"),

    # --- Motor / Hélice ---
    1 << 8:  ("Célula de carga sem calibração — tare e calibre antes de medir", "Motor/Hélice"),
    1 << 9:  ("Célula de carga desconectada (HX711 não responde)", "Motor/Hélice"),
    1 << 10: ("Corrente do motor acima do limite permitido", "Motor/Hélice"),

    # --- Segurança ---
    1 << 16: ("Tensão da bateria fora da faixa segura de operação", "Segurança"),
    1 << 17: ("Corrente da bateria excessiva (acima do limite de segurança)", "Segurança"),
    1 << 18: ("Rotação do motor (RPM) acima do limite de segurança", "Segurança"),
    1 << 19: ("PARADA DE EMERGÊNCIA ativa — motor desarmado", "Segurança"),

    # --- Sistema ---
    1 << 24: ("Falha ao inicializar um módulo de hardware (GPIO)", "Sistema"),
}


def decode_error_code(code):
    """`code` (int) -> lista de (bit, mensagem_pt, grupo) das condições ATIVAS.

    Bits não mapeados aparecem com uma mensagem genérica em português para não
    passarem despercebidos se o firmware ganhar um erro novo antes desta tabela.
    """
    if not code:
        return []
    active = []
    for bit in range(32):
        mask = 1 << bit
        if not (code & mask):
            continue
        if mask in SIBA_ERROR_BITS:
            msg, group = SIBA_ERROR_BITS[mask]
        else:
            msg, group = (f"Erro desconhecido (bit {bit}) — atualize a tabela de erros", "Sistema")
        active.append((mask, msg, group))
    return active


def format_error_code(code):
    """Uma linha resumida para a barra de status, em português.

    'Tudo OK' quando não há erro; senão a contagem + as mensagens dos erros
    ativos separadas por ' • '."""
    active = decode_error_code(code)
    if not active:
        return "Tudo OK"
    n = len(active)
    msgs = " • ".join(msg for _mask, msg, _grp in active)
    return f"{n} problema(s): {msgs}"


def format_error_detail(code):
    """Texto multi-linha agrupado por área — bom para tooltip / caixa de detalhes."""
    active = decode_error_code(code)
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
