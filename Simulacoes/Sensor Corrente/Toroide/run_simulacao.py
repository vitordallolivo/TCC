#!/usr/bin/env python3
"""
Porta em Python (via pyFEMM) do ToroideMedicaoCorrente.lua.

Mesma geometria, mesmas duas etapas, mesmos CSVs de saida -- so troca o
canal de automacao do FEMM (Lua colado no console -> pyFEMM/win32com), o
que permite rodar e depurar aqui sem ida e volta manual pelo FEMM.

NT-27/16/12-100-IP6 + SS49E + AWG12. Gap do sensor da ETAPA 2 fica em -y
(encostado na chapa de cobre / GND do PCB).

ETAPA 1 fica em DC: e' a caracterizacao de ganho/linearidade do sensor
(G/A do nucleo, base do mV/A depois de multiplicar pela sensibilidade do
SS49E) -- uma curva de transferencia nucleo+gap, nao uma medida do sinal
real, e por isso convencionalmente feita em DC.

ETAPA 2 fica em AC, freq_ac = 12 kHz: essa sim e' a condicao real de
operacao (corrente de fase de um BLDC vindo de um conversor, nunca DC de
verdade), e e' onde teria efeito qualquer correntes de Foucault induzidas
na chapa de cobre pelos proprios sensores -- efeito que so existe em AC.

Uso:
    python run_simulacao.py
"""

import math
import femm

D_ext, D_int, h_core, gap = 27.0, 16.0, 12.0, 2.3
I_prim = 100
d_awg12 = 2.053
mu_ip6 = 2500
r_bnd = 60
freq_ac = 12000  # Hz -- corrente de fase do BLDC (o sinal a medir, nao o PWM)

r_ext = D_ext / 2
r_int = D_int / 2
r_med = (r_ext + r_int) / 2
r_awg = d_awg12 / 2
hg = gap / 2

xe_g = math.sqrt(r_ext * r_ext - hg * hg)
xi_g = math.sqrt(r_int * r_int - hg * hg)
ang_e = math.degrees(math.atan(hg / xe_g))
ang_i = math.degrees(math.atan(hg / xi_g))

x_ar = 6.5 * math.cos(math.radians(45))
y_ar = 6.5 * math.sin(math.radians(45))

margem_chapa = 20
folga_fronteira = 20
folga_pcb = 1.0        # mm, vao entre a borda do toroide e a chapa (GND do PCB)
espessura_pcb = 2.0    # mm, "espessura" modelada do plano de cobre


def draw_toroid(cx, cy, label_id):
    """Sensor unico da ETAPA 1, gap virado para +x (sem chapa de cobre por perto)."""
    femm.mi_addnode(cx + xe_g, cy + hg)
    femm.mi_addnode(cx + xe_g, cy - hg)
    femm.mi_addnode(cx - r_ext, cy)
    femm.mi_addarc(cx + xe_g, cy + hg, cx - r_ext, cy, 180 - ang_e, 1)
    femm.mi_addarc(cx - r_ext, cy, cx + xe_g, cy - hg, 180 - ang_e, 1)
    femm.mi_addarc(cx + xe_g, cy - hg, cx + xe_g, cy + hg, 2 * ang_e, 1)

    femm.mi_addnode(cx + xi_g, cy + hg)
    femm.mi_addnode(cx + xi_g, cy - hg)
    femm.mi_addnode(cx - r_int, cy)
    femm.mi_addarc(cx + xi_g, cy + hg, cx - r_int, cy, 180 - ang_i, 1)
    femm.mi_addarc(cx - r_int, cy, cx + xi_g, cy - hg, 180 - ang_i, 1)
    femm.mi_addarc(cx + xi_g, cy - hg, cx + xi_g, cy + hg, 2 * ang_i, 1)

    femm.mi_addsegment(cx + xi_g, cy + hg, cx + xe_g, cy + hg)
    femm.mi_addsegment(cx + xi_g, cy - hg, cx + xe_g, cy - hg)

    femm.mi_addnode(cx + r_awg, cy)
    femm.mi_addnode(cx - r_awg, cy)
    femm.mi_addarc(cx + r_awg, cy, cx - r_awg, cy, 180, 1)
    femm.mi_addarc(cx - r_awg, cy, cx + r_awg, cy, 180, 1)

    femm.mi_selectnode(cx + r_awg, cy)
    femm.mi_selectnode(cx - r_awg, cy)
    femm.mi_setnodeprop("<None>", 2)
    femm.mi_clearselected()

    femm.mi_selectarcsegment(cx, cy + r_awg)
    femm.mi_selectarcsegment(cx, cy - r_awg)
    femm.mi_setarcsegmentprop(1, "<None>", 0, 2)
    femm.mi_clearselected()

    femm.mi_addblocklabel(cx - r_med, cy)
    femm.mi_selectlabel(cx - r_med, cy)
    femm.mi_setblockprop("IP6", 0, 0.5, "<None>", 0, label_id * 10 + 1, 0)
    femm.mi_clearselected()

    femm.mi_addblocklabel(cx, cy)
    femm.mi_selectlabel(cx, cy)
    femm.mi_setblockprop("Copper", 0, 0.4, "PRIM", 0, label_id * 10 + 2, 1)
    femm.mi_clearselected()

    femm.mi_addblocklabel(cx + x_ar, cy + y_ar)
    femm.mi_selectlabel(cx + x_ar, cy + y_ar)
    femm.mi_setblockprop("Air", 0, 0.35, "<None>", 0, label_id * 10 + 3, 0)
    femm.mi_clearselected()

    femm.mi_addblocklabel(cx + r_med, cy)
    femm.mi_selectlabel(cx + r_med, cy)
    femm.mi_setblockprop("Air", 0, 0.1, "<None>", 0, label_id * 10 + 4, 0)
    femm.mi_clearselected()


