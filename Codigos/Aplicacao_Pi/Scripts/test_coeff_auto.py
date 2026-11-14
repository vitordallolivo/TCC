import numpy as np
import matplotlib.pyplot as plt
from numba import njit
from scipy.optimize import minimize, differential_evolution
import math

# ===========================================================================
# CONFIGURAÇÕES
# ===========================================================================
CONFIG_INVERT_CURRENTS = False

PHASE_TEST_MODE = 1          # 0=sem atraso, 1/2/3=atraso do ampop (ver código antigo)
PLANT_BEMF = 1               # 0=retangular, 1=senoidal  (planta real)
SMO_BEMF   = 1               # 0=retangular, 1=senoidal  (observador)

# MODIFICADO: Seleção da função de comutação do SMO (0 = saturação, 1 = tanh, 2 = sigmoide)
SMO_SWITCH_FUNC = 1 
BL = 18.0                     # Camada de contorno / parâmetro de escala

# ---- Otimização ----------------------------------------------------------
OTIMIZAR      = True     # OTIMIZAR=0 no ambiente pula e usa KS*_0 direto
                         # (útil para só regerar as figuras)
METODO        = "differential-evolution"   # "nelder-mead" (local, rápido) ou "differential-evolution" (global)
TORQUE_METRIC = "mean"          # "mean" (recomendado) ou "rms"
OPT_T_END     = 4.8             # horizonte curto p/ a função-custo (cobre 1k/5k/10k/4k RPM)

# Pesos do custo (unidades: ângulo[graus] + velocidade[%] + torque[%])
# Ângulo domina de propósito — é o termo sensível aos ganhos.
W_ANG = 1.0
W_SPD = 0.5
W_TQ  = 0.2
TQ_FLOOR = 0.1   # [N.m] piso do denominador do erro de torque (média em regime ~0)

# Ganhos do autotuning (autotune_cost.py, custo 0,3427) — os MESMOS que o
# firmware traz como DEF_GAMMA* em Observer_Manager.c:
#     Γ1 = 31,622777   Γ2 = -10,0   Γt = 0,066771   (com Ts = 10 µs)
#     Ks = Γ/Ts  ->  3.162.278 / -1.000.000 / 6.677,1
# Servem de ponto de partida do otimizador e de valor direto quando
# OTIMIZAR = False. ⚠️ Γ carrega Ts embutido: se TS_ADC mudar, regerar com
# `autotune_cost.py --Ts <novo>` em vez de reescalar à mão.
KS1_0, KS2_0, KT_0 = 3_162_277.66, -1_000_000.0, 6_677.1
SIGN = np.array([+1.0, -1.0, +1.0])   # Ks1>0, Ks2<0, Kt>0

# Parâmetros do circuito analógico
GBW_OPAMP  = 135_544.0
GAIN_OPAMP = 3.0

# ===========================================================================
# Parâmetros do Motor
# ===========================================================================
# R de fase do ensaio de rotor bloqueado (R_LL/2); L de fase ajustada nos
# registros de osciloscópio (a ressonância deu 6,95 uH e prevê pior a corrente).
R_s, L_s = 8.69e-3, 11.3e-6
P, p = 14, 7
# k_load: hélice APC 15x6E (tabela do TCC). O valor antigo, 2e-5, dava uma
# carga ~75x maior que a medida na bancada.
Kv, J, k_load = 310.0, 0.0001, 1.0e-6
# Carga da hélice T = k_load*w^2 [N.m/(rad/s)^2]; SMO_K_LOAD sobrescreve
# (k_Q = C_Q*rho*D^5/(4*pi^2), ex.: APC 15x6E ~1,0e-6 e 17x10E ~2,6e-6).
import os as _os_kl
k_load = float(_os_kl.environ.get("SMO_K_LOAD", k_load))
# Perdas mecânicas a vazio medidas em bancada (T_perdas = B*w + tau_c), ligadas
# por padrão com os valores dos 19 patamares. SMO_B_LOSS/SMO_TAU_C sobrescrevem.
B_loss = float(_os_kl.environ.get("SMO_B_LOSS", 32.4e-6))
TAU_C  = float(_os_kl.environ.get("SMO_TAU_C", 15.5e-3))
# Barramento: bateria 6S medida na bancada (21,5 V), não 30 V.
V_bus, t_end = 21.5, 10.0
# Kv de placa é por volt de barramento; o pico de fase é V_bus/sqrt(3), logo
# Ke1 = 60/(2*pi*Kv*sqrt(3)) = 0,017785 (bancada: 0,017773).
Ke_mec = 60.0 / (2 * math.pi * Kv * math.sqrt(3.0))
Ke = Ke_mec / p

# ===========================================================================
# CADEIA DE AQUISIÇÃO — duas bases de tempo
# ===========================================================================
# A planta é contínua; o observador NÃO. Ele só enxerga o motor através do
# ADC, uma vez por frame de conversão. Rodar os dois no mesmo passo (como era
# feito antes) esconde tudo o que o firmware sofre de verdade: quantização,
# retenção (ZOH), filtro antialiasing, offset do Hall e o skew entre as fases
# dentro do frame.
#
#   Ts_plant -> passo da PLANTA (fino, "contínuo")
#   TS_ADC   -> período do FRAME do ADC = passo do SMO (== OBS_TS do firmware)
#   N_SUB    -> sub-passos da planta por frame (múltiplo de 3: são 3 conversões)
#
# ⚠️ TS_ADC tem de ser o mesmo OBS_TS do firmware: Γ = K·Ts carrega o passo
# embutido (ver SAME/CLAUDE.md). O frame real do hardware sai de
#     TS_ADC = (SMP + 11) * n_canais / ADCCLK
# ex.: SMP = 28,5 / 3 canais / ADCCLK = 12 MHz -> 9,875 µs.
TS_ADC   = 10e-6
N_SUB    = 12
Ts_plant = TS_ADC / N_SUB
Ts       = TS_ADC          # passo do OBSERVADOR (o que entra nos Γ)

ADC_MODEL = 1        # 1 = observador enxerga o ADC; 0 = sinais ideais (antigo)
ADC_BITS  = 12
ADC_VREF  = 3.3

