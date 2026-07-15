-- =============================================================================
--  FEMM 4.2 / Lua 4.0  --  NT-27/16/12-100-IP6 + SS49E + AWG12
--
--  v7: fronteira circular MANUAL (sem mi_makeABC), faixa 0-100 A,
--      figuras para o TCC, CSV da curva I x B e varredura de posicao do cabo.
--
--  POR QUE SEM mi_makeABC:
--    Ele cria camadas de shell entre r_bnd e ~2*r_bnd e espera gerenciar os
--    labels delas. Misturado com um label de ar externo proprio, as camadas
--    ficam sem material atribuido -> um erro "material nao definido" por
--    camada. Uma condicao de contorno mista num circulo unico resolve o mesmo
--    problema, e' previsivel, e nao briga com os labels.
--
--    Justificativa fisica: o fluxo do nucleo e' fixado por Ampere no caminho
--    magnetico, Phi = I/(R_nucleo + R_gap). Isso e' LOCAL. A fronteira so
--    governa o campo longe. Prove rodando com r_bnd = 60 e depois 120: se o
--    G/A nao mudar, a fronteira nao influencia e a ausencia de condutor de
--    retorno esta justificada.
--
--  Lua 4.0: sem tabela "math"/"string". cos/sin/sqrt/atan/deg/rad/abs/PI e
--           format() sao GLOBAIS. Arquivo: openfile/write/closefile.
--  FEMM: arcos limitados a 180 graus.
--
--  REGIOES: 5 labels, 5 regioes. Aviso de "more than one block label" ou
--  "material nao definido" = alguma regiao nao fechou ou ficou orfa.
--
--  SAIDAS no diretorio do .fem:
--    curva_I_B.csv    pos_cabo.csv
--    geral_XXXA.bmp   gap_XXXA.bmp   geral_ac12k.bmp
-- =============================================================================

showconsole()
clearconsole()

-- ----- parametros ------------------------------------------------------------
D_ext   = 27.0        -- mm  diametro externo (datasheet NT-27/16/12)
D_int   = 16.0        -- mm  diametro interno
h_core  = 12.0        -- mm  altura -> profundidade planar
gap     = 2.3         -- mm  rasgo (~g)
I_prim  = 100         -- A   fundo de escala do projeto
freq_ac = 12000       -- Hz
d_awg12 = 2.053       -- mm  diametro do COBRE AWG12
mu_ip6  = 2500        -- CHUTE inicial. Calibrar pelo A_l (o script avisa).
r_bnd   = 60          -- mm  raio da fronteira. Rodar 60 e 120 para validar.
cinza   = 0           -- 0 = colorido, 1 = tons de cinza (impressao P&B)
img_w   = 900         -- px  janela do FEMM -> resolucao dos BMP
img_h   = 900

r_ext = D_ext/2                 -- 13.50
r_int = D_int/2                 --  8.00
r_med = (r_ext + r_int)/2       -- 10.75  <- elemento Hall
r_awg = d_awg12/2               --  1.0265
hg    = gap/2                   --  1.15

-- Interseccao das faces paralelas do gap (y = +-hg) com cada circulo
xe_g  = sqrt(r_ext*r_ext - hg*hg)      -- 13.4510
xi_g  = sqrt(r_int*r_int - hg*hg)      --  7.9169
ang_e = deg(atan(hg/xe_g))             --  4.888 graus
ang_i = deg(atan(hg/xi_g))             --  8.266 graus

-- Label do ar da janela: DIAGONAL a 45 graus, raio 6.5 mm.
-- O cabo so anda sobre os eixos (+-x, +-y) ate 6 mm. A menor distancia deste
-- ponto ao centro do cabo em qualquer posicao da varredura e' 4.64 mm, contra
-- r_awg = 1.03 mm. Um label sobre os eixos seria engolido pelo cabo em dy=+4
-- (bug da v5): o cobre ficava com dois labels e o ar da janela com nenhum.
x_ar = 6.5*cos(rad(45))                --  4.60
y_ar = 6.5*sin(rad(45))                --  4.60

