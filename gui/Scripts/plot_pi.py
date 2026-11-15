import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import importlib.util, os

# ===================== CONFIG =====================
# Pasta onde salvar (ajuste p/ a pasta do TCC se quiser salvar direto lá):
IMG_DIR = "."                      # ex.: "Textuais/Imgs/Desenvolvimento"

# Ganhos: lê do autotuning (smo_gains.json) se existir; senão, usa fallback.
import json
# SMO_GAINS_JSON escolhe o arquivo (ex.: smo_gains_15x6E.json); SMO_PLOT_SUFFIX
# vai no nome das figuras (ex.: "_15x6E"); SMO_PLOT_ONLY limita quais saem
# (ex.: "Torque_rpm"). A carga da hélice vem de SMO_K_LOAD (test_coeff_auto).
GAINS_JSON = os.environ.get("SMO_GAINS_JSON", "smo_gains.json")
PLOT_SUFFIX = os.environ.get("SMO_PLOT_SUFFIX", "")
PLOT_ONLY = [x for x in os.environ.get("SMO_PLOT_ONLY", "").split(",") if x]
KS1, KS2, KT = 3_316_182.1, -1_313_590.0, 423.7   # fallback
if os.path.exists(GAINS_JSON):
    with open(GAINS_JSON) as _f:
        _g = json.load(_f)["gains"]
    KS1, KS2, KT = _g["Ks1"], _g["Ks2"], _g["Kt"]
    print(f"ganhos de {GAINS_JSON}: Ks1={KS1:,.0f} Ks2={KS2:,.0f} Kt={KT:,.1f}")
# Janela de zoom (platô de 4000 RPM, sem saturação) p/ ângulo e eixos αβ:
ZOOM_T0, ZOOM_T1 = 6.70, 6.73
# =================================================