def draw_sensor_gap_down(cx, cy, circuit_label, label_id):
    """Mesmo desenho de draw_toroid, rotacionado -90 graus: gap em -y."""
    femm.mi_addnode(cx + hg, cy - xe_g)
    femm.mi_addnode(cx - hg, cy - xe_g)
    femm.mi_addnode(cx, cy + r_ext)
    femm.mi_addarc(cx + hg, cy - xe_g, cx, cy + r_ext, 180 - ang_e, 1)
    femm.mi_addarc(cx, cy + r_ext, cx - hg, cy - xe_g, 180 - ang_e, 1)
    femm.mi_addarc(cx - hg, cy - xe_g, cx + hg, cy - xe_g, 2 * ang_e, 1)

    femm.mi_addnode(cx + hg, cy - xi_g)
    femm.mi_addnode(cx - hg, cy - xi_g)
    femm.mi_addnode(cx, cy + r_int)
    femm.mi_addarc(cx + hg, cy - xi_g, cx, cy + r_int, 180 - ang_i, 1)
    femm.mi_addarc(cx, cy + r_int, cx - hg, cy - xi_g, 180 - ang_i, 1)
    femm.mi_addarc(cx - hg, cy - xi_g, cx + hg, cy - xi_g, 2 * ang_i, 1)

    femm.mi_addsegment(cx + hg, cy - xi_g, cx + hg, cy - xe_g)
    femm.mi_addsegment(cx - hg, cy - xi_g, cx - hg, cy - xe_g)

    femm.mi_addnode(cx + r_awg, cy)
    femm.mi_addnode(cx - r_awg, cy)
    femm.mi_addarc(cx + r_awg, cy, cx - r_awg, cy, 180, 1)
    femm.mi_addarc(cx - r_awg, cy, cx + r_awg, cy, 180, 1)

    femm.mi_selectnode(cx + r_awg, cy)
    femm.mi_selectnode(cx - r_awg, cy)
    femm.mi_setnodeprop("<None>", 2)
    femm.mi_clearselected()

    femm.mi_selectarcsegment(cx, cy + r_awg)
    femm.mi_selectarcsegment(cx, cy - r_awg)
    femm.mi_setarcsegmentprop(1, "<None>", 0, 2)
    femm.mi_clearselected()

    femm.mi_addblocklabel(cx, cy + r_med)
    femm.mi_selectlabel(cx, cy + r_med)
    femm.mi_setblockprop("IP6", 0, 0.5, "<None>", 0, label_id * 10 + 1, 0)
    femm.mi_clearselected()

    femm.mi_addblocklabel(cx, cy)
    femm.mi_selectlabel(cx, cy)
    femm.mi_setblockprop("Copper", 0, 0.4, circuit_label, 0, label_id * 10 + 2, 1)
    femm.mi_clearselected()

    femm.mi_addblocklabel(cx + x_ar, cy + y_ar)
    femm.mi_selectlabel(cx + x_ar, cy + y_ar)
    femm.mi_setblockprop("Air", 0, 0.35, "<None>", 0, label_id * 10 + 3, 0)
    femm.mi_clearselected()

    femm.mi_addblocklabel(cx, cy - r_med)
    femm.mi_selectlabel(cx, cy - r_med)
    femm.mi_setblockprop("Air", 0, 0.1, "<None>", 0, label_id * 10 + 4, 0)
    femm.mi_clearselected()