-- ----- documento -------------------------------------------------------------
newdocument(0)                                     -- 0 = magnetics
mi_probdef(0, "millimeters", "planar", 1e-8, h_core, 30)
main_resize(img_w, img_h)

-- ----- materiais -------------------------------------------------------------
mi_getmaterial("Air")
mi_getmaterial("Copper")

-- IP6 (Thornton): MnZn de potencia. O FEMM nao traz ferrite na biblioteca.
-- sigma ~ 0.2 S/m = 2e-7 MS/m -> corrente parasita desprezivel. E' justamente
-- por isso que se usa ferrite e nao aco em alta frequencia.
mi_addmaterial("IP6", mu_ip6, mu_ip6, 0, 0, 2e-7, 0, 0, 1, 0, 0, 0, 0, 0)

-- Curva B-H APROXIMADA de MnZn de potencia generico (joelho ~0.39 T).
-- NAO e' do datasheet do IP6. Com A_l = 100 nH e 100 A o nucleo deve ficar
-- em ~0.16 T, longe do joelho -- a curva existe para CONFIRMAR isso, nao
-- para ser usada perto da saturacao.
mi_addbhpoint("IP6", 0.000,      0)
mi_addbhpoint("IP6", 0.050,      8)
mi_addbhpoint("IP6", 0.150,     20)
mi_addbhpoint("IP6", 0.280,     45)
mi_addbhpoint("IP6", 0.370,     90)
mi_addbhpoint("IP6", 0.430,    180)
mi_addbhpoint("IP6", 0.470,    400)
mi_addbhpoint("IP6", 0.500,   1200)
mi_addbhpoint("IP6", 0.520,   4000)
mi_addbhpoint("IP6", 0.540,  20000)
mi_addbhpoint("IP6", 0.560, 100000)

-- ----- circuito --------------------------------------------------------------
mi_addcircprop("PRIM", I_prim, 1)                  -- 1 = serie

-- =============================================================================
--  1) CIRCULO EXTERNO do toroide (r = 13.5) -- 3 arcos, todos < 180 graus
-- =============================================================================
mi_addnode(xe_g,  hg)                              -- canto NE do gap
mi_addnode(xe_g, -hg)                              -- canto SE do gap
mi_addnode(-r_ext, 0)                              -- oeste (parte o arco longo)

mi_addarc(xe_g,  hg, -r_ext,  0, 180 - ang_e, 1)   -- 175.11 graus
mi_addarc(-r_ext, 0,  xe_g, -hg, 180 - ang_e, 1)   -- 175.11 graus
mi_addarc(xe_g, -hg,  xe_g,  hg, 2*ang_e,     1)   --   9.78 graus, cruza o gap

-- =============================================================================
--  2) CIRCULO INTERNO do toroide (r = 8.0)
-- =============================================================================
mi_addnode(xi_g,  hg)
mi_addnode(xi_g, -hg)
mi_addnode(-r_int, 0)

mi_addarc(xi_g,  hg, -r_int,  0, 180 - ang_i, 1)   -- 171.73 graus
mi_addarc(-r_int, 0,  xi_g, -hg, 180 - ang_i, 1)   -- 171.73 graus
mi_addarc(xi_g, -hg,  xi_g,  hg, 2*ang_i,     1)   --  16.53 graus, cruza o gap

-- =============================================================================
--  3) RECORTE DO GAP: faces PARALELAS (corte de serra), 2.3 mm constantes
--     Fecha a regiao do gap: 2 faces retas + os 2 arcos curtos acima.
--     Os arcos curtos nao tem condicao de contorno -> fluxo passa livre.
--     Servem so para dar malha propria ao gap, que e' onde mora o fringing.
-- =============================================================================
mi_addsegment(xi_g,  hg, xe_g,  hg)                -- face norte
mi_addsegment(xi_g, -hg, xe_g, -hg)                -- face sul

