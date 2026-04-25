import numpy as np
import matplotlib.pyplot as plt

# ============================================================================
# 1. PARÂMETROS DO MOTOR, CARGA E BATERIA
# ============================================================================
R_s = 0.05          # Resistência estatórica (Ohms)
L_s = 0.0005        # Indutância estatórica (Henrys)
P = 24              # Número de polos
p = P / 2           # Pares de polos (12)
Kv = 310.0          # Constante de velocidade (RPM/V)
J = 0.001           # Inércia do rotor (kg.m^2)
k_load = 5e-5       # Constante aerodinâmica da hélice (N.m / (rad/s)^2)

V_bus = 6 * 4.2     # Tensão máxima de 6 células (25.2V)

Ke_mec = 60.0 / (2 * np.pi * Kv)
Ke = Ke_mec / p     

Ts = 33.3e-6       
t_end = 2.0         
t = np.arange(0, t_end, Ts)
N = len(t)

# ============================================================================
# 2. FUNÇÕES DA FEM TRAPEZOIDAL
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

def sat(x, epsilon=1.5):
    return np.clip(x / epsilon, -1.0, 1.0)

# ============================================================================
# 3. GERAÇÃO DA PLANTA REAL (COM DEGRAUS DE CORRENTE)
# ============================================================================
x_true = np.zeros((4, N))
u_input = np.zeros((2, N))
y_meas = np.zeros((2, N))
torque_real, load_real = np.zeros(N), np.zeros(N)
v_abc = np.zeros((3, N))
I_dc = np.zeros(N)

# Criando o perfil de corrente antes do loop
I_profile = np.zeros(N)
for k in range(N):
    curr_t = t[k]
    # Definição dos degraus baseada no tempo
    if curr_t < 0.4:    target = 15.0
    elif curr_t < 0.8:  target = 40.0
    elif curr_t < 1.2:  target = 60.0
    elif curr_t < 1.6:  target = 25.0
    else:               target = 10.0
    
    # Suavização (Rampa) para o observador não "capotar" no transiente
    if k == 0:
        I_profile[k] = target
    else:
        # Ajuste o 0.01 se quiser uma subida mais rápida ou mais lenta
        I_profile[k] = I_profile[k-1] + 0.01 * (target - I_profile[k-1])

omega_m, theta_e = 0.0, 0.0

for k in range(N):
    # USAMOS O PERFIL DE CORRENTE AQUI
    I_current = I_profile[k]
    
    i_al = I_current * np.sin(theta_e)
    i_be = -I_current * np.cos(theta_e)
    
    # Derivadas (importante: a derivada agora considera que I_current muda!)
    # Como a rampa é suave (0.01), a aproximação abaixo ainda é válida:
    omega_e = omega_m * p
    di_al_dt = I_current * omega_e * np.cos(theta_e)
    di_be_dt = I_current * omega_e * np.sin(theta_e)
    
    # Cálculo de Torque e Dinâmica
    Te = Ke * p * (f_alpha(theta_e) * i_al + f_beta(theta_e) * i_be)
    Tl = k_load * (omega_m ** 2)
    
    omega_m += (1/J) * (Te - Tl) * Ts
    omega_e = omega_m * p
    theta_e = np.mod(theta_e + omega_e * Ts, 2 * np.pi)
    
    # Tensões de fase e BEMF
    e_al = Ke * omega_e * f_alpha(theta_e)
    e_be = Ke * omega_e * f_beta(theta_e)
    v_al = R_s * i_al + L_s * di_al_dt + e_al
    v_be = R_s * i_be + L_s * di_be_dt + e_be
    
    # Armazenamento de dados
    x_true[:, k] = [i_al, i_be, omega_e, theta_e]
    u_input[:, k] = [v_al, v_be]
    torque_real[k], load_real[k] = Te, Tl
    
    # Tensões para o gráfico Va, Vb, Vc
    v_abc[0, k] = v_al
    v_abc[1, k] = -0.5 * v_al + (np.sqrt(3)/2.0) * v_be
    v_abc[2, k] = -0.5 * v_al - (np.sqrt(3)/2.0) * v_be
    
    # Corrente DC aproximada
    P_inst = 1.5 * (v_al * i_al + v_be * i_be)
    I_dc[k] = P_inst / V_bus
    
    # Medição com ruído para o SMO
    y_meas[0, k] = i_al + np.random.normal(0, 0.3)
    y_meas[1, k] = i_be + np.random.normal(0, 0.3)
