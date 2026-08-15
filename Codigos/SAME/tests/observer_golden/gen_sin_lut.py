#!/usr/bin/env python3
"""Gera lib/SYS/Observer_Sin_LUT.h — tabela de seno do caminho quente do V5.

Uso:  python3 gen_sin_lut.py <caminho/Observer_Sin_LUT.h>
"""

import math
import sys

N = 512

HEADER = """/**
 * @file Observer_Sin_LUT.h
 * @brief Tabela de seno de {n} pontos (+1 de guarda) para o caminho quente do
 * Observer_Step (OBS_IMPL >= 5). GERADO — não editar à mão.
 *
 * Gerado por: tests/observer_golden/gen_sin_lut.py
 *   obs_sin_lut[i] = sin(2*pi*i/{n}), i = 0..{n}   (obs_sin_lut[{n}] == obs_sin_lut[0])
 *
 * `static const` é OBRIGATÓRIO: é o que põe a tabela em .rodata (flash) em vez
 * de .bss. Gerar em runtime com sinf() custaria +2 kB de RAM E manteria a
 * libm linkada — pior nos dois eixos (ver CLAUDE.md, "Flash e RAM").
 *
 * Erro da interpolação linear: max |e| ~ (2*pi/{n})^2/8 = {err:.1e} (absoluto),
 * bem abaixo da tolerância de 1 % no torque.
 */

#ifndef OBSERVER_SIN_LUT_H
#define OBSERVER_SIN_LUT_H

#define OBS_LUT_N        {n}
#define OBS_LUT_MASK     (OBS_LUT_N - 1)
#define OBS_LUT_QUARTER  (OBS_LUT_N / 4)   /* deslocamento de +pi/2 -> cosseno */

/* {entries} entradas = {size} B em .rodata */
static const float obs_sin_lut[OBS_LUT_N + 1] = {{"""


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out_path = sys.argv[1]

    vals = [math.sin(2 * math.pi * i / N) for i in range(N)]
    vals.append(vals[0])   # guarda: interpola i = N-1 sem wrap no índice

    lines = [HEADER.format(n=N, entries=len(vals), size=len(vals) * 4,
                           err=(2 * math.pi / N) ** 2 / 8)]
    for i in range(0, len(vals), 6):
        lines.append("    " + " ".join("%+.9ef," % v for v in vals[i:i + 6]))
    lines.append("};\n\n#endif /* OBSERVER_SIN_LUT_H */")

    with open(out_path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"{out_path}: {len(vals)} entradas ({len(vals) * 4} B)")


if __name__ == "__main__":
    main()