-- =============================================================================
--  4) CONDUTOR AWG12 -- tudo no grupo 2 para poder ser transladado depois
--     A isolacao plastica nao entra: mu_r = 1, magneticamente identica ao ar.
-- =============================================================================
mi_addnode( r_awg, 0)
mi_addnode(-r_awg, 0)
mi_addarc( r_awg, 0, -r_awg, 0, 180, 1)
mi_addarc(-r_awg, 0,  r_awg, 0, 180, 1)

mi_selectnode( r_awg, 0)
mi_selectnode(-r_awg, 0)
mi_setnodeprop("<None>", 2)
mi_clearselected()

mi_selectarcsegment(0,  r_awg)
mi_selectarcsegment(0, -r_awg)
mi_setarcsegmentprop(1, "<None>", 0, 2)
mi_clearselected()

-- =============================================================================
--  5) FRONTEIRA circular MANUAL + condicao de contorno mista
--     c0 = 1/(mu0*R) com R em METROS. BdryFormat 2 = Mixed.
-- =============================================================================
mi_addnode( r_bnd, 0)
mi_addnode(-r_bnd, 0)
mi_addarc( r_bnd, 0, -r_bnd, 0, 180, 5)            -- 5 graus/segmento
mi_addarc(-r_bnd, 0,  r_bnd, 0, 180, 5)

c0_abc = 1/(4e-7*PI*r_bnd*0.001)
mi_addboundprop("ABC", 0, 0, 0, 0, 0, 0, c0_abc, 0, 2)
mi_selectarcsegment(0,  r_bnd)
mi_selectarcsegment(0, -r_bnd)
mi_setarcsegmentprop(5, "ABC", 0, 0)
mi_clearselected()

-- =============================================================================
--  6) BLOCOS -- 5 regioes, 5 labels
-- =============================================================================
mi_addblocklabel(-r_med, 0)                        -- 1: nucleo IP6
mi_selectlabel(-r_med, 0)
mi_setblockprop("IP6", 0, 0.5, "<None>", 0, 1, 0)
mi_clearselected()

mi_addblocklabel(0, 0)                             -- 2: cobre (grupo 2, movel)
mi_selectlabel(0, 0)
mi_setblockprop("Copper", 0, 0.4, "PRIM", 0, 2, 1)
mi_clearselected()

mi_addblocklabel(x_ar, y_ar)                       -- 3: ar da janela (diagonal)
mi_selectlabel(x_ar, y_ar)
mi_setblockprop("Air", 0, 0.35, "<None>", 0, 3, 0)
mi_clearselected()

mi_addblocklabel(r_med, 0)                         -- 4: ar do GAP, malha fina
mi_selectlabel(r_med, 0)
mi_setblockprop("Air", 0, 0.1, "<None>", 0, 4, 0)
mi_clearselected()

mi_addblocklabel(0, (r_ext + r_bnd)/2)             -- 5: ar externo, automalha
mi_selectlabel(0, (r_ext + r_bnd)/2)
mi_setblockprop("Air", 1, 0, "<None>", 0, 5, 0)
mi_clearselected()

mi_zoomnatural()
mi_saveas("ss49e_nt27_gap.fem")

-- =============================================================================
--  AUXILIARES
-- =============================================================================
-- |B| no centro do gap = posicao do elemento Hall
function B_hall()
  local a, b1, b2 = mo_getpointvalues(r_med, 0)
  return sqrt(b1*b1 + b2*b2)
end

-- |B| medio ao longo do gap (radial) -> mostra o espalhamento por fringing
function B_medio()
  local soma = 0
  local n = 0
  local r
  for r = r_int + 0.25, r_ext - 0.25, 0.5 do
    local a, b1, b2 = mo_getpointvalues(r, 0)
    soma = soma + sqrt(b1*b1 + b2*b2)
    n = n + 1
  end
  return soma/n
