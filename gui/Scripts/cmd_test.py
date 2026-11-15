"""cmd_test.py — banco de testes de todos os CMD da placa.

Monta cada comando pela mesma gramática do TinyTerminal e do placa.py
(`Communication_Data/comandos.py`), mostra os 8 bytes + a decodificação e,
com `--send`, sobe o firmware real (native_sim) e mede se a TELEMETRIA muda
antes/depois de cada comando. Responde objetivamente:
  - o Python está montando o CMD certo? (coluna hex/decode)
  - o firmware REAGE ao comando? (coluna Δ telemetria)

Uso (de dentro de gui/):
  .venv/bin/python Scripts/cmd_test.py            # só monta+decodifica
  .venv/bin/python Scripts/cmd_test.py --send     # + native_sim (Δ)
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from Communication_Data import codecs                       # noqa: E402
from Communication_Data.comandos import ErroComando, Protocolo  # noqa: E402
from Communication_Data.manager import LinkManager          # noqa: E402

CONFIG_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "Config"))


def test_cases(proto):
    """Lista (linha de comando, variável de telemetria a observar ou None)."""
    cases = [("arm", None), ("off", None), ("estop", None), ("esc 0", None),
             ("reset", None)]
    # Calibração: cada campo de telemetria que também é alvo de sensor.
    for f in codecs.telemetry_layout(proto.raw):
        var = f["name"]
        try:
            t = proto.alvo(var)
        except ErroComando:
            continue
        if proto.e_parametro(t):
            continue
        cases += [(f"set {var} offset 50", var), (f"set {var} linear 50", var),
                  (f"set {var} reset", var)]
    # Parâmetros do observador e da bateria.
    _, params = proto.alvos()
    cases += [(f"set {p} reset", None) for p in params]
    cases += [("bat temp_limit 60", None), ("bat relay 1", None), ("bat p0 1", None)]
    return cases


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--send", action="store_true", help="sobe o native_sim e mede Δ telemetria")
    ap.add_argument("--settle", type=float, default=1.2, help="s de média antes/depois")
    args = ap.parse_args()

    proto = Protocolo.de_pasta(CONFIG_DIR)
    cases = test_cases(proto)

    print("\n=== CMDs (montagem + decode) ===")
    print(f"{'comando':<28} {'hex (8 bytes)':<26} decode")
    print("-" * 110)
    for linha, _ in cases:
        cmd = proto.interpretar(linha)[0]
        print(f"{linha:<28} {cmd.hex(' ').upper():<26} {proto.descrever(cmd)}")

    if not args.send:
        print("\n(sem --send: não testou o efeito no firmware)")
        return 0

    layout = codecs.telemetry_layout(proto.raw)
    latest = {}

    def on_rx(_board, data, _addr):
        d = codecs.decode_telemetry(layout, data)
        if d:
            latest.update(d)

    eth = LinkManager(config_dir=CONFIG_DIR, handle_callback=on_rx)
    print("\nsubindo o native_sim...")
    if not eth.start_native_sim_auto("PLACA"):
        print("falhou (rode 'make native_sim' na raiz do repositório).")
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

    print("\n=== efeito no firmware (Δ da telemetria) ===")
    print(f"{'comando':<28} {'var':<14} {'antes':>12} {'depois':>12} {'Δ':>12}  reagiu?")
    print("-" * 96)
    for linha, var in cases:
        if linha == "reset":
            # FUNC_RESET reinicia o firmware (sys_reboot) e mataria o native_sim.
            print(f"{linha:<28} (pulado no --send: reinicia o firmware)")
            continue
        var = var or layout[1]["name"]
        before = avg(var, args.settle)
        for cmd in proto.interpretar(linha):
            eth.send_command("PLACA", cmd)
        after = avg(var, args.settle)
        d = after - before
        reagiu = "SIM" if abs(d) > 1e-6 else "não"
        print(f"{linha:<28} {var:<14} {before:>12.4f} {after:>12.4f} {d:>12.4f}  {reagiu}")

    eth.stop_native_sim("PLACA")
    eth.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
