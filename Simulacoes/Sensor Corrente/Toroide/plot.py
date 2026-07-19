#!/usr/bin/env python3
"""
Figuras a partir dos CSVs do script FEMM (ToroideMedicaoCorrente.lua, v15).

Saida em PDF vetorial -> \\includegraphics do abntex2 sem perda na impressao.

Convencao ABNT: a figura NAO leva titulo nem texto solto. A identificacao vai
na legenda (\\caption) e a discussao no corpo do texto. Linhas de referencia
aparecem na legenda do grafico, nao como anotacao flutuante. Os numeros que
voce precisa citar no texto sao impressos no terminal.

ETAPA 1 do Lua -> curva_I_B_single.csv     (sensor unico, DC: ganho/linearidade)
ETAPA 2 do Lua -> crosstalk_matrix.csv     (3 sensores + chapa, AC 12 kHz, por distancia)
                  crosstalk_resumo.csv     (pior desvio por distancia)

Uso:
    python plot.py [diretorio]
"""

import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["DejaVu Serif"],
    "font.size": 10,
    "axes.grid": True,
    "grid.alpha": 0.3,
    "grid.linewidth": 0.5,
    "axes.linewidth": 0.8,
    "figure.dpi": 150,
})

LIM_SS49E    = 1000.0   # G, fim da faixa linear do SS49E
LIM_DESPREZ  = 1.0      # %, referencia de crosstalk "desprezivel"
LARGURA      = 5.5      # pol, largura util de uma A4 com margens ABNT

AZUL   = "#1f4e79"
VERM   = "#c0504d"
CINZA  = "#595959"
VERDE  = "#70ad47"
CORES_CENARIO = [AZUL, VERM, VERDE, "#7030a0"]
MARCADORES    = ["o", "s", "^", "D"]


def fig_I_vs_B(df: pd.DataFrame, base: Path) -> None:
    fig, ax = plt.subplots(figsize=(LARGURA, 3.4))

    ax.plot(df.I_A, df.B_hall_G, "o-", ms=4, lw=1.4, color=AZUL,
            label="Centro do gap (elemento Hall)")

    if LIM_SS49E <= df.B_hall_G.max() * 1.15:
        ax.axhline(LIM_SS49E, ls=":", lw=1.2, color=CINZA,
                   label="Fim da faixa linear do SS49E (1000 G)")

    ax.set_xlabel("Corrente primária $I$ [A]")
    ax.set_ylabel("Densidade de fluxo $B$ [G]")
    ax.set_xlim(0, df.I_A.max() * 1.02)
    ax.set_ylim(0, None)
    ax.legend(frameon=False, fontsize=8, loc="upper left")

    fig.tight_layout()
    fig.savefig(base / "fig_I_vs_B.pdf")
    plt.close(fig)


def fig_linearidade(df: pd.DataFrame, base: Path) -> None:
    """
    G/A vs I. A curva I x B parece uma reta perfeita a olho nu mesmo com
    varios por cento de erro. Quem mostra linearidade e' a sensibilidade:
    horizontal = linear.
    """
    fig, ax = plt.subplots(figsize=(LARGURA, 3.4))

    ax.plot(df.I_A, df.GA_hall, "o-", ms=4, lw=1.4, color=AZUL)
    ax.set_xlabel("Corrente primária $I$ [A]")
    ax.set_ylabel("Sensibilidade [G/A]")
    ax.set_xlim(0, df.I_A.max() * 1.02)

    fig.tight_layout()
    fig.savefig(base / "fig_linearidade.pdf")
    plt.close(fig)


# Ganho de nucleo extraido da simulacao FEMM (fig_I_vs_B), constante com I.
G_NUCLEO_GA = 5.355  # G/A

# SS49E, valores de datasheet (Tabela 1).
VO_TYP, VO_MIN, VO_MAX = 2.50, 2.25, 2.75   # V, tensao de repouso
SH_TYP, SH_MIN, SH_MAX = 2.5, 2.0, 3.0      # mV/G, sensibilidade
V_OUT_MIN, V_OUT_MAX = 0.86, 4.21           # V, limite fisico de saida

