import numpy as np
import matplotlib.pyplot as plt
from numba import njit
import math

CONFIG_INVERT_CURRENTS = False
CONFIG_PLOT_PROPELLER_COEFFICIENT = True
CONFIG_SMO_PLOT = True

# --- Parâmetros Físicos e Motor ---
R_s, L_s = 0.000_05, 0.000_5 
P, p = 24, 12
Kv, J, k_load = 310.0, 0.0001, 10.0e-5
V_bus, Ts, t_end = 30.0, 10e-6, 10.0
Ke_mec = 60.0 / (2 * math.pi * Kv)
Ke = Ke_mec / p 

t = np.arange(0, t_end, Ts)
N = len(t)

# Perfil de velocidade alvo
RPM_target_profile = np.zeros(N)
for k in range(N):
    curr_t = k * Ts
    if curr_t < 0.2:    target = 0.0
    elif curr_t < 1.2:  target = 1000.0  
    elif curr_t < 2.4:  target = 500.0  
    elif curr_t < 3.6:  target = 700.0  
    elif curr_t < 4.8:  target = 1000.0  
    elif curr_t < 6.1:  target = 800.0  
    elif curr_t < 8.0:  target = 600.0  
    else:               target = 600.0   
    RPM_target_profile[k] = target

@njit
def f_a_jit(theta):
    th = theta % (2 * math.pi)
    if th < math.pi/6: return (6/math.pi) * th
    elif th < 5*math.pi/6: return 1.0
    elif th < 7*math.pi/6: return -(6/math.pi) * (th - math.pi)
    elif th < 11*math.pi/6: return -1.0
    else: return (6/math.pi) * (th - 2*math.pi)

