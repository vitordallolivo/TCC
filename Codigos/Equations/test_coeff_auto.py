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
OTIMIZAR      = True
METODO        = "differential-evolution"   # "nelder-mead" (local, rápido) ou "differential-evolution" (global)
TORQUE_METRIC = "mean"          # "mean" (recomendado) ou "rms"
OPT_T_END     = 4.8             # horizonte curto p/ a função-custo (cobre 1k/5k/10k/4k RPM)

# Pesos do custo (unidades: ângulo[graus] + velocidade[%] + torque[%])
# Ângulo domina de propósito — é o termo sensível aos ganhos.
W_ANG = 1.0
W_SPD = 0.5
W_TQ  = 0.2
TQ_FLOOR = 0.1   # [N.m] piso do denominador do erro de torque (média em regime ~0)

# Ganhos nominais (ponto de partida do otimizador) e sinais FIXOS
KS1_0, KS2_0, KT_0 = 500_000.0, -100_000.0, 5_000.0
SIGN = np.array([+1.0, -1.0, +1.0])   # Ks1>0, Ks2<0, Kt>0

# Parâmetros do circuito analógico
GBW_OPAMP  = 135_544.0
GAIN_OPAMP = 3.0

# ===========================================================================
# Parâmetros do Motor
# ===========================================================================
R_s, L_s = 17.3e-3, 7e-6
P, p = 14, 7
Kv, J, k_load = 310.0/math.sqrt(3), 0.0001, 20.0e-6
V_bus, Ts, t_end = 30.0, 10e-6, 10.0
Ke_mec = 60.0 / (2 * math.pi * Kv)
Ke = Ke_mec / p