# Condicionamento — mesmos valores do SAME.yaml / HAL_prm.h
K_VOLT   = 5.6 / 105.6       # V no pino por V de fase (divisor 100k/5k6)
G_CURR   = 0.012755          # V no pino por A  (18,75 mV/A x 10/14,7, divisor 4k7/10k)
V_HALL0  = 2.4866 * 10/14.7  # repouso do Hall no pino [V]

# Polos do antialiasing (RC do front-end) e do próprio elemento Hall
FC_AA_V   = 40.1e3
FC_AA_I   = 40.2e3
FC_SENSOR = 56.0e3

# Offset do Hall que SOBRA depois da calibração de boot do firmware: a
# calibração remove a média medida, mas não a deriva térmica durante o ensaio.
# Em bancada o repouso variou 5,8 mV entre corridas (Apêndice A da monografia).
CAL_RESID_V = 0.0005         # 0,5 mV -> ~30 mA de erro residual por fase

# ---------------------------------------------------------------------------
# Perfil de RPM (vetorizado). np.select pega a 1ª condição verdadeira.
# Lista de segmentos (t0, t1, rpm) usada também pela função-custo.
# ---------------------------------------------------------------------------
# Escada de 1000 a 5000 rpm (passo de 500, patamares de 1 s) para o
# perfilamento da hélice, e uma desaceleração no fim. 5000 rpm cabe no
# barramento de 21,5 V mesmo com a 17x10E.
PROFILE_PTS = [
    (0.2,     0.0),
    (1.2,  1_000.0),
    (2.2,  1_500.0),
    (3.2,  2_000.0),
    (4.2,  2_500.0),
    (5.2,  3_000.0),
    (6.2,  3_500.0),
    (7.2,  4_000.0),
    (8.2,  4_500.0),
    (9.2,  5_000.0),
    (10.0, 3_000.0),
]

def build_profile(t_arr):
    conds = [t_arr < tf for (tf, _) in PROFILE_PTS[:-1]]
    vals  = [rpm      for (_, rpm) in PROFILE_PTS[:-1]]
    return np.select(conds, vals, default=PROFILE_PTS[-1][1])

# SEGMENTS derivado automaticamente (só trechos com rpm > 0)
SEGMENTS = []
_t0 = 0.0
for (_t1, _rpm) in PROFILE_PTS:
    if _rpm > 0.0:
        SEGMENTS.append((_t0, _t1, _rpm))
    _t0 = _t1

t = np.arange(0, t_end, Ts)
N = len(t)
RPM_target_profile = build_profile(t)

# ===========================================================================
@njit
def f_shape(theta, kind):
    # Forma normalizada (±1) da BEMF da fase A.
    #   kind == 1 -> senoidal ; kind == 0 -> trapezoidal/retangular
    if kind == 1:
        return math.sin(theta)
    th = theta % (2 * math.pi)
    if th < math.pi/6:         return (6/math.pi) * th
    elif th < 5*math.pi/6:     return 1.0
    elif th < 7*math.pi/6:     return -(6/math.pi) * (th - math.pi)
    elif th < 11*math.pi/6:    return -1.0
    else:                      return (6/math.pi) * (th - 2*math.pi)

# ===========================================================================
# NÚCLEO DA SIMULAÇÃO
# ===========================================================================
@njit
def adc_convert(v_pin, vref, nlev):
    """SAR de nlev degraus: satura nos trilhos e trunca para o código abaixo.

    É o que o hardware faz — a leitura NUNCA é o valor analógico, é o degrau.
    Sem isto o observador enxerga uma resolução que a placa não tem.
    """
    if v_pin <= 0.0:
        return 0.0
    code = int(v_pin / vref * nlev)
    if code > nlev - 1:
        code = nlev - 1
    return code * vref / nlev