@njit
def run_sim_core(N, Ts, RPM_target):
    res = np.zeros((17, N)) 
    i_a = i_b = i_c = omega_m = rpm_set = 0.0
    theta_e = 1.5*np.pi
    integral_w = 0.0
    
    i_hat_al = i_hat_be = e_hat_al = e_hat_be = 0.0
    omega_est = th_est = tq_lpf = 0.0
    integral_pll = 0.0
    omega_plot_lpf = 0.0

    fc_torque = 10_000_000.0   
    fc_plot   = 2.0  

    Ks1, Ks2, Kt = 30_000.0, -30_000.0, 5000.0
    Phi_i, Gamma_v, Gamma_s1, Phi_e = 1.0-(R_s*Ts/L_s), Ts/L_s, Ks1*Ts, 1.0-(Kt*Ts)
    
    alpha_tq = (2*math.pi*fc_torque*Ts)/(1+2*math.pi*fc_torque*Ts)
    alpha_plot = (2*math.pi*fc_plot*Ts)/(1+2*math.pi*fc_plot*Ts)

    # Configuração do PLL
    w_n = 2 * math.pi * 150.0  
    zeta = 0.707
    Kp_pll = 2 * zeta * w_n
    Ki_pll = w_n * w_n

    for k in range(N):
        # Controle de rampa e velocidade
        target = RPM_target[k]
        max_step = 20_000.0 * Ts 
        if target > rpm_set: rpm_set = min(rpm_set + max_step, target)
        elif target < rpm_set: rpm_set = max(rpm_set - max_step, target)
        
        err_w = rpm_set - (omega_m * 30/math.pi)
        V_cmd_raw = 20.1 * err_w + 100.5 * integral_w
        
        # 3. Calcula a saída REAL saturada (o que o inversor realmente aplica)
        V_cmd = max(min(V_cmd_raw, 30.0), 0.0)
        
        # 4. ANTI-WINDUP: Back-Calculation
        # Calcula a diferença entre o desejado e o possível
        erro_saturacao = V_cmd - V_cmd_raw
        
        # Define o ganho de rastreamento (Ka = 1 / Kp é um valor padrão na literatura)
        Ka = 1.0 / 20.1 
        
        # Integra o erro da velocidade SOMADO à correção de saturação
        integral_w += (err_w + Ka * erro_saturacao) * Ts
        V_act = 30.0 if ((k*Ts)%(1/8000.0) < (V_cmd/30.0)/8000.0) else 0.0
        
        # Back-EMF e tensões de fase
        ea, eb, ec = Ke_mec*omega_m*f_a_jit(theta_e), Ke_mec*omega_m*f_a_jit(theta_e-2*math.pi/3), Ke_mec*omega_m*f_a_jit(theta_e-4*math.pi/3)
        th_p = theta_e % (2*math.pi)
        va = vb = vc = 0.0
        
        # Logica do inversor 6 passos
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

        # Calculo de torque e dinâmica mecânica real
        Te = Ke_mec*(i_a*f_a_jit(theta_e) + i_b*f_a_jit(theta_e-2*math.pi/3) + i_c*f_a_jit(theta_e-4*math.pi/3))
        omega_m += (1/J)*(Te - k_load*omega_m**2)*Ts
        theta_e = (theta_e + (omega_m*p)*Ts) % (2*math.pi)

        # =================== SMO =================================

        # Conversão para Alpha-Beta
        vn = (1.0/3.0)*((va+vb+vc)-(ea+eb+ec))
        ual, ube = va - vn, (1.0/math.sqrt(3.0))*((vb-vn)-(vc-vn))
        
        if CONFIG_INVERT_CURRENTS == False:
            i_al, i_be = i_a, (1.0/math.sqrt(3.0))*(i_a +2*i_b)
        else:
            i_al, i_be = i_a, (1.0/math.sqrt(3.0))*(i_a +2*i_c)

        # Estimador SMO
        err_al, err_be = i_al - i_hat_al, i_be - i_hat_be
        s_al, s_be = max(min(err_al/2.0, 1.0), -1.0), max(min(err_be/2.0, 1.0), -1.0)
        
        i_hat_al = Phi_i * i_hat_al + Gamma_v*(ual - e_hat_al) + Gamma_s1 * s_al
        i_hat_be = Phi_i * i_hat_be + Gamma_v*(ube - e_hat_be) + Gamma_s1 * s_be
        
        e_hat_al = Phi_e * e_hat_al + (Ks2 * Ts) * s_al
        e_hat_be = Phi_e * e_hat_be + (Ks2 * Ts) * s_be
        
        # Malha do PLL
        amp_E = math.sqrt(e_hat_al**2 + e_hat_be**2)
        if amp_E > 1.0: 
            erro_fase = (e_hat_al * math.cos(th_est) + e_hat_be * math.sin(th_est)) / amp_E
        else:
            erro_fase = 0.0
            
        integral_pll += erro_fase * Ts
        omega_est_elec = Kp_pll * erro_fase + Ki_pll * integral_pll
        omega_est = omega_est_elec / p
        
        th_est = (th_est + omega_est_elec * Ts) % (2 * math.pi)
        
        # Filtro de velocidade para o gráfico
        omega_plot_lpf += alpha_plot * (omega_est - omega_plot_lpf)

        # Reconstrução da Back-EMF e Torque estimado
        fa_est = f_a_jit(th_est)
        fb_est = (1.0/math.sqrt(3.0))*(f_a_jit(th_est - 2*math.pi/3) - f_a_jit(th_est - 4*math.pi/3))
        
        E_amp_clean = Ke * omega_est_elec 
        e_recon_al = E_amp_clean * fa_est
        e_recon_be = E_amp_clean * fb_est
        
        t_inst = 1.5 * Ke * p * (fa_est * i_hat_al + fb_est * i_hat_be)
        tq_lpf = alpha_tq * t_inst + (1 - alpha_tq) * tq_lpf

        # Grava os resultados
        res[0,k], res[1,k] = omega_m*30/math.pi, omega_plot_lpf*30/math.pi
        res[2,k], res[3,k] = Te, tq_lpf
        res[4,k], res[5,k] = theta_e, th_est
        res[6,k], res[7,k] = i_al, i_hat_al
        res[8,k], res[9,k] = i_be, i_hat_be
        res[10,k], res[11,k] = ea, e_recon_al
        res[12,k], res[13,k] = (1.0/math.sqrt(3.0))*(eb-ec), e_recon_be
        res[14,k] = err_w
        res[15,k] = integral_w
        res[16,k] = V_cmd
        
    return res

# Execução e processamento dos gráficos
out = run_sim_core(N, Ts, RPM_target_profile)


from scipy.signal import butter, lfilter
import numpy as np
import math

# Extração dos dados do simulador
rpm_real_plot = out[0]  
rpm_est_plot  = out[1]  

omega_m_rad   = rpm_real_plot * (math.pi / 30.0)
omega_est_rad = rpm_est_plot * (math.pi / 30.0)

torque_real = out[2]
torque_est_raw = out[3]

# ============================================================================
# 3. IDENTIFICAÇÃO DINÂMICA DE k (MÉTRICA FINAL: RMS)
# ============================================================================

w_m_rad = out[0] * (math.pi / 30.0)      
w_est_rad = out[1] * (math.pi / 30.0)    
tq_real = out[2]                         
tq_est = out[3]                          

target_diff = np.diff(RPM_target_profile)
target_diff = np.append(target_diff, 0) 
mascara_patamar = (target_diff == 0) & (RPM_target_profile > 200.0)

edges = np.diff(mascara_patamar.astype(int))
starts = np.where(edges == 1)[0] + 1
ends = np.where(edges == -1)[0] + 1