# Deriva termica ilustrativa: T >= 25 C, pior caso de cada coeficiente.
DELTA_T = 50.0          # C acima da referencia (T_A = 25 C)
ERRO_TEMP_PCT = 0.10    # %/C, null drift (pior caso, Tabela 1)
DERIVA_SH_PCT = -0.15   # %/C, deriva de sensibilidade p/ T >= 25 C (pior caso)


def fig_erro_sensor(base: Path) -> None:
    """
    Vsense(I) tipico vs. envelope de tolerancia de fabricacao (VO, SH) e
    deriva termica, combinando o ganho de nucleo (FEMM) com os parametros
    de datasheet do SS49E (Tabela 1). Erro de linearidade (0,007% do span)
    nao e plotado por ser desprezivel nesta escala.
    """
    I = np.linspace(-100, 100, 400)

    def vsense(vo, sh, i):
        return vo + (sh * G_NUCLEO_GA / 1000.0) * i

    v_typ = vsense(VO_TYP, SH_TYP, I)

    # Envelope: SH_MAX sempre amplia o desvio (p/ +I e p/ -I); VO desloca.
    v_env_hi = np.where(I >= 0, vsense(VO_MAX, SH_MAX, I), vsense(VO_MAX, SH_MIN, I))
    v_env_lo = np.where(I >= 0, vsense(VO_MIN, SH_MIN, I), vsense(VO_MIN, SH_MAX, I))

    vo_hot = VO_TYP * (1 + ERRO_TEMP_PCT / 100 * DELTA_T)
    sh_hot = SH_TYP * (1 + DERIVA_SH_PCT / 100 * DELTA_T)
    v_hot = vsense(vo_hot, sh_hot, I)

    fig, ax = plt.subplots(figsize=(LARGURA, 3.6))

    ax.fill_between(I, v_env_lo, v_env_hi, color=VERM, alpha=0.15, lw=0,
                     label="Envelope de tolerância de fabricação")
    ax.plot(I, v_hot, "--", lw=1.2, color=VERDE, alpha=0.6,
            label=f"Deriva térmica (+{DELTA_T:.0f}$\\,^\\circ$C, pior caso)")
    ax.plot(I, v_typ, "-", lw=1.6, color=AZUL, label="Típico (catálogo)")

    ax.axhline(V_OUT_MAX, ls=":", lw=1.0, color=CINZA)
    ax.axhline(V_OUT_MIN, ls=":", lw=1.0, color=CINZA,
               label="Limite físico de saída do sensor")

    ax.set_xlabel("Corrente de fase $I$ [A]")
    ax.set_ylabel("$V_{sense}$ [V]")
    ax.set_xlim(-100, 100)
    ax.legend(frameon=False, fontsize=7, loc="upper left")

    fig.tight_layout()
    fig.savefig(base / "fig_erro_sensor.pdf")
    plt.close(fig)


