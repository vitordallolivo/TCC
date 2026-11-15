import numpy as np, json, importlib.util, os
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ---------------- config ----------------
IMG_DIR = "."
spec = importlib.util.spec_from_file_location("ac", "autotune_cost.py")
ac = importlib.util.module_from_spec(spec); spec.loader.exec_module(ac)
m, Ts = ac.m, ac.Ts

t_opt = np.arange(0, ac.OPT_T_END, Ts); prof = m.build_profile(t_opt)
x0 = np.log10(np.abs([ac.KS1_0, ac.KS2_0, ac.KT_0]))
cost, seg = ac.make_cost(len(t_opt), prof, t_opt, x0)

def save(fig, name):
    p = os.path.join(IMG_DIR, name); fig.savefig(p, dpi=120, bbox_inches="tight"); plt.close(fig)
    print("salvo:", p)

# ======================================================================
# 1) CONVERGÊNCIA — custo por avaliação (Nelder-Mead, modo fft)
# ======================================================================
from scipy.optimize import minimize
hist = []
def cost_log(x):
    c = cost(x); hist.append(c); return c
res = minimize(cost_log, x0, method="Nelder-Mead",
               options=dict(maxiter=400, xatol=1e-3, fatol=1e-4))
hist = np.array(hist)
best = np.minimum.accumulate(hist)           # melhor custo até cada avaliação
x_star = res.x
K_star = ac.unpack(x_star)
print("otimo:", [round(v,1) for v in K_star], "custo", round(res.fun,4), "avals", len(hist))

fig, ax = plt.subplots(figsize=(9, 5))
ax.plot(hist, color='lightsteelblue', lw=0.9, label='Custo por avaliação')
ax.plot(best, color='navy', lw=2.0, label='Melhor custo acumulado')
ax.set_yscale('log')
ax.set_title("Convergência da Otimização (Nelder-Mead, custo FFT)")
ax.set_xlabel("Avaliação da função-custo"); ax.set_ylabel("Custo  J  (escala log)")
ax.legend(); ax.grid(True, alpha=0.4, which='both')
save(fig, "Otim_convergencia.png")

# ======================================================================
# 2) SENSIBILIDADE 1-D — varre cada ganho, fixando os outros no ótimo
# ======================================================================
nomes = [r"$K_{s1}$", r"$K_{s2}$", r"$K_{t}$"]
spans = 0.6   # +/- décadas em torno do ótimo (log10)
Npts = 41

fig, ax = plt.subplots(1, 3, figsize=(14, 4.2))
for i in range(3):
    xs = np.linspace(x_star[i]-spans, x_star[i]+spans, Npts)
    cs = []
    for v in xs:
        xx = x_star.copy(); xx[i] = v
        cs.append(cost(xx))
    cs = np.array(cs)
    valores = ac.SIGN[i] * 10.0**xs        # ganho real (com sinal)
    ax[i].plot(valores, cs, 'b-', lw=1.6)
    ax[i].axvline(K_star[i], color='r', ls='--', lw=1.3, label='ótimo')
    ax[i].set_title(f"Sensibilidade a {nomes[i]}")
    ax[i].set_xlabel(f"{nomes[i]}"); ax[i].set_yscale('log')
    ax[i].grid(True, alpha=0.4, which='both'); ax[i].legend(fontsize=8)
    if ac.SIGN[i] > 0: ax[i].set_xscale('log')
ax[0].set_ylabel("Custo  J  (log)")
fig.suptitle("Sensibilidade da Função-Custo aos Ganhos (em torno do ótimo)", fontsize=11)
fig.tight_layout()
save(fig, "Otim_sensibilidade.png")


import numpy as np, json, importlib.util, os
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

IMG_DIR = "."
spec = importlib.util.spec_from_file_location("ac", "autotune_cost.py")
ac = importlib.util.module_from_spec(spec); spec.loader.exec_module(ac)
m, Ts = ac.m, ac.Ts

# perfil completo e janela em regime (ajuste se quiser outro patamar)
t = np.arange(0, ac.OPT_T_END, Ts); prof = m.build_profile(t)
ZOOM_T0, ZOOM_T1 = 4.2, 4.8
win = (t >= ZOOM_T0) & (t <= ZOOM_T1)

def run(K):
    return m.run_sim_core(len(t), Ts, prof, m.PHASE_TEST_MODE, m.GBW_OPAMP,
        m.GAIN_OPAMP, m.PLANT_BEMF, m.SMO_BEMF, *K, m.SMO_SWITCH_FUNC)

def spec_mag(x):
    x = x - np.mean(x)
    w = np.hanning(len(x))
    X = np.abs(np.fft.rfft(x * w))
    f = np.fft.rfftfreq(len(x), Ts)
    return f, X / np.max(X)            # normalizado p/ comparar formas

K_nom = (ac.KS1_0, ac.KS2_0, ac.KT_0)
K_opt = tuple(json.load(open("smo_gains.json"))["gains"].values())

out_n, out_o = run(K_nom), run(K_opt)
rpm = np.mean(out_n[0][win]); f0 = rpm/60*m.p

f, Xr = spec_mag(out_n[10][win])       # BEMF alfa real (mesma planta nos dois)
_, Xn = spec_mag(out_n[11][win])       # estimada nominal
_, Xo = spec_mag(out_o[11][win])       # estimada otimizada

fig, ax = plt.subplots(figsize=(9, 5))
ax.semilogy(f, Xr, 'k',  lw=1.6, label='Real')
ax.semilogy(f, Xn, 'r--', lw=1.2, alpha=0.9, label='Estimada (nominal)')
ax.semilogy(f, Xo, 'b-',  lw=1.2, alpha=0.9, label='Estimada (otimizada)')
ax.axvline(f0, color='green', ls=':', lw=1.2, label=f'Fundamental ($f_0$={f0:.0f} Hz)')
ax.axvline(2.5*f0, color='gray', ls=':', lw=1.0, label='Corte fora-de-banda (2,5$f_0$)')
ax.set_xlim(0, 12*f0)                   # mostra a fundamental + harmônicas/chattering
ax.set_ylim(1e-4, 1.5)
ax.set_title("Espectro da Back-EMF $\\alpha$ — Real vs. Estimada (nominal e otimizada)")
ax.set_xlabel("Frequência [Hz]"); ax.set_ylabel("Magnitude normalizada (log)")
ax.legend(fontsize=8); ax.grid(True, alpha=0.4, which='both')
fig.tight_layout()
fig.savefig(os.path.join(IMG_DIR, "FFT_bemf_comparacao.png"), dpi=120, bbox_inches="tight")
print("salvo: FFT_bemf_comparacao.png  | f0 =", round(f0), "Hz")