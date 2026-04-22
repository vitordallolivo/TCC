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

# Configuração do Barramento DC (Bateria LiPo 6S)
V_bus = 6 * 4.2     # Tensão máxima de 6 células (25.2V)

# Conversão da constante elétrica
Ke_mec = 60.0 / (2 * np.pi * Kv)
Ke = Ke_mec / p     

Ts = 100.0e-6       # Período de amostragem (10 kHz)
t_end = 2.0         # Tempo (segundos)
t = np.arange(0, t_end, Ts)
N = len(t)

# ============================================================================
# 2. FUNÇÕES DA FEM TRAPEZOIDAL E DERIVADAS
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

# ============================================================================
# 3. GERAÇÃO DA PLANTA REAL E CORRENTE DC
# ============================================================================
x_true = np.zeros((4, N))
u_input = np.zeros((2, N))
y_meas = np.zeros((2, N))
torque_real, load_real = np.zeros(N), np.zeros(N)
v_abc = np.zeros((3, N))
I_dc = np.zeros(N)

omega_m, theta_e = 0.0, 0.0
I_max = 60.0  

for k in range(N):
    # Injeção de correntes senoidais
    i_al = I_max * np.sin(theta_e)
    i_be = -I_max * np.cos(theta_e)
    
    # Derivadas reais
    omega_e = omega_m * p
    di_al_dt = I_max * omega_e * np.cos(theta_e)
    di_be_dt = I_max * omega_e * np.sin(theta_e)
    
    # Dinâmica mecânica
    Te = Ke * p * (f_alpha(theta_e) * i_al + f_beta(theta_e) * i_be)
    Tl = k_load * (omega_m ** 2)
    d_omega_m = (1/J) * (Te - Tl)
    omega_m += d_omega_m * Ts
    omega_e = omega_m * p
    theta_e = np.mod(theta_e + omega_e * Ts, 2 * np.pi)
    
    # Tensão Real Aplicada
    e_al = Ke * omega_e * f_alpha(theta_e)
    e_be = Ke * omega_e * f_beta(theta_e)
    v_al = R_s * i_al + L_s * di_al_dt + e_al
    v_be = R_s * i_be + L_s * di_be_dt + e_be
    
    x_true[:, k] = [i_al, i_be, omega_e, theta_e]
    u_input[:, k] = [v_al, v_be]
    torque_real[k], load_real[k] = Te, Tl
    
    # Transformada Inversa de Clarke (Va, Vb, Vc)
    v_abc[0, k] = v_al
    v_abc[1, k] = -0.5 * v_al + (np.sqrt(3)/2.0) * v_be
    v_abc[2, k] = -0.5 * v_al - (np.sqrt(3)/2.0) * v_be
    
    # Cálculo da Corrente DC
    P_inst = 1.5 * (v_al * i_al + v_be * i_be)
    I_dc[k] = P_inst / V_bus
    
    # Ruído dos sensores
    y_meas[0, k] = i_al + np.random.normal(0, 0.5)
    y_meas[1, k] = i_be + np.random.normal(0, 0.5)

# ============================================================================
# 4. OBSERVADOR POR MODOS DESLIZANTES (SMO)
# ============================================================================
# Estados do observador
i_hat = np.zeros((2, N))      # Correntes estimadas
e_hat = np.zeros((2, N))      # FEM filtrada
theta_est = np.zeros(N)       # Posição estimada (compensada)
omega_est = np.zeros(N)       # Velocidade estimada
torque_est_raw = np.zeros(N)
torque_est_lpf = np.zeros(N)

# Inicializa correntes
i_hat[:, 0] = y_meas[:, 0]

# --- Parâmetros de Sintonia do SMO ---
K_smo = 40.0                  # Ganho de chaveamento (K > max|e|)
fc_fem = 800.0                # Freq. de corte do LPF da FEM (Hz)
alpha_e = (2 * np.pi * fc_fem) * Ts

fc_spd = 50.0                 # Freq. de corte do LPF da Velocidade (Hz)
alpha_w = (2 * np.pi * fc_spd) * Ts

alpha_lpf_tq = 0.05           # Filtro para o torque estimado

z_al, z_be = 0.0, 0.0         # Sinais descontínuos (Termos corretivos)
th_raw_prev = 0.0             # Posição crua anterior (para cálculo de vel.)