def fig_crosstalk(base: Path) -> pd.DataFrame | None:
    """
    Maior desvio (entre os 3 sensores) introduzido pelas fases vizinhas,
    por cenario de corrente combinada, em funcao da distancia entre os
    centros dos toroides. Avaliado em AC 12 kHz (corrente de fase do
    BLDC) -- a condicao real de operacao, nao uma referencia DC. Cenarios
    de fase unica (sem vizinho energizado) sao a propria referencia e por
    construcao dao erro 0 -- ficam fora do grafico.
    """
    f = base / "crosstalk_matrix.csv"
    if not f.exists() or f.stat().st_size == 0:
        print("crosstalk_matrix.csv não encontrado ou vazio — pulando fig_crosstalk")
        return None

    df = pd.read_csv(f, sep=";")
    if df.empty:
        print("crosstalk_matrix.csv sem linhas — rode a ETAPA 2 do Lua primeiro")
        return None

    df["n_fases"] = ((df.Ia > 0.1).astype(int)
                      + (df.Ib > 0.1).astype(int)
                      + (df.Ic > 0.1).astype(int))
    comb = df[df.n_fases > 1].copy()
    comb["erro_max_pct"] = comb[["erro_a_pct", "erro_b_pct", "erro_c_pct"]].abs().max(axis=1)

    fig, ax = plt.subplots(figsize=(LARGURA, 3.4))

    for i, cenario in enumerate(sorted(comb.cenario.unique())):
        sub = comb[comb.cenario == cenario].sort_values("dist_mm")
        ax.plot(sub.dist_mm, sub.erro_max_pct,
                MARCADORES[i % len(MARCADORES)] + "-", ms=4, lw=1.2,
                color=CORES_CENARIO[i % len(CORES_CENARIO)],
                label=cenario.strip())

    # so desenha a referencia de 1% se ela cair perto da faixa plotada --
    # com o crosstalk desse sensor ficando bem abaixo disso, forcar o eixo
    # ate 1% espremia a curva real (que e' o que interessa mostrar) nos
    # 10-15% inferiores do grafico.
    erro_max = comb.erro_max_pct.max()
    if LIM_DESPREZ <= erro_max * 1.5:
        ax.axhline(LIM_DESPREZ, ls=":", lw=1.2, color=CINZA,
                   label=f"{LIM_DESPREZ:.0f} % (referência de desprezível)")
        ax.set_ylim(0, None)
    else:
        # erro_max*1.15 pode colapsar pra 0 se o CSV vier com poucas casas
        # decimais e o crosstalk real for muito pequeno (ja aconteceu) --
        # sem o piso, isso vira ylim(0, 0), que o matplotlib rejeita.
        ax.set_ylim(0, max(erro_max * 1.15, 1e-6))

    ax.set_xlabel("Distância entre centros dos toroides [mm]")
    ax.set_ylabel(r"Maior desvio por crosstalk [\%]".replace(r"\%", "%"))
    ax.legend(frameon=False, fontsize=7, loc="upper right")

    fig.tight_layout()
    fig.savefig(base / "fig_crosstalk.pdf")
    plt.close(fig)

    return comb


def fig_inductancia_self(base: Path) -> pd.DataFrame | None:
    """
    Indutância própria de cada fase em função da distância entre os
    sensores. Deveria ficar praticamente constante: a geometria de CADA
    sensor com a chapa (gap, chapa por baixo) não muda com dist_phase, só
    a distância ENTRE eles muda. Serve de controle -- se variar, é o
    acoplamento com o vizinho (já visto como desprezível no crosstalk de
    campo). A referência tracejada é o sensor isolado, sem chapa nem
    vizinhos, pra comparar contra o ambiente completo.
    """
    f = base / "inductancia_matrix.csv"
    if not f.exists() or f.stat().st_size == 0:
        print("inductancia_matrix.csv não encontrado — pulando figuras de indutância")
        return None

    df = pd.read_csv(f, sep=";")
    if df.empty:
        return None

    fig, ax = plt.subplots(figsize=(LARGURA, 3.4))
    for col, label, cor, marc in [
        ("L_aa_nH", "$L_{AA}$ (fase A)", AZUL, "o"),
        ("L_bb_nH", "$L_{BB}$ (fase B)", VERM, "s"),
        ("L_cc_nH", "$L_{CC}$ (fase C)", VERDE, "^"),
    ]:
        ax.plot(df.dist_mm, df[col], marc + "-", ms=4, lw=1.2, color=cor, label=label)

    iso_f = base / "inductancia_isolada.csv"
    if iso_f.exists() and iso_f.stat().st_size > 0:
        iso = pd.read_csv(iso_f, sep=";")
        l_iso_ac = float(iso.loc[iso.freq_hz == iso.freq_hz.max(), "L_nH"].iloc[0])
        ax.axhline(l_iso_ac, ls=":", lw=1.2, color=CINZA,
                   label=f"Sensor isolado, sem chapa/vizinhos ({l_iso_ac:.1f} nH)")

    ax.set_xlabel("Distância entre centros dos toroides [mm]")
    ax.set_ylabel("Indutância própria [nH]")
    ax.legend(frameon=False, fontsize=7, loc="best")

    fig.tight_layout()
    fig.savefig(base / "fig_inductancia_self.pdf")
    plt.close(fig)

    return df