@njit
def run_sim_core(N, Ts, RPM_target, phase_mode, GBW, G_opamp,
                 plant_bemf, smo_bemf, Ks1, Ks2, Kt, smo_switch_func,
                 adc_model=1, n_sub=12, adc_bits=12, adc_vref=3.3,
                 cal_resid=0.0005):
    """Planta contínua + observador amarrado ao frame do ADC.

    `Ts` é o passo do OBSERVADOR (= período do frame do ADC = OBS_TS do
    firmware). A planta roda `n_sub` vezes mais rápido, em Ts/n_sub. Os
    resultados são gravados na grade do ADC, que é a única que o firmware
    enxerga.

    Com adc_model=1 o caminho de medição reproduz o hardware da SAME:
      1. filtro antialiasing RC por FASE (+ polo do próprio Hall na corrente);
      2. condicionamento -> tensão no pino (divisor / ganho + repouso do Hall);
      3. amostragem sequencial DENTRO do frame: em modo dual simultâneo V_x e
         i_x da mesma fase caem juntos, mas A, B e C são convertidas uma após
         a outra — a fase C é amostrada 2 conversões depois da A;
      4. quantização de 12 bits com saturação nos trilhos;
      5. reconstrução em firmware e Clarke de 3 termos (igual ao HAL.c).
    Com adc_model=0 volta ao comportamento antigo (sinais ideais, sem ADC).
    """
    res = np.zeros((24, N))

    Ts_p = Ts / n_sub                 # passo da planta
    nlev = 1 << adc_bits
    # Instantes de amostragem de cada fase dentro do frame (3 conversões)
    s_a = 0
    s_b = n_sub // 3
    s_c = (2 * n_sub) // 3

    i_a = i_b = i_c = omega_m = rpm_set = 0.0
    theta_e = 1.5 * math.pi
    integral_w = 0.0

    i_hat_al = i_hat_be = e_hat_al = e_hat_be = 0.0
    omega_est = th_est = tq_lpf = 0.0
    integral_pll = 0.0
    omega_plot_lpf = 0.0

    filt_ual = filt_ube = filt_ial = filt_ibe = 0.0

    # Estados dos filtros antialiasing, POR FASE (é assim no hardware: cada
    # fase tem seu RC; filtrar em αβ misturaria as fases antes do filtro).
    fv_a = fv_b = fv_c = 0.0
    fi_a = fi_b = fi_c = 0.0
    hs_a = hs_b = hs_c = 0.0          # polo do elemento Hall (só corrente)

    # Amostras retidas (ZOH) — o que o firmware efetivamente lê
    sv_a = sv_b = sv_c = 0.0
    si_a = si_b = si_c = 0.0

    fc_torque = 10_000.0
    fc_plot   = 2.0

    Phi_i    = 1.0 - (R_s * Ts / L_s)
    Gamma_v  = Ts / L_s
    Gamma_s1 = Ks1 * Ts
    Phi_e    = 1.0 - (Kt * Ts)

    alpha_tq   = (2*math.pi*fc_torque*Ts) / (1 + 2*math.pi*fc_torque*Ts)
    alpha_plot = (2*math.pi*fc_plot*Ts)   / (1 + 2*math.pi*fc_plot*Ts)

    # Filtros do front-end discretizados no passo da PLANTA
    a_aa_v = (2*math.pi*FC_AA_V*Ts_p)   / (1 + 2*math.pi*FC_AA_V*Ts_p)
    a_aa_i = (2*math.pi*FC_AA_I*Ts_p)   / (1 + 2*math.pi*FC_AA_I*Ts_p)
    a_hall = (2*math.pi*FC_SENSOR*Ts_p) / (1 + 2*math.pi*FC_SENSOR*Ts_p)

    w_n    = 2 * math.pi * 150.0
    zeta   = 0.707
    Kp_pll = 2 * zeta * w_n
    Ki_pll = w_n * w_n

    omega_polo  = 2 * math.pi * GBW / G_opamp
    alpha_opamp = omega_polo * Ts / (1.0 + omega_polo * Ts)

    ea = eb = ec = 0.0
    va = vb = vc = 0.0
    Te = 0.0
    V_cmd = err_w = 0.0

    for k in range(N):
        # =================================================================
        # PLANTA — n_sub passos de Ts_p por frame do ADC
        # =================================================================
        for s in range(n_sub):
            t_now = k * Ts + s * Ts_p

            # ── Controle PI com anti-windup ───────────────────────────────
            target = RPM_target[k]
            max_step = 20_000.0 * Ts_p
            if target > rpm_set:   rpm_set = min(rpm_set + max_step, target)
            elif target < rpm_set: rpm_set = max(rpm_set - max_step, target)

            err_w     = rpm_set - (omega_m * 30 / math.pi)
            V_cmd_raw = 20.1 * err_w + 100.5 * integral_w
            V_cmd     = max(min(V_cmd_raw, 30.0), 0.0)
            Ka        = 1.0 / 20.1
            integral_w += (err_w + Ka * (V_cmd - V_cmd_raw)) * Ts_p

            V_act = 30.0 if (t_now % (1/8000.0) < (V_cmd/30.0)/8000.0) else 0.0

            # ── Inversor 6 passos ─────────────────────────────────────────
            ea = Ke_mec * omega_m * f_shape(theta_e, plant_bemf)
            eb = Ke_mec * omega_m * f_shape(theta_e - 2*math.pi/3, plant_bemf)
            ec = Ke_mec * omega_m * f_shape(theta_e - 4*math.pi/3, plant_bemf)
            th_p = theta_e % (2*math.pi)
            va = vb = vc = 0.0

            if th_p >= math.pi/6 and th_p < math.pi/2:
                va, vb = V_act, 0.0; di = (1/(2*L_s))*((va-vb)-2*R_s*i_a-(ea-eb))
                i_a += di*Ts_p; i_b = -i_a; i_c = 0.0; vc = R_s*i_a + L_s*di + ec
            elif th_p >= math.pi/2 and th_p < 5*math.pi/6:
                va, vc = V_act, 0.0; di = (1/(2*L_s))*((va-vc)-2*R_s*i_a-(ea-ec))
                i_a += di*Ts_p; i_c = -i_a; i_b = 0.0; vb = R_s*i_a + L_s*di + eb
            elif th_p >= 5*math.pi/6 and th_p < 7*math.pi/6:
                vb, vc = V_act, 0.0; di = (1/(2*L_s))*((vb-vc)-2*R_s*i_b-(eb-ec))
                i_b += di*Ts_p; i_c = -i_b; i_a = 0.0; va = R_s*i_b + L_s*di + ea
            elif th_p >= 7*math.pi/6 and th_p < 3*math.pi/2:
                vb, va = V_act, 0.0; di = (1/(2*L_s))*((vb-va)-2*R_s*i_b-(eb-ea))
                i_b += di*Ts_p; i_a = -i_b; i_c = 0.0; vc = R_s*i_b + L_s*di + ec
            elif th_p >= 3*math.pi/2 and th_p < 11*math.pi/6:
                vc, va = V_act, 0.0; di = (1/(2*L_s))*((vc-va)-2*R_s*i_c-(ec-ea))
                i_c += di*Ts_p; i_a = -i_c; i_b = 0.0; vb = R_s*i_c + L_s*di + eb
            else:
                vc, vb = V_act, 0.0; di = (1/(2*L_s))*((vc-vb)-2*R_s*i_c-(ec-eb))
                i_c += di*Ts_p; i_b = -i_c; i_a = 0.0; va = R_s*i_c + L_s*di + ea

            # ── Dinâmica mecânica ─────────────────────────────────────────
            Te = Ke_mec*(i_a*f_shape(theta_e, plant_bemf)
                         + i_b*f_shape(theta_e-2*math.pi/3, plant_bemf)
                         + i_c*f_shape(theta_e-4*math.pi/3, plant_bemf))
            T_perdas = B_loss*omega_m + (TAU_C if omega_m > 1e-3 else 0.0)
            omega_m += (1/J) * (Te - T_perdas - k_load*omega_m**2) * Ts_p
            theta_e  = (theta_e + (omega_m*p)*Ts_p) % (2*math.pi)

            # ── Front-end analógico, por fase, no passo da planta ─────────
            fv_a += a_aa_v * (va - fv_a)
            fv_b += a_aa_v * (vb - fv_b)
            fv_c += a_aa_v * (vc - fv_c)

            # corrente: polo do elemento Hall e depois o RC do antialiasing
            hs_a += a_hall * (i_a - hs_a)
            hs_b += a_hall * (i_b - hs_b)
            hs_c += a_hall * (i_c - hs_c)
            fi_a += a_aa_i * (hs_a - fi_a)
            fi_b += a_aa_i * (hs_b - fi_b)
            fi_c += a_aa_i * (hs_c - fi_c)

            # ── Amostragem: cada fase no SEU instante dentro do frame ─────
            # (V_x e i_x da mesma fase caem juntos — modo dual simultâneo)
            if s == s_a:
                sv_a = fv_a; si_a = fi_a
            elif s == s_b:
                sv_b = fv_b; si_b = fi_b
            elif s == s_c:
                sv_c = fv_c; si_c = fi_c

        # =================================================================
        # ADC + FIRMWARE — uma vez por frame
        # =================================================================
        # αβ ideais (referência para os gráficos, não é o que o SMO vê)
        vn        = (1.0/3.0) * ((va+vb+vc) - (ea+eb+ec))
        ual_ideal = va - vn
        ube_ideal = (1.0/math.sqrt(3.0)) * ((vb-vn) - (vc-vn))
        ial_ideal = i_a
        if not CONFIG_INVERT_CURRENTS:
            ibe_ideal = (1.0/math.sqrt(3.0)) * (i_a + 2*i_b)
        else:
            ibe_ideal = (1.0/math.sqrt(3.0)) * (i_a + 2*i_c)

        if adc_model == 1:
            # Condicionamento -> pino, quantização, volta para grandeza física
            qva = adc_convert(sv_a * K_VOLT, adc_vref, nlev) / K_VOLT
            qvb = adc_convert(sv_b * K_VOLT, adc_vref, nlev) / K_VOLT
            qvc = adc_convert(sv_c * K_VOLT, adc_vref, nlev) / K_VOLT

            # O Hall repousa em V_HALL0 (+ resíduo que a calibração de boot
            # não pegou); o firmware subtrai V_HALL0 e divide pelo ganho.
            qia = (adc_convert(V_HALL0 + cal_resid + si_a * G_CURR,
                               adc_vref, nlev) - V_HALL0) / G_CURR
            qib = (adc_convert(V_HALL0 + cal_resid + si_b * G_CURR,
                               adc_vref, nlev) - V_HALL0) / G_CURR
            qic = (adc_convert(V_HALL0 + cal_resid + si_c * G_CURR,
                               adc_vref, nlev) - V_HALL0) / G_CURR

            # Clarke de 3 termos — a MESMA do HAL.c (rejeita modo comum,
            # não assume i_a+i_b+i_c = 0)
            ual  = (2.0/3.0) * (qva - 0.5*qvb - 0.5*qvc)
            ube  = (1.0/math.sqrt(3.0)) * (qvb - qvc)
            i_al = (2.0/3.0) * (qia - 0.5*qib - 0.5*qic)
            i_be = (1.0/math.sqrt(3.0)) * (qib - qic)
            i_sum = qia + qib + qic
        else:
            # Caminho antigo: sinais ideais, com o polo do ampop opcional
            filt_ual = filt_ual + alpha_opamp * (ual_ideal - filt_ual)
            filt_ube = filt_ube + alpha_opamp * (ube_ideal - filt_ube)
            filt_ial = filt_ial + alpha_opamp * (ial_ideal - filt_ial)
            filt_ibe = filt_ibe + alpha_opamp * (ibe_ideal - filt_ibe)
            if phase_mode == 0:
                ual = ual_ideal; ube = ube_ideal; i_al = ial_ideal; i_be = ibe_ideal
            elif phase_mode == 1:
                ual = filt_ual;  ube = filt_ube;  i_al = ial_ideal; i_be = ibe_ideal
            else:
                ual = filt_ual;  ube = filt_ube;  i_al = filt_ial;  i_be = filt_ibe
            i_sum = 0.0

        # ── SMO (MODIFICADO: Seletor de função de comutação) ──────────────
        err_al = i_al - i_hat_al
        err_be = i_be - i_hat_be
        BL_val = BL
        
        if smo_switch_func == 0:     # Saturação
            s_al = max(min(err_al / BL_val, 1.0), -1.0)
            s_be = max(min(err_be / BL_val, 1.0), -1.0)
        elif smo_switch_func == 1:   # Tangente Hiperbólica
            s_al = math.tanh(err_al / BL_val)
            s_be = math.tanh(err_be / BL_val)
        else:                        # Sigmoide algorítmica
            val_al = err_al / BL_val
            val_be = err_be / BL_val
            s_al = val_al / (1.0 + abs(val_al))
            s_be = val_be / (1.0 + abs(val_be))

        i_hat_al = Phi_i*i_hat_al + Gamma_v*(ual - e_hat_al) + Gamma_s1*s_al
        i_hat_be = Phi_i*i_hat_be + Gamma_v*(ube - e_hat_be) + Gamma_s1*s_be

        e_hat_al = Phi_e*e_hat_al + (Ks2*Ts)*s_al
        e_hat_be = Phi_e*e_hat_be + (Ks2*Ts)*s_be

        # ── PLL (detector genérico p/ a forma do SMO) ─────────────────────
        amp_E = math.sqrt(e_hat_al**2 + e_hat_be**2)
        if amp_E > 1.0:
            g_al = f_shape(th_est, smo_bemf)
            g_be = (1.0/math.sqrt(3.0)) * (f_shape(th_est - 2*math.pi/3, smo_bemf)
                                           - f_shape(th_est - 4*math.pi/3, smo_bemf))
            erro_fase = (g_al*e_hat_be - g_be*e_hat_al) / amp_E
        else:
            erro_fase = 0.0

        integral_pll  += erro_fase * Ts
        omega_est_elec = Kp_pll*erro_fase + Ki_pll*integral_pll
        omega_est      = omega_est_elec / p
        th_est         = (th_est + omega_est_elec*Ts) % (2*math.pi)

        if phase_mode == 3:
            phi_comp = math.atan(omega_est_elec / omega_polo)
            th_out   = (th_est + phi_comp) % (2*math.pi)
        else:
            th_out = th_est

        omega_plot_lpf += alpha_plot * (omega_est - omega_plot_lpf)

        # ── Reconstrução / torque (forma do SMO) ──────────────────────────
        fa_est = f_shape(th_out, smo_bemf)
        fb_est = (1.0/math.sqrt(3.0)) * (f_shape(th_out-2*math.pi/3, smo_bemf)
                                         - f_shape(th_out-4*math.pi/3, smo_bemf))
        E_amp_clean = Ke * omega_est_elec
        e_recon_al  = E_amp_clean * fa_est
        e_recon_be  = E_amp_clean * fb_est

        # MODIFICADO: Garantia analítica de filtragem exclusiva da estimativa SMO
        t_inst_smo = 1.5 * Ke * p * (fa_est*i_hat_al + fb_est*i_hat_be)
        tq_lpf = alpha_tq*t_inst_smo + (1-alpha_tq)*tq_lpf

        # ── Grava ─────────────────────────────────────────────────────────
        res[0,k]  = omega_m * 30/math.pi
        res[1,k]  = omega_plot_lpf * 30/math.pi
        res[2,k]  = Te          # Te Real puro (Bruto c/ ripple)
        res[3,k]  = tq_lpf      # Estimativa do SMO filtrada
        res[4,k]  = theta_e
        res[5,k]  = th_out
        res[6,k]  = i_al
        res[7,k]  = i_hat_al
        res[8,k]  = i_be
        res[9,k]  = i_hat_be
        res[10,k] = ea
        res[11,k] = e_recon_al
        res[12,k] = (1.0/math.sqrt(3.0))*(eb-ec)
        res[13,k] = e_recon_be
        res[14,k] = err_w
        res[15,k] = integral_w
        res[16,k] = V_cmd
        res[17,k] = ual_ideal
        res[18,k] = filt_ual
        # ---- O que o ADC efetivamente entregou ao observador -------------
        res[19,k] = i_al            # corrente α medida (quantizada)
        res[20,k] = i_be            # corrente β medida
        res[21,k] = ual             # tensão α medida
        res[22,k] = i_sum           # i_a+i_b+i_c medido: deveria ser 0.
                                    # O que sobra é skew entre as fases
                                    # dentro do frame + quantização + o
                                    # resíduo de offset do Hall.
        # Corrente α FILTRADA mas ainda NÃO amostrada: separa o que é atraso
        # do front-end (RC antialiasing + polo do Hall) do que é efeito da
        # amostragem/quantização. Entre res[6] (planta), res[23] (filtrada) e
        # res[19] (amostrada) dá para atribuir cada parcela do desvio.
        res[23,k] = (2.0/3.0) * (fi_a - 0.5*fi_b - 0.5*fi_c)

    return res