def build_3phase(dist_phase):
    femm.newdocument(0)
    femm.mi_probdef(freq_ac, "millimeters", "planar", 1e-8, h_core, 30)

    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addmaterial("IP6", mu_ip6, mu_ip6, 0, 0, 2e-7, 0, 0, 1, 0, 0, 0, 0, 0)
    femm.mi_addcircprop("PRIM_A", I_prim, 1)
    femm.mi_addcircprop("PRIM_B", I_prim, 1)
    femm.mi_addcircprop("PRIM_C", I_prim, 1)

    x_A, y_A = -dist_phase, 0
    x_B, y_B = 0, 0
    x_C, y_C = dist_phase, 0

    draw_sensor_gap_down(x_A, y_A, "PRIM_A", 1)
    draw_sensor_gap_down(x_B, y_B, "PRIM_B", 2)
    draw_sensor_gap_down(x_C, y_C, "PRIM_C", 3)

    chapa_x1 = x_A - margem_chapa
    chapa_x2 = x_C + margem_chapa
    # topo da chapa a folga_pcb mm da borda externa do toroide (r_ext): e' o
    # ponto do anel mais proximo da chapa, gap incluso, ja que o gap fica
    # exatamente nesse lado (-y).
    chapa_y2 = -(r_ext + folga_pcb)
    chapa_y1 = chapa_y2 - espessura_pcb

    femm.mi_addnode(chapa_x1, chapa_y1)
    femm.mi_addnode(chapa_x2, chapa_y1)
    femm.mi_addnode(chapa_x2, chapa_y2)
    femm.mi_addnode(chapa_x1, chapa_y2)

    femm.mi_addsegment(chapa_x1, chapa_y1, chapa_x2, chapa_y1)
    femm.mi_addsegment(chapa_x2, chapa_y1, chapa_x2, chapa_y2)
    femm.mi_addsegment(chapa_x2, chapa_y2, chapa_x1, chapa_y2)
    femm.mi_addsegment(chapa_x1, chapa_y2, chapa_x1, chapa_y1)

    femm.mi_addblocklabel(0, (chapa_y1 + chapa_y2) / 2)
    femm.mi_selectlabel(0, (chapa_y1 + chapa_y2) / 2)
    femm.mi_setblockprop("Copper", 0, 0, "<None>", 0, 50, 0)
    femm.mi_clearselected()

    r_bnd_local = math.sqrt(chapa_x2 * chapa_x2 + chapa_y1 * chapa_y1) + folga_fronteira
    y_ar_ext = 0.85 * r_bnd_local

    femm.mi_addblocklabel(0, y_ar_ext)
    femm.mi_selectlabel(0, y_ar_ext)
    femm.mi_setblockprop("Air", 1, 0, "<None>", 0, 99, 0)
    femm.mi_clearselected()

    femm.mi_makeABC(7, r_bnd_local, 0, 0, 0)

    femm.mi_zoomnatural()
    femm.mi_saveas(f"ss49e_3phase_d{dist_phase:03.0f}mm.fem")

    return x_A, y_A, x_B, y_B, x_C, y_C


def B_field_G(x, y):
    """
    abs() em cada componente antes de somar os quadrados: em DC, b1/b2 sao
    reais e abs() nao muda nada. Em AC (freq != 0), b1/b2 vem como fasores
    complexos do FEMM (defasagem por correntes de Foucault na chapa) -- essa
    e' a mesma convencao que o FEMM usa nos proprios exemplos pra reportar
    uma magnitude de |B| a partir de fasores.
    """
    vals = femm.mo_getpointvalues(x, y)
    b1, b2 = vals[1], vals[2]
    return math.sqrt(abs(b1) ** 2 + abs(b2) ** 2) * 1e4


