"""Extrai a condição estática (J = 0) das tabelas PER3 da APC.

Gera, para cada hélice, apc_<helice>_estatico.dat com rotação, torque e
C_Q = C_P / (2*pi). Uso: python extrai_apc.py  (a partir desta pasta).
"""
import math
import re
from pathlib import Path

AQUI = Path(__file__).resolve().parent
ORIGEM = AQUI.parent.parent          # Textuais/Imgs/
HELICES = {"15x6E": 15 * 0.0254, "17x10E": 17 * 0.0254}   # diâmetro [m]
RPM_MAX = 10000

for nome, diam in HELICES.items():
    linhas = (ORIGEM / f"PER3_{nome}.dat").read_text(errors="ignore").splitlines()
    rpm, saida = None, []
    for i, l in enumerate(linhas):
        m = re.search(r"PROP RPM =\s+(\d+)", l)
        if m:
            rpm = int(m.group(1))
            continue
        campos = l.split()
        # linha de V = 0 (J = 0) de cada bloco. Colunas: V J Pe Ct Cp PWR(Hp)
        # Torque(in-lbf) Thrust(lbf) PWR(W) Torque(N-m) Thrust(N) ...
        if rpm and len(campos) >= 15 and campos[0] == "0.00":
            cp = float(campos[4])
            torque = float(campos[6]) * 0.1129848   # in-lbf -> N.m (mais casas que a coluna em N.m)
            saida.append((rpm, torque, cp / (2 * math.pi), float(campos[3])))
            rpm = None
    saida = [s for s in saida if s[0] <= RPM_MAX]
    with open(AQUI / f"apc_{nome}_estatico.dat", "w") as f:
        f.write("rpm torque_Nm CQ CT\n")
        for r, q, cq, ct in saida:
            f.write(f"{r} {q:.4f} {cq:.6f} {ct:.4f}\n")
    print(nome, len(saida), "pontos")