# ============================================================================
# 4. OBSERVADOR POR MODOS DESLIZANTES (SMO) COM AUTO-TUNING
# ============================================================================
i_hat = np.zeros((2, N))      
e_hat = np.zeros((2, N))      
theta_est = np.zeros(N)       
omega_est = np.zeros(N)       
torque_est_lpf = np.zeros(N)
e_hat_filt = np.zeros((2, N))

i_hat[:, 0] = y_meas[:, 0]

# --- PARÂMETROS DE BASE ---
Ks1 = 35000.0   # Ganho de rastreio de corrente (firme)
Kt  = 2.0      

# Ganhos para o Auto-Tuning de Ks2
Ks2_min = 20.0   # Ganho mínimo para partida
Ks2_max = 50000.0  # Ganho máximo para alta rotação
w_nom = 3000.0 * (np.pi/30) * p # Frequência elétrica nominal (ex: 3000 RPM)

Phi_i    = 1.0 - (R_s * Ts) / L_s
Gamma_v  = Ts / L_s
Gamma_s1 = Ks1 * Ts
Phi_e    = 1.0 - Kt * Ts  # Agora Phi_e é estável (perto de 1.0)

# Coeficientes de filtragem
alpha_w = (2 * np.pi * 60.0) * Ts
alpha_bemf = 0.05        
alpha_lpf_tq = 0.02      

th_est_prev = 0.0             

for k in range(1, N):
    v_al_k, v_be_k = u_input[0, k-1], u_input[1, k-1]
    
    # 1. Erro de Corrente
    err_al = y_meas[0, k] - i_hat[0, k-1]
    err_be = y_meas[1, k] - i_hat[1, k-1]
    
    # 2. Superfície de Deslizamento (Epsilon 10 para suavidade)
    s_al = sat(err_al, epsilon=10.0)
    s_be = sat(err_be, epsilon=10.0)
    
    # 3. Estimativa de Corrente (Eq. 23)
    i_hat[0, k] = Phi_i * i_hat[0, k-1] + Gamma_v * (v_al_k - e_hat[0, k-1]) + Gamma_s1 * s_al
    i_hat[1, k] = Phi_i * i_hat[1, k-1] + Gamma_v * (v_be_k - e_hat[1, k-1]) + Gamma_s1 * s_be
    
    # --- MÁGICA DO AUTO-TUNING (AJUSTE DINÂMICO DE KS2) ---
    # O ganho cresce proporcionalmente à velocidade estimada
    ratio = abs(omega_est[k-1]) / w_nom
    Ks2_dynamic = Ks2_min + (Ks2_max - Ks2_min) * np.clip(ratio, 0.0, 1.0)
    g_s2_dyn = Ks2_dynamic * Ts
    
    # 4. Estimativa de BEMF Bruta (Usando ganho adaptativo)
    e_raw_al = Phi_e * e_hat[0, k-1] - g_s2_dyn * s_al
    e_raw_be = Phi_e * e_hat[1, k-1] - g_s2_dyn * s_be
    e_hat[0, k], e_hat[1, k] = e_raw_al, e_raw_be 
    
    # 5. Filtragem BEMF (Anti-impulso)
    e_hat_filt[0, k] = alpha_bemf * e_raw_al + (1 - alpha_bemf) * e_hat_filt[0, k-1]
    e_hat_filt[1, k] = alpha_bemf * e_raw_be + (1 - alpha_bemf) * e_hat_filt[1, k-1]
    
    # 6. Ângulo e Velocidade
    th_est = np.arctan2(e_hat_filt[0, k], -e_hat_filt[1, k])
    theta_est[k] = np.mod(th_est, 2*np.pi)
    
    dth = th_est - th_est_prev
    if dth > np.pi: dth -= 2*np.pi
    elif dth < -np.pi: dth += 2*np.pi
    
    w_raw = dth / Ts
    omega_est[k] = omega_est[k-1] + alpha_w * (w_raw - omega_est[k-1])
    th_est_prev = th_est
    
    # 7. Torque Estimado (Correção de Sinal e Amplitude)
    w_safe = omega_est[k] if abs(omega_est[k]) > 5.0 else 5.0 
    
    # Calculamos o torque. Se ele aparecer invertido no gráfico, 
    # basta trocar o sinal da conta abaixo.
    t_calc = abs((3 * p / (4 * w_safe)) * (e_hat_filt[0, k] * i_hat[0, k] + e_hat_filt[1, k] * i_hat[1, k]))
    
    # O sinal do torque deve acompanhar a aceleração. 
    # Em motores BLDC/PMSM, torque e velocidade costumam ter o mesmo sinal.
    torque_est_lpf[k] = alpha_lpf_tq * t_calc + (1 - alpha_lpf_tq) * torque_est_lpf[k-1]
