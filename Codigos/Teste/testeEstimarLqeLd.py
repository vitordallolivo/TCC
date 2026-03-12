import matplotlib
matplotlib.use('Agg')  # <-- adiciona isso ANTES de importar pyplot
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

# =============================================
# PARÂMETROS DO MOTOR (valores do artigo - PMSM1)
# =============================================
Rs_real = 0.06      # Ohm
Ld_real = 140e-6    # H
Lq_real = 210e-6    # H
Vdc     = 24.0      # V
theta   = 1.23      # rad (posição do rotor)

# Parâmetros de aquisição
DELTA_T  = 20e-6    # 20µs — duração do pulso
Tw       = 4e-6     # delay anti-ruído
Td       = 700e-9   # dead-time
td       = Td + Tw  # atraso total de amostragem
Ts       = 0.125e-3    # período entre pulsos

# Simulação do ADC
ADC_RATE = 40_000
t_decay  = np.arange(0, Ts, 1/ADC_RATE)

T1_IDX = int((DELTA_T + Tw) * ADC_RATE)
T2_IDX = int((DELTA_T + Tw + 10e-6) * ADC_RATE)
t21    = 10e-6

# =============================================
# FUNÇÕES
# =============================================
def abc_to_dq(ia, ib, ic, theta):
    id_ = (2/3) * ( ia*np.cos(theta)
                  + ib*np.cos(theta - 2*np.pi/3)
                  + ic*np.cos(theta + 2*np.pi/3))
    iq_ = (2/3) * (-ia*np.sin(theta)
                  - ib*np.sin(theta - 2*np.pi/3)
                  - ic*np.sin(theta + 2*np.pi/3))
    return id_, iq_

def dq_to_abc(id_, iq_, theta):
    ia = np.cos(theta)           * id_ - np.sin(theta)           * iq_
    ib = np.cos(theta-2*np.pi/3) * id_ - np.sin(theta-2*np.pi/3) * iq_
    ic = np.cos(theta+2*np.pi/3) * id_ - np.sin(theta+2*np.pi/3) * iq_
    return ia, ib, ic

def vector_voltages_abc(vector, Vdc):
    if   vector == 1: return  2/3*Vdc, -1/3*Vdc, -1/3*Vdc
    elif vector == 3: return -1/3*Vdc,  2/3*Vdc, -1/3*Vdc
    elif vector == 5: return -1/3*Vdc, -1/3*Vdc,  2/3*Vdc

def simulate_phase_peak(va, vb, vc, Ld, Lq, theta, dt):
    """
    Corrente de pico nas 3 fases considerando saliência — Eq. (8)
    Converte tensão abc→dq, calcula id/iq com Ld/Lq separados, volta para abc
    """
    vd, vq = abc_to_dq(va, vb, vc, theta)
    id_pk  = (vd * dt) / Ld
    iq_pk  = (vq * dt) / Lq
    ia, ib, ic = dq_to_abc(id_pk, iq_pk, theta)
    return ia, ib, ic, id_pk, iq_pk

def simulate_decay(i0, L, R, t):
    """ Decaimento exponencial — Eq. (15): i(t) = i0 * exp(-t*R/L) """
    return i0 * np.exp(-t * R / L)

# =============================================
# SIMULAÇÃO DOS 3 PULSOS
# =============================================
vectors  = [1, 3, 5]
va_list, vb_list, vc_list = [], [], []
ia_peak, ib_peak, ic_peak = [], [], []
id_decay, iq_decay        = [], []
id_t1, id_t2              = [], []
ia_v1 = ib_v3 = ic_v5    = 0.0

for v in vectors:
    va, vb, vc = vector_voltages_abc(v, Vdc)

    ia_pk, ib_pk, ic_pk, id_pk, iq_pk = simulate_phase_peak(
        va, vb, vc, Ld_real, Lq_real, theta, DELTA_T
    )

    ia_peak.append(ia_pk)
    ib_peak.append(ib_pk)
    ic_peak.append(ic_pk)

    # Fase principal de cada vetor para estimativa de theta — Eq. (3)
    if   v == 1: ia_v1 = ia_pk
    elif v == 3: ib_v3 = ib_pk
    elif v == 5: ic_v5 = ic_pk

    # Decaimento em dq
    id_dec = simulate_decay(id_pk, Ld_real, Rs_real, t_decay)
    iq_dec = simulate_decay(iq_pk, Lq_real, Rs_real, t_decay)
    id_decay.append(id_dec)
    iq_decay.append(iq_dec)

    id_t1.append(id_dec[T1_IDX])
    id_t2.append(id_dec[T2_IDX])

    va_list.append(va); vb_list.append(vb); vc_list.append(vc)