# ===========================================================================
# FUNÇÃO-CUSTO POR PATAMAR
# ===========================================================================
def plateau_metrics(out, t_arr, segments, steady_frac=0.5,
                    torque_metric="mean", verbose=False):
    eps = 1e-9
    linhas = []
    custo = 0.0
    n_seg = 0
    for (t0, t1, rpm) in segments:
        tw0 = t0 + steady_frac * (t1 - t0)
        m = (t_arr >= tw0) & (t_arr <= t1)
        if np.count_nonzero(m) < 10 or rpm < 1.0:
            continue

        err_th = (out[4][m] - out[5][m] + np.pi) % (2*np.pi) - np.pi
        ang_rms = float(np.sqrt(np.mean(np.degrees(err_th)**2)))

        rpm_real = float(np.mean(out[0][m]))
        rpm_est  = float(np.mean(out[1][m]))
        spd_pct  = 100.0 * abs(rpm_est - rpm_real) / (abs(rpm_real) + eps)

        if torque_metric == "rms":
            te_real = float(np.sqrt(np.mean(out[2][m]**2)))
            te_est  = float(np.sqrt(np.mean(out[3][m]**2)))
        else:
            te_real = float(np.mean(out[2][m]))
            te_est  = float(np.mean(out[3][m]))
        tq_pct = 100.0 * abs(te_est - te_real) / max(abs(te_real), TQ_FLOOR)

        c = W_ANG*ang_rms + W_SPD*spd_pct + W_TQ*tq_pct
        custo += c
        n_seg += 1
        linhas.append((rpm, ang_rms, spd_pct, tq_pct, c))

    custo = custo / max(n_seg, 1)
    if not np.isfinite(custo):
        custo = 1e9

    if verbose:
        print(f"{'RPM':>8} {'ang[°]':>9} {'vel[%]':>8} {'tq[%]':>8} {'custo':>9}")
        print("-"*46)
        for (rpm, a, s, q, c) in linhas:
            print(f"{rpm:>8.0f} {a:>9.2f} {s:>8.2f} {q:>8.2f} {c:>9.3f}")
        print(f"{'TOTAL':>8} {'':>9} {'':>8} {'':>8} {custo:>9.3f}\n")
    return custo, linhas

