import numpy as np
import matplotlib.pyplot as plt

CONFIG_PLOT_SMO = True
CONFIG_PLOT_PROPELLER_COEFFICIENT = True

# ============================================================================
# 1. PARÂMETROS DO MOTOR, CARGA E BATERIA
# ============================================================================
R_s = 0.005          # Ohms
L_s = 0.0005        # Henrys
P = 24              # Polos
p = P / 2           # Pares de polos
Kv = 310.0          # RPM/V
J = 0.001           # kg.m^2
k_load = 5e-5       # N.m / (rad/s)^2
V_bus = 25.2        # 6S LiPo

# Constantes de FEM
Ke_mec = 60.0 / (2 * np.pi * Kv)
Ke = Ke_mec / p     

Ts = 33.3e-6       
t_end = 5.0         
t = np.arange(0, t_end, Ts)
N = len(t)

# ============================================================================
# 2. FUNÇÕES DA FEM TRAPEZOIDAL E AUXILIARES
# ============================================================================
def f_a(theta):
    th = np.mod(theta, 2 * np.pi)
    if th < np.pi/6: return (6/np.pi) * th
    elif th < 5*np.pi/6: return 1.0
    elif th < 7*np.pi/6: return -(6/np.pi) * (th - np.pi)
    elif th < 11*np.pi/6: return -1.0
    else: return (6/np.pi) * (th - 2*np.pi)

def f_alpha(theta): return f_a(theta)
def f_beta(theta): return (1/np.sqrt(3)) * (f_a(theta - 2*np.pi/3) - f_a(theta - 4*np.pi/3))

def sat(x, epsilon=10.0):
    return np.clip(x / epsilon, -1.0, 1.0)

# ============================================================================
# 3. SIMULAÇÃO DA PLANTA (DINÂMICA DE TENSÃO -> CORRENTE)
# ============================================================================
x_true = np.zeros((4, N))
u_input = np.zeros((2, N))
y_meas = np.zeros((2, N))
torque_real = np.zeros(N)
load_real = np.zeros(N)

# Perfil de Corrente de Referência
I_target_profile = np.zeros(N)
for k in range(N):
    curr_t = t[k]
    if curr_t < 0.4:    target = 15.0
    elif curr_t < 0.5:  target = 40.0
    elif curr_t < 2.0:  target = 100.0
    elif curr_t < 3.0:  target = 40.0
    elif curr_t < 4.0:  target = 20.0
    else:               target = 0.0
    I_target_profile[k] = target

# Estados iniciais
omega_m, theta_e, i_al, i_be = 0.0, 0.0, 0.0, 0.0
Kp_i = 1000.0 # Ganho do controlador de corrente

for k in range(N):
    # Malha de Controle (Inversor)
    i_al_ref = I_target_profile[k] * np.sin(theta_e)
    i_be_ref = -I_target_profile[k] * np.cos(theta_e)
    
    e_al_k = Ke * (omega_m * p) * f_alpha(theta_e)
    e_be_k = Ke * (omega_m * p) * f_beta(theta_e)
    
    v_al_ref = Kp_i * (i_al_ref - i_al) + e_al_k
    v_be_ref = Kp_i * (i_be_ref - i_be) + e_be_k
    
    # Saturação de Tensão (Bateria)
    v_al = np.clip(v_al_ref, -V_bus, V_bus)
    v_be = np.clip(v_be_ref, -V_bus, V_bus)
    
    # Integração da Corrente (Planta Física)
    di_al = (1.0/L_s) * (v_al - R_s * i_al - e_al_k)
    di_be = (1.0/L_s) * (v_be - R_s * i_be - e_be_k)
    i_al += di_al * Ts
    i_be += di_be * Ts

    # Dinâmica Mecânica
    Te = Ke * p * (f_alpha(theta_e) * i_al + f_beta(theta_e) * i_be)
    Tl = k_load * (omega_m ** 2)
    omega_m += (1/J) * (Te - Tl) * Ts
    theta_e = np.mod(theta_e + (omega_m * p) * Ts, 2 * np.pi)
    
    # Sensores e Armazenamento
    x_true[:, k] = [i_al, i_be, (omega_m * p), theta_e]
    u_input[:, k] = [v_al, v_be]
    y_meas[:, k] = [i_al + np.random.normal(0, 0.2), i_be + np.random.normal(0, 0.2)]
    torque_real[k], load_real[k] = Te, Tl