# =============================================
# ESTIMATIVA DE THETA — Eq. (4), (5), (6)
# =============================================
i0   = (ia_v1 + ib_v3 + ic_v5) / 3.0
da   = ia_v1 - i0
db   = ib_v3 - i0
dc   = ic_v5 - i0
s3_2 = np.sqrt(3) / 2

print(f"  da={da:.6f}  db={db:.6f}  dc={dc:.6f}")
print(f"  sinais: da={'+'if da>0 else '-'}  db={'+'if db>0 else '-'}  dc={'+'if dc>0 else '-'}")

if   da > 0 and db < 0 and dc < 0:
    theta_est =             s3_2 * (dc - db) / (2*da - db - dc)
elif da > 0 and db < 0 and dc > 0:
    theta_est = np.pi/6   + s3_2 * (da - dc) / (2*db - da - dc)
elif da < 0 and db < 0 and dc > 0:
    theta_est = 2*np.pi/6 + s3_2 * (db - da) / (2*dc - da - db)
elif da < 0 and db > 0 and dc > 0:
    theta_est = 3*np.pi/6 + s3_2 * (dc - db) / (2*da - db - dc)
elif da < 0 and db > 0 and dc < 0:
    theta_est = 4*np.pi/6 + s3_2 * (da - dc) / (2*db - da - dc)
else:
    theta_est = 5*np.pi/6 + s3_2 * (db - da) / (2*dc - da - db)

# =============================================
# ESTIMATIVA Ld e Lq — Eq. (14)
# =============================================
sum_vd = sum_vq = sum_id = sum_iq = 0.0
iq_per_vector = []

for i in range(3):
    va, vb, vc = va_list[i], vb_list[i], vc_list[i]
    vd_i, vq_i = abc_to_dq(va, vb, vc, theta_est)
    id_i, iq_i = abc_to_dq(ia_peak[i], ib_peak[i], ic_peak[i], theta_est)

    sum_vd += abs(vd_i);  sum_id += abs(id_i)
    sum_vq += abs(vq_i);  sum_iq += abs(iq_i)
    iq_per_vector.append(abs(iq_i))

Ld_est = (sum_vd / sum_id) * DELTA_T
Lq_est = (sum_vq / sum_iq) * DELTA_T

# =============================================
# ESTIMATIVA Rs — Eq. (16) e (17)
# =============================================
best      = int(np.argmin(iq_per_vector))
tau_d_est = t21 / np.log(abs(id_t1[best]) / abs(id_t2[best]))   # Eq. (17)
Rs_est    = Ld_est / tau_d_est                                    # Eq. (16)

# =============================================
# CORREÇÃO FINAL — Eq. (19)
# =============================================
tau_q_est = tau_d_est * (Lq_est / Ld_est)
Ld_corr   = Ld_est * np.exp(-td / tau_d_est) - 0.5 * Rs_est * DELTA_T
Lq_corr   = Lq_est * np.exp(-td / tau_q_est) - 0.5 * Rs_est * DELTA_T
Rs_corr   = Ld_corr / tau_d_est

# =============================================
# RESULTADOS
# =============================================
print("=" * 50)
print(f"  Theta real:   {theta:.4f} rad")
print(f"  Theta estim.: {theta_est:.4f} rad  (erro: {abs(theta-theta_est)/theta*100:.2f}%)")
print("-" * 50)
print(f"  Ld real:      {Ld_real*1e6:.2f} µH")
print(f"  Ld estimado:  {Ld_est*1e6:.2f} µH")
print(f"  Ld corrigido: {Ld_corr*1e6:.2f} µH  (erro: {abs(Ld_real-Ld_corr)/Ld_real*100:.2f}%)")
print("-" * 50)
print(f"  Lq real:      {Lq_real*1e6:.2f} µH")
print(f"  Lq estimado:  {Lq_est*1e6:.2f} µH")
print(f"  Lq corrigido: {Lq_corr*1e6:.2f} µH  (erro: {abs(Lq_real-Lq_corr)/Lq_real*100:.2f}%)")
print("-" * 50)
print(f"  Rs real:      {Rs_real*1000:.2f} mΩ")
print(f"  Rs estimado:  {Rs_est*1000:.2f} mΩ")
print(f"  Rs corrigido: {Rs_corr*1000:.2f} mΩ  (erro: {abs(Rs_real-Rs_corr)/Rs_real*100:.2f}%)")
print("=" * 50)

