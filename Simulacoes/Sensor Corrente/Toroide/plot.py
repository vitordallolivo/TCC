#!/usr/bin/env python3
"""
Figuras I x B a partir dos CSVs do script FEMM (ss49e_nt27_gap_v6.lua).

Saida em PDF vetorial -> \\includegraphics do abntex2 sem perda na impressao.

Convencao ABNT: a figura NAO leva titulo nem texto solto. A identificacao vai
na legenda (\\caption) e a discussao no corpo do texto. Linhas de referencia
aparecem na legenda do grafico, nao como anotacao flutuante. Os numeros que
voce precisa citar no texto sao impressos no terminal.

Uso:
    python plot_curva_IB.py [diretorio]
"""

import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
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

LIM_SS49E = 1000.0      # G, fim da faixa linear do SS49E
B_JOELHO = 0.39         # T, joelho do MnZn
LARGURA = 5.5           # pol, largura util de uma A4 com margens ABNT

AZUL = "#1f4e79"
VERM = "#c0504d"
CINZA = "#595959"

ROTULO_DIR = {
    "+x": "$+x$ (para o gap)",
    "-x": "$-x$ (afastando)",
    "+y": "$+y$",
    "-y": "$-y$",
}


def fig_I_vs_B(df: pd.DataFrame, base: Path) -> None:
    fig, ax = plt.subplots(figsize=(LARGURA, 3.4))

    ax.plot(df.I_A, df.B_hall_G, "o-", ms=4, lw=1.4, color=AZUL,
            label="Centro do gap (elemento Hall)")
    ax.plot(df.I_A, df.B_med_G, "s--", ms=3.5, lw=1.1, color=VERM,
            label="Média radial ao longo do gap")

    # so desenha a referencia se ela cair dentro da faixa plotada
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
    G/A vs I com |B| do nucleo no eixo direito.

    A curva I x B parece uma reta perfeita a olho nu mesmo com varios por
    cento de erro. Quem mostra linearidade e' a sensibilidade: horizontal
    = linear. O |B| do nucleo ao lado mostra a causa junto com o efeito.
    """
    fig, ax1 = plt.subplots(figsize=(LARGURA, 3.4))

    ax1.plot(df.I_A, df.GA_hall, "o-", ms=4, lw=1.4, color=AZUL,
             label="Sensibilidade no elemento Hall")
    ax1.set_xlabel("Corrente primária $I$ [A]")
    ax1.set_ylabel("Sensibilidade [G/A]", color=AZUL)
    ax1.tick_params(axis="y", labelcolor=AZUL)
    ax1.set_xlim(0, df.I_A.max() * 1.02)

    ax2 = ax1.twinx()
    ax2.plot(df.I_A, df.B_fe_T, "^--", ms=3.5, lw=1.1, color=VERM,
             label="$|B|$ no núcleo")
    if B_JOELHO <= df.B_fe_T.max() * 1.3:
        ax2.axhline(B_JOELHO, ls="--", lw=1.0, color=VERM, alpha=0.6,
                    label=f"Joelho do MnZn ({B_JOELHO:.2f} T)".replace(".", ","))
    ax2.set_ylabel("$|B|$ no núcleo [T]", color=VERM)
    ax2.tick_params(axis="y", labelcolor=VERM)
    ax2.set_ylim(0, None)
    ax2.grid(False)

    h1, l1 = ax1.get_legend_handles_labels()
    h2, l2 = ax2.get_legend_handles_labels()
    ax1.legend(h1 + h2, l1 + l2, frameon=False, fontsize=8, loc="lower right")

    fig.tight_layout()
    fig.savefig(base / "fig_linearidade.pdf")
    plt.close(fig)


def fig_posicao(base: Path) -> None:
    f = base / "pos_cabo.csv"
    if not f.exists():
        print("pos_cabo.csv não encontrado — pulando fig_pos_cabo")
        return

    df = pd.read_csv(f, sep=";")
    dados = df[df.direcao != "centro"].copy()

    fig, ax = plt.subplots(figsize=(LARGURA, 3.0))

    dirs = ["+x", "-x", "+y", "-y"]
    passos = sorted(dados.desloc_mm.unique())
    cores = ["#9dc3e6", "#5b9bd5", AZUL]
    larg = 0.25
    x = range(len(dirs))

    for i, s in enumerate(passos):
        sub = dados[dados.desloc_mm == s].set_index("direcao").reindex(dirs)
        ax.bar([v + (i - 1) * larg for v in x], sub.desvio_pct, larg,
               color=cores[i % len(cores)], label=f"{s:.0f} mm")

    ax.axhline(0, lw=0.8, color="black")
    ax.set_xticks(list(x))
    ax.set_xticklabels([ROTULO_DIR[d] for d in dirs])
    ax.set_xlabel("Deslocamento do cabo em relação ao centro da janela")
    ax.set_ylabel(r"Desvio em $B_{\rm Hall}$ [\%]".replace("\\%", "%"))
    ax.legend(frameon=False, fontsize=8, title="Deslocamento", title_fontsize=8)

    fig.tight_layout()
    fig.savefig(base / "fig_pos_cabo.pdf")
    plt.close(fig)

    return dados


def relatorio(df: pd.DataFrame, pos: pd.DataFrame | None) -> None:
    """Numeros para citar no texto. Nao vao dentro das figuras."""
    print("\n--- para citar no texto do TCC ---\n")

    s0 = float(df.GA_hall.iloc[0])
    sfe = float(df.GA_hall.iloc[-1])
    ife = float(df.I_A.iloc[-1])
    bfe = float(df.B_hall_G.iloc[-1])

    print(f"Sensibilidade média         : {df.GA_hall.mean():.3f} G/A")
    print(f"Desvio de linearidade no FE : {(sfe/s0 - 1)*100:+.2f} %")
    print(f"B no Hall em {ife:.0f} A          : {bfe:.1f} G")
    print(f"|B| máx no núcleo           : {df.B_fe_T.max():.4f} T "
          f"({df.B_fe_T.max()/B_JOELHO*100:.0f} % do joelho)")

    if bfe > LIM_SS49E:
        print(f"\n  ATENÇÃO: {bfe:.0f} G > 1000 G — o SS49E sai da faixa "
              f"linear em ~{LIM_SS49E/df.GA_hall.mean():.0f} A.")
    else:
        print(f"Margem até 1000 G           : {(LIM_SS49E/bfe - 1)*100:.1f} %")

    if pos is None or pos.empty:
        return

    # +y e -y sao simetricos por construcao: a diferenca e' ruido de malha
    n = pos[pos.direcao == "+y"].set_index("desloc_mm").B_hall_G
    s = pos[pos.direcao == "-y"].set_index("desloc_mm").B_hall_G
    if not n.empty and not s.empty:
        piso = float(((n - s).abs() / n * 100).max())
        print(f"\nPiso de ruído numérico      : {piso:.3f} % "
              f"(assimetria +y vs -y)")
        print("  Desvios abaixo disso não são físicos — são malha.")

        pior = pos.desvio_pct.abs().max()
        print(f"Maior desvio por posição    : {pior:.2f} %")
        if pior < piso * 3:
            print("  -> abaixo do ruído: posição do cabo é irrelevante,")
            print("     confirmando o paper mesmo com gap.")
        else:
            print("  -> acima do ruído: efeito real. Espaçador na janela.")


def main() -> None:
    base = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
    f = base / "curva_I_B.csv"
    if not f.exists():
        sys.exit(f"não achei {f} — rode o script Lua no FEMM primeiro")

    df = pd.read_csv(f, sep=";")
    fig_I_vs_B(df, base)
    fig_linearidade(df, base)
    pos = fig_posicao(base)
    relatorio(df, pos)

    print(f"\nGerado em {base.resolve()}:")
    print("  fig_I_vs_B.pdf        curva principal")
    print("  fig_linearidade.pdf   G/A + |B| do núcleo")
    print("  fig_pos_cabo.pdf      influência da posição do cabo")


if __name__ == "__main__":
    main()