if mascara_patamar[0]: starts = np.insert(starts, 0, 0)
if mascara_patamar[-1]: ends = np.append(ends, N)

k_real_list = []
k_smo_list = []
rpm_patamares = []

header = f"{'Intervalo (s)':<15} | {'Alvo':<7} | {'TqReal_Avg':<10} | {'TqSMO_RMS':<10} | {'k_Real':<9} | {'k_SMO'}"
print("=" * len(header))
print(header)
print("-" * len(header))

atraso_pi_samples = int(0.4 / Ts) 

for s, e in zip(starts, ends):
    if (e - s) > atraso_pi_samples:
        idx_s = s + atraso_pi_samples
        idx_e = e - 10 
        
        # Sinais da janela
        w_v = w_m_rad[idx_s:idx_e]
        w_s_v = w_est_rad[idx_s:idx_e]
        tr_v = tq_real[idx_s:idx_e]
        ts_v = tq_est[idx_s:idx_e]
        
        # --- CÁLCULO BASEADO NA ENERGIA (RMS) ---
        tq_real_avg = np.mean(tr_v)
        tq_smo_rms  = np.sqrt(np.mean(ts_v**2))
        
        k_r = tq_real_avg / (np.mean(w_v)**2)
        k_s = tq_smo_rms / (np.mean(w_s_v)**2)
        
        k_real_list.append(k_r)
        k_smo_list.append(k_s)
        rpm_patamares.append(RPM_target_profile[idx_s])
        
        print(f"[{s*Ts:>5.2f}-{e*Ts:>5.2f}] | {RPM_target_profile[idx_s]:>7.1f} | {tq_real_avg:>10.4f} | {tq_smo_rms:>10.4f} | {k_r:>9.2e} | {k_s:>9.2e}")

print("-" * len(header))
media_k_smo = np.mean(k_smo_list) if k_smo_list else 0
print(f"VALOR NOMINAL: {k_load:.2e}  |  MÉDIA K_REAL: {np.mean(k_real_list):.2e}  |  MÉDIA K_SMO: {media_k_smo:.2e}")
print("=" * len(header))


# ============================================================================
# 5. PLOTs
# ============================================================================


if CONFIG_SMO_PLOT:
    fig1, ax1 = plt.subplots(2, 2, figsize=(14, 8), sharex=True)
    ax1[0, 0].plot(t, out[6], 'b', alpha=0.8, label='Real')
    ax1[0, 0].plot(t, out[7], 'r--', label='Estimado')
    ax1[0, 0].set_title("Corrente Eixo Alpha ($i_{\\alpha}$)")
    ax1[0, 0].set_ylabel("Corrente [A]")
    ax1[0, 0].legend()
    ax1[0, 0].grid(True)

    ax1[0, 1].plot(t, out[8], 'b', alpha=0.8, label='Real')
    ax1[0, 1].plot(t, out[9], 'r--', label='Estimado')
    ax1[0, 1].set_title("Corrente Eixo Beta ($i_{\\beta}$)")
    ax1[0, 1].legend()
    ax1[0, 1].grid(True)

    ax1[1, 0].plot(t, out[10], 'b', alpha=0.8, label='Real')
    ax1[1, 0].plot(t, out[11], 'r--', label='Reconstruído Analítico')
    ax1[1, 0].set_title("Back-EMF Eixo Alpha ($e_{\\alpha}$)")
    ax1[1, 0].set_ylabel("Tensão [V]")
    ax1[1, 0].set_xlabel("Tempo [s]")
    ax1[1, 0].legend()
    ax1[1, 0].grid(True)

    ax1[1, 1].plot(t, out[12], 'b', alpha=0.8, label='Real')
    ax1[1, 1].plot(t, out[13], 'r--', label='Reconstruído Analítico')
    ax1[1, 1].set_title("Back-EMF Eixo Beta ($e_{\\beta}$)")
    ax1[1, 1].set_xlabel("Tempo [s]")
    ax1[1, 1].legend()
    ax1[1, 1].grid(True)
    fig1.tight_layout()

    fig2, ax2 = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    ax2[0].plot(t, out[4], 'b', label='Theta Real')
    ax2[0].plot(t, out[5], 'r--', label='Theta Estimado')
    ax2[0].set_title("Rastreamento de Ângulo Elétrico via PLL")
    ax2[0].set_ylabel("Ângulo [rad]")
    ax2[0].legend()
    ax2[0].grid(True)

    error = (out[4] - out[5] + np.pi) % (2 * np.pi) - np.pi
    ax2[1].plot(t, np.degrees(error), 'm')
    ax2[1].axhline(0, color='k', linestyle=':')
    ax2[1].set_title("Erro de Estimação de Ângulo")
    ax2[1].set_ylabel("Erro [Graus Elétricos]")
    ax2[1].set_xlabel("Tempo [s]")
    ax2[1].grid(True)
    fig2.tight_layout()

    fig3, ax3 = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    ax3[0].plot(t, out[0], 'b', label='RPM Real')
    ax3[0].plot(t, out[1], 'r--', label='RPM Estimado')
    ax3[0].set_title("Desempenho Mecânico")
    ax3[0].set_ylabel("Velocidade [RPM]")
    ax3[0].legend()
    ax3[0].grid(True)

    ax3[1].plot(t, out[2], color='silver', alpha=0.8, label='Te Real (Ripple)')
    ax3[1].plot(t, out[3], 'g', label='Te Est')
    ax3[1].set_ylabel("Torque [N.m]")
    ax3[1].set_xlabel("Tempo [s]")
    ax3[1].legend()
    ax3[1].grid(True)
    fig3.tight_layout()