def L_nH(circuit, I_ref):
    """
    Indutancia (self, se circuit e' o proprio energizado, ou mutua, se e'
    outro) a partir do enlace de fluxo que o FEMM ja calcula por circuito:
    mo_getcircuitproperties(nome) -> [corrente, queda_de_tensao, enlace_de_fluxo].
    L = enlace_de_fluxo / I_ref. abs() pega so' a magnitude do fasor em AC
    (mesma convencao usada em B_field_G) -- a parte imaginaria e' perda
    (Foucault/histerese equivalente), pequena perto da parte real aqui.
    """
    _, _, flux_linkage = femm.mo_getcircuitproperties(circuit)
    return abs(flux_linkage) / I_ref * 1e9


def etapa1():
    """
    DC de proposito: essa etapa existe pra caracterizar o ganho do sensor
    (G/A no nucleo -> mV/G do SS49E -> ganho final em mV/A), que e' uma
    curva de LINEARIDADE nucleo+gap, nao uma medida do sinal real. Como o
    material IP6 e' linear (sem curva B-H) essa curva da praticamente o
    mesmo resultado em DC ou em 12 kHz -- mas a caracterizacao de
    linearidade/ganho e' convencionalmente feita em DC, entao fica em DC
    aqui. Quem roda em AC (a condicao real de operacao) e' a ETAPA 2.
    """
    print("=" * 67)
    print(" ETAPA 1 - UM SENSOR: VARREDURA DE CORRENTE (DC, ganho/linearidade)")
    print("=" * 67)

    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, h_core, 30)
    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addmaterial("IP6", mu_ip6, mu_ip6, 0, 0, 2e-7, 0, 0, 1, 0, 0, 0, 0, 0)
    femm.mi_addcircprop("PRIM", I_prim, 1)

    draw_toroid(0, 0, 1)

    femm.mi_addblocklabel(0, (r_ext + r_bnd) / 2)
    femm.mi_selectlabel(0, (r_ext + r_bnd) / 2)
    femm.mi_setblockprop("Air", 1, 0, "<None>", 0, 99, 0)
    femm.mi_clearselected()

    femm.mi_makeABC(7, r_bnd, 0, 0, 0)
    femm.mi_zoomnatural()
    femm.mi_saveas("ss49e_single.fem")

    correntes = [1, 2, 5, 10, 20, 30, 40, 50, 60, 75, 90, 100]
    rows = []
    S_nom = 0
    L_dc = 0.0
    print("  I[A]  B_hall[G]  G/A")
    for I in correntes:
        femm.mi_modifycircprop("PRIM", 1, I)
        femm.mi_analyze(1)
        femm.mi_loadsolution()
        bh = B_field_G(r_med, 0)
        if I == I_prim:
            S_nom = bh / I
            L_dc = L_nH("PRIM", I)  # indutancia isolada, sem chapa/vizinhos
        print(f" {I:3.0f}   {bh:8.1f}  {bh/I:6.3f}")
        rows.append((I, bh, bh / I))
        femm.mo_close()

    with open("curva_I_B_single.csv", "w") as f:
        f.write("I_A;B_hall_G;GA_hall\n")
        for I, bh, ga in rows:
            f.write(f"{I:.0f};{bh:.2f};{ga:.4f}\n")

    print(f"\n G/A nominal: {S_nom:.3f} G/A")
    print(" -> curva_I_B_single.csv")

    # indutancia ISOLADA (sem chapa, sem vizinhos) em DC e em freq_ac, pra
    # servir de referencia na figura de indutancia propria da ETAPA 2 --
    # mostra o quanto (se e' que muda algo) a chapa/vizinhos alteram a
    # indutancia propria de cada sensor em relacao ao caso isolado.
    femm.newdocument(0)
    femm.mi_probdef(freq_ac, "millimeters", "planar", 1e-8, h_core, 30)
    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addmaterial("IP6", mu_ip6, mu_ip6, 0, 0, 2e-7, 0, 0, 1, 0, 0, 0, 0, 0)
    femm.mi_addcircprop("PRIM", I_prim, 1)
    draw_toroid(0, 0, 1)
    femm.mi_addblocklabel(0, (r_ext + r_bnd) / 2)
    femm.mi_selectlabel(0, (r_ext + r_bnd) / 2)
    femm.mi_setblockprop("Air", 1, 0, "<None>", 0, 99, 0)
    femm.mi_clearselected()
    femm.mi_makeABC(7, r_bnd, 0, 0, 0)
    femm.mi_saveas("ss49e_single_ac.fem")
    femm.mi_analyze(1)
    femm.mi_loadsolution()
    L_ac = L_nH("PRIM", I_prim)
    femm.mo_close()

    with open("inductancia_isolada.csv", "w") as f:
        f.write("freq_hz;L_nH\n")
        f.write(f"0;{L_dc:.4f}\n")
        f.write(f"{freq_ac:.0f};{L_ac:.4f}\n")

    print(f"\n Indutancia propria isolada (sem chapa/vizinhos), {I_prim} A:")
    print(f"   L_DC = {L_dc:.2f} nH   L_AC ({freq_ac:.0f} Hz) = {L_ac:.2f} nH")
    print(" -> inductancia_isolada.csv")


