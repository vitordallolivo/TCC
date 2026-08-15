"""autotune_cost.py — função-custo modular pro autotuning do SMO.

Importa run_sim_core/params de test_coeff_auto.py, otimiza os ganhos e
salva smo_gains.json, gerando as figuras (convergência, sensibilidade,
FFT) + chama plot_pi.py.

Otimiza sobre Gamma = K*Ts (invariante ao passo), não K direto — assim o
resultado é transferível entre simulação e firmware (cada um recupera
K = Gamma/Ts com seu próprio Ts).

COST_MODE: "time" (torque+RPM) | "fft" (BEMF) | "hybrid". TIME_METRIC:
mae/mse/rmse/mape/huber.
"""
import numpy as np
from scipy.optimize import minimize, differential_evolution
import importlib.util, json, time, os, subprocess, sys
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ===================== CONFIG =====================
COST_MODE   = "time"         # "time" | "fft" | "hybrid"
# "mae"|"mse"|"rmse"|"mape"|"huber" — sobrescrevível por --time-metric.
#
# Default = rmse, NÃO huber, e o motivo é medido: o Huber é quadrático abaixo
# de HUBER_DELTA e linear acima, então ele comprime o termo de resíduo MENOR.
# Nesta bancada os resíduos normalizados de torque caem em r ~ 0,32-0,56 (ramo
# quadrático) e os de rpm em r ~ 0,78-1,06 (já no linear). Resultado medido:
#
#   métrica | termo torque (W=1,0) | termo rpm (W=0,5) | razão rpm/torque
#   mae     |        0,548         |       0,528       |      0,96
#   rmse    |        0,635         |       0,645       |      1,02
#   huber   |        0,199         |       0,323       |      1,62
#
# Ou seja: com Huber o rpm pesa 1,6x o torque APESAR de W_TQ ser o dobro de
# W_RPM — os pesos deixam de valer o que dizem, e justo contra o torque, que é
# a grandeza que a bancada entrega. Com rmse/mae eles ficam equilibrados.
TIME_METRIC = "rmse"
METODO      = "differential-evolution"   # ou "nelder-mead"
MAXITER     = 30             # nº máx. de gerações (DE) — pode vir por --maxiter
POPSIZE     = 10             # tamanho da população (DE) — pode vir por --popsize

# --- Pesos do SEGUIMENTO (modo time/hybrid) — torque é o foco ---
W_TQ      = 1.0      # erro de torque  (principal)
W_RPM     = 0.5      # erro de RPM
TQ_FOLLOW = "pp"    # "pp" (ponto-a-ponto) | "rms" | "mean" (nível por patamar)
TQ_FLOOR  = 0.1      # piso no denominador do erro relativo de torque

# --- Pesos do domínio da frequência (modo fft/hybrid) ---
W_AMP   = 1.0
W_CHAT  = 0.0
W_PHASE = 0.0

# Regularização L2 nos ganhos (penaliza ir acima do ponto inicial; 0=off)
GAIN_REG = 0.0

HUBER_DELTA = 1.0    # em desvios-padrão do sinal real
OOB_CUT_K   = 2.5    # corte fora-de-banda = OOB_CUT_K * f0
OPT_T_END   = 4.8
SAIDA_JSON  = "smo_gains.json"

# ---- Ganhos otimizados na forma CONTÍNUA (Gamma = K*Ts) ----
# Ponto inicial em Gamma (= K0*Ts). Ex.: Ks1≈3e5 @ Ts=10us -> Gamma_s1≈3.0
GAMMA1_0, GAMMA2_0, GAMMAT_0 = 3.0, -3.0, 0.03    # Gamma_s1, Gamma_s2, Gamma_t
SIGN = np.array([+1.0, -1.0, +1.0])
# bounds em log10(|Gamma|)
BOUNDS_LOG = [(-1.0, 1.5), (-1.5, 1.0), (-3.0, 0.0)]

# Figuras
FAZER_PLOTS = True
RODAR_PLOT_PI = True
IMG_DIR     = "."     # ex.: "Textuais/Imgs/Desenvolvimento"
FFT_ZOOM    = None    # (t0,t1) p/ janela da FFT; None = maior patamar
# ==================================================