if CONFIG_PLOT_PROPELLER_COEFFICIENT:
    from scipy.signal import butter, lfilter
    
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))
    
    # Plot de Torque: Comparando a Média do Real com o RMS do SMO
    ax1.plot(t, tq_real, color='silver', alpha=0.3, label='Te Real (Bruto)')
    ax1.plot(t, tq_est, color='green', alpha=0.3, label='Te SMO (Bruto)')
    
    # Filtro para visualização das tendências
    fs = 1.0/Ts
    b, a = butter(1, 40.0/(0.5*fs), btype='low')
    tq_real_f = lfilter(b, a, tq_real)
    tq_est_f  = lfilter(b, a, tq_est)
    
    ax1.plot(t, tq_real_f, 'b', lw=2, label='Te Real (Tendência Média)')
    ax1.plot(t, tq_est_f, 'r--', lw=1.5, label='Te SMO (Tendência Filtrada)')
    
    ax1.set_title("Comparativo de Torque: Dinâmica Real vs Estimada")
    ax1.set_ylabel("Torque [N.m]")
    ax1.legend(loc='upper right')
    ax1.grid(True)
    
    # Plot do Coeficiente K identificado por patamar
    ax2.axhline(y=k_load, color='k', ls='--', label='k Nominal (Teórico)')
    if rpm_patamares:
        ax2.scatter(rpm_patamares, k_real_list, c='blue', s=100, label='k Real (Média)')
        ax2.scatter(rpm_patamares, k_smo_list, c='red', marker='x', s=100, label='k SMO (RMS)')
        
        ax2.set_ylim(k_load*0.8, k_load*1.2) # Zoom de 20% em volta do alvo
        
    ax2.set_title(f"Identificação do Coeficiente k (Erro Final: {abs(media_k_smo-k_load)/k_load*100:.2f}%)")
    ax2.set_ylabel("Coeficiente k")
    ax2.set_xlabel("Velocidade [RPM]")
    ax2.legend()
    ax2.grid(True)
    
    plt.tight_layout()



# Extraindo as novas variáveis do PI gravadas na simulação
erro_velocidade = out[14]
acao_integral = out[15]
v_cmd = out[16]

fig4, ax4 = plt.subplots(3, 1, figsize=(12, 10), sharex=True)

# 1. Referência vs Realidade
ax4[0].plot(t, RPM_target_profile, 'k--', label='RPM Alvo (Target)')
ax4[0].plot(t, out[0], 'b', alpha=0.8, label='RPM Real')
ax4[0].set_title("Desempenho da Malha de Velocidade")
ax4[0].set_ylabel("Velocidade [RPM]")
ax4[0].legend()
ax4[0].grid(True)

# 2. Erro Processado pelo PI
ax4[1].plot(t, erro_velocidade, 'r', label='Erro de Velocidade (Set - Real)')
ax4[1].axhline(0, color='k', linestyle=':')
ax4[1].set_title("Erro Processado pelo Controlador PI")
ax4[1].set_ylabel("Erro [RPM]")
ax4[1].legend()
ax4[1].grid(True)

# 3. Saída do PI (Comando de Tensão e Ação Integral)
# Multiplicamos a acao_integral pelo ganho Ki (100.5) usado no seu codigo 
# para ver o peso real dela na tensão final
ax4[2].plot(t, v_cmd, 'g', label='Comando de Tensão Final (V_cmd)')
ax4[2].plot(t, acao_integral * 100.5, 'orange', linestyle='--', label='Parcela Integral (Ki * int(e))')
ax4[2].axhline(30.0, color='red', linestyle=':', alpha=0.6, label='Limite de Saturação (30V)')
ax4[2].set_title("Esforço de Controle (Saída do Inversor)")
ax4[2].set_ylabel("Tensão [V]")
ax4[2].set_xlabel("Tempo [s]")
ax4[2].legend()
ax4[2].grid(True)

fig4.tight_layout()

plt.show()