def fig_inductancia_mutua(df: pd.DataFrame, base: Path) -> None:
    """
    Indutância mútua entre pares de fases (escala log, pH) -- é a resposta
    direta pra "modelar como um monte de indutores acoplados": M_xy é o
    termo fora da diagonal da matriz de indutância que um simulador de
    circuito usaria pra representar esse acoplamento entre as 3 fases.
    """
    fig, ax = plt.subplots(figsize=(LARGURA, 3.4))

    for col, label, cor, marc in [
        ("M_ab_nH", "$M_{AB}$ (vizinhos adjacentes)", AZUL, "o"),
        ("M_bc_nH", "$M_{BC}$ (vizinhos adjacentes)", VERM, "s"),
        ("M_ac_nH", "$M_{AC}$ (extremos, 2× a distância)", VERDE, "^"),
    ]:
        ax.plot(df.dist_mm, df[col].abs() * 1000, marc + "-", ms=4, lw=1.2,
                color=cor, label=label)

    ax.set_yscale("log")
    ax.set_xlabel("Distância entre centros dos toroides [mm]")
    ax.set_ylabel("Indutância mútua [pH]")
    ax.legend(frameon=False, fontsize=7, loc="best")

    fig.tight_layout()
    fig.savefig(base / "fig_inductancia_mutua.pdf")
    plt.close(fig)


def fig_coef_acoplamento(df: pd.DataFrame, base: Path) -> None:
    """
    k = M/sqrt(La*Lb): normaliza a mútua pelas próprias, direto comparável
    com o k de transformadores/indutores acoplados convencionais (k=1 =
    acoplamento perfeito, k~0 = independentes).
    """
    fig, ax = plt.subplots(figsize=(LARGURA, 3.4))

    for col, label, cor, marc in [
        ("k_ab", "$k_{AB}$", AZUL, "o"),
        ("k_bc", "$k_{BC}$", VERM, "s"),
        ("k_ac", "$k_{AC}$", VERDE, "^"),
    ]:
        ax.plot(df.dist_mm, df[col].abs(), marc + "-", ms=4, lw=1.2, color=cor, label=label)

    ax.set_yscale("log")
    ax.set_xlabel("Distância entre centros dos toroides [mm]")
    ax.set_ylabel("Coeficiente de acoplamento $k$")
    ax.legend(frameon=False, fontsize=7, loc="best")

    fig.tight_layout()
    fig.savefig(base / "fig_coef_acoplamento.pdf")
    plt.close(fig)


def relatorio_inducao(df_ind: pd.DataFrame | None, base: Path) -> None:
    if df_ind is None or df_ind.empty:
        return

    print("\n--- indutância própria e mútua, AC 12 kHz (modelo de indutores acoplados) ---\n")

    d_min = df_ind.dist_mm.min()
    row = df_ind[df_ind.dist_mm == d_min].iloc[0]
    print(f"Na menor distância testada ({d_min:.0f} mm):")
    print(f"  L_aa = {row.L_aa_nH:.2f} nH   L_bb = {row.L_bb_nH:.2f} nH   L_cc = {row.L_cc_nH:.2f} nH")
    print(f"  M_ab = {row.M_ab_nH*1000:.3f} pH   M_ac = {row.M_ac_nH*1000:.3f} pH   "
          f"M_bc = {row.M_bc_nH*1000:.3f} pH")
    print(f"  k_ab = {row.k_ab:.2e}   k_ac = {row.k_ac:.2e}   k_bc = {row.k_bc:.2e}")

    # M_xy e M_yx vem de duas solucoes DIFERENTES (fase X sozinha / fase Y
    # sozinha) -- deveriam bater por reciprocidade. Divergencia grande
    # indicaria malha/geometria problematica.
    recip = pd.concat([
        (df_ind.M_ab_nH - df_ind.M_ba_nH).abs() / df_ind.M_ab_nH.abs() * 100,
        (df_ind.M_ac_nH - df_ind.M_ca_nH).abs() / df_ind.M_ac_nH.abs() * 100,
        (df_ind.M_bc_nH - df_ind.M_cb_nH).abs() / df_ind.M_bc_nH.abs() * 100,
    ])
    print(f"\nChecagem de reciprocidade (M_xy vs M_yx): maior divergência = "
          f"{recip.max():.2f} % (baixo confirma malha/geometria consistentes)")

    iso_f = base / "inductancia_isolada.csv"
    if iso_f.exists() and iso_f.stat().st_size > 0:
        iso = pd.read_csv(iso_f, sep=";")
        l_dc = float(iso.loc[iso.freq_hz == 0, "L_nH"].iloc[0])
        l_ac = float(iso.loc[iso.freq_hz != 0, "L_nH"].iloc[0])
        print(f"\nIndutância própria isolada (sem chapa/vizinhos): "
              f"{l_dc:.2f} nH (DC), {l_ac:.2f} nH (AC 12 kHz)")

    print("\nPara comparar com a indutância de fase do motor: se a indutância "
          "de fase do BLDC estiver na faixa de µH a mH, o acoplamento mútuo "
          "introduzido por este arranjo de sensores (ordem de pH, k~1e-5 ou "
          "menor) é desprezível em qualquer distância testada — não deve "
          "afetar a dinâmica elétrica do motor.")