def etapa2():
    print("\n" + "=" * 67)
    print(" ETAPA 2 - TRES SENSORES TRIFASICOS + PLACA DE COBRE")
    print(f" Varredura de dist_phase (distancia entre centros) para o crosstalk, AC {freq_ac} Hz")
    print("=" * 67)

    cenarios = [
        (100, 0, 0, "Ia=100, Ib=0,   Ic=0"),
        (0, 100, 0, "Ia=0,   Ib=100, Ic=0"),
        (0, 0, 100, "Ia=0,   Ib=0,   Ic=100"),
        (100, 100, 0, "Ia=100, Ib=100, Ic=0"),
        (100, 0, 100, "Ia=100, Ib=0,   Ic=100"),
        (0, 100, 100, "Ia=0,   Ib=100, Ic=100"),
        (100, 100, 100, "Ia=100, Ib=100, Ic=100"),
    ]

    distancias_mm = [25, 28, 30, 35, 40, 50, 60, 80]

    matrix_rows = []
    resumo_rows = []
    inductancia_rows = []
    dist_fig = next(d for d in distancias_mm if d > 2 * r_ext)

    for dist in distancias_mm:
        if dist <= 2 * r_ext:
            print(f" [{dist:.0f} mm] PULADO: toroides se sobrepoem (2*r_ext = {2*r_ext:.1f} mm)")
            continue

        x_A, y_A, x_B, y_B, x_C, y_C = build_3phase(dist)

        print(f"\n -- dist_phase = {dist:.0f} mm (borda a borda: {dist - 2*r_ext:.1f} mm) --")
        print("cenario                   | B_Ha[G] B_Hb[G] B_Hc[G] | desvio")

        bha_ref = bhb_ref = bhc_ref = 0.0
        pior = 0.0
        # indutancia propria/mutua: sai de graca dos MESMOS 3 cenarios de
        # fase unica ja resolvidos pra referencia de crosstalk -- nenhuma
        # analise extra, so' mais uma leitura (mo_getcircuitproperties) em
        # cima da solucao que ja esta carregada.
        L_matrix = {}

        for Ia, Ib, Ic, desc in cenarios:
            femm.mi_modifycircprop("PRIM_A", 1, Ia)
            femm.mi_modifycircprop("PRIM_B", 1, Ib)
            femm.mi_modifycircprop("PRIM_C", 1, Ic)
            femm.mi_analyze(1)
            femm.mi_loadsolution()

            bha = B_field_G(x_A, y_A - r_med)
            bhb = B_field_G(x_B, y_B - r_med)
            bhc = B_field_G(x_C, y_C - r_med)

            if (Ia, Ib, Ic) == (100, 0, 0):
                bha_ref = bha
                L_matrix["L_aa"] = L_nH("PRIM_A", Ia)
                L_matrix["M_ab"] = L_nH("PRIM_B", Ia)
                L_matrix["M_ac"] = L_nH("PRIM_C", Ia)
            if (Ia, Ib, Ic) == (0, 100, 0):
                bhb_ref = bhb
                L_matrix["M_ba"] = L_nH("PRIM_A", Ib)
                L_matrix["L_bb"] = L_nH("PRIM_B", Ib)
                L_matrix["M_bc"] = L_nH("PRIM_C", Ib)
            if (Ia, Ib, Ic) == (0, 0, 100):
                bhc_ref = bhc
                L_matrix["M_ca"] = L_nH("PRIM_A", Ic)
                L_matrix["M_cb"] = L_nH("PRIM_B", Ic)
                L_matrix["L_cc"] = L_nH("PRIM_C", Ic)

            err_a = (bha - bha_ref) / bha_ref * 100 if (Ia > 0.1 and bha_ref > 0.1) else 0.0
            err_b = (bhb - bhb_ref) / bhb_ref * 100 if (Ib > 0.1 and bhb_ref > 0.1) else 0.0
            err_c = (bhc - bhc_ref) / bhc_ref * 100 if (Ic > 0.1 and bhc_ref > 0.1) else 0.0

            pior = max(pior, abs(err_a), abs(err_b), abs(err_c))

            print(f"{desc:<25s} | {bha:7.1f} {bhb:7.1f} {bhc:7.1f} | {err_a:+5.2f} {err_b:+5.2f} {err_c:+5.2f}")

            matrix_rows.append((dist, desc, Ia, Ib, Ic, bha, bhb, bhc, err_a, err_b, err_c))
            femm.mo_close()

        print(f" pior desvio nesta distancia: {pior:.2f} %")
        resumo_rows.append((dist, pior))

        k_ab = L_matrix["M_ab"] / math.sqrt(L_matrix["L_aa"] * L_matrix["L_bb"])
        k_ac = L_matrix["M_ac"] / math.sqrt(L_matrix["L_aa"] * L_matrix["L_cc"])
        k_bc = L_matrix["M_bc"] / math.sqrt(L_matrix["L_bb"] * L_matrix["L_cc"])
        print(f" L_aa={L_matrix['L_aa']:.2f} nH  L_bb={L_matrix['L_bb']:.2f} nH  L_cc={L_matrix['L_cc']:.2f} nH")
        print(f" M_ab={L_matrix['M_ab']*1000:.2f} pH  M_ac={L_matrix['M_ac']*1000:.2f} pH  M_bc={L_matrix['M_bc']*1000:.2f} pH"
              f"  (k_ab={k_ab:.2e}, k_ac={k_ac:.2e}, k_bc={k_bc:.2e})")
        inductancia_rows.append((dist, L_matrix["L_aa"], L_matrix["L_bb"], L_matrix["L_cc"],
                                  L_matrix["M_ab"], L_matrix["M_ba"], L_matrix["M_ac"],
                                  L_matrix["M_ca"], L_matrix["M_bc"], L_matrix["M_cb"],
                                  k_ab, k_ac, k_bc))

    # erro_*_pct em 5 casas: em AC 12 kHz o crosstalk fica na ordem de
    # 0,002 %, entao 2 casas decimais (usado nas colunas de B, onde faz
    # sentido) arredondava tudo pra "0.00" e escondia o proprio sinal que a
    # varredura existe pra medir.
    with open("crosstalk_matrix.csv", "w") as h:
        h.write("dist_mm;cenario;Ia;Ib;Ic;B_Ha_G;B_Hb_G;B_Hc_G;erro_a_pct;erro_b_pct;erro_c_pct\n")
        for dist, desc, Ia, Ib, Ic, bha, bhb, bhc, ea, eb, ec in matrix_rows:
            h.write(f"{dist:.0f};{desc};{Ia:.0f};{Ib:.0f};{Ic:.0f};{bha:.2f};{bhb:.2f};{bhc:.2f};{ea:.5f};{eb:.5f};{ec:.5f}\n")

    with open("crosstalk_resumo.csv", "w") as r:
        r.write("dist_mm;pior_erro_pct\n")
        for dist, pior in resumo_rows:
            r.write(f"{dist:.0f};{pior:.5f}\n")

    # M_xy e M_yx vem de duas solucoes DIFERENTES (uma so com Ix, outra so
    # com Iy) -- deveriam bater por reciprocidade. E' um checkbox de
    # qualidade da malha/geometria de graca.
    with open("inductancia_matrix.csv", "w") as m:
        m.write("dist_mm;L_aa_nH;L_bb_nH;L_cc_nH;M_ab_nH;M_ba_nH;M_ac_nH;M_ca_nH;M_bc_nH;M_cb_nH;k_ab;k_ac;k_bc\n")
        for row in inductancia_rows:
            (dist, laa, lbb, lcc, mab, mba, mac, mca, mbc, mcb, kab, kac, kbc) = row
            m.write(f"{dist:.0f};{laa:.5f};{lbb:.5f};{lcc:.5f};{mab:.6f};{mba:.6f};"
                    f"{mac:.6f};{mca:.6f};{mbc:.6f};{mcb:.6f};{kab:.3e};{kac:.3e};{kbc:.3e}\n")

    print("\n -> crosstalk_matrix.csv  (todas as distancias e cenarios)")
    print(" -> crosstalk_resumo.csv  (pior desvio por distancia)")
    print(" -> inductancia_matrix.csv  (self/mutua por distancia)")

    return dist_fig


