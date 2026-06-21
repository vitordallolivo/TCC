"""
identificacao_helice.py — identificação do coeficiente global de carga da
hélice (kQ) por patamar de RPM, a partir do torque real (média) e do torque
estimado pelo SMO (RMS, robusto às quedas de observabilidade).

Lê os ganhos otimizados de smo_gains.json (fallback embutido), roda a simulação
via test_coeff_auto e gera a figura com o nome usado no TCC.
"""
import numpy as np, math, json, os, importlib.util
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy.signal import butter, lfilter

# ===================== CONFIG =====================
IMG_DIR    = "."                       # ex.: "Textuais/Imgs/Desenvolvimento"
GAINS_JSON = "smo_gains.json"
KS1, KS2, KT = 2_872_277.0, -842_983.0, 2_641.5   # fallback
ATRASO_PI_S = 0.2                      # descarta o transitório do PI (s)
RPM_MIN     = 1000.0                    # ignora patamares abaixo disso
LP_HZ       = 40.0                     # corte do filtro de tendência (figura)
# =================================================

if os.path.exists(GAINS_JSON):
    _g = json.load(open(GAINS_JSON))["gains"]
    KS1, KS2, KT = _g["Ks1"], _g["Ks2"], _g["Kt"]
    print(f"ganhos de {GAINS_JSON}: Ks1={KS1:,.0f} Ks2={KS2:,.0f} Kt={KT:,.1f}")

# carrega o simulador
spec = importlib.util.spec_from_file_location("smo", "test_coeff_auto.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
t, Ts, N = m.t, m.Ts, m.N
RPM_target_profile, k_load = m.RPM_target_profile, m.k_load

out = m.run_sim_core(N, Ts, RPM_target_profile, m.PHASE_TEST_MODE,
                     m.GBW_OPAMP, m.GAIN_OPAMP, m.PLANT_BEMF, m.SMO_BEMF,
                     KS1, KS2, KT, m.SMO_SWITCH_FUNC)

# ----------------------------------------------------------------------
# Detecção dos patamares (alvo constante e acima de RPM_MIN)
# ----------------------------------------------------------------------
w_m_rad   = out[0] * (math.pi / 30.0)     # vel. real  [rad/s]
w_est_rad = out[1] * (math.pi / 30.0)     # vel. estimada
tq_real   = out[2]                        # torque real
tq_est    = out[3]                        # torque estimado (SMO)

target_diff = np.append(np.diff(RPM_target_profile), 0)
mascara = (target_diff == 0) & (RPM_target_profile > RPM_MIN)
edges = np.diff(mascara.astype(int))
starts = np.where(edges == 1)[0] + 1
ends   = np.where(edges == -1)[0] + 1
if mascara[0]:  starts = np.insert(starts, 0, 0)
if mascara[-1]: ends   = np.append(ends, N)

# ----------------------------------------------------------------------
# Identificação de kQ por patamar:  T = kQ * omega^2
#   real -> média do torque ;  SMO -> RMS (robusto às quedas)
# ----------------------------------------------------------------------
atraso = int(ATRASO_PI_S / Ts)
k_real_list, k_smo_list, rpm_patamares = [], [], []

hdr = f"{'Intervalo (s)':<15} | {'Alvo':<7} | {'TqReal_Avg':<10} | {'TqSMO_RMS':<10} | {'k_Real':<9} | {'k_SMO'}"
print("=" * len(hdr)); print(hdr); print("-" * len(hdr))
for s, e in zip(starts, ends):
    if (e - s) <= atraso:
        continue
    idx_s, idx_e = s + atraso, e - 10
    w_v, w_s_v = w_m_rad[idx_s:idx_e], w_est_rad[idx_s:idx_e]
    tr_v, ts_v = tq_real[idx_s:idx_e], tq_est[idx_s:idx_e]

    tq_real_avg = np.mean(tr_v)
    tq_smo_rms  = np.mean(ts_v)
    k_r = tq_real_avg / (np.mean(w_v)**2)
    k_s = tq_smo_rms  / (np.mean(w_s_v)**2)

    k_real_list.append(k_r); k_smo_list.append(k_s)
    rpm_patamares.append(RPM_target_profile[idx_s])
    print(f"[{s*Ts:>5.2f}-{e*Ts:>5.2f}] | {RPM_target_profile[idx_s]:>7.1f} | "
          f"{tq_real_avg:>10.4f} | {tq_smo_rms:>10.4f} | {k_r:>9.2e} | {k_s:>9.2e}")

media_k_smo  = np.mean(k_smo_list)  if k_smo_list  else 0.0
media_k_real = np.mean(k_real_list) if k_real_list else 0.0
erro_pct = abs(media_k_smo - k_load) / k_load * 100 if k_load else 0.0
print("-" * len(hdr))
print(f"VALOR NOMINAL: {k_load:.2e}  |  MÉDIA K_REAL: {media_k_real:.2e}  |  "
      f"MÉDIA K_SMO: {media_k_smo:.2e}  |  ERRO: {erro_pct:.2f}%")
print("=" * len(hdr))

# ----------------------------------------------------------------------
# Figura
# ----------------------------------------------------------------------
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))

ax1.plot(t, tq_real, color='silver', alpha=0.3, label='Te Real (Bruto)')
ax1.plot(t, tq_est,  color='green',  alpha=0.3, label='Te SMO (Bruto)')
fs = 1.0 / Ts
b, a = butter(1, LP_HZ / (0.5 * fs), btype='low')
ax1.plot(t, lfilter(b, a, tq_real), 'b',   lw=2,   label='Te Real (Tendência Média)')
ax1.plot(t, lfilter(b, a, tq_est),  'r--', lw=1.5, label='Te SMO (Tendência Filtrada)')
ax1.set_title("Comparativo de Torque: Dinâmica Real vs Estimada")
ax1.set_ylabel("Torque [N.m]"); ax1.legend(loc='upper right'); ax1.grid(True)

ax2.axhline(y=k_load, color='k', ls='--', label='k Nominal (Teórico)')
if rpm_patamares:
    ax2.scatter(rpm_patamares, k_real_list, c='blue', s=100, label='k Real')
    ax2.scatter(rpm_patamares, k_smo_list, c='red', marker='x', s=100, label='k SMO')
    ax2.set_ylim(k_load*0.8, k_load*1.2)
ax2.set_title(f"Identificação do Coeficiente k (Erro Final: {erro_pct:.2f}%)")
ax2.set_ylabel("Coeficiente k"); ax2.set_xlabel("Velocidade [RPM]")
ax2.legend(); ax2.grid(True)

plt.tight_layout()
os.makedirs(IMG_DIR, exist_ok=True)
saida = os.path.join(IMG_DIR, "Algortimo_Coeficiente_helice.png")
fig.savefig(saida, dpi=120, bbox_inches="tight"); plt.close(fig)
print("salvo:", saida)