# ============================================================================
# 4. OBSERVADOR POR MODOS DESLIZANTES (SMO) COM AUTO-TUNING
# ============================================================================
i_hat = np.zeros((2, N))      
e_hat = np.zeros((2, N))      
e_hat_filt = np.zeros((2, N))
theta_est = np.zeros(N)       
omega_est = np.zeros(N)       
torque_est_lpf = np.zeros(N)

# Ganhos de Base
Ks1 = 80000.0   
Kt  = 05000.0      # Amortecimento para evitar drift DC
Ks2_min, Ks2_max = 200.0, 1000000.0
w_nom = 3000.0 * p * (np.pi/30)

# Discretização
Phi_i    = 1.0 - (R_s * Ts) / L_s
Gamma_v  = Ts / L_s
Gamma_s1 = Ks1 * Ts
Phi_e    = 1.0 - Kt * Ts

# Filtros
alpha_w = (2 * np.pi * 40.0) * Ts
alpha_bemf = 0.05        
alpha_lpf_tq = 0.05      

th_est_prev = 0.0             

for k in range(1, N):
    # 4.1. Erro de Corrente
    err_al = y_meas[0, k] - i_hat[0, k-1]
    err_be = y_meas[1, k] - i_hat[1, k-1]
    
    s_al = sat(err_al, epsilon=8.0)
    s_be = sat(err_be, epsilon=8.0)
    
    # 4.2. Estimativa de Corrente (Corrigido para receber V_alpha/beta real)
    i_hat[0, k] = Phi_i * i_hat[0, k-1] + Gamma_v * (u_input[0, k-1] - e_hat[0, k-1]) + Gamma_s1 * s_al
    i_hat[1, k] = Phi_i * i_hat[1, k-1] + Gamma_v * (u_input[1, k-1] - e_hat[1, k-1]) + Gamma_s1 * s_be
    
    # 4.3. Auto-Tuning do Ganho Ks2
    ratio = abs(omega_est[k-1]) / w_nom
    Ks2_dyn = Ks2_min + (Ks2_max - Ks2_min) * np.clip(ratio, 0.0, 1.0)
    g_s2 = Ks2_dyn * Ts
    
    # 4.4. Estimativa de BEMF
    e_raw_al = Phi_e * e_hat[0, k-1] - g_s2 * s_al
    e_raw_be = Phi_e * e_hat[1, k-1] - g_s2 * s_be
    e_hat[:, k] = [e_raw_al, e_raw_be]
    
    e_hat_filt[0, k] = alpha_bemf * e_raw_al + (1 - alpha_bemf) * e_hat_filt[0, k-1]
    e_hat_filt[1, k] = alpha_bemf * e_raw_be + (1 - alpha_bemf) * e_hat_filt[1, k-1]
    
    # 4.5. Velocidade e Ângulo
    th_est = np.arctan2(e_hat_filt[0, k], -e_hat_filt[1, k])
    theta_est[k] = np.mod(th_est, 2*np.pi)
    
    dth = th_est - th_est_prev
    if dth > np.pi: dth -= 2*np.pi
    elif dth < -np.pi: dth += 2*np.pi
    
    w_raw = dth / Ts
    omega_est[k] = omega_est[k-1] + alpha_w * (w_raw - omega_est[k-1])
    th_est_prev = th_est
    
    # 4.6. Torque Estimado (Usando Potência Instantânea 1.5 * p / omega_e)
    w_safe = abs(omega_est[k]) if abs(omega_est[k]) > 10.0 else 10.0 
    t_inst = (1.5 * p / w_safe) * (e_hat_filt[0, k] * i_hat[0, k] + e_hat_filt[1, k] * i_hat[1, k])
    
    # O sinal pode vir invertido dependendo do referencial do arctan2
    # Se o Te real for positivo e o est for negativo, multiplique por -1
    torque_est_lpf[k] = alpha_lpf_tq * abs(t_inst) + (1 - alpha_lpf_tq) * torque_est_lpf[k-1]