def relatorio(df: pd.DataFrame, comb: pd.DataFrame | None) -> None:
    """Numeros para citar no texto. Nao vao dentro das figuras."""
    print("\n--- para citar no texto do TCC ---\n")

    ife = float(df.I_A.iloc[-1])
    bfe = float(df.B_hall_G.iloc[-1])

    print(f"Sensibilidade média         : {df.GA_hall.mean():.3f} G/A")
    print(f"B no Hall em {ife:.0f} A          : {bfe:.1f} G")

    if bfe > LIM_SS49E:
        print(f"\n  ATENÇÃO: {bfe:.0f} G > 1000 G — o SS49E sai da faixa "
              f"linear em ~{LIM_SS49E/df.GA_hall.mean():.0f} A.")
    else:
        print(f"Margem até 1000 G           : {(LIM_SS49E/bfe - 1)*100:.1f} %")

    if comb is None or comb.empty:
        return

    print("\n--- crosstalk entre toroides, AC 12 kHz (sensor gap para baixo + chapa GND) ---\n")

    pior_por_dist = comb.groupby("dist_mm").erro_max_pct.max().sort_index()
    for dist, erro in pior_por_dist.items():
        print(f"  dist = {dist:5.0f} mm  ->  pior desvio = {erro:8.5f} %")

    abaixo = pior_por_dist[pior_por_dist < LIM_DESPREZ]
    if not abaixo.empty:
        d_min = abaixo.index.min()
        print(f"\nA partir de {d_min:.0f} mm o crosstalk fica abaixo de "
              f"{LIM_DESPREZ:.0f} % em todos os cenários testados "
              "-> pode ser desprezado a partir dessa distância.")
    else:
        print(f"\nNenhuma distância testada trouxe o crosstalk abaixo de "
              f"{LIM_DESPREZ:.0f} % — considere afastar mais os toroides "
              "no layout ou blindar entre eles.")


def main() -> None:
    base = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
    f = base / "curva_I_B_single.csv"
    if not f.exists():
        sys.exit(f"não achei {f} — rode a ETAPA 1 do Lua no FEMM primeiro")

    df = pd.read_csv(f, sep=";")
    fig_I_vs_B(df, base)
    fig_linearidade(df, base)
    fig_erro_sensor(base)
    comb = fig_crosstalk(base)
    relatorio(df, comb)

    df_ind = fig_inductancia_self(base)
    if df_ind is not None:
        fig_inductancia_mutua(df_ind, base)
        fig_coef_acoplamento(df_ind, base)
    relatorio_inducao(df_ind, base)

    print(f"\nGerado em {base.resolve()}:")
    print("  fig_I_vs_B.pdf              curva principal (sensor único)")
    print("  fig_linearidade.pdf         sensibilidade G/A")
    print("  fig_erro_sensor.pdf         Vsense vs tolerância/deriva do SS49E")
    if comb is not None:
        print("  fig_crosstalk.pdf           crosstalk vs distância entre toroides")
    if df_ind is not None:
        print("  fig_inductancia_self.pdf    indutância própria vs distância")
        print("  fig_inductancia_mutua.pdf   indutância mútua vs distância")
        print("  fig_coef_acoplamento.pdf    coeficiente de acoplamento vs distância")


if __name__ == "__main__":
    main()
