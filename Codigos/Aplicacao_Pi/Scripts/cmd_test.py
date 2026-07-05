"""cmd_test.py — banco de testes de TODAS as possibilidades de CMD (SIBA/SAME).

Monta cada comando, mostra os 8 bytes + a decodificação, e — se `--send` — sobe
o firmware real (native_sim) e mede se a TELEMETRIA muda antes/depois de cada
comando. Serve para responder objetivamente:
  - o Python está montando o CMD certo? (coluna hex/decode)
  - o micro REAGE ao comando? (coluna Δ telemetria)

Uso (rodar de dentro de Aplicacao_Pi/):
  ../.venv/bin/python Scripts/cmd_test.py                 # só monta+decodifica
  ../.venv/bin/python Scripts/cmd_test.py --send          # + native_sim SIBA (Δ)
  ../.venv/bin/python Scripts/cmd_test.py --board SAME --send
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from Communication_Data import codecs               # noqa: E402
from Communication_Data.registry import BoardRegistry  # noqa: E402
from Communication_Data.manager import LinkManager     # noqa: E402

CONFIG_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "Config"))


# ---------------------------------------------------------------------------
# Construtor de CMD (mesma lógica do TinyTerminal, isolado e testável).
# ---------------------------------------------------------------------------
def build_cmd(protocol_cfg, board, action, target="DEFAULT", sub="", value=0.0):
    """(board, action, target, sub, value) -> CMD 8 bytes + descrição do decode."""
    enums = protocol_cfg.get("enums", {})

    def idx(cat, name, default=0):
        lst = enums.get(cat, [])
        return lst.index(name) if name in lst else default

    action = action.upper(); target = target.upper(); sub = sub.upper()

    # --- FUNC ---
    if action in ("SET", "GET"):
        if board == "SIBA":
            if sub in ("LINEAR", "OFFSET", "RESET"):
                func_name = "FUNC_SENSOR"
            else:
                bat = ["TEMP", "RELAY", "SOC", "P0", "P1", "P2", "P3", "P4"]
                func_name = "FUNC_BATTERY" if any(k in target for k in bat) else "FUNC_SENSOR"
        else:
            func_name = "FUNC_SENSOR"
    else:
        func_name = f"FUNC_{action}"     # FUNC_START/STOP/RESET/ESC...

    # --- INSTR ---
    instr_name = ("INSTR_WRITE" if action == "SET" else
                  "INSTR_READ" if action == "GET" else "INSTR_NONE")

    # --- TARGET (mesma resolução do TinyTerminal: alias -> exato -> substring) ---
    tlist = enums.get("target_type", [])
    aliases = {
        "V_BATT": "TARGET_VOLTAGE_DIVIDOR", "I_BATT": "TARGET_CURRENT_SENSOR",
        "AIR_SPEED": "TARGET_PITOT_TUBE", "PITOT": "TARGET_PITOT_TUBE",
        "TEMPERATURE_BATTERY": "TARGET_TEMPERATURE_SENSOR",
        "TEMP_SENSOR": "TARGET_TEMPERATURE_SENSOR",
    }
    if target in aliases and aliases[target] in tlist:
        tval = tlist.index(aliases[target])
    elif f"TARGET_{target}" in tlist:
        tval = tlist.index(f"TARGET_{target}")
    else:
        tval = next((i for i, n in enumerate(tlist) if target in n), idx("target_type", "TARGET_DEFAULT", 0))

    # --- ACTION/STATUS ---
    aval = 0
    if func_name == "FUNC_BATTERY":
        blist = enums.get("battery_action", [])
        clean = target.replace("SOC_", "")
        for i, n in enumerate(blist):
            if all(k in n for k in clean.split("_")):
                aval = i; break
    elif sub:
        dlist = enums.get("default_action", [])
        aval = next((i for i, n in enumerate(dlist) if sub in n), 0)

    cmd = struct.pack("<BBBfb", idx("func_type", func_name, 0),
                      idx("instruct_type", instr_name, 0), tval, float(value), aval)
    # decode legível
    def nm(cat, i):
        lst = enums.get(cat, [])
        return lst[i] if 0 <= i < len(lst) else str(i)
    aname = (nm("battery_action", aval) if func_name == "FUNC_BATTERY"
             else nm("default_action", aval))
    desc = (f"{func_name} | {instr_name} | {nm('target_type', tval)} | "
            f"VAL={value} | ACT={aname}")
    return cmd, desc


# ---------------------------------------------------------------------------
# Casos de teste (o "switch") — gerados a partir do próprio YAML.
# ---------------------------------------------------------------------------
def test_cases(protocol_cfg, board):
    """Lista (descrição, board, action, target, sub, value, telem_var) cobrindo
    tudo: start/stop/reset + calibração de cada sensor + parâmetros de bateria."""
    cases = []
    # operação
    cases += [("start", board, "START", "DEFAULT", "", 1.0, None),
              ("stop",  board, "STOP",  "DEFAULT", "", 0.0, None),
              ("reset", board, "RESET", "DEFAULT", "", 0.0, None)]

    log = codecs.telemetry_layout(protocol_cfg)
    telem_vars = {f["name"] for f in log}

    # calibração de sensores: para cada campo de telemetria que também é um
    # target (offset/linear/reset), testa o efeito na PRÓPRIA variável.
    # Usa a MESMA resolução de target do build_cmd (alias + substring) — sem
    # isso, v_batt/i_batt caíam de fora (não batem por substring com
    # TARGET_VOLTAGE_DIVIDOR/TARGET_CURRENT_SENSOR) e nem apareciam no teste.
    for var in sorted(telem_vars):
        _, dec = build_cmd(protocol_cfg, board, "SET", var, "OFFSET", 0.0)
        target_field = dec.split("|")[2].strip()  # ex.: "TARGET_VOLTAGE_DIVIDOR"
        if target_field == "TARGET_DEFAULT":
            continue  # não é um alvo calibrável
        for sub in ("OFFSET", "LINEAR"):
            cases.append((f"{var} {sub.lower()} +50", board, "SET", var, sub, 50.0, var))

    # parâmetros de bateria (SIBA)
    if board == "SIBA":
        for p, v in [("TEMP_LIMIT", 60.0), ("RELAY", 1.0), ("SOC_P0", 1.0)]:
            cases.append((f"battery {p}", board, "SET", p, "", v, None))
    return cases


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--board", default="SIBA")
    ap.add_argument("--send", action="store_true", help="sobe o native_sim e mede Δ telemetria")
    ap.add_argument("--settle", type=float, default=1.2, help="s de média antes/depois")
    args = ap.parse_args()
    board = args.board.upper()

    reg = BoardRegistry.load(CONFIG_DIR)
    cfg = reg.get(board)
    if cfg is None:
        print(f"placa {board} não encontrada em {CONFIG_DIR}"); return 1
    pcfg = cfg.protocol_cfg
    cases = test_cases(pcfg, board)

    # ---- fase 1: só monta + decodifica (sempre roda) ----
    print(f"\n=== CMDs de {board} (montagem + decode) ===")
    print(f"{'caso':<22} {'hex (8 bytes)':<26} decode")
    print("-" * 100)
    for desc, b, act, tgt, sub, val, tv in cases:
        cmd, dec = build_cmd(pcfg, b, act, tgt, sub, val)
        print(f"{desc:<22} {cmd.hex(' ').upper():<26} {dec}")

    if not args.send:
        print("\n(sem --send: não testou o efeito no micro)")
        return 0

    # ---- fase 2: sobe native_sim e mede Δ telemetria ----
    latest = {}
    def on_rx(bd, data, addr):
        d = codecs.decode_telemetry(codecs.telemetry_layout(pcfg), data)
        if d:
            latest.update(d)

    eth = LinkManager(config_dir=CONFIG_DIR, handle_callback=on_rx)
    print(f"\nsubindo native_sim de {board}...")
    if not eth.start_native_sim_auto(board):
        print("falhou (rode 'make native_sim' no firmware). Só a SIBA tem hoje.")
        return 1
    time.sleep(1.5)

    def avg(var, secs):
        vals = []
        t0 = time.time()
        while time.time() - t0 < secs:
            if var in latest:
                vals.append(latest[var])
            time.sleep(0.05)
        return sum(vals) / len(vals) if vals else float("nan")

    print(f"\n=== efeito no micro (Δ da telemetria) — {board} ===")
    print(f"{'caso':<22} {'var':<14} {'antes':>12} {'depois':>12} {'Δ':>12}  reagiu?")
    print("-" * 92)
    for desc, b, act, tgt, sub, val, tv in cases:
        if act == "RESET":
            # FUNC_RESET reinicia o firmware (sys_reboot) — mataria o native_sim
            # e zeraria todos os testes seguintes. Só decodifica, não envia.
            print(f"{desc:<22} {'(pulado no --send: reinicia o firmware)':<40}")
            continue
        cmd, _ = build_cmd(pcfg, b, act, tgt, sub, val)
        var = tv or (codecs.telemetry_layout(pcfg)[1]["name"] if len(codecs.telemetry_layout(pcfg)) > 1 else None)
        before = avg(var, args.settle) if var else float("nan")
        eth.send_command(board, cmd)
        after = avg(var, args.settle) if var else float("nan")
        d = after - before
        reagiu = "SIM" if abs(d) > 1e-6 else "não"
        print(f"{desc:<22} {str(var):<14} {before:>12.4f} {after:>12.4f} {d:>12.4f}  {reagiu}")

    eth.stop_native_sim(board); eth.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