end

-- |B| no ferro, oposto ao gap -> checa saturacao
function B_ferro()
  local a, b1, b2 = mo_getpointvalues(-r_med, 0)
  return sqrt(b1*b1 + b2*b2)
end

pos_x = 0
pos_y = 0
function move_cabo(x, y)
  mi_selectgroup(2)
  mi_movetranslate(x - pos_x, y - pos_y)
  mi_clearselected()
  pos_x = x
  pos_y = y
end

function resolve(I)
  mi_modifycircprop("PRIM", 1, I)
  mi_analyze(0)
  mi_loadsolution()
end

-- =============================================================================
--  PASSO 1 -- varredura de corrente, 0 a 100 A
-- =============================================================================
correntes = {1, 2, 5, 10, 20, 30, 40, 50, 60, 75, 90, 100}
n_corr = 12

f = openfile("curva_I_B.csv", "w")
if f then
  write(f, "I_A;B_hall_G;GA_hall;B_med_G;GA_med;B_fe_T;V_ss49e_mV\n")
end

print("=================================================================")
print(" VARREDURA DE CORRENTE  |  NT-27/16/12-100-IP6  |  AWG12")
print(format(" mu_r = %d | gap = %.2f mm | r_bnd = %d mm | FE = %d A",
      mu_ip6, gap, r_bnd, I_prim))
print("=================================================================")
print("")
print("   I[A]   B_hall[G]  G/A_hall   B_med[G]  G/A_med   B_fe[T]")
print("  -----   ---------  --------   --------  -------   -------")

S_nom    = 0
S_ref    = 0
B_fe_max = 0
for k = 1, n_corr do
  I = correntes[k]
  resolve(I)

  bh = B_hall()*1e4
  bm = B_medio()*1e4
  bf = B_ferro()

  if k == 1      then S_ref = bh/I end
  if I == I_prim then S_nom = bh/I end
  if bf > B_fe_max then B_fe_max = bf end

  print(format("  %5.0f   %9.1f  %8.3f   %8.1f  %7.3f   %7.4f",
        I, bh, bh/I, bm, bm/I, bf))

  if f then
    write(f, format("%.0f;%.2f;%.4f;%.2f;%.4f;%.5f;%.2f\n",
          I, bh, bh/I, bm, bm/I, bf, bh*1.4))
  end
  mo_close()
end

if f then closefile(f) end

print("")
print(format("  Desvio de linearidade no FE: %+.2f %%  (G/A de %.3f p/ %.3f)",
      (S_nom/S_ref - 1)*100, S_ref, S_nom))
print(format("  |B| max no nucleo: %.4f T   (joelho do MnZn ~ 0.39 T)", B_fe_max))
print("  -> curva_I_B.csv gravado (separador ';', decimal '.')")

-- =============================================================================
--  PASSO 2 -- figuras para o TCC
--  Escala de cor FIXA entre todas -> a sequencia fica comparavel. Com auto
--  escala cada figura fica bonita e a sequencia nao conta historia nenhuma.
-- =============================================================================
img_correntes = {25, 50, 75, 100}
n_img = 4
B_escala = B_fe_max*1.15                           -- teto comum

print("")
print("=================================================================")
print(" FIGURAS")
print("=================================================================")
print(format(" Escala de cor FIXA: 0 a %.3f T em TODAS as imagens.", B_escala))
print(" Informe essa escala na legenda das figuras.")
print("")