# ============================================================================
# 5. VISUALIZAÇÃO DOS RESULTADOS (CORRIGIDO)
# ============================================================================
tempo_zoom = 0.05
zoom_start = max(0, t_end - tempo_zoom)

# --- Plot 1: Dinâmica Mecânica ---
plt.figure(figsize=(10, 8))
plt.subplot(2, 1, 1)
plt.plot(t, x_true[2, :]/p * (30/np.pi), 'b--', linewidth=2, label='Real')
plt.plot(t, omega_est/p * (30/np.pi), 'r-', alpha=0.8, label='Estimado (Novo SMO)')
plt.ylabel('RPM')
plt.title('Velocidade Estimada vs Real')
plt.legend(); plt.grid(True)

plt.subplot(2, 1, 2)
plt.plot(t, torque_real, 'b--', linewidth=2, label='Real')
plt.plot(t, torque_est_lpf, 'r-', linewidth=2, label='Estimado (Eq. 28)')
plt.plot(t, load_real, 'k-', label='Carga')
plt.ylabel('Torque (N.m)'); plt.xlabel('Tempo (s)')
plt.title('Torque Eletromagnético'); plt.legend(); plt.grid(True)
plt.ylim(0,3)
plt.tight_layout()
plt.show()

# # --- Plot 2: Zoom nas Correntes (Strings r'$...$') ---
# plt.figure(figsize=(10, 5))
# plt.plot(t, x_true[0, :], 'b-', alpha=0.4, linewidth=4, label=r'$i_\alpha$ Real')
# plt.plot(t, i_hat[0, :], 'r--', linewidth=2, label=r'$i_\alpha$ Est. SMO')
# plt.plot(t, x_true[1, :], 'g-', alpha=0.4, linewidth=4, label=r'$i_\beta$ Real')
# plt.plot(t, i_hat[1, :], 'k--', linewidth=2, label=r'$i_\beta$ Est. SMO')
# plt.xlim(zoom_start, t_end)
# plt.ylabel('Corrente (A)'); plt.xlabel('Tempo (s)')
# plt.title('Zoom nas Correntes Alpha-Beta (Estabilidade Alcançada)')
# plt.legend(loc='upper right'); plt.grid(True)
# plt.tight_layout(); plt.show()

# # --- Plot 3: Tensões (Strings r'$...$') ---
# fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
# ax1.plot(t, v_abc[0, :], 'r', label=r'$V_a$')
# ax1.plot(t, v_abc[1, :], 'g', label=r'$V_b$')
# ax1.plot(t, v_abc[2, :], 'b', label=r'$V_c$')
# ax1.set_ylabel('Tensão (V)'); ax1.set_title('Tensões Trifásicas'); ax1.grid(True); ax1.legend()

# ax2.plot(t, u_input[0, :], 'purple', label=r'$V_\alpha$')
# ax2.plot(t, u_input[1, :], 'orange', label=r'$V_\beta$')
# ax2.set_ylabel('Tensão (V)'); ax2.set_title('Entrada Alpha-Beta do SMO'); ax2.grid(True); ax2.legend()

# plt.xlim(zoom_start, t_end)
# plt.tight_layout() 
# plt.show()

# # --- Plot 4: Corrente da Bateria ---
# plt.figure(figsize=(10, 4))
# plt.plot(t, I_dc, 'b-', label='Corrente do Barramento')
# plt.ylabel('Corrente (A)'); plt.xlabel('Tempo (s)'); plt.title('Estimativa de Corrente DC (Barramento)')
# plt.grid(True); plt.legend(); plt.show()

# print("Simulação completa com gráficos restaurados e labels corrigidos!")