# ---------------------------------------------------------------------------
# Perfil de RPM (vetorizado). np.select pega a 1ª condição verdadeira.
# Lista de segmentos (t0, t1, rpm) usada também pela função-custo.
# ---------------------------------------------------------------------------
PROFILE_PTS = [
    (0.2,     0.0),
    (1.2,  1_000.0),
    (2.4,  3_000.0),
    (3.6,  1_000.0),
    (4.8,  2_000.0),
    (6.1,  3_000.0),
    (8.0,  2_000.0),
    (10.0, 500.0),
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
def run_sim_core(N, Ts, RPM_target, phase_mode, GBW, G_opamp,
                 plant_bemf, smo_bemf, Ks1, Ks2, Kt, smo_switch_func):

    res = np.zeros((19, N))

    i_a = i_b = i_c = omega_m = rpm_set = 0.0
    theta_e = 1.5 * math.pi
    integral_w = 0.0

    i_hat_al = i_hat_be = e_hat_al = e_hat_be = 0.0
    omega_est = th_est = tq_lpf = 0.0
    integral_pll = 0.0
    omega_plot_lpf = 0.0

    filt_ual = filt_ube = filt_ial = filt_ibe = 0.0

    fc_torque = 10_000.0
    fc_plot   = 2.0

    Phi_i    = 1.0 - (R_s * Ts / L_s)
    Gamma_v  = Ts / L_s
    Gamma_s1 = Ks1 * Ts
    Phi_e    = 1.0 - (Kt * Ts)

    alpha_tq   = (2*math.pi*fc_torque*Ts) / (1 + 2*math.pi*fc_torque*Ts)
    alpha_plot = (2*math.pi*fc_plot*Ts)   / (1 + 2*math.pi*fc_plot*Ts)

    w_n    = 2 * math.pi * 150.0
    zeta   = 0.707
    Kp_pll = 2 * zeta * w_n
    Ki_pll = w_n * w_n

    omega_polo  = 2 * math.pi * GBW / G_opamp
    alpha_opamp = omega_polo * Ts / (1.0 + omega_polo * Ts)

    for k in range(N):
        # ── Controle PI com anti-windup ───────────────────────────────────
        target = RPM_target[k]
        max_step = 20_000.0 * Ts
        if target > rpm_set:   rpm_set = min(rpm_set + max_step, target)
        elif target < rpm_set: rpm_set = max(rpm_set - max_step, target)

        err_w     = rpm_set - (omega_m * 30 / math.pi)
        V_cmd_raw = 20.1 * err_w + 100.5 * integral_w
        V_cmd     = max(min(V_cmd_raw, 30.0), 0.0)
        Ka        = 1.0 / 20.1
        integral_w += (err_w + Ka * (V_cmd - V_cmd_raw)) * Ts

        V_act = 30.0 if ((k*Ts) % (1/8000.0) < (V_cmd/30.0)/8000.0) else 0.0

        # ── Inversor 6 passos ─────────────────────────────────────────────
        ea = Ke_mec * omega_m * f_shape(theta_e, plant_bemf)
        eb = Ke_mec * omega_m * f_shape(theta_e - 2*math.pi/3, plant_bemf)
        ec = Ke_mec * omega_m * f_shape(theta_e - 4*math.pi/3, plant_bemf)
        th_p = theta_e % (2*math.pi)
        va = vb = vc = 0.0

        if th_p >= math.pi/6 and th_p < math.pi/2:
            va, vb = V_act, 0.0; di = (1/(2*L_s))*((va-vb)-2*R_s*i_a-(ea-eb))
            i_a += di*Ts; i_b = -i_a; i_c = 0.0; vc = R_s*i_a + L_s*di + ec
        elif th_p >= math.pi/2 and th_p < 5*math.pi/6:
            va, vc = V_act, 0.0; di = (1/(2*L_s))*((va-vc)-2*R_s*i_a-(ea-ec))
            i_a += di*Ts; i_c = -i_a; i_b = 0.0; vb = R_s*i_a + L_s*di + eb
        elif th_p >= 5*math.pi/6 and th_p < 7*math.pi/6:
            vb, vc = V_act, 0.0; di = (1/(2*L_s))*((vb-vc)-2*R_s*i_b-(eb-ec))
            i_b += di*Ts; i_c = -i_b; i_a = 0.0; va = R_s*i_b + L_s*di + ea
        elif th_p >= 7*math.pi/6 and th_p < 3*math.pi/2:
            vb, va = V_act, 0.0; di = (1/(2*L_s))*((vb-va)-2*R_s*i_b-(eb-ea))
            i_b += di*Ts; i_a = -i_b; i_c = 0.0; vc = R_s*i_b + L_s*di + ec
        elif th_p >= 3*math.pi/2 and th_p < 11*math.pi/6:
            vc, va = V_act, 0.0; di = (1/(2*L_s))*((vc-va)-2*R_s*i_c-(ec-ea))
            i_c += di*Ts; i_a = -i_c; i_b = 0.0; vb = R_s*i_c + L_s*di + eb
        else:
            vc, vb = V_act, 0.0; di = (1/(2*L_s))*((vc-vb)-2*R_s*i_c-(ec-eb))
            i_c += di*Ts; i_b = -i_c; i_a = 0.0; va = R_s*i_c + L_s*di + ea

        # ── Dinâmica mecânica ─────────────────────────────────────────────
        Te = Ke_mec*(i_a*f_shape(theta_e, plant_bemf)
                     + i_b*f_shape(theta_e-2*math.pi/3, plant_bemf)
                     + i_c*f_shape(theta_e-4*math.pi/3, plant_bemf))
        omega_m += (1/J) * (Te - k_load*omega_m**2) * Ts
        theta_e  = (theta_e + (omega_m*p)*Ts) % (2*math.pi)

        # ── αβ ideais ─────────────────────────────────────────────────────
        vn        = (1.0/3.0) * ((va+vb+vc) - (ea+eb+ec))
        ual_ideal = va - vn
        ube_ideal = (1.0/math.sqrt(3.0)) * ((vb-vn) - (vc-vn))

        if not CONFIG_INVERT_CURRENTS:
            ial_ideal = i_a
            ibe_ideal = (1.0/math.sqrt(3.0)) * (i_a + 2*i_b)
        else:
            ial_ideal = i_a
            ibe_ideal = (1.0/math.sqrt(3.0)) * (i_a + 2*i_c)

        # ── Polo do ampop (IIR 1ª ordem) ──────────────────────────────────
        filt_ual = filt_ual + alpha_opamp * (ual_ideal - filt_ual)
        filt_ube = filt_ube + alpha_opamp * (ube_ideal - filt_ube)
        filt_ial = filt_ial + alpha_opamp * (ial_ideal - filt_ial)
        filt_ibe = filt_ibe + alpha_opamp * (ibe_ideal - filt_ibe)

        if phase_mode == 0:
            ual = ual_ideal;  ube = ube_ideal;  i_al = ial_ideal; i_be = ibe_ideal
        elif phase_mode == 1:
            ual = filt_ual;   ube = filt_ube;   i_al = ial_ideal; i_be = ibe_ideal
        else:
            ual = filt_ual;   ube = filt_ube;   i_al = filt_ial;  i_be = filt_ibe

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

    if OTIMIZAR:
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

    plt.show()