# ============================================================================
# 5. VISUALIZAÇÃO DOS RESULTADOS
# ============================================================================


if CONFIG_PLOT_SMO == True:

    plt.figure(figsize=(12, 10))

    # Subplot 1: Velocidade
    plt.subplot(3, 1, 1)
    plt.plot(t, x_true[2, :]/p * (30/np.pi), 'b--', label='RPM Real')
    plt.plot(t, omega_est/p * (30/np.pi), 'r-', alpha=0.7, label='RPM Estimado')
    # plt.xlim(0.1, t_end)
    plt.title('Rastreamento de Velocidade (Auto-tuned SMO)'); plt.ylabel('RPM'); plt.grid(True); plt.legend()

    # Subplot 2: Torque
    plt.subplot(3, 1, 2)
    plt.plot(t, torque_real, 'b--', linewidth=2, label='Te Real')
    plt.plot(t, torque_est_lpf, 'r-', linewidth=1.5, label='Te Estimado (SMO)')
    plt.plot(t, load_real, 'k', alpha=0.5, label='Carga (Hélice)')
    # NOVO: Torque Resultante em Tracejado -.-
    # plt.plot(t, torque_real - load_real, 'g', linestyle='-.', alpha=0.6, label='T_resultante (Real)')
    plt.ylabel('Torque (N.m)'); plt.title('Dinâmica de Torques'); plt.grid(True); plt.legend(); plt.ylim(-0.5, 3.5)

    # Subplot 3: Erro de Ângulo
    plt.subplot(3, 1, 3)
    erro_theta = x_true[3, :] - theta_est
    erro_theta = (erro_theta + np.pi) % (2 * np.pi) - np.pi
    plt.plot(t, np.degrees(erro_theta), 'm')
    plt.ylabel('Erro (Graus)'); plt.xlabel('Tempo (s)'); plt.title('Erro de Estimação de Ângulo Elétrico'); plt.grid(True)

    plt.tight_layout(); plt.show()

import numpy as np
import matplotlib.pyplot as plt
from scipy.signal import butter, lfilter
# ============================================================================
# 6. IDENTIFICAÇÃO DINÂMICA DE k (VERSÃO FINAL DESTRAVADA)
# ============================================================================

# --- 6.1. Filtro Digital Butterworth ---
fc = 2.0  # Abaixei para 2Hz para o torque ficar MAIS liso e não resetar o contador
fs = 1.0 / Ts           
nyq = 0.5 * fs          
low = fc / nyq
b, a = butter(1, low, btype='low') 
tq_real_lp = lfilter(b, a, torque_real)

# --- 6.2. Parâmetros de Estabilidade (MAIS TOLERANTES) ---
# Aumentei a margem. 0.01 é muito pouco para simulação com ripple.
threshold_tq_margin = 0.05  
min_omega = 500.0           
janela_minima = 30 

# Listas de armazenamento
k_identificado_real = []
k_identificado_smo = []
rpm_estabilizado = []
contador_estavel = 0

print("-" * 65)
print(f"{'Tempo (s)':<10} | {'RPM':<10} | {'k Real':<12} | {'k SMO':<12}")
print("-" * 65)