for k = 1, n_img do
  I = img_correntes[k]
  resolve(I)
  mo_hidepoints()

  -- (a) vista geral do toroide
  mo_showdensityplot(1, cinza, B_escala, 0, "bmag")
  mo_showvectorplot(0, 1)                          -- 0 = sem vetores
  mo_zoom(-16, -16, 16, 16)
  mo_savebitmap(format("geral_%03dA.bmp", I))

  -- (b) detalhe do gap com vetores de B -> evidencia o fringing
  mo_showvectorplot(1, 2)                          -- 1 = B, escala 2
  mo_zoom(4.7, -6, 16.7, 6)
  mo_savebitmap(format("gap_%03dA.bmp", I))

  print(format("  %3d A -> geral_%03dA.bmp + gap_%03dA.bmp   (B_hall = %.1f G)",
        I, I, I, B_hall()*1e4))
  mo_close()
end

print("")
print("  BMP -> PNG para o LaTeX:")
print("    mogrify -format png -density 300 *.bmp")

-- =============================================================================
--  PASSO 3 -- posicao do cabo dentro da janela, 4 direcoes
--
--  O paper afirma que o concentrador torna a posicao do fio irrelevante.
--  Isso vale para toroide FECHADO. Com gap de 2.3 mm o campo direto do
--  proprio cabo alcanca o Hall e soma:
--      B_fio = mu0*I/(2*pi*r) = 18.6 G @ 100 A, r = 10.75 mm
--
--  Janela de raio 8 mm, cabo de raio 1.03 mm -> passeio de +-6.9 mm.
--
--  CONTROLE: +y e -y sao simetricos por construcao. A diferenca entre eles
--  e' o piso de ruido numerico da malha. Desvio menor que isso nao e' fisico.
-- =============================================================================
resolve(I_prim)
B_ref = B_hall()*1e4
mo_close()

g = openfile("pos_cabo.csv", "w")
if g then
  write(g, "direcao;desloc_mm;B_hall_G;GA;desvio_pct\n")
  write(g, format("centro;0.0;%.2f;%.4f;0.00\n", B_ref, B_ref/I_prim))
end

print("")
print("=================================================================")
print(format(" POSICAO DO CABO  @  %d A     (centrado: %.1f G)", I_prim, B_ref))
print("=================================================================")
print("")
print("  dir   desloc   B_hall[G]    G/A     desvio")
print("  ---   ------   ---------  -------   -------")

dirs_cod = {"+x", "-x", "+y", "-y"}
dirs_dx  = {1, -1, 0, 0}
dirs_dy  = {0, 0, 1, -1}
passos   = {2, 4, 6}
n_passos = 3

for d = 1, 4 do
  for p = 1, n_passos do
    s  = passos[p]
    dx = dirs_dx[d]*s
    dy = dirs_dy[d]*s

    move_cabo(dx, dy)
    resolve(I_prim)
    bh = B_hall()*1e4
    mo_close()

    dev = (bh/B_ref - 1)*100
    print(format("  %-3s   %4.0f mm  %9.1f  %7.3f   %+6.2f %%",
          dirs_cod[d], s, bh, bh/I_prim, dev))

    if g then
      write(g, format("%s;%.1f;%.2f;%.4f;%.2f\n",
            dirs_cod[d], s, bh, bh/I_prim, dev))
    end
  end
end

move_cabo(0, 0)                                    -- devolve ao centro
if g then closefile(g) end

print("")
print("  +x = cabo empurrado PARA o gap.  -x = afastado do gap.")
print("  +y e -y devem coincidir. A diferenca entre eles e' ruido de malha:")
print("  so acredite em desvios maiores que ela.")
print("  -> pos_cabo.csv gravado")

-- =============================================================================
--  PASSO 4 -- ancoragem do modelo no A_l medido pelo fabricante
--
--  O A_l e' o UNICO numero deste script conferivel contra dado independente.
--  Ele e' medido pela Thornton no nucleo JA COM O GAP. Se o FEMM reproduz o
--  A_l, o mu_r assumido esta certo PARA ESTE NUCLEO -- nao por sorte, mas
--  porque o modelo foi amarrado num valor medido. Sem isso, tudo acima esta
--  ancorado num chute.
-- =============================================================================
resolve(I_prim)
mo_addcontour(-r_int, 0)                           -- linha radial NO NUCLEO,
mo_addcontour(-r_ext, 0)                           -- oposta ao gap
flux_core = mo_lineintegral(0)
mo_clearcontour()
Al_sim = abs(flux_core)/I_prim*1e9
B_dc   = B_hall()