spec = importlib.util.spec_from_file_location("smo", "test_coeff_auto.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

t = m.t
out = m.run_sim_core(m.N, m.Ts, m.RPM_target_profile, m.PHASE_TEST_MODE,
                     m.GBW_OPAMP, m.GAIN_OPAMP, m.PLANT_BEMF, m.SMO_BEMF,
                     KS1, KS2, KT, m.SMO_SWITCH_FUNC)

os.makedirs(IMG_DIR, exist_ok=True)
def save(fig_fn, name):
    base = os.path.splitext(name)[0]
    if PLOT_ONLY and base not in PLOT_ONLY:
        return
    fig = fig_fn()
    p = os.path.join(IMG_DIR, base + PLOT_SUFFIX + ".png")
    fig.savefig(p, dpi=120, bbox_inches="tight"); plt.close(fig)
    print("salvo:", p)

zmask = (t >= ZOOM_T0) & (t <= ZOOM_T1)
tz_ms = t[zmask] * 1e3

# ------------------------------------------------------------------ #
# 1) PI_controle_Inversor.png
# ------------------------------------------------------------------ #
def fig_pi():
    rpm_real, err_w, integ_w, V_cmd = out[0], out[14], out[15], out[16]
    period = 1.0/8000.0
    duty = np.clip(V_cmd/30.0, 0, 1)
    V_act = np.where(np.mod(t, period) < duty*period, 30.0, 0.0)
    integ_parcel = 100.5*integ_w

    fig, ax = plt.subplots(3, 1, figsize=(9, 7), sharex=True)
    ax[0].plot(t, m.RPM_target_profile, 'k--', lw=1.2, label='RPM Alvo (Target)')
    ax[0].plot(t, rpm_real, 'b', lw=1.2, label='RPM Real')
    ax[0].set_title("Desempenho da Malha de Velocidade")
    ax[0].set_ylabel("Velocidade [RPM]"); ax[0].legend(loc='upper right', fontsize=8); ax[0].grid(True, alpha=0.4)

    ax[1].plot(t, err_w, 'r', lw=1.0, label='Erro de Velocidade (Set - Real)')
    ax[1].axhline(0, color='k', ls=':', lw=0.8)
    ax[1].set_title("Erro Processado pelo Controlador PI")
    ax[1].set_ylabel("Erro [RPM]"); ax[1].legend(loc='upper right', fontsize=8); ax[1].grid(True, alpha=0.4)

    ax[2].fill_between(t, 0, V_act, color='green', lw=0, alpha=0.9, label='Comando de Tensão Final (V_act)')
    ax[2].plot(t, integ_parcel, color='orange', ls='--', lw=1.3, label='Parcela Integral (Ki·int(e))')
    ax[2].axhline(30, color='red', ls=':', lw=1.2, label='Limite de Saturação (30V)')
    ax[2].set_title("Esforço de Controle (Saída do Inversor)")
    ax[2].set_ylabel("Tensão [V]"); ax[2].set_xlabel("Tempo [s]"); ax[2].set_ylim(-1, 32)
    ax[2].legend(loc='upper right', fontsize=8); ax[2].grid(True, alpha=0.4)
    fig.tight_layout(); return fig

# ------------------------------------------------------------------ #
# 2) PLL_angulos.png
# ------------------------------------------------------------------ #
def fig_pll():
    err = (out[4] - out[5] + np.pi) % (2*np.pi) - np.pi
    fig, ax = plt.subplots(2, 1, figsize=(9, 6.5), sharex=True)
    ax[0].plot(tz_ms, out[4][zmask], 'b', lw=1.3, label='Theta Real')
    ax[0].plot(tz_ms, out[5][zmask], 'r--', lw=1.3, label='Theta Estimado (PLL)')
    ax[0].set_title("Sincronismo do PLL — Ângulo Elétrico")
    ax[0].set_ylabel("θ [rad]"); ax[0].legend(loc='upper right', fontsize=8); ax[0].grid(True, alpha=0.4)
    ax[1].plot(tz_ms, np.degrees(err[zmask]), 'm', lw=1.3)
    ax[1].axhline(0, color='k', ls=':', lw=0.8)
    ax[1].set_title("Erro de Estimação de Fase")
    ax[1].set_ylabel("Erro [°]"); ax[1].set_xlabel("t [ms]"); ax[1].grid(True, alpha=0.4)
    fig.tight_layout(); return fig

# ------------------------------------------------------------------ #
# 3) Torque_rpm.png
# ------------------------------------------------------------------ #
def fig_torque_rpm():
    fig, ax = plt.subplots(2, 1, figsize=(9, 6.5), sharex=True)
    ax[0].plot(t, out[0], 'b', lw=1.2, label='RPM Real')
    ax[0].plot(t, out[1], 'r--', lw=1.2, label='RPM Estimado')
    ax[0].set_title("Velocidade: Real vs. Estimada")
    ax[0].set_ylabel("RPM"); ax[0].legend(loc='upper right', fontsize=8); ax[0].grid(True, alpha=0.4)
    # Ripple do seis passos (±1 N.m) ao fundo; por cima, a média pelo MESMO
    # LPF de 1ª ordem de 2 Hz que o firmware aplica no torque publicado
    # (OBS_OUT_LPF_HZ). Sem ela o degrau de torque da hélice some no ripple.
    def lpf(x, fc=2.0):
        w = 2*np.pi*fc*m.Ts; a = w/(1+w); y = np.empty_like(x); acc = 0.0
        for i, v in enumerate(x):
            acc += a*(v - acc); y[i] = acc
        return y
    ax[1].plot(t, out[2], color='0.75', lw=0.6, label='Te Real (ripple)')
    ax[1].plot(t, out[3], color='#9fd8a8', lw=0.6, label='Te Estimado (ripple)')
    ax[1].plot(t, lpf(out[2]), 'b', lw=1.8, label='Te Real (média, LPF 2 Hz)')
    ax[1].plot(t, lpf(out[3]), 'r--', lw=1.8, label='Te Estimado (média, LPF 2 Hz)')
    ax[1].set_title("Torque: Real vs. Estimado")
    ax[1].set_ylabel("Torque [N.m]"); ax[1].set_xlabel("Tempo [s]")
    ax[1].legend(loc='upper left', fontsize=8, ncol=2); ax[1].grid(True, alpha=0.4)
    fig.tight_layout(); return fig

# ------------------------------------------------------------------ #
# 4) Eixo_alpha_beta.png  (zoom — correntes e BEMF αβ)
# ------------------------------------------------------------------ #
def fig_eixos():
    fig, ax = plt.subplots(2, 2, figsize=(11, 7), sharex=True)
    ax[0,0].plot(tz_ms, out[6][zmask], 'b', lw=1.3, label='Real'); ax[0,0].plot(tz_ms, out[7][zmask], 'r--', lw=1.3, label='Estimado')
    ax[0,0].set_title("Corrente α"); ax[0,0].set_ylabel("A"); ax[0,0].legend(fontsize=8); ax[0,0].grid(True, alpha=0.4)
    ax[0,1].plot(tz_ms, out[8][zmask], 'b', lw=1.3, label='Real'); ax[0,1].plot(tz_ms, out[9][zmask], 'r--', lw=1.3, label='Estimado')
    ax[0,1].set_title("Corrente β"); ax[0,1].legend(fontsize=8); ax[0,1].grid(True, alpha=0.4)
    ax[1,0].plot(tz_ms, out[10][zmask], 'b', lw=1.3, label='Real'); ax[1,0].plot(tz_ms, out[11][zmask], 'r--', lw=1.3, label='Reconstruído')
    ax[1,0].set_title("Back-EMF α"); ax[1,0].set_ylabel("V"); ax[1,0].set_xlabel("t [ms]"); ax[1,0].legend(fontsize=8); ax[1,0].grid(True, alpha=0.4)
    ax[1,1].plot(tz_ms, out[12][zmask], 'b', lw=1.3, label='Real'); ax[1,1].plot(tz_ms, out[13][zmask], 'r--', lw=1.3, label='Reconstruído')
    ax[1,1].set_title("Back-EMF β"); ax[1,1].set_xlabel("t [ms]"); ax[1,1].legend(fontsize=8); ax[1,1].grid(True, alpha=0.4)
    fig.tight_layout(); return fig

save(fig_pi,         "PI_controle_Inversor.png")
save(fig_pll,        "PLL_angulos.png")
save(fig_torque_rpm, "Torque_rpm.png")
save(fig_eixos,      "Eixo_alpha_beta.png")
print("\nGanhos usados:", KS1, KS2, KT)