spec = importlib.util.spec_from_file_location("smo", "test_coeff_auto.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
Ts = m.Ts

# ----------------------------------------------------------------------
# Conversão Gamma (contínuo) <-> K (discreto):  K = Gamma / Ts
# ----------------------------------------------------------------------
def gamma_de_x(x):
    """x = log10|Gamma| -> Gamma com sinal."""
    return SIGN * 10.0**np.asarray(x, float)

def K_de_gamma(gamma):
    """Ks1, Ks2, Kt = Gamma / Ts."""
    return tuple(np.asarray(gamma, float) / Ts)

def unpack(x):
    """log10|Gamma| -> (Ks1, Ks2, Kt) discretos p/ a simulação."""
    return K_de_gamma(gamma_de_x(x))

# ----------------------------------------------------------------------
# Métrica genérica (resíduo normalizado pelo desvio-padrão do real)
# ----------------------------------------------------------------------
def metric(resid, real, kind):
    eps = 1e-9
    r = resid / (np.std(real) + eps)
    if kind == "mae":
        return float(np.mean(np.abs(r)))
    if kind == "mse":
        return float(np.mean(r**2))
    if kind == "rmse":
        return float(np.sqrt(np.mean(r**2)))
    if kind == "mape":
        return float(np.mean(np.abs(resid) / (np.abs(real) + eps)))
    if kind == "huber":
        d = HUBER_DELTA; a = np.abs(r)
        return float(np.mean(np.where(a <= d, 0.5*r**2, d*(a - 0.5*d))))
    raise ValueError(kind)

# ----------------------------------------------------------------------
# Termo de torque por patamar (seguimento)
# ----------------------------------------------------------------------
def torque_err(tq_real, tq_est, kind):
    if TQ_FOLLOW == "pp":                        # ponto-a-ponto (usa a métrica)
        return metric(tq_real - tq_est, tq_real, kind)
    if TQ_FOLLOW == "rms":                        # nível RMS por patamar
        a = np.sqrt(np.mean(tq_real**2)); b = np.sqrt(np.mean(tq_est**2))
    else:                                         # nível médio por patamar
        a = np.mean(tq_real); b = np.mean(tq_est)
    return abs(b - a) / max(abs(a), TQ_FLOOR)

# ----------------------------------------------------------------------
# Análise espectral (rfft + Hann)
# ----------------------------------------------------------------------
def _spec(x):
    x = x - np.mean(x)
    Xc = np.fft.rfft(x * np.hanning(len(x)))
    f = np.fft.rfftfreq(len(x), Ts)
    return f, Xc

def fft_terms(real, est, f0):
    eps = 1e-9
    f, Rr = _spec(real); _, Re = _spec(est)
    Ar, Ae = np.abs(Rr), np.abs(Re)
    band0 = (f > 0.7*f0) & (f < 1.3*f0)
    A_r = np.max(Ar[band0]) if band0.any() else eps
    A_e = np.max(Ae[band0]) if band0.any() else eps
    amp_err = abs(A_e - A_r) / (A_r + eps)
    oob = f > OOB_CUT_K * f0
    Er, Ee = np.sum(Ar**2), np.sum(Ae**2)
    chat = max(0.0, np.sum(Ae[oob]**2)/(Ee+eps) - np.sum(Ar[oob]**2)/(Er+eps))
    k0 = np.argmin(np.abs(f - f0))
    dphi = np.angle(Re[k0]) - np.angle(Rr[k0])
    dphi = (dphi + np.pi) % (2*np.pi) - np.pi
    return amp_err, chat, abs(np.degrees(dphi))/180.0

# ----------------------------------------------------------------------
# Custo por patamar
# ----------------------------------------------------------------------
def plateau_cost(out, t_arr, segments, mode, time_metric, verbose=False):
    rows, custo, n = [], 0.0, 0
    for (t0, t1, _rpm) in segments:
        mk = (t_arr >= t0 + 0.5*(t1-t0)) & (t_arr <= t1)
        if np.count_nonzero(mk) < 32:
            continue
        rpm_real = float(np.mean(out[0][mk]))
        if rpm_real < 1.0:
            continue
        f0 = rpm_real / 60.0 * m.p

        c_fft = c_time = 0.0
        if mode in ("time", "hybrid"):
            e_tq  = torque_err(out[2][mk], out[3][mk], time_metric)
            e_rpm = metric(out[0][mk] - out[1][mk], out[0][mk], time_metric)
            c_time = W_TQ*e_tq + W_RPM*e_rpm
        if mode in ("fft", "hybrid"):
            amp = chat = pha = 0.0
            for (r, e) in [(out[10][mk], out[11][mk]), (out[12][mk], out[13][mk])]:
                a, c, p = fft_terms(r, e, f0); amp += a; chat += c; pha += p
            c_fft = (W_AMP*amp + W_CHAT*chat + W_PHASE*pha) / 2.0

        c = (c_time if mode == "time" else
             c_fft  if mode == "fft"  else c_time + c_fft)
        custo += c; n += 1
        rows.append((rpm_real, f0, c_fft, c_time, c))

    custo = custo / max(n, 1)
    if not np.isfinite(custo):
        custo = 1e9
    if verbose:
        print(f"{'RPM':>7} {'f0[Hz]':>7} {'fft':>9} {'time':>9} {'custo':>9}")
        print("-"*46)
        for (rpm, f0, cf, ct, c) in rows:
            print(f"{rpm:>7.0f} {f0:>7.0f} {cf:>9.3f} {ct:>9.3f} {c:>9.3f}")
        print(f"{'TOTAL':>7} {'':>7} {'':>9} {'':>9} {custo:>9.3f}\n")
    return custo, rows

# ----------------------------------------------------------------------
# Figuras
# ----------------------------------------------------------------------
def _save(fig, name):
    os.makedirs(IMG_DIR, exist_ok=True)
    p = os.path.join(IMG_DIR, name)
    fig.savefig(p, dpi=120, bbox_inches="tight"); plt.close(fig)
    print("salvo:", p)

def _run(K, N, prof):
    return m.run_sim_core(N, Ts, prof, m.PHASE_TEST_MODE, m.GBW_OPAMP,
                          m.GAIN_OPAMP, m.PLANT_BEMF, m.SMO_BEMF, *K, m.SMO_SWITCH_FUNC)

def _specmag(x):
    X = np.abs(np.fft.rfft((x - np.mean(x)) * np.hanning(len(x))))
    f = np.fft.rfftfreq(len(x), Ts)
    mx = np.max(X)
    return f, X / (mx if mx > 0 else 1.0)

def plot_convergencia(hist):
    hist = np.asarray(hist); best = np.minimum.accumulate(hist)
    fig, ax = plt.subplots(figsize=(9, 5))
    ax.plot(hist, color='lightsteelblue', lw=0.8, label='Custo por avaliação')
    ax.plot(best, color='navy', lw=2.0, label='Melhor custo acumulado')
    ax.set_yscale('log')
    ax.set_title("Convergência da Otimização dos Ganhos do Observador")
    ax.set_xlabel("Avaliação da função-custo"); ax.set_ylabel("Custo  J  (log)")
    ax.legend(); ax.grid(True, alpha=0.4, which='both')
    _save(fig, "Otim_convergencia.png")

def plot_sensibilidade(cost_raw, x_star, span=0.6, npts=31):
    # eixos em Gamma (grandeza otimizada, invariante ao passo)
    nomes = [r"$\Gamma_{s1}$", r"$\Gamma_{s2}$", r"$\Gamma_{t}$"]
    x_star = np.asarray(x_star, float)
    fig, ax = plt.subplots(1, 3, figsize=(14, 4.2))
    for i in range(3):
        xs = np.linspace(x_star[i]-span, x_star[i]+span, npts)
        cs = []
        for v in xs:
            xx = x_star.copy(); xx[i] = v; cs.append(cost_raw(xx))
        ax[i].plot(SIGN[i]*10.0**xs, cs, 'b-', lw=1.6)
        ax[i].axvline(SIGN[i]*10.0**x_star[i], color='r', ls='--', lw=1.3, label='ótimo')
        ax[i].set_title(f"Sensibilidade a {nomes[i]}")
        ax[i].set_xlabel(nomes[i]); ax[i].set_yscale('log')
        ax[i].grid(True, alpha=0.4, which='both'); ax[i].legend(fontsize=8)
        if SIGN[i] > 0: ax[i].set_xscale('log')
    ax[0].set_ylabel("Custo  J  (log)")
    fig.suptitle("Sensibilidade da Função-Custo aos Ganhos (em torno do ótimo)", fontsize=11)
    fig.tight_layout(); _save(fig, "Otim_sensibilidade.png")

def plot_fft_comparacao(t_opt, prof, K_nom, K_opt, seg):
    if FFT_ZOOM is not None:
        t0, t1 = FFT_ZOOM
    else:
        t0a, t1a, _ = max(seg, key=lambda s: s[1]-s[0])
        t0, t1 = t0a + 0.5*(t1a-t0a), t1a
    win = (t_opt >= t0) & (t_opt <= t1)
    on, oo = _run(K_nom, len(t_opt), prof), _run(K_opt, len(t_opt), prof)
    rpm = float(np.mean(on[0][win])); f0 = rpm/60.0*m.p
    pares = [("Back-EMF $\\alpha$", on[10], on[11], oo[11]),
             ("Torque",            on[2],  on[3],  oo[3])]
    fig, ax = plt.subplots(1, 2, figsize=(13, 5))
    for k, (titulo, real, est_n, est_o) in enumerate(pares):
        f, Xr = _specmag(real[win]); _, Xn = _specmag(est_n[win]); _, Xo = _specmag(est_o[win])
        ax[k].semilogy(f, Xr, 'k',   lw=1.5, label='Real')
        ax[k].semilogy(f, Xn, 'r--', lw=1.1, alpha=0.9, label='Estimada (inicial)')
        ax[k].semilogy(f, Xo, 'b-',  lw=1.1, alpha=0.9, label='Estimada (otimizada)')
        ax[k].axvline(f0, color='green', ls=':', lw=1.1, label=f'$f_0$={f0:.0f} Hz')
        ax[k].set_xlim(0, 12*f0); ax[k].set_ylim(1e-4, 1.5)
        ax[k].set_title(f"{titulo} — espectro (RPM≈{rpm:.0f})")
        ax[k].set_xlabel("Frequência [Hz]"); ax[k].grid(True, alpha=0.4, which='both')
        ax[k].legend(fontsize=8)
    ax[0].set_ylabel("Magnitude normalizada (log)")
    fig.tight_layout(); _save(fig, "FFT_comparacao.png")

# ----------------------------------------------------------------------
# Otimização (sobre Gamma)
# ----------------------------------------------------------------------
def _reg(x, x0):
    if GAIN_REG <= 0:
        return 0.0
    over = np.maximum(0.0, np.asarray(x) - np.asarray(x0))
    return GAIN_REG * float(np.sum(over**2))

def make_cost(N_opt, prof, t_opt, x0):
    seg = [s for s in m.SEGMENTS if s[1] <= t_opt[-1] + 1e-9]
    def cost(x):
        K = unpack(x)                 # log10|Gamma| -> K discreto = Gamma/Ts
        out = _run(K, N_opt, prof)
        if not np.all(np.isfinite(out)):
            return 1e9
        c, _ = plateau_cost(out, t_opt, seg, COST_MODE, TIME_METRIC)
        return c + _reg(x, x0)
    return cost, seg

def otimizar():
    t_opt = np.arange(0, OPT_T_END, Ts); prof = m.build_profile(t_opt)
    x0 = np.log10(np.abs([GAMMA1_0, GAMMA2_0, GAMMAT_0]))
    cost, seg = make_cost(len(t_opt), prof, t_opt, x0)

    hist = []
    best_so_far = [float("inf")]
    def cost_log(x):
        c = cost(x); hist.append(c)
        if c < best_so_far[0]:
            best_so_far[0] = c
        # Linha parseável p/ a GUI (custo em tempo real / nº de avaliações).
        print(f"PROGRESS eval={len(hist)} cost={c:.6f} best={best_so_far[0]:.6f}", flush=True)
        return c

    gen = [0]
    def de_cb(*a, **k):
        gen[0] += 1
        b = min(hist) if hist else float("nan")
        print(f"GEN iter={gen[0]} best={b:.6f}", flush=True)
        return False

    print(f"[{COST_MODE}] metric={TIME_METRIC} | W_tq={W_TQ} W_rpm={W_RPM} "
          f"tq_follow={TQ_FOLLOW} | W_amp={W_AMP} W_chat={W_CHAT} reg={GAIN_REG}")
    print(f"otimizando Gamma (contínuo); Ts_sim={Ts:.1e}")
    print(f"custo inicial = {cost(x0):.4f}\n")

    if METODO == "differential-evolution":
        r = differential_evolution(cost_log, BOUNDS_LOG, x0=x0, maxiter=MAXITER,
                                   popsize=POPSIZE, tol=1e-3, seed=0, polish=True,
                                   callback=de_cb)
    else:
        r = minimize(cost_log, x0, method="Nelder-Mead",
                     options=dict(maxiter=400, xatol=1e-3, fatol=1e-4))

    gamma = gamma_de_x(r.x)           # Gamma ótimo (contínuo, reportar no TCC)
    K = K_de_gamma(gamma)             # K discreto @ Ts da simulação
    print(f"convergiu: custo={r.fun:.4f}")
    print(f"  Gamma:  Gs1={gamma[0]:.4f}  Gs2={gamma[1]:.4f}  Gt={gamma[2]:.5f}")
    print(f"  K(@Ts={Ts:.1e}): Ks1={K[0]:,.0f} Ks2={K[1]:,.0f} Kt={K[2]:,.1f}\n")
    # Linha parseável p/ a GUI (resultado final aplicável).
    print(f"RESULT Ks1={K[0]:.6f} Ks2={K[1]:.6f} Kt={K[2]:.6f} "
          f"Gamma_s1={gamma[0]:.6f} Gamma_s2={gamma[1]:.6f} Gamma_t={gamma[2]:.6f} "
          f"Ts={Ts:.3e} cost={float(r.fun):.6f}", flush=True)

    out = _run(K, len(t_opt), prof)
    _, rows = plateau_cost(out, t_opt, seg, COST_MODE, TIME_METRIC, verbose=True)

    data = {
        "gains_continuo": {"Gamma_s1": gamma[0], "Gamma_s2": gamma[1], "Gamma_t": gamma[2]},
        "gains": {"Ks1": K[0], "Ks2": K[1], "Kt": K[2]},     # discreto @ Ts_sim
        "Ts_sim": Ts,
        "cost": float(r.fun),
        "config": {"mode": COST_MODE, "time_metric": TIME_METRIC, "metodo": METODO,
                   "w_tq": W_TQ, "w_rpm": W_RPM, "tq_follow": TQ_FOLLOW,
                   "w_amp": W_AMP, "w_chat": W_CHAT, "w_phase": W_PHASE,
                   "gain_reg": GAIN_REG, "otimiza_continuo": True},
        "per_plateau": [{"rpm": r_[0], "f0": r_[1], "fft": r_[2], "time": r_[3], "custo": r_[4]}
                        for r_ in rows],
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
    }
    with open(SAIDA_JSON, "w") as f:
        json.dump(data, f, indent=2)
    print(f"salvo: {os.path.abspath(SAIDA_JSON)}")
    print(f"  -> firmware: Ks = Gamma / Ts_firmware  (ex.: Ts=2e-6 -> "
          f"Ks1={gamma[0]/2e-6:,.0f})")

    if FAZER_PLOTS:
        print("\ngerando figuras...")
        plot_convergencia(hist)
        plot_sensibilidade(cost, r.x)
        plot_fft_comparacao(t_opt, prof, unpack(x0), K, seg)
    if RODAR_PLOT_PI and os.path.exists("plot_pi.py"):
        if getattr(sys, "frozen", False):
            # Build empacotado: sys.executable é o próprio autotune_cost
            # (não um Python genérico) — reabriria a si mesmo em vez de
            # rodar plot_pi.py. Figura opcional, só pula com aviso.
            print("\n[plot_pi] pulado: build empacotado não tem Python "
                  "genérico pra rodar plot_pi.py como subprocesso.")
        else:
            print("\nrodando plot_pi.py...")
            subprocess.run([sys.executable, "plot_pi.py"], check=True)
    return gamma, K

if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="Autotuning dos ganhos do SMO (R/L da bancada)")
    ap.add_argument("--R", type=float, default=None, help="Resistência de fase [ohm]")
    ap.add_argument("--L", type=float, default=None, help="Indutância de fase [H]")
    ap.add_argument("--Ts", type=float, default=None, help="Passo da simulação [s]")
    ap.add_argument("--maxiter", type=int, default=None, help="Gerações (DE)")
    ap.add_argument("--popsize", type=int, default=None, help="População (DE)")
    ap.add_argument("--plot", action="store_true", help="Gera as figuras ao final")
    ap.add_argument("--json", default=None, help="Caminho do smo_gains.json de saída")
    ap.add_argument("--outdir", default=None, help="Diretório das figuras")
    ap.add_argument("--time-metric", default=None,
                    choices=["mae", "mse", "rmse", "mape", "huber"],
                    help="Métrica do erro no tempo (default: %s)" % TIME_METRIC)
    ap.add_argument("--w-tq", type=float, default=None,
                    help="Peso do termo de TORQUE no custo (default: %s)" % W_TQ)
    ap.add_argument("--w-rpm", type=float, default=None,
                    help="Peso do termo de RPM no custo (default: %s)" % W_RPM)
    args = ap.parse_args()

    # Injeta R/L no módulo do simulador ANTES de qualquer chamada a run_sim_core
    # (é @njit — numba congela os globais no 1º compile; aqui ainda não compilou).
    if args.R is not None: m.R_s = args.R
    if args.L is not None: m.L_s = args.L
    if args.Ts is not None:
        # Ts é o passo do OBSERVADOR (= período do frame do ADC). run_sim_core
        # deriva o passo da planta do próprio argumento (Ts/n_sub), mas
        # TS_ADC/Ts_plant do módulo alimentam rótulos e a corrida de detalhe —
        # manter os três coerentes, senão a figura diz um Ts e a simulação usa
        # outro.
        m.Ts = args.Ts
        m.TS_ADC = args.Ts
        m.Ts_plant = args.Ts / m.N_SUB
        Ts = args.Ts
    if args.maxiter is not None: MAXITER = args.maxiter
    if args.popsize is not None: POPSIZE = args.popsize

    # Métrica e pesos do custo. Mudam O QUE está sendo otimizado, não só a
    # velocidade — por isso são expostos na GUI e vão gravados no
    # smo_gains.json (bloco `config`), senão não dá para saber depois contra
    # que critério aqueles Γ foram escolhidos.
    #
    # ⚠️ `huber` re-pondera os termos sem avisar. Ele é quadrático abaixo de
    # HUBER_DELTA e linear acima; medido nesta bancada, os resíduos de torque
    # ficam em r ~ 0,32-0,56 (ramo quadrático, comprimidos a 0,5r²) e os de
    # rpm em r ~ 0,78-1,06 (já no ramo linear). Resultado: o termo de rpm pesa
    # ~1,6x o de torque MESMO com W_TQ = 2*W_RPM. Com mae/rmse os dois ficam
    # equilibrados (razão ~1,0) e os pesos declarados valem o que dizem.
    if args.time_metric is not None: TIME_METRIC = args.time_metric
    if args.w_tq  is not None: W_TQ  = args.w_tq
    if args.w_rpm is not None: W_RPM = args.w_rpm
    FAZER_PLOTS = bool(args.plot)
    RODAR_PLOT_PI = bool(args.plot)
    if args.json: SAIDA_JSON = args.json
    if args.outdir: IMG_DIR = args.outdir

    print(f"PARAMS R_s={m.R_s:.6g} L_s={m.L_s:.6g} Ts={Ts:.3e} "
          f"maxiter={MAXITER} popsize={POPSIZE} plot={FAZER_PLOTS}", flush=True)
    otimizar()