print("")
print("=================================================================")
print(" ANCORAGEM DO MODELO")
print("=================================================================")
print(format("Fluxo no nucleo ....... %.3f uWb", abs(flux_core)*1e6))
print(format("A_l simulado .......... %.1f nH", Al_sim))
print("A_l datasheet ......... 100 nH +-15%  (85 a 115)")
if Al_sim > 115 then
  print("  >>> ALTO: reduza mu_ip6 e rode de novo. Nada abaixo vale ainda.")
elseif Al_sim < 85 then
  print("  >>> BAIXO: aumente mu_ip6 e rode de novo. Nada abaixo vale ainda.")
else
  print("  >>> BATE. Modelo ancorado em dado medido do fabricante.")
end

print("")
print(format("PROJETO @ %d A (fundo de escala):", I_prim))
print(format("  sensibilidade ....... %.3f G/A", S_nom))
print(format("  B no Hall no FE ..... %.1f G", S_nom*I_prim))
print(format("  V_SS49E ............. %.1f mV pico (1.4 mV/G)", S_nom*I_prim*1.4))
if S_nom*I_prim > 1000 then
  print("  >>> ACIMA de 1000 G: o SS49E sai da faixa linear antes do FE.")
  print(format("      Linear so ate %.1f A. Alargue o gap com espacador.",
        1000/S_nom))
else
  print(format("  margem ate 1000 G ... %.1f %%  (linear em todo o FE)",
        (1000/(S_nom*I_prim) - 1)*100))
end
print(format("  ganho do diff amp ... %.2f x  (para +-1.336 V no ADC)",
      1336/(S_nom*I_prim*1.4)))
mo_close()

-- =============================================================================
--  PASSO 5 -- AC 12 kHz (frequencia de chaveamento)
-- =============================================================================
mi_probdef(freq_ac, "millimeters", "planar", 1e-8, h_core, 30)
mi_analyze(0)
mi_loadsolution()

a, B1, B2 = mo_getpointvalues(r_med, 0)
B_ac = sqrt(abs(B1)*abs(B1) + abs(B2)*abs(B2))     -- abs() = modulo do complexo

print("")
print("=================================================================")
print(format(" AC %d Hz  |  %d A pico", freq_ac, I_prim))
print("=================================================================")
print(format("B no Hall ............. %.1f G  (%.3f G/A)",
      B_ac*1e4, B_ac*1e4/I_prim))
print(format("Razao AC/DC ........... %.4f  (1.0 = sem atenuacao)", B_ac/B_dc))
print("  Ferrite tem sigma baixissimo -> esperado ~1.0.")
print("  Se cair muito, o sigma do modelo esta errado.")

mo_hidepoints()
mo_showdensityplot(1, cinza, B_escala, 0, "bmag")
mo_showvectorplot(0, 1)
mo_zoom(-16, -16, 16, 16)
mo_savebitmap("geral_ac12k.bmp")
mo_close()

print("")
print("=================================================================")
print(" LEMBRETES")
print("=================================================================")
print(" 1. Leia o A_l ANTES de qualquer figura definitiva.")
print(" 2. Rode com r_bnd = 60 e 120. G/A igual = fronteira nao influencia.")
print(" 3. Modelo PLANAR: ignora o vazamento pela altura de 12 mm.")
print("    Trate o G/A como LIMITE SUPERIOR.")
print(" 4. Juiz final: 10 espiras x 2 A na bancada. FEMM x medicao e' que")
print("    vale secao de validacao, nao o FEMM sozinho.")
print("")
print("Pronto.")