# ===========================================================================
# WRAPPER DO OTIMIZADOR
# ===========================================================================
def unpack_gains(x):
    mag = 10.0**np.asarray(x, dtype=float)
    return SIGN[0]*mag[0], SIGN[1]*mag[1], SIGN[2]*mag[2]

def make_cost_fn(N_opt, prof_opt, t_opt):
    def cost(x):
        Ks1, Ks2, Kt = unpack_gains(x)
        out = run_sim_core(N_opt, Ts, prof_opt, PHASE_TEST_MODE,
                           GBW_OPAMP, GAIN_OPAMP, PLANT_BEMF, SMO_BEMF,
                           Ks1, Ks2, Kt, SMO_SWITCH_FUNC)
        if not np.all(np.isfinite(out)):
            return 1e9
        seg = [s for s in SEGMENTS if s[1] <= t_opt[-1] + 1e-9]
        c, _ = plateau_metrics(out, t_opt, seg, torque_metric=TORQUE_METRIC)
        return c
    return cost

def otimizar_ganhos():
    t_opt = np.arange(0, OPT_T_END, Ts)
    prof_opt = build_profile(t_opt)
    N_opt = len(t_opt)
    cost = make_cost_fn(N_opt, prof_opt, t_opt)

    x0 = np.log10(np.abs([KS1_0, KS2_0, KT_0]))
    bounds_log = [(4.5, 7.0), (3.5, 6.5), (2.5, 5.0)]

    print(f"Otimizando ({METODO}, horizonte={OPT_T_END}s, {N_opt} passos)...")
    print(f"Custo inicial: {cost(x0):.4f}  | x0={x0}\n")

    if METODO == "differential-evolution":
        r = differential_evolution(cost, bounds_log, x0=x0, maxiter=30,
                                   popsize=10, tol=1e-3, seed=0, polish=True)
    else:
        r = minimize(cost, x0, method="Nelder-Mead",
                     options=dict(maxiter=400, xatol=1e-3, fatol=1e-4))

    Ks1, Ks2, Kt = unpack_gains(r.x)
    print(f"\nConvergiu: custo={r.fun:.4f}")
    print(f"  Ks1 = {Ks1:,.1f}")
    print(f"  Ks2 = {Ks2:,.1f}")
    print(f"  Kt  = {Kt:,.1f}\n")
    return Ks1, Ks2, Kt