def figura_single():
    """
    Foto do sensor unico (caso 1, ETAPA 1: gap virado pra +x, sem chapa por
    perto). mo_savebitmap precisa de janela visivel -- so' roda com o FEMM
    aberto sem esconder.
    """
    print("\n" + "=" * 67)
    print(" FIGURA - sensor unico (ETAPA 1)")
    print("=" * 67)

    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, h_core, 30)
    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addmaterial("IP6", mu_ip6, mu_ip6, 0, 0, 2e-7, 0, 0, 1, 0, 0, 0, 0, 0)
    femm.mi_addcircprop("PRIM", I_prim, 1)
    draw_toroid(0, 0, 1)
    femm.mi_addblocklabel(0, (r_ext + r_bnd) / 2)
    femm.mi_selectlabel(0, (r_ext + r_bnd) / 2)
    femm.mi_setblockprop("Air", 1, 0, "<None>", 0, 99, 0)
    femm.mi_clearselected()
    femm.mi_makeABC(7, r_bnd, 0, 0, 0)
    femm.mi_saveas("ss49e_single_figura.fem")
    femm.mi_analyze(1)
    femm.mi_loadsolution()

    femm.mo_showdensityplot(1, 0, 0.06, 0, "bmag")
    femm.main_resize(800, 700)
    femm.mo_zoom(-(r_ext + 3), -(r_ext + 3), r_ext + 8, r_ext + 3)
    femm.mo_savebitmap("single_gap.bmp")

    femm.mo_close()
    print(" Figura gerada: single_gap.bmp")