for k in range(1, N):
    # Variação do torque filtrado
    delta_tq_lp = abs(tq_real_lp[k] - tq_real_lp[k-1])
    
    # Se a variação for pequena, o sistema está "parado" em um patamar
    if delta_tq_lp <= threshold_tq_margin and x_true[2, k] > min_omega:
        contador_estavel += 1
    else:
        contador_estavel = 0 

    # IMPORTANTE: A cada 100 amostras estáveis, tiramos uma foto do k
    # Isso garante que pegamos dados ao longo de TODO o patamar, não só no início
    if contador_estavel >= janela_minima and contador_estavel % 100 == 0:
        w_m_r = x_true[2, k] / p
        w_m_s = omega_est[k] / p
        
        if w_m_r > 1.0:
            k_r = tq_real_lp[k] / (w_m_r**2)
            k_s = torque_est_lpf[k] / (w_m_s**2)
            rpm = w_m_r * (30/np.pi)

            if k_r > 0 and k_s > 0:
                k_identificado_real.append(k_r)
                k_identificado_smo.append(k_s)
                rpm_estabilizado.append(rpm)
                # Printa apenas alguns para não inundar o terminal
                if len(k_identificado_real) % 10 == 0:
                    print(f"{t[k]:<10.2f} | {rpm:<10.1f} | {k_r:<12.2e} | {k_s:<12.2e}")

# --- 6.3. Média com Proteção contra NaN ---
if len(k_identificado_real) > 10:
    # Se tiver muitos dados, tira os 10% das pontas
    amount_trimm = int(len(k_identificado_real) * 0.1)
    k_real_sorted = np.sort(k_identificado_real)
    k_smo_sorted = np.sort(k_identificado_smo)
    
    media_real = np.mean(k_real_sorted[amount_trimm:-amount_trimm])
    media_smo = np.mean(k_smo_sorted[amount_trimm:-amount_trimm])
else:
    # Se tiver poucos dados, tira a média simples para não dar NaN
    media_real = np.mean(k_identificado_real) if k_identificado_real else 0
    media_smo = np.mean(k_identificado_smo) if k_identificado_smo else 0

print("=" * 65)
print("IDENTIFICACAO DE PARAMETRO CONSTANTE DA HELICE")
print("=" * 65)


print("-" * 65)
print(f"k_Load Nominal:      {k_load:.2e}")
print(f"k_Real Médio:        {media_real:.2e} (Erro: {abs(media_real-k_load)/k_load*100 if k_load!=0 else 0:.2f}%)")
print(f"k_SMO Médio:         {media_smo:.2e} (Erro: {abs(media_smo-k_load)/k_load*100 if k_load!=0 else 0:.2f}%)")
print("-" * 65)
# ============================================================================
# PLOTS DE VALIDAÇÃO
# ============================================================================
if CONFIG_PLOT_PROPELLER_COEFFICIENT:
    # Comparativo de Torque (Ripple vs Médio)
    plt.figure(figsize=(10, 4))
    plt.plot(t, torque_real, color='lightgray', alpha=0.5, label='Torque Real Bruto')
    plt.plot(t, tq_real_lp, color='blue', linewidth=1.5, label=f'Torque Médio (LPF {fc}Hz)')
    plt.title('Filtragem de Torque para Identificação de Regime Permanente')
    plt.ylabel('Torque [N.m]'); plt.xlabel('Tempo [s]'); plt.legend(); plt.grid(True)
    
    # Caracterização k vs RPM
    plt.figure(figsize=(10, 6))
    plt.axhline(y=k_load, color='k', linestyle='--', label='k Nominal (Teórico)')
    if rpm_estabilizado:
        plt.scatter(rpm_estabilizado, k_identificado_real, color='blue', alpha=0.6, label='k Real (Pontos Estáveis)')
        plt.scatter(rpm_estabilizado, k_identificado_smo, color='red', marker='x', label='k SMO (Pontos Estáveis)')
        plt.ylim(k_load*0.5, k_load*1.5)
    
    plt.title(f'Identificação do Coeficiente da Hélice\nK_SMO Final: {media_smo:.2e}')
    plt.xlabel('Velocidade Mecânica [RPM]'); plt.ylabel('Coeficiente k'); plt.legend(); plt.grid(True)
    plt.show()