# ===========================================================================
# EXECUÇÃO PRINCIPAL E PLOTS
# ===========================================================================
if __name__ == "__main__":
    bemf_labels = {0: "retangular/trapezoidal", 1: "senoidal"}
    bemf_tag = f"Planta={bemf_labels[PLANT_BEMF]} | SMO={bemf_labels[SMO_BEMF]}"
    print(f"\nModo {PHASE_TEST_MODE} | BEMF: {bemf_tag}\n")

    import os as _os
    _otim = OTIMIZAR and _os.environ.get("OTIMIZAR", "1") != "0"

    if _otim:
        t_opt = np.arange(0, OPT_T_END, Ts)
        out0 = run_sim_core(len(t_opt), Ts, build_profile(t_opt), PHASE_TEST_MODE,
                            GBW_OPAMP, GAIN_OPAMP, PLANT_BEMF, SMO_BEMF,
                            KS1_0, KS2_0, KT_0, SMO_SWITCH_FUNC)
        print(">>> NOMINAL (baseline):")
        plateau_metrics(out0, t_opt,
                        [s for s in SEGMENTS if s[1] <= OPT_T_END+1e-9],
                        torque_metric=TORQUE_METRIC, verbose=True)

        Ks1_use, Ks2_use, Kt_use = otimizar_ganhos()

        out1 = run_sim_core(len(t_opt), Ts, build_profile(t_opt), PHASE_TEST_MODE,
                            GBW_OPAMP, GAIN_OPAMP, PLANT_BEMF, SMO_BEMF,
                            Ks1_use, Ks2_use, Kt_use, SMO_SWITCH_FUNC)
        print(">>> OTIMIZADO:")
        plateau_metrics(out1, t_opt,
                        [s for s in SEGMENTS if s[1] <= OPT_T_END+1e-9],
                        torque_metric=TORQUE_METRIC, verbose=True)
    else:
        Ks1_use, Ks2_use, Kt_use = KS1_0, KS2_0, KT_0

    # Execução completa da simulação
    out = run_sim_core(N, Ts, RPM_target_profile, PHASE_TEST_MODE,
                       GBW_OPAMP, GAIN_OPAMP, PLANT_BEMF, SMO_BEMF,
                       Ks1_use, Ks2_use, Kt_use, SMO_SWITCH_FUNC)

    error_full = (out[4] - out[5] + np.pi) % (2*np.pi) - np.pi

    # Configuração da janela de zoom temporal
    ZOOM_T0, ZOOM_T1 = 2.4, 2.45
    zmask = (t >= ZOOM_T0) & (t <= ZOOM_T1)
    tz_ms = t[zmask] * 1e3
    gtag = f"Ks1={Ks1_use:,.0f} Ks2={Ks2_use:,.0f} Kt={Kt_use:,.0f}"

    # FIGURA 1: SMO COMPLETO
    fig1, ax1 = plt.subplots(2, 2, figsize=(14, 8), sharex=True)
    fig1.suptitle(f"SMO — {bemf_tag} — {gtag}", fontsize=10)
    ax1[0,0].plot(t, out[6], 'b', label='Real'); ax1[0,0].plot(t, out[7], 'r--', alpha=0.6, label='Estimado')
    ax1[0,0].set_title("Corrente Alpha"); ax1[0,0].set_ylabel("A"); ax1[0,0].legend(); ax1[0,0].grid(True)
    ax1[0,1].plot(t, out[8], 'b', label='Real'); ax1[0,1].plot(t, out[9], 'r--', alpha=0.6, label='Estimado')
    ax1[0,1].set_title("Corrente Beta"); ax1[0,1].legend(); ax1[0,1].grid(True)
    ax1[1,0].plot(t, out[10], 'b', label='Real'); ax1[1,0].plot(t, out[11], 'r--', alpha=0.6, label='Reconstruído')
    ax1[1,0].set_title("Back-EMF Alpha"); ax1[1,0].set_ylabel("V"); ax1[1,0].set_xlabel("t [s]"); ax1[1,0].legend(); ax1[1,0].grid(True)
    ax1[1,1].plot(t, out[12], 'b', label='Real'); ax1[1,1].plot(t, out[13], 'r--', alpha=0.6, label='Reconstruído')
    ax1[1,1].set_title("Back-EMF Beta"); ax1[1,1].set_xlabel("t [s]"); ax1[1,1].legend(); ax1[1,1].grid(True)
    for axx in ax1.ravel():
        axx.axvspan(ZOOM_T0, ZOOM_T1, color='orange', alpha=0.15)
    fig1.tight_layout()

    # FIGURA 2: ÂNGULO ZOOM
    fig2, ax2 = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    rms_zoom = np.sqrt(np.mean(np.degrees(error_full[zmask])**2))
    fig2.suptitle(f"Ângulo — ZOOM — {bemf_tag} — RMS_zoom={rms_zoom:.2f}°", fontsize=10)
    ax2[0].plot(tz_ms, out[4][zmask], 'b', lw=1.2, label='Theta Real')
    ax2[0].plot(tz_ms, out[5][zmask], 'r--', lw=1.2, label='Theta Estimado')
    ax2[0].set_ylabel("rad"); ax2[0].legend(); ax2[0].grid(True)
    ax2[1].plot(tz_ms, np.degrees(error_full[zmask]), 'm', lw=1.2)
    ax2[1].axhline(0, color='k', linestyle=':')
    ax2[1].set_title("Erro de Theta"); ax2[1].set_ylabel("Graus"); ax2[1].set_xlabel("t [ms]"); ax2[1].grid(True)
    fig2.tight_layout()

    # FIGURA 3: MECÂNICO COMPLETO
    fig3, ax3 = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    fig3.suptitle(f"Mecânico — {bemf_tag} — {gtag}", fontsize=10)
    ax3[0].plot(t, out[0], 'b', label='RPM Real'); ax3[0].plot(t, out[1], 'r--', label='RPM Estimado')
    ax3[0].set_ylabel("RPM"); ax3[0].legend(); ax3[0].grid(True)
    ax3[1].plot(t, out[2], color='silver', label='Te Real (Ripple)')
    ax3[1].plot(t, out[3], 'g', alpha=0.7, label='Te Est (SMO)')
    ax3[1].set_ylabel("Torque [N.m]"); ax3[1].set_xlabel("t [s]"); ax3[1].legend(); ax3[1].grid(True)
    fig3.tight_layout()

    # MODIFICADO: FIGURA 4 COM SINCRONISMO DE MÁSCARAS (zmask)
    fig4, ax4 = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    fig4.suptitle(f"Mecânico - Zoom ({ZOOM_T0*1000:.0f}ms a {ZOOM_T1*1000:.0f}ms)", fontsize=10)
    ax4[0].plot(tz_ms, out[0][zmask], 'b', label='RPM Real')
    ax4[0].plot(tz_ms, out[1][zmask], 'r--', label='RPM Estimado')
    ax4[0].set_ylabel("RPM"); ax4[0].legend(); ax4[0].grid(True)
    ax4[1].plot(tz_ms, out[2][zmask], color='silver', label='Te Real (Ripple)')
    ax4[1].plot(tz_ms, out[3][zmask], 'g', alpha=0.7, label='Te Est (SMO)')
    ax4[1].set_ylabel("Torque [N.m]"); ax4[1].set_xlabel("t [ms]"); ax4[1].legend(); ax4[1].grid(True)
    fig4.tight_layout()

    # MODIFICADO: FIGURA 5 COM SINCRONISMO DE MÁSCARAS (zmask)
    fig5, ax5 = plt.subplots(2, 2, figsize=(14, 8), sharex=True)
    fig5.suptitle(f"SMO — Zoom", fontsize=10)
    ax5[0,0].plot(tz_ms, out[6][zmask], 'b', label='Real'); ax5[0,0].plot(tz_ms, out[7][zmask], 'r--', alpha=0.6, label='Estimado')
    ax5[0,0].set_title("Corrente Alpha"); ax5[0,0].set_ylabel("A"); ax5[0,0].legend(); ax5[0,0].grid(True)
    ax5[0,1].plot(tz_ms, out[8][zmask], 'b', label='Real'); ax5[0,1].plot(tz_ms, out[9][zmask], 'r--', alpha=0.6, label='Estimado')
    ax5[0,1].set_title("Corrente Beta"); ax5[0,1].legend(); ax5[0,1].grid(True)
    ax5[1,0].plot(tz_ms, out[10][zmask], 'b', label='Real'); ax5[1,0].plot(tz_ms, out[11][zmask], 'r--', alpha=0.6, label='Reconstruído')
    ax5[1,0].set_title("Back-EMF Alpha"); ax5[1,0].set_ylabel("V"); ax5[1,0].set_xlabel("t [ms]"); ax5[1,0].legend(); ax5[1,0].grid(True)
    ax5[1,1].plot(tz_ms, out[12][zmask], 'b', label='Real'); ax5[1,1].plot(tz_ms, out[13][zmask], 'r--', alpha=0.6, label='Reconstruído')
    ax5[1,1].set_title("Back-EMF Beta"); ax5[1,1].set_xlabel("t [ms]"); ax5[1,1].legend(); ax5[1,1].grid(True)
    fig5.tight_layout()

    # =====================================================================
    # FIGURAS DA CADEIA DE AQUISIÇÃO
    #
    # O observador não enxerga a planta: enxerga o ADC. Estas figuras
    # mostram o que se perde entre um e outro — retenção, quantização,
    # skew entre as fases dentro do frame — e quanto disso chega na
    # estimativa final.
    # =====================================================================
    import os
    IMG_DIR = os.environ.get("SMO_IMG_DIR", ".")

    # --- Corrida dedicada, rotação CONSTANTE, para o detalhe da amostragem.
    # Duas passadas da MESMA planta: uma no passo fino (referência
    # "contínua") e outra na grade do ADC. Determinísticas e com o mesmo
    # estado inicial, então são diretamente comparáveis.
    RPM_DET  = 3000.0
    T_DET    = 0.30
    t_fine   = np.arange(0, T_DET, Ts_plant)
    t_adc    = np.arange(0, T_DET, Ts)
    prof_f   = np.full(len(t_fine), RPM_DET)
    prof_a   = np.full(len(t_adc),  RPM_DET)

    det_fine = run_sim_core(len(t_fine), Ts_plant, prof_f, PHASE_TEST_MODE,
                            GBW_OPAMP, GAIN_OPAMP, PLANT_BEMF, SMO_BEMF,
                            Ks1_use, Ks2_use, Kt_use, SMO_SWITCH_FUNC, 0, 1)
    det_adc  = run_sim_core(len(t_adc), Ts, prof_a, PHASE_TEST_MODE,
                            GBW_OPAMP, GAIN_OPAMP, PLANT_BEMF, SMO_BEMF,
                            Ks1_use, Ks2_use, Kt_use, SMO_SWITCH_FUNC, 1, N_SUB)

    # Janela curta o bastante para as amostras individuais aparecerem
    MZ_T0, MZ_T1 = T_DET - 600e-6, T_DET - 200e-6
    mf = (t_fine >= MZ_T0) & (t_fine <= MZ_T1)
    ma = (t_adc  >= MZ_T0) & (t_adc  <= MZ_T1)

    # FIGURA 6: a amostragem em si
    fig6, ax6 = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    fig6.suptitle(f"Cadeia de aquisição — planta contínua x ADC "
                  f"(Ts_planta={Ts_plant*1e6:.2f} µs, frame={Ts*1e6:.2f} µs, "
                  f"{ADC_BITS} bits) — {RPM_DET:.0f} rpm", fontsize=10)
    ax6[0].plot((t_fine[mf]-MZ_T0)*1e6, det_fine[6][mf], color='steelblue',
                lw=1.2, label=f'1. Planta (passo {Ts_plant*1e6:.2f} µs)')
    ax6[0].plot((t_fine[mf]-MZ_T0)*1e6, det_fine[23][mf], color='darkorange',
                lw=1.2, ls='--',
                label=f'2. Após o front-end (RC {FC_AA_I/1e3:.0f} kHz + Hall {FC_SENSOR/1e3:.0f} kHz)')
    ax6[0].step((t_adc[ma]-MZ_T0)*1e6, det_adc[19][ma], where='post',
                color='crimson', lw=1.3, label='3. Retido pelo ADC (ZOH)')
    ax6[0].plot((t_adc[ma]-MZ_T0)*1e6, det_adc[19][ma], 'o', color='crimson',
                ms=3.5, label='Instantes de conversão')
    ax6[0].set_ylabel(r'$i_\alpha$ [A]'); ax6[0].legend(fontsize=8, loc='upper right')
    ax6[0].grid(True, alpha=0.3)
    ax6[0].set_title("1→2 é o atraso do filtro; 2→3 é a amostragem. "
                     "O observador só enxerga os pontos vermelhos", fontsize=9)

    ax6[1].step((t_adc[ma]-MZ_T0)*1e6, det_adc[22][ma], where='post', color='darkorange', lw=1.2)
    ax6[1].axhline(0, color='k', ls=':', lw=0.8)
    ax6[1].set_title(r"$i_a+i_b+i_c$ MEDIDO — deveria ser zero. "
                     "O resíduo é o skew entre as 3 conversões do frame "
                     "(+ quantização e offset residual do Hall)", fontsize=9)
    ax6[1].set_ylabel('[A]'); ax6[1].set_xlabel(f't − {MZ_T0*1e3:.1f} ms  [µs]')
    ax6[1].grid(True, alpha=0.3)
    fig6.tight_layout()
    fig6.savefig(os.path.join(IMG_DIR, "ADC_amostragem.png"), dpi=140)

    lsb_A  = (ADC_VREF / (1 << ADC_BITS)) / G_CURR
    sl_det = t_adc > T_DET*0.6

    # FIGURA 7: com ADC x sem ADC, sobre o PERFIL COMPLETO e em regime.
    # A corrida curta de detalhe acima não serve aqui — em 0,3 s o motor ainda
    # está subindo de rotação, e comparar duas estimativas num transitório não
    # diz nada sobre o custo da aquisição.
    out_ideal = run_sim_core(N, Ts, RPM_target_profile, PHASE_TEST_MODE,
                             GBW_OPAMP, GAIN_OPAMP, PLANT_BEMF, SMO_BEMF,
                             Ks1_use, Ks2_use, Kt_use, SMO_SWITCH_FUNC, 0, N_SUB)

    def _suaviza(x, ms):
        """Média móvel de `ms` milissegundos — o ripple de torque do inversor
        six-step é grande demais para ler ponto a ponto."""
        n = max(1, int(ms*1e-3 / Ts))
        return np.convolve(x, np.ones(n)/n, mode='same')

    fig7, ax7 = plt.subplots(2, 1, figsize=(12, 8), sharex=True)

    # Métricas em regime (metade final de cada patamar), para o título
    d_rpm = d_tq = 0.0
    nseg = 0
    for (s0, s1, _r) in SEGMENTS:
        mk = (t >= s0 + 0.5*(s1-s0)) & (t <= s1)
        if np.count_nonzero(mk) < 32:
            continue
        d_rpm += abs(out[1][mk].mean() - out_ideal[1][mk].mean())
        d_tq  += abs(out[3][mk].mean() - out_ideal[3][mk].mean())
        nseg += 1
    d_rpm /= max(nseg, 1); d_tq /= max(nseg, 1)

    fig7.suptitle(f"Quanto a modelagem do ADC muda a estimativa (mesma planta, "
                  f"mesmos Γ)\n1 LSB = {lsb_A*1e3:.1f} mA / "
                  f"{(ADC_VREF/(1<<ADC_BITS))/K_VOLT*1e3:.1f} mV de fase  |  "
                  f"desvio médio em regime: {d_rpm:.2f} rpm e {d_tq*1e3:.1f} mN·m",
                  fontsize=10)

    ax7[0].plot(t, out[0], color='0.75', lw=0.8, label='rpm real')
    ax7[0].plot(t, out_ideal[1], color='seagreen', lw=1.1, label='rpm est. — sinais ideais')
    ax7[0].plot(t, out[1], 'r--', lw=1.1, label='rpm est. — amostrado pelo ADC')
    ax7[0].set_ylabel('rpm'); ax7[0].legend(fontsize=8, loc='upper right')
    ax7[0].grid(True, alpha=0.3)

    ax7[1].plot(t, _suaviza(out[2], 20), color='0.75', lw=1.0, label='Te real')
    ax7[1].plot(t, _suaviza(out_ideal[3], 20), color='seagreen', lw=1.1,
                label='Te est. — sinais ideais')
    ax7[1].plot(t, _suaviza(out[3], 20), 'r--', lw=1.1,
                label='Te est. — amostrado pelo ADC')
    ax7[1].set_title("torque em média móvel de 20 ms — o ripple do inversor "
                     "six-step é grande demais para ler ponto a ponto", fontsize=9)
    ax7[1].set_ylabel('Torque [N·m]'); ax7[1].set_xlabel('t [s]')
    ax7[1].legend(fontsize=8, loc='upper right'); ax7[1].grid(True, alpha=0.3)
    fig7.tight_layout()
    fig7.savefig(os.path.join(IMG_DIR, "ADC_ideal_vs_amostrado.png"), dpi=140)

    # Resumo numérico do que a aquisição custa
    print("\n=== Cadeia de aquisição (regime, %.0f rpm) ===" % RPM_DET)
    print("  passo da planta   : %.3f µs" % (Ts_plant*1e6))
    print("  frame do ADC/SMO  : %.3f µs  (%.1f kSps por canal)"
          % (Ts*1e6, 1e-3/Ts))
    print("  resolução         : %d bits -> %.1f mA e %.1f mV de fase por LSB"
          % (ADC_BITS, lsb_A*1e3, (ADC_VREF/(1<<ADC_BITS))/K_VOLT*1e3))
    print("  skew A->C no frame: %.2f µs (2 de 3 conversões)" % (Ts/3*2*1e6))
    print("  i_a+i_b+i_c medido: %.3f A rms (deveria ser 0)"
          % np.sqrt(np.mean(det_adc[22][sl_det]**2)))
    print("  desvio ADC x ideal (media em regime): %.2f rpm e %.1f mN.m"
          % (d_rpm, d_tq*1e3))

    # fig1..fig5 ficam SÓ interativas de propósito: o conteúdo delas já está
    # nas figuras da monografia geradas por plot_pi.py (Eixo_alpha_beta,
    # PLL_angulos, Torque_rpm, PI_controle_Inversor). Salvá-las duplicaria
    # figura no repositório sem acrescentar informação.

    if os.environ.get("SMO_NO_SHOW", "") == "":
        plt.show()