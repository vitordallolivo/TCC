#!/usr/bin/env python3
"""Confronta dois CSVs do harness e aplica as tolerâncias do CLAUDE.md.

Reporta o desvio em duas janelas, porque elas respondem a perguntas
diferentes:

  - REGIME  (último terço de cada patamar): é o número que importa para a
    medição. É contra ele que as tolerâncias são aplicadas.
  - GLOBAL  (todos os passos, transitório de degrau incluído): serve para
    ver se a variante nova diverge no degrau. Um pico grande aqui com regime
    limpo significa transitório diferente, não erro de estado estacionário.

O denominador do erro relativo é o maior |valor| entre as duas séries na
janela — não o ponto a ponto. Erro relativo ponto a ponto perto do zero de
uma senoide (torque no arranque, rpm antes do lock) estoura para infinito e
não diz nada sobre a qualidade da estimativa.
"""

import argparse
import csv
import math
import sys

# Tolerâncias do plano de validação (CLAUDE.md, "make native_sim").
TOL = {
    "rpm_same": 0.01,           # %
    "torque_eletrico": 1.0,     # %
}


def load(path):
    with open(path, newline="") as fh:
        rows = list(csv.DictReader(fh))
    if not rows:
        sys.exit(f"erro: {path} vazio")
    return rows


def deviation(ref, new, col, idx):
    """Maior desvio relativo (%) entre as duas séries nos índices `idx`."""
    scale = max(
        max(abs(float(ref[i][col])) for i in idx),
        max(abs(float(new[i][col])) for i in idx),
    )
    if scale == 0.0:
        return 0.0, -1
    worst, where = 0.0, -1
    for i in idx:
        d = abs(float(new[i][col]) - float(ref[i][col])) / scale * 100.0
        if d > worst:
            worst, where = d, i
    return worst, where


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True)
    ap.add_argument("--new", required=True)
    ap.add_argument("--label", default="")
    args = ap.parse_args()

    ref, new = load(args.ref), load(args.new)
    if len(ref) != len(new):
        sys.exit(f"erro: {len(ref)} x {len(new)} linhas — estímulos diferentes")

    # NaN/Inf em qualquer coluna já reprova, independente de tolerância.
    for name, rows in (("ref", ref), ("new", new)):
        for i, r in enumerate(rows):
            for col in ("rpm_same", "torque_eletrico"):
                if not math.isfinite(float(r[col])):
                    sys.exit(f"FAIL: {name} não-finito em {col}, passo {i}")

    plateaus = sorted({int(r["plateau"]) for r in ref})
    n = len(ref)
    idx_all = range(n)

    # Regime = último terço de cada patamar (PLL já travada).
    idx_regime = []
    for p in plateaus:
        pi = [i for i in idx_all if int(ref[i]["plateau"]) == p]
        idx_regime += pi[2 * len(pi) // 3:]

    print(f"\n=== {args.label or (args.ref + ' x ' + args.new)} ===")
    print(f"{n} passos, {len(plateaus)} patamares "
          f"({', '.join(ref[i * (n // len(plateaus))]['rpm_ref'] for i in range(len(plateaus)))} rpm)\n")
    print(f"{'grandeza':<20}{'regime':>12}{'global':>12}{'tol':>10}  veredito")
    print("-" * 66)

    ok = True
    for col, tol in TOL.items():
        d_reg, _ = deviation(ref, new, col, idx_regime)
        d_all, where = deviation(ref, new, col, idx_all)
        passed = d_reg <= tol
        ok &= passed
        print(f"{col:<20}{d_reg:>11.4f}%{d_all:>11.4f}%{tol:>9.2f}%  "
              f"{'PASS' if passed else 'FAIL'}"
              + ("" if passed else f"  (pior global no passo {where})"))

    # Contexto: valores médios em regime, para ver se as duas de fato mediram
    # a mesma coisa (uma tolerância cumprida sobre lixo não vale nada).
    print()
    for p in plateaus:
        pi = [i for i in idx_regime if int(ref[i]["plateau"]) == p]
        rpm_ref_val = float(ref[pi[0]]["rpm_ref"])
        m_ref = sum(float(ref[i]["rpm_same"]) for i in pi) / len(pi)
        m_new = sum(float(new[i]["rpm_same"]) for i in pi) / len(pi)
        t_ref = sum(float(ref[i]["torque_eletrico"]) for i in pi) / len(pi)
        t_new = sum(float(new[i]["torque_eletrico"]) for i in pi) / len(pi)
        print(f"  patamar {p}: ref {rpm_ref_val:7.0f} rpm | "
              f"estimado {m_ref:8.2f} / {m_new:8.2f} rpm | "
              f"Te {t_ref:7.4f} / {t_new:7.4f} N·m")

    print(f"\n{'PASS' if ok else 'FAIL'}\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