# =============================================
# GRÁFICOS — 4 plots, backend Agg (Wayland safe)
# =============================================
fig, axes = plt.subplots(2, 2, figsize=(12, 8))
fig.suptitle("Identificação de Parâmetros PMSM via SVPWM\n(Dilys et al., 2022)",
             fontsize=13, fontweight='bold')
fig.tight_layout(pad=3.5)

colors = ['#2196F3', '#FF5722', '#4CAF50']
t_us   = t_decay * 1e6

# --- Plot 1: decaimento id dos 3 vetores ---
ax1 = axes[0, 0]
for i, v in enumerate(vectors):
    ax1.plot(t_us, id_decay[i]*1e3, color=colors[i],
             lw=1.8, label=f'v{v}' + (' ★' if i == best else ''))
ax1.axvline((DELTA_T+Tw)*1e6,       color='gray',  linestyle='--', lw=1, label='t1')
ax1.axvline((DELTA_T+Tw+t21)*1e6,   color='black', linestyle='--', lw=1, label='t2')
ax1.set_xlim(0, 500)
ax1.set_title("Decaimento $i_d$ — 3 vetores", fontsize=10)
ax1.set_xlabel("Tempo [µs]"); ax1.set_ylabel("$i_d$ [mA]")
ax1.legend(fontsize=8); ax1.grid(True, alpha=0.3)

# --- Plot 2: posição do rotor ---
ax2 = axes[0, 1]
ang = np.linspace(0, 2*np.pi, 300)
ax2.plot(np.cos(ang), np.sin(ang), 'lightgray', lw=1)
ax2.annotate("", xy=(np.cos(theta), np.sin(theta)), xytext=(0, 0),
             arrowprops=dict(arrowstyle='->', color='#2196F3', lw=2.5))
ax2.annotate("", xy=(np.cos(theta_est), np.sin(theta_est)), xytext=(0, 0),
             arrowprops=dict(arrowstyle='->', color='#FF5722', lw=2.5, linestyle='dashed'))
ax2.set_title("Posição do Rotor", fontsize=10)
ax2.legend([f'Real:  {theta:.4f} rad',
            f'Estim: {theta_est:.4f} rad  (erro: {abs(theta-theta_est)/theta*100:.2f}%)'],
           fontsize=8, loc='upper right')
ax2.set_aspect('equal'); ax2.set_xlim(-1.4, 1.4); ax2.set_ylim(-1.4, 1.4)
ax2.grid(True, alpha=0.3)

# --- Plot 3: real vs estimado ---
ax3   = axes[1, 0]
pnms  = ['$L_d$ [µH]', '$L_q$ [µH]', '$R_s$ [mΩ]']
reais = [Ld_real*1e6, Lq_real*1e6, Rs_real*1e3]
estim = [Ld_corr*1e6, Lq_corr*1e6, Rs_corr*1e3]
x = np.arange(3); w = 0.35
ax3.bar(x-w/2, reais, w, label='Real',     color='#2196F3', alpha=0.85, edgecolor='black', lw=0.8)
ax3.bar(x+w/2, estim, w, label='Estimado', color='#FF5722', alpha=0.85, edgecolor='black', lw=0.8)
ax3.set_title("Parâmetros: Real vs Estimado", fontsize=10)
ax3.set_xticks(x); ax3.set_xticklabels(pnms)
ax3.legend(fontsize=8); ax3.grid(True, alpha=0.3, axis='y')

# --- Plot 4: erro percentual ---
ax4   = axes[1, 1]
erros = [abs(Ld_real-Ld_corr)/Ld_real*100,
         abs(Lq_real-Lq_corr)/Lq_real*100,
         abs(Rs_real-Rs_corr)/Rs_real*100]
bars  = ax4.bar(pnms, erros,
                color=['#4CAF50' if e < 5 else '#FF5722' for e in erros],
                alpha=0.85, edgecolor='black', lw=0.8)
ax4.axhline(5, color='red', linestyle='--', lw=1.2, label='Limite 5%')
for bar, e in zip(bars, erros):
    ax4.text(bar.get_x()+bar.get_width()/2, bar.get_height()+0.02,
             f'{e:.2f}%', ha='center', va='bottom', fontsize=9)
ax4.set_title("Erro Percentual", fontsize=10)
ax4.set_ylabel("Erro [%]"); ax4.legend(fontsize=8); ax4.grid(True, alpha=0.3, axis='y')

plt.savefig("identificacao_pmsm.png", dpi=150, bbox_inches='tight')
print("  Gráfico salvo em: identificacao_pmsm.png")