def gerar_figuras(dist_fig):
    """
    Caso 3 (ETAPA 2): so' a vista geral -- o pedido foi "uma foto tipo
    trifase_geral ja esta bom", entao os detalhes por sensor (A/B/C) saem.
    mo_savebitmap precisa de janela visivel, por isso roda separado da
    etapa1()/etapa2() (essas ficam ocultas, mais rapido).
    """
    print("\n" + "=" * 67)
    print(" FIGURA - tres sensores + chapa (pior caso, menor distancia valida)")
    print("=" * 67)

    x_A, _, x_B, _, x_C, _ = build_3phase(dist_fig)
    femm.mi_modifycircprop("PRIM_A", 1, 100)
    femm.mi_modifycircprop("PRIM_B", 1, 100)
    femm.mi_modifycircprop("PRIM_C", 1, 100)
    femm.mi_analyze(1)
    femm.mi_loadsolution()

    # janela redimensionada pra bater com a proporcao do zoom -- sem isso
    # o FEMM preenche a sobra com fundo branco
    femm.mo_showdensityplot(1, 0, 0.06, 0, "bmag")
    femm.main_resize(1200, 480)
    femm.mo_zoom(x_A - margem_chapa, -20, x_C + margem_chapa, 18)
    femm.mo_savebitmap(f"trifase_geral_d{dist_fig:03.0f}mm.bmp")

    femm.mo_close()
    print(f" Figura gerada para dist_phase = {dist_fig:.0f} mm.")


def main():
    femm.openfemm(1)
    try:
        etapa1()
        dist_fig = etapa2()
        femm.closefemm()

        # bitmap precisa de janela visivel -- reabre sem ocultar so' pra isso
        femm.openfemm()
        figura_single()
        gerar_figuras(dist_fig)
    finally:
        femm.closefemm()
    print("\nPronto.")


if __name__ == "__main__":
    main()