for k in range(1, N):
    v_al, v_be = u_input[0, k-1], u_input[1, k-1]
    
    # 1. Equação do Observador (Predição de Corrente)
    i_al_pred = i_hat[0, k-1] + Ts * (-(R_s/L_s)*i_hat[0, k-1] + (v_al - z_al)/L_s)
    i_be_pred = i_hat[1, k-1] + Ts * (-(R_s/L_s)*i_hat[1, k-1] + (v_be - z_be)/L_s)
    
    i_hat[0, k] = i_al_pred
    i_hat[1, k] = i_be_pred
    
    # 2. Erro de Corrente
    err_al = i_al_pred - y_meas[0, k]
    err_be = i_be_pred - y_meas[1, k]
    
    # 3. Superfície de Deslizamento (Função Sigmoide para evitar chattering extremo numérico)
    # Equivalente ao np.sign(), porém contínuo: saturação suave.
    z_al = K_smo * np.tanh(5.0 * err_al)
    z_be = K_smo * np.tanh(5.0 * err_be)
    
    # 4. Filtro Passa-Baixas (Extração da FEM equivalente)
    e_hat[0, k] = e_hat[0, k-1] + alpha_e * (z_al - e_hat[0, k-1])
    e_hat[1, k] = e_hat[1, k-1] + alpha_e * (z_be - e_hat[1, k-1])
    
    # 5. Cálculo do Ângulo Cru (Arco-tangente)
    th_raw = np.arctan2(e_hat[0, k], -e_hat[1, k])
    
    # 6. Derivada para extrair Velocidade e aplicação de LPF
    dth = th_raw - th_raw_prev
    # Tratamento da virada de ângulo (unwrap)
    if dth > np.pi: dth -= 2*np.pi
    elif dth < -np.pi: dth += 2*np.pi
    
    w_raw = dth / Ts
    omega_est[k] = omega_est[k-1] + alpha_w * (w_raw - omega_est[k-1])
    th_raw_prev = th_raw
    
    # 7. Compensação de Atraso de Fase
    # Atraso induzido pelo LPF da FEM: arctan(w / w_c)
    delta_th = np.arctan(omega_est[k] / (2 * np.pi * fc_fem))
    theta_est[k] = np.mod(th_raw + delta_th, 2*np.pi)
    
    # 8. Estimativa de Torque
    t_bruto = Ke * p * (f_alpha(theta_est[k]) * i_hat[0, k] + f_beta(theta_est[k]) * i_hat[1, k])
    torque_est_raw[k] = t_bruto
    torque_est_lpf[k] = alpha_lpf_tq * t_bruto + (1 - alpha_lpf_tq) * torque_est_lpf[k-1]

# ============================================================================
# 5. GERANDO E SALVANDO OS JPEGS
# ============================================================================
tempo_zoom = 0.05
zoom_start = max(0, t_end - tempo_zoom)

# --- Plot 1: Dinâmica Mecânica ---
plt.figure(figsize=(10, 8))
plt.subplot(2, 1, 1)
plt.plot(t, x_true[2, :]/p * (30/np.pi), 'b--', linewidth=2, label='Real')
plt.plot(t, omega_est/p * (30/np.pi), 'r-', alpha=0.8, label='Estimado (SMO)')
plt.ylabel('RPM')
plt.title('Velocidade Estimada vs Real')
plt.legend()
plt.grid(True)

plt.subplot(2, 1, 2)
plt.plot(t, torque_real, 'b--', linewidth=2, label='Real')
plt.plot(t, torque_est_lpf, 'r-', linewidth=2, label='Estimado LPF (SMO)')
plt.plot(t, load_real, 'k-', label='Carga')
plt.ylabel('Torque (N.m)')
plt.xlabel('Tempo (s)')
plt.title('Torque Eletromagnético')
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.savefig('SMO.jpg', dpi=150)
plt.show()

# --- Plot 2: Detalhe das Correntes (Zoom) ---
plt.figure(figsize=(10, 5))
plt.plot(t, x_true[0, :], 'b-', alpha=0.4, linewidth=3, label='$i_\\alpha$ Real')
plt.plot(t, i_hat[0, :], 'r--', linewidth=2, label='$i_\\alpha$ Est. SMO')
plt.plot(t, x_true[1, :], 'g-', alpha=0.4, linewidth=3, label='$i_\\beta$ Real')
plt.plot(t, i_hat[1, :], 'k--', linewidth=2, label='$i_\\beta$ Est. SMO')
plt.xlim(zoom_start, t_end)
plt.ylabel('Corrente (A)')
plt.xlabel('Tempo (s)')
plt.title(f'Zoom nas Correntes $\\alpha\\beta$ SMO ({zoom_start:.3f}s a {t_end:.3f}s)')
plt.legend(loc='upper right')
plt.grid(True)
plt.tight_layout()
plt.show()

# --- Plot 3: Tensões e Referencial ---
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
ax1.plot(t, v_abc[0, :], 'r', label='$V_a$')
ax1.plot(t, v_abc[1, :], 'g', label='$V_b$')
ax1.plot(t, v_abc[2, :], 'b', label='$V_c$')
ax1.set_ylabel('Tensão (V)')
ax1.set_title('Tensões Trifásicas Aplicadas')
ax1.grid(True)
ax1.legend(loc='upper right')

ax2.plot(t, u_input[0, :], 'purple', label='$V_\\alpha$')
ax2.plot(t, u_input[1, :], 'orange', label='$V_\\beta$')
ax2.set_ylabel('Tensão (V)')
ax2.set_title('Referencial Alpha-Beta (Entrada do SMO)')
ax2.grid(True)
ax2.legend(loc='upper right')
plt.xlim(zoom_start, t_end)
plt.xlabel('Tempo (s)')
plt.tight_layout()
plt.show()

# --- Plot 4: Corrente do Inversor (Barramento DC) ---
plt.figure(figsize=(10, 6))

plt.subplot(2, 1, 1)
plt.plot(t, I_dc, 'b-')
plt.ylabel('Corrente (A)')
plt.title(f'Corrente Puxada da Bateria (Vbus = {round(V_bus,2)}V)')
plt.grid(True)

plt.subplot(2, 1, 2)
plt.plot(t, I_dc, 'b-', label='$I_{dc}$ (Zoom)')
plt.xlim(zoom_start, t_end)
zoom_data = I_dc[int(zoom_start/Ts):]
if len(zoom_data) > 0:
    plt.ylim(min(zoom_data)*0.9, max(zoom_data)*1.1)
plt.ylabel('Corrente (A)')
plt.xlabel('Tempo (s)')
plt.title('Ripple da Corrente no Barramento DC')
plt.grid(True)
plt.legend()
plt.tight_layout()
plt.show()

print("Simulação do SMO concluída!")