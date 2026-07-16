-- =============================================================================
--  FEMM 4.2 / Lua 4.0  --  NT-27/16/12-100-IP6 + SS49E + AWG12
--
--  v15: placa de cobre retangular FECHADA, gap apontando para y negativo.
--       ETAPA 1 (sensor unico) fica em DC -- caracterizacao de ganho e
--       linearidade nucleo+gap, nao uma medida do sinal real. ETAPA 2
--       (3 sensores + chapa) roda em AC 12 kHz -- essa sim e' a condicao
--       real de operacao (corrente de fase de um BLDC via conversor,
--       nunca DC de verdade), e e' onde apareceriam correntes de Foucault
--       induzidas na chapa (efeito que so' existe em AC).
-- =============================================================================

showconsole()
clearconsole()

D_ext   = 27.0
D_int   = 16.0
h_core  = 12.0
gap     = 2.3
I_prim  = 100
d_awg12 = 2.053
mu_ip6  = 2500
r_bnd   = 60
freq_ac = 12000   -- Hz, corrente de fase do BLDC (o sinal a medir, nao o PWM)

r_ext = D_ext/2
r_int = D_int/2
r_med = (r_ext + r_int)/2
r_awg = d_awg12/2
hg    = gap/2

xe_g  = sqrt(r_ext*r_ext - hg*hg)
xi_g  = sqrt(r_int*r_int - hg*hg)
ang_e = deg(atan(hg/xe_g))
ang_i = deg(atan(hg/xi_g))

x_ar = 6.5*cos(rad(45))
y_ar = 6.5*sin(rad(45))

newdocument(0)
mi_probdef(0, "millimeters", "planar", 1e-8, h_core, 30)
main_resize(900, 900)

mi_getmaterial("Air")
mi_getmaterial("Copper")
mi_addmaterial("IP6", mu_ip6, mu_ip6, 0, 0, 2e-7, 0, 0, 1, 0, 0, 0, 0, 0)
mi_addcircprop("PRIM", I_prim, 1)

function draw_toroid(cx, cy, label_id)
  mi_addnode(cx + xe_g,  cy + hg)
  mi_addnode(cx + xe_g,  cy - hg)
  mi_addnode(cx - r_ext, cy)
  mi_addarc(cx + xe_g,  cy + hg, cx - r_ext, cy, 180 - ang_e, 1)
  mi_addarc(cx - r_ext, cy, cx + xe_g, cy - hg, 180 - ang_e, 1)
  mi_addarc(cx + xe_g,  cy - hg, cx + xe_g, cy + hg, 2*ang_e, 1)
  
  mi_addnode(cx + xi_g,  cy + hg)
  mi_addnode(cx + xi_g,  cy - hg)
  mi_addnode(cx - r_int, cy)
  mi_addarc(cx + xi_g,  cy + hg, cx - r_int, cy, 180 - ang_i, 1)
  mi_addarc(cx - r_int, cy, cx + xi_g, cy - hg, 180 - ang_i, 1)
  mi_addarc(cx + xi_g,  cy - hg, cx + xi_g, cy + hg, 2*ang_i, 1)
  
  mi_addsegment(cx + xi_g,  cy + hg, cx + xe_g,  cy + hg)
  mi_addsegment(cx + xi_g,  cy - hg, cx + xe_g,  cy - hg)
  
  mi_addnode( cx + r_awg, cy)
  mi_addnode( cx - r_awg, cy)
  mi_addarc( cx + r_awg, cy, cx - r_awg, cy, 180, 1)
  mi_addarc( cx - r_awg, cy, cx + r_awg, cy, 180, 1)
  
  mi_selectnode(cx + r_awg, cy)
  mi_selectnode(cx - r_awg, cy)
  mi_setnodeprop("<None>", 2)
  mi_clearselected()
  
  mi_selectarcsegment(cx, cy + r_awg)
  mi_selectarcsegment(cx, cy - r_awg)
  mi_setarcsegmentprop(1, "<None>", 0, 2)
  mi_clearselected()
  
  mi_addblocklabel(cx - r_med, cy)
  mi_selectlabel(cx - r_med, cy)
  mi_setblockprop("IP6", 0, 0.5, "<None>", 0, label_id*10 + 1, 0)
  mi_clearselected()
  
  mi_addblocklabel(cx, cy)
  mi_selectlabel(cx, cy)
  mi_setblockprop("Copper", 0, 0.4, "PRIM", 0, label_id*10 + 2, 1)
  mi_clearselected()
  
  mi_addblocklabel(cx + x_ar, cy + y_ar)
  mi_selectlabel(cx + x_ar, cy + y_ar)
  mi_setblockprop("Air", 0, 0.35, "<None>", 0, label_id*10 + 3, 0)
  mi_clearselected()
  
  mi_addblocklabel(cx + r_med, cy)
  mi_selectlabel(cx + r_med, cy)
  mi_setblockprop("Air", 0, 0.1, "<None>", 0, label_id*10 + 4, 0)
  mi_clearselected()
end

draw_toroid(0, 0, 1)

mi_addblocklabel(0, (r_ext + r_bnd)/2)
mi_selectlabel(0, (r_ext + r_bnd)/2)
mi_setblockprop("Air", 1, 0, "<None>", 0, 99, 0)
mi_clearselected()

-- fronteira aberta: usa o Open Boundary do proprio FEMM (equivalente ao
-- botao "Add Open Boundary" do editor) em vez de desenhar circulo+ABC na
-- mao. mi_makeABC ja desenha as cascas, cria a boundprop e atribui tudo
-- sozinho -- nao desenhe nenhum circulo de fronteira antes de chamar isso.
mi_makeABC(7, r_bnd, 0, 0, 0)

mi_zoomnatural()
mi_saveas("ss49e_single.fem")

function B_hall()
  local a, b1, b2 = mo_getpointvalues(r_med, 0)
  return sqrt(b1*b1 + b2*b2)
end

function resolve(I)
  mi_modifycircprop("PRIM", 1, I)
  mi_analyze(0)
  mi_loadsolution()
end

-- Indutancia (self, se circuit e' o proprio energizado, ou mutua, se e'
-- outro) a partir do enlace de fluxo que o FEMM ja calcula por circuito:
-- mo_getcircuitproperties(nome) -> corrente, queda_de_tensao, enlace_de_fluxo.
-- L = enlace_de_fluxo / I_ref. abs() pega so' a magnitude do fasor em AC
-- (mesma convencao de B_hall_at) -- a parte imaginaria e' perda
-- (Foucault/histerese equivalente), pequena perto da parte real aqui.
function circ_L_nH(circuit, I_ref)
  local ii, vv, flux = mo_getcircuitproperties(circuit)
  return abs(flux)/I_ref*1e9
end

-- DC de proposito: esta etapa caracteriza o ganho/linearidade do sensor
-- (G/A do nucleo -> mV/A depois de multiplicar pela sensibilidade do
-- SS49E), uma curva de transferencia nucleo+gap que e' convencionalmente
-- levantada em DC. Quem roda na condicao real de operacao (corrente de
-- fase do BLDC, AC) e' a ETAPA 2.
print("===================================================================")
print(" ETAPA 1 - UM SENSOR: VARREDURA DE CORRENTE (DC, ganho/linearidade)")
print("===================================================================")
print("")

correntes = {1, 2, 5, 10, 20, 30, 40, 50, 60, 75, 90, 100}

f = openfile("curva_I_B_single.csv", "w")
if f then
  write(f, "I_A;B_hall_G;GA_hall\n")
end

print("  I[A]  B_hall[G]  G/A")
print(" ----  ---------  -----")

S_nom = 0
L_dc = 0
for k = 1, 12 do
  I = correntes[k]
  resolve(I)

  bh = B_hall()*1e4

  if I == I_prim then
    S_nom = bh/I
    L_dc = circ_L_nH("PRIM", I)   -- indutancia isolada, sem chapa/vizinhos
  end

  print(format(" %3.0f   %8.1f  %6.3f", I, bh, bh/I))

  if f then
    write(f, format("%.0f;%.2f;%.4f\n", I, bh, bh/I))
  end
  mo_close()
end

if f then closefile(f) end
print("")
print(format(" G/A nominal: %.3f G/A", S_nom))
print(" -> curva_I_B_single.csv")

-- indutancia ISOLADA (sem chapa, sem vizinhos) em DC e em freq_ac, pra
-- servir de referencia na figura de indutancia propria da ETAPA 2 --
-- mostra o quanto (se e' que muda algo) a chapa/vizinhos alteram a
-- indutancia propria de cada sensor em relacao ao caso isolado.
newdocument(0)
mi_probdef(freq_ac, "millimeters", "planar", 1e-8, h_core, 30)
mi_getmaterial("Air")
mi_getmaterial("Copper")
mi_addmaterial("IP6", mu_ip6, mu_ip6, 0, 0, 2e-7, 0, 0, 1, 0, 0, 0, 0, 0)
mi_addcircprop("PRIM", I_prim, 1)
draw_toroid(0, 0, 1)
mi_addblocklabel(0, (r_ext + r_bnd)/2)
mi_selectlabel(0, (r_ext + r_bnd)/2)
mi_setblockprop("Air", 1, 0, "<None>", 0, 99, 0)
mi_clearselected()
mi_makeABC(7, r_bnd, 0, 0, 0)
mi_saveas("ss49e_single_ac.fem")
mi_analyze(1)
mi_loadsolution()
L_ac = circ_L_nH("PRIM", I_prim)
mo_close()

iso_csv = openfile("inductancia_isolada.csv", "w")
if iso_csv then
  write(iso_csv, "freq_hz;L_nH\n")
  write(iso_csv, format("0;%.4f\n", L_dc))
  write(iso_csv, format("%.0f;%.4f\n", freq_ac, L_ac))
  closefile(iso_csv)
end

print(format("\n Indutancia propria isolada (sem chapa/vizinhos), %d A:", I_prim))
print(format("   L_DC = %.2f nH   L_AC (%.0f Hz) = %.2f nH", L_dc, freq_ac, L_ac))
print(" -> inductancia_isolada.csv")

print("")
print("===================================================================")
print(" FIGURA - sensor unico (ETAPA 1)")
print("===================================================================")

-- foto do sensor unico (caso 1: gap virado pra +x, sem chapa por perto)
newdocument(0)
mi_probdef(0, "millimeters", "planar", 1e-8, h_core, 30)
mi_getmaterial("Air")
mi_getmaterial("Copper")
mi_addmaterial("IP6", mu_ip6, mu_ip6, 0, 0, 2e-7, 0, 0, 1, 0, 0, 0, 0, 0)
mi_addcircprop("PRIM", I_prim, 1)
draw_toroid(0, 0, 1)
mi_addblocklabel(0, (r_ext + r_bnd)/2)
mi_selectlabel(0, (r_ext + r_bnd)/2)
mi_setblockprop("Air", 1, 0, "<None>", 0, 99, 0)
mi_clearselected()
mi_makeABC(7, r_bnd, 0, 0, 0)
mi_saveas("ss49e_single_figura.fem")
mi_analyze(1)
mi_loadsolution()

mo_showdensityplot(1, 0, 0.06, 0, "bmag")
main_resize(800, 700)
mo_zoom(-(r_ext + 3), -(r_ext + 3), r_ext + 8, r_ext + 3)
mo_savebitmap("single_gap.bmp")
mo_close()

print(" Figura gerada: single_gap.bmp")

print("")
print("===================================================================")
print(" ETAPA 2 - TRES SENSORES TRIFASICOS + PLACA DE COBRE")
print(format(" Varredura de dist_phase (distancia entre centros) para o crosstalk, AC %.0f Hz", freq_ac))
print(" (corrente de fase do BLDC: sinal sempre alternado, nunca DC de verdade)")
print("===================================================================")
print("")

-- folga da chapa de cobre alem do sensor mais externo, e folga de ar entre
-- a chapa/sensores e a fronteira ABC. Com isso a fronteira SEMPRE contem a
-- chapa e os toroides, para qualquer dist_phase da varredura (na v12 a
-- fronteira era fixa em 60 mm enquanto a chapa ia ate 80 mm -- geometria
-- parcialmente fora do dominio de solucao).
margem_chapa     = 20
folga_fronteira  = 20
folga_pcb        = 1.0   -- mm, vao entre a borda do toroide e a chapa (GND do PCB)
espessura_pcb    = 2.0   -- mm, "espessura" modelada do plano de cobre

-- draw_toroid() (acima) tem o gap virado para +x. Esta versao e' o MESMO
-- desenho rotacionado -90 graus em torno de (cx,cy) -- ou seja, aplica a
-- transformacao (dx,dy) -> (dy,-dx) em cada no local antes de somar o
-- centro -- para que o gap fique virado para -y (para baixo, encostado na
-- chapa de cobre). A v12 so trocava a ORDEM dos argumentos dos mesmos nos
-- de draw_toroid (ainda em +x): isso so inverte o sentido do arco, os nos
-- continuavam nas mesmas coordenadas, e o gap nunca saia do lado +x.
function draw_sensor_gap_down(cx, cy, circuit_label, label_id)
  mi_addnode(cx + hg,  cy - xe_g)
  mi_addnode(cx - hg,  cy - xe_g)
  mi_addnode(cx,       cy + r_ext)
  mi_addarc(cx + hg,  cy - xe_g, cx, cy + r_ext, 180 - ang_e, 1)
  mi_addarc(cx, cy + r_ext, cx - hg,  cy - xe_g, 180 - ang_e, 1)
  mi_addarc(cx - hg,  cy - xe_g, cx + hg,  cy - xe_g, 2*ang_e, 1)

  mi_addnode(cx + hg,  cy - xi_g)
  mi_addnode(cx - hg,  cy - xi_g)
  mi_addnode(cx,       cy + r_int)
  mi_addarc(cx + hg,  cy - xi_g, cx, cy + r_int, 180 - ang_i, 1)
  mi_addarc(cx, cy + r_int, cx - hg,  cy - xi_g, 180 - ang_i, 1)
  mi_addarc(cx - hg,  cy - xi_g, cx + hg,  cy - xi_g, 2*ang_i, 1)

  mi_addsegment(cx + hg,  cy - xi_g, cx + hg,  cy - xe_g)
  mi_addsegment(cx - hg,  cy - xi_g, cx - hg,  cy - xe_g)

  mi_addnode( cx + r_awg, cy)
  mi_addnode( cx - r_awg, cy)
  mi_addarc( cx + r_awg, cy, cx - r_awg, cy, 180, 1)
  mi_addarc( cx - r_awg, cy, cx + r_awg, cy, 180, 1)

  mi_selectnode(cx + r_awg, cy)
  mi_selectnode(cx - r_awg, cy)
  mi_setnodeprop("<None>", 2)
  mi_clearselected()

  mi_selectarcsegment(cx, cy + r_awg)
  mi_selectarcsegment(cx, cy - r_awg)
  mi_setarcsegmentprop(1, "<None>", 0, 2)
  mi_clearselected()

  mi_addblocklabel(cx, cy + r_med)
  mi_selectlabel(cx, cy + r_med)
  mi_setblockprop("IP6", 0, 0.5, "<None>", 0, label_id*10 + 1, 0)
  mi_clearselected()

  mi_addblocklabel(cx, cy)
  mi_selectlabel(cx, cy)
  mi_setblockprop("Copper", 0, 0.4, circuit_label, 0, label_id*10 + 2, 1)
  mi_clearselected()

  mi_addblocklabel(cx + x_ar, cy + y_ar)
  mi_selectlabel(cx + x_ar, cy + y_ar)
  mi_setblockprop("Air", 0, 0.35, "<None>", 0, label_id*10 + 3, 0)
  mi_clearselected()

  mi_addblocklabel(cx, cy - r_med)
  mi_selectlabel(cx, cy - r_med)
  mi_setblockprop("Air", 0, 0.1, "<None>", 0, label_id*10 + 4, 0)
  mi_clearselected()
end

-- =============================================================================
--  build_3phase(dist_phase) -- monta um documento novo com os 3 sensores
--  (gap para baixo) + chapa de cobre (GND) para uma dada distancia entre
--  centros. A chapa e a fronteira ABC sao dimensionadas a PARTIR da
--  distancia, entao nunca ficam menores que a geometria (bug da v12: chapa
--  ia ate x=+-80 mm com fronteira fixa em 60 mm).
--
--  Retorna as posicoes (x_A,y_A, x_B,y_B, x_C,y_C) para uso no resto do
--  script.
-- =============================================================================
function build_3phase(dist_phase, freq)
  freq = freq or 0
  newdocument(0)
  mi_probdef(freq, "millimeters", "planar", 1e-8, h_core, 30)
  main_resize(900, 900)

  mi_getmaterial("Air")
  mi_getmaterial("Copper")
  mi_addmaterial("IP6", mu_ip6, mu_ip6, 0, 0, 2e-7, 0, 0, 1, 0, 0, 0, 0, 0)
  mi_addcircprop("PRIM_A", I_prim, 1)
  mi_addcircprop("PRIM_B", I_prim, 1)
  mi_addcircprop("PRIM_C", I_prim, 1)

  local x_A, y_A = -dist_phase, 0
  local x_B, y_B =  0,          0
  local x_C, y_C =  dist_phase, 0

  draw_sensor_gap_down(x_A, y_A, "PRIM_A", 1)
  draw_sensor_gap_down(x_B, y_B, "PRIM_B", 2)
  draw_sensor_gap_down(x_C, y_C, "PRIM_C", 3)

  local chapa_x1 = x_A - margem_chapa
  local chapa_x2 = x_C + margem_chapa
  -- topo da chapa a folga_pcb mm da borda externa do toroide (r_ext): e' o
  -- ponto do anel mais proximo da chapa, gap incluso, ja que o gap fica
  -- exatamente nesse lado (-y). "bem proxima" = poucos mm, nao os 11.5 mm
  -- que a v13 original tinha (chapa_y2 fixo em -25 com r_ext=13.5).
  local chapa_y2 = -(r_ext + folga_pcb)
  local chapa_y1 = chapa_y2 - espessura_pcb

  mi_addnode(chapa_x1, chapa_y1)
  mi_addnode(chapa_x2, chapa_y1)
  mi_addnode(chapa_x2, chapa_y2)
  mi_addnode(chapa_x1, chapa_y2)

  mi_addsegment(chapa_x1, chapa_y1, chapa_x2, chapa_y1)
  mi_addsegment(chapa_x2, chapa_y1, chapa_x2, chapa_y2)
  mi_addsegment(chapa_x2, chapa_y2, chapa_x1, chapa_y2)
  mi_addsegment(chapa_x1, chapa_y2, chapa_x1, chapa_y1)

  mi_addblocklabel(0, (chapa_y1 + chapa_y2)/2)
  mi_selectlabel(0, (chapa_y1 + chapa_y2)/2)
  mi_setblockprop("Copper", 0, 0, "<None>", 0, 50, 0)
  mi_clearselected()

  -- fronteira: raio ate o canto mais distante da chapa (o caso pior, ja
  -- que a chapa e' mais larga que alta) + folga de ar
  local r_bnd_local = sqrt(chapa_x2*chapa_x2 + chapa_y1*chapa_y1) + folga_fronteira

  -- label do ar externo: tem que cair DENTRO do circulo de fronteira
  -- (r_bnd_local), senao a regiao entre a geometria e a fronteira fica sem
  -- nenhum label -> "regiao nao definida" no FEMM. A v12 usava um offset
  -- fixo "+40" calibrado pro r_bnd=60 antigo; com r_bnd_local variando por
  -- distancia (79 a 126 mm no sweep), esse "+40" ficava do lado de FORA da
  -- fronteira para dist_phase = 28, 30, 35 e 40 mm -- exatamente onde a
  -- ETAPA 2 travava. 0.85*r_bnd_local fica sempre bem dentro, em qualquer
  -- distancia, e longe da chapa (que so ocupa o lado -y).
  local y_ar_ext = 0.85 * r_bnd_local

  mi_addblocklabel(0, y_ar_ext)
  mi_selectlabel(0, y_ar_ext)
  mi_setblockprop("Air", 1, 0, "<None>", 0, 99, 0)
  mi_clearselected()

  -- Open Boundary do proprio FEMM, como na ETAPA 1 -- sem desenhar circulo
  -- manualmente antes.
  mi_makeABC(7, r_bnd_local, 0, 0, 0)

  mi_zoomnatural()
  if freq == 0 then
    mi_saveas(format("ss49e_3phase_d%03.0fmm.fem", dist_phase))
  else
    mi_saveas(format("ss49e_3phase_d%03.0fmm_f%.0fhz.fem", dist_phase, freq))
  end

  return x_A, y_A, x_B, y_B, x_C, y_C
end

function B_hall_at(cx, cy)
  -- gap fica em -y (draw_sensor_gap_down): o elemento Hall mora ali, nao
  -- em +x como no sensor unico da ETAPA 1 (draw_toroid).
  -- abs() em cada componente antes de somar os quadrados: aqui (ETAPA 2,
  -- AC) b1/b2 vem como fasores complexos (defasagem por correntes de
  -- Foucault na chapa) -- mesma convencao do FEMM pra reportar |B| a
  -- partir de fasores. Em DC (ETAPA 1 usa B_hall(), nao esta funcao) seria
  -- equivalente, ja que abs() de um real nao muda nada.
  local a, b1, b2 = mo_getpointvalues(cx, cy - r_med)
  return sqrt(abs(b1)*abs(b1) + abs(b2)*abs(b2))*1e4
end

function resolve_3phase(Ia, Ib, Ic)
  mi_modifycircprop("PRIM_A", 1, Ia)
  mi_modifycircprop("PRIM_B", 1, Ib)
  mi_modifycircprop("PRIM_C", 1, Ic)
  mi_analyze(0)
  mi_loadsolution()
end

cenarios = {
  {100,   0,   0, "Ia=100, Ib=0,   Ic=0"},
  {  0, 100,   0, "Ia=0,   Ib=100, Ic=0"},
  {  0,   0, 100, "Ia=0,   Ib=0,   Ic=100"},
  {100, 100,   0, "Ia=100, Ib=100, Ic=0"},
  {100,   0, 100, "Ia=100, Ib=0,   Ic=100"},
  {  0, 100, 100, "Ia=0,   Ib=100, Ic=100"},
  {100, 100, 100, "Ia=100, Ib=100, Ic=100"},
}

-- cenarios 1, 2 e 3 sao fase unica: a leitura de CADA UM deles no seu
-- proprio sensor e' a referencia "sem crosstalk" (bha_ref, bhb_ref,
-- bhc_ref), medida na propria simulacao em vez de uma constante fixa
-- (a v12 usava 5.406 G/A cravado no codigo, que so valia para
-- dist_phase = 30 mm e nem sequer vinha desta geometria com chapa).
-- Os cenarios 4-7 combinam fases e o desvio de cada sensor contra a sua
-- propria referencia e' o crosstalk introduzido pelas fases vizinhas.

distancias_mm = {25, 28, 30, 35, 40, 50, 60, 80}

h = openfile("crosstalk_matrix.csv", "w")
if h then
  write(h, "dist_mm;cenario;Ia;Ib;Ic;B_Ha_G;B_Hb_G;B_Hc_G;erro_a_pct;erro_b_pct;erro_c_pct\n")
end

resumo = openfile("crosstalk_resumo.csv", "w")
if resumo then
  write(resumo, "dist_mm;pior_erro_pct\n")
end

ind = openfile("inductancia_matrix.csv", "w")
if ind then
  write(ind, "dist_mm;L_aa_nH;L_bb_nH;L_cc_nH;M_ab_nH;M_ba_nH;M_ac_nH;M_ca_nH;M_bc_nH;M_cb_nH;k_ab;k_ac;k_bc\n")
end

dist_fig = nil
for d = 1, getn(distancias_mm) do
  if distancias_mm[d] > 2*r_ext and dist_fig == nil then
    dist_fig = distancias_mm[d]
  end
end

for d = 1, getn(distancias_mm) do
  local dist = distancias_mm[d]

  if dist <= 2*r_ext then
    print(format(" [%.0f mm] PULADO: toroides se sobrepoem (2*r_ext = %.1f mm)", dist, 2*r_ext))
  else
    local x_A, y_A, x_B, y_B, x_C, y_C = build_3phase(dist, freq_ac)

    print("")
    print(format(" -- dist_phase = %.0f mm (borda a borda: %.1f mm) --",
          dist, dist - 2*r_ext))
    print("cenario                   | B_Ha[G] B_Hb[G] B_Hc[G] | desvio")

    local bha_ref, bhb_ref, bhc_ref = 0, 0, 0
    local pior = 0
    -- indutancia propria/mutua: sai de graca dos MESMOS 3 cenarios de fase
    -- unica ja resolvidos pra referencia de crosstalk -- nenhuma analise
    -- extra, so' mais uma leitura (mo_getcircuitproperties) em cima da
    -- solucao que ja esta carregada.
    local L_aa, L_bb, L_cc = 0, 0, 0
    local M_ab, M_ba, M_ac, M_ca, M_bc, M_cb = 0, 0, 0, 0, 0, 0

    for k = 1, 7 do
      local Ia, Ib, Ic = cenarios[k][1], cenarios[k][2], cenarios[k][3]
      local desc = cenarios[k][4]

      resolve_3phase(Ia, Ib, Ic)

      local bha = B_hall_at(x_A, y_A)
      local bhb = B_hall_at(x_B, y_B)
      local bhc = B_hall_at(x_C, y_C)

      if k == 1 then
        bha_ref = bha
        L_aa = circ_L_nH("PRIM_A", Ia)
        M_ab = circ_L_nH("PRIM_B", Ia)
        M_ac = circ_L_nH("PRIM_C", Ia)
      end
      if k == 2 then
        bhb_ref = bhb
        M_ba = circ_L_nH("PRIM_A", Ib)
        L_bb = circ_L_nH("PRIM_B", Ib)
        M_bc = circ_L_nH("PRIM_C", Ib)
      end
      if k == 3 then
        bhc_ref = bhc
        M_ca = circ_L_nH("PRIM_A", Ic)
        M_cb = circ_L_nH("PRIM_B", Ic)
        L_cc = circ_L_nH("PRIM_C", Ic)
      end

      local err_a = (Ia > 0.1 and bha_ref > 0.1) and (bha - bha_ref)/bha_ref * 100 or 0
      local err_b = (Ib > 0.1 and bhb_ref > 0.1) and (bhb - bhb_ref)/bhb_ref * 100 or 0
      local err_c = (Ic > 0.1 and bhc_ref > 0.1) and (bhc - bhc_ref)/bhc_ref * 100 or 0

      if abs(err_a) > pior then pior = abs(err_a) end
      if abs(err_b) > pior then pior = abs(err_b) end
      if abs(err_c) > pior then pior = abs(err_c) end

      print(format("%-25s | %7.1f %7.1f %7.1f | %+5.2f %+5.2f %+5.2f",
            desc, bha, bhb, bhc, err_a, err_b, err_c))

      -- erro em 5 casas: em AC 12 kHz o crosstalk fica na ordem de 0,002 %,
      -- entao 2 casas (usadas nas colunas de B, onde faz sentido)
      -- arredondava tudo pra "0.00" e escondia o proprio sinal que a
      -- varredura existe pra medir.
      if h then
        write(h, format("%.0f;%s;%.0f;%.0f;%.0f;%.2f;%.2f;%.2f;%.5f;%.5f;%.5f\n",
              dist, desc, Ia, Ib, Ic, bha, bhb, bhc, err_a, err_b, err_c))
      end

      mo_close()
    end

    print(format(" pior desvio nesta distancia: %.2f %%", pior))
    if resumo then
      write(resumo, format("%.0f;%.5f\n", dist, pior))
    end

    local k_ab = M_ab / sqrt(L_aa * L_bb)
    local k_ac = M_ac / sqrt(L_aa * L_cc)
    local k_bc = M_bc / sqrt(L_bb * L_cc)
    print(format(" L_aa=%.2f nH  L_bb=%.2f nH  L_cc=%.2f nH", L_aa, L_bb, L_cc))
    print(format(" M_ab=%.2f pH  M_ac=%.2f pH  M_bc=%.2f pH  (k_ab=%.3e, k_ac=%.3e, k_bc=%.3e)",
          M_ab*1000, M_ac*1000, M_bc*1000, k_ab, k_ac, k_bc))
    -- M_xy e M_yx vem de duas solucoes DIFERENTES (uma so com Ix, outra so
    -- com Iy) -- deveriam bater por reciprocidade. E' um checkbox de
    -- qualidade da malha/geometria de graca.
    if ind then
      write(ind, format("%.0f;%.5f;%.5f;%.5f;%.6f;%.6f;%.6f;%.6f;%.6f;%.6f;%.3e;%.3e;%.3e\n",
            dist, L_aa, L_bb, L_cc, M_ab, M_ba, M_ac, M_ca, M_bc, M_cb, k_ab, k_ac, k_bc))
    end
  end
end

if ind then closefile(ind) end

if h then closefile(h) end
if resumo then closefile(resumo) end

print("")
print(" -> crosstalk_matrix.csv  (todas as distancias e cenarios)")
print(" -> crosstalk_resumo.csv  (pior desvio por distancia)")
print(" -> inductancia_matrix.csv  (self/mutua por distancia)")

print("")
print("===================================================================")
print(" FIGURA - tres sensores + chapa (pior caso, menor distancia valida)")
print("===================================================================")

-- reconstroi a geometria do pior caso: apos o loop acima, o documento ativo
-- no FEMM e' o da ULTIMA distancia resolvida (a maior, a menos interessante
-- para a figura), entao precisa recriar aqui antes de plotar.
local x_A, y_A, x_B, y_B, x_C, y_C = build_3phase(dist_fig, freq_ac)
resolve_3phase(100, 100, 100)

-- janela redimensionada pra bater com a proporcao do zoom -- sem isso o
-- FEMM preenche a sobra com fundo branco. So' a vista geral: pedido foi
-- "uma foto tipo trifase_geral ja esta bom" -- os detalhes por sensor
-- (A/B/C) saem.
mo_showdensityplot(1, 0, 0.06, 0, "bmag")
main_resize(1200, 480)
mo_zoom(x_A - margem_chapa, -20, x_C + margem_chapa, 18)
mo_savebitmap(format("trifase_geral_d%03.0fmm.bmp", dist_fig))

mo_close()

print(format(" Figura gerada para dist_phase = %.0f mm.", dist_fig))
print("Pronto.")