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
t_end = 1.0         # Tempo (segundos)
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

def df_a(theta):
    th = np.mod(theta, 2 * np.pi)
    if th < np.pi/6: return 6/np.pi
    elif th < 5*np.pi/6: return 0.0
    elif th < 7*np.pi/6: return -6/np.pi
    elif th < 11*np.pi/6: return 0.0
    else: return 6/np.pi

def f_alpha(theta): return f_a(theta)
def f_beta(theta): return (1/np.sqrt(3)) * (f_a(theta - 2*np.pi/3) - f_a(theta - 4*np.pi/3))
def df_alpha(theta): return df_a(theta)
def df_beta(theta): return (1/np.sqrt(3)) * (df_a(theta - 2*np.pi/3) - df_a(theta - 4*np.pi/3))

# ============================================================================
# 3. GERAÇÃO DA PLANTA REAL, TENSÕES E CORRENTE DC
# ============================================================================
x_true = np.zeros((4, N))
u_input = np.zeros((2, N))
y_meas = np.zeros((2, N))
torque_real, load_real = np.zeros(N), np.zeros(N)
v_abc = np.zeros((3, N))
I_dc = np.zeros(N) # Array para a corrente do barramento

omega_m, theta_e = 0.0, 0.0
I_max = 60.0  

for k in range(N):
    # Injeção de correntes senoidais para a planta
    i_al = I_max * np.sin(theta_e)
    i_be = -I_max * np.cos(theta_e)
    
    # Derivadas exatas para simular a tensão necessária
    di_al_dt = I_max * (omega_m * p) * np.cos(theta_e)
    di_be_dt = I_max * (omega_m * p) * np.sin(theta_e)
    
    # Torque Eletromagnético e Carga Mecânica
    Te = Ke * p * (f_alpha(theta_e) * i_al + f_beta(theta_e) * i_be)
    Tl = k_load * (omega_m ** 2)
    
    # Integração Mecânica
    d_omega_m = (1/J) * (Te - Tl)
    omega_m += d_omega_m * Ts
    omega_e = omega_m * p
    theta_e = np.mod(theta_e + omega_e * Ts, 2 * np.pi)
    
    # TENSÃO APLICADA EXATA
    e_al = Ke * omega_e * f_alpha(theta_e)
    e_be = Ke * omega_e * f_beta(theta_e)
    v_al = R_s * i_al + L_s * di_al_dt + e_al
    v_be = R_s * i_be + L_s * di_be_dt + e_be
    
    # Salva os estados
    x_true[:, k] = [i_al, i_be, omega_e, theta_e]
    u_input[:, k] = [v_al, v_be]
    torque_real[k], load_real[k] = Te, Tl
    
    # Transformada Inversa de Clarke para obter Va, Vb, Vc
    v_abc[0, k] = v_al
    v_abc[1, k] = -0.5 * v_al + (np.sqrt(3)/2.0) * v_be
    v_abc[2, k] = -0.5 * v_al - (np.sqrt(3)/2.0) * v_be
    
    # Cálculo da Corrente DC (Potência do Barramento = Potência do Motor)
    P_inst = 1.5 * (v_al * i_al + v_be * i_be)
    I_dc[k] = P_inst / V_bus
    
    # Adiciona ruído aos sensores
    y_meas[0, k] = i_al + np.random.normal(0, 0.5)
    y_meas[1, k] = i_be + np.random.normal(0, 0.5)

# ============================================================================
# 4. FILTRO DE KALMAN ESTENDIDO E TORQUE ESTIMADO
# ============================================================================
x_hat = np.zeros((4, N))
torque_est_raw = np.zeros(N)
torque_est_lpf = np.zeros(N)

# Inicializa o filtro copiando a primeira medição
x_hat[0, 0] = y_meas[0, 0]
x_hat[1, 0] = y_meas[1, 0]

# Covariância inicial
P_ekf = np.diag([0.1, 0.1, 100.0, 0.1])

# Matrizes Q e R balanceadas
Q = np.diag([1e-3, 1e-3, 10.0, 1e-6]) 
R_mat = np.diag([0.5, 0.5])
H = np.array([[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0]])

alpha_lpf = 0.05 # Constante do filtro do torque

for k in range(1, N):
    v_al, v_be = u_input[0, k-1], u_input[1, k-1]
    i_al_prev, i_be_prev, we_prev, th_prev = x_hat[:, k-1]
    
    # 1. Predição
    f_al_val, f_be_val = f_alpha(th_prev), f_beta(th_prev)
    i_al_pred = i_al_prev + Ts * (-(R_s/L_s)*i_al_prev - (Ke/L_s)*we_prev*f_al_val + (1/L_s)*v_al)
    i_be_pred = i_be_prev + Ts * (-(R_s/L_s)*i_be_prev - (Ke/L_s)*we_prev*f_be_val + (1/L_s)*v_be)
    we_pred   = we_prev
    th_pred   = np.mod(th_prev + Ts * we_prev, 2*np.pi)
    x_pred = np.array([i_al_pred, i_be_pred, we_pred, th_pred])
    
    # 2. Jacobiana F
    df_al_val, df_be_val = df_alpha(th_prev), df_beta(th_prev)
    F = np.array([
        [1 - R_s*Ts/L_s, 0, -Ke*Ts*f_al_val/L_s, -Ke*Ts*we_prev*df_al_val/L_s],
        [0, 1 - R_s*Ts/L_s, -Ke*Ts*f_be_val/L_s, -Ke*Ts*we_prev*df_be_val/L_s],
        [0, 0, 1, 0],
        [0, 0, Ts, 1]
    ])
    P_pred = F @ P_ekf @ F.T + Q
    
    # 3. Correção
    S = H @ P_pred @ H.T + R_mat
    K_gain = P_pred @ H.T @ np.linalg.inv(S)
    y = np.array([y_meas[0, k], y_meas[1, k]])
    
    x_hat[:, k] = x_pred + K_gain @ (y - (H @ x_pred))
    x_hat[3, k] = np.mod(x_hat[3, k], 2*np.pi)
    P_ekf = (np.eye(4) - K_gain @ H) @ P_pred
    
    # 4. Cálculo do Torque Estimado (Bruto e Filtrado)
    t_bruto = Ke * p * (f_alpha(x_hat[3, k]) * x_hat[0, k] + f_beta(x_hat[3, k]) * x_hat[1, k])
    torque_est_raw[k] = t_bruto
    torque_est_lpf[k] = alpha_lpf * t_bruto + (1 - alpha_lpf) * torque_est_lpf[k-1]

# ============================================================================
# 5. GERANDO E SALVANDO OS JPEGS
# ============================================================================
tempo_zoom = 0.05
zoom_start = max(0, t_end - tempo_zoom)

# --- Plot 1: Dinâmica Mecânica ---
plt.figure(figsize=(10, 8))
plt.subplot(2, 1, 1)
plt.plot(t, x_true[2, :]/p * (30/np.pi), 'b--', linewidth=2, label='Real')
plt.plot(t, x_hat[2, :]/p * (30/np.pi), 'r-', alpha=0.8, label='Estimado')
plt.ylabel('RPM')
plt.title('Velocidade')
plt.legend()
plt.grid(True)

plt.subplot(2, 1, 2)
plt.plot(t, torque_real, 'b--', linewidth=2, label='Real')
plt.plot(t, torque_est_lpf, 'r-', linewidth=2, label='Estimado (LPF)')
plt.plot(t, load_real, 'k-', label='Carga')
plt.ylabel('Torque (N.m)')
plt.xlabel('Tempo (s)')
plt.title('Torque Eletromagnético')
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.savefig('EKF.jpg', dpi=150)
plt.show()

# --- Plot 2: Detalhe das Correntes (Zoom) ---
plt.figure(figsize=(10, 5))
plt.plot(t, x_true[0, :], 'b-', alpha=0.4, linewidth=3, label='$i_\\alpha$ Real')
plt.plot(t, x_hat[0, :], 'r--', linewidth=2, label='$i_\\alpha$ Est.')
plt.plot(t, x_true[1, :], 'g-', alpha=0.4, linewidth=3, label='$i_\\beta$ Real')
plt.plot(t, x_hat[1, :], 'k--', linewidth=2, label='$i_\\beta$ Est.')
plt.xlim(zoom_start, t_end)
plt.ylabel('Corrente (A)')
plt.xlabel('Tempo (s)')
plt.title(f'Zoom nas Correntes $\\alpha\\beta$ ({zoom_start:.3f}s a {t_end:.3f}s)')
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
ax1.set_title('Tensões Trifásicas')
ax1.grid(True)
ax1.legend(loc='upper right')

ax2.plot(t, u_input[0, :], 'purple', label='$V_\\alpha$')
ax2.plot(t, u_input[1, :], 'orange', label='$V_\\beta$')
ax2.set_ylabel('Tensão (V)')
ax2.set_title('Referencial Alpha-Beta (Entrada do EKF)')
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
# Ajusta o eixo Y dinamicamente para o zoom
zoom_data = I_dc[int(zoom_start/Ts):]
if len(zoom_data) > 0:
    plt.ylim(min(zoom_data)*0.9, max(zoom_data)*1.1)
plt.ylabel('Corrente (A)')
plt.xlabel('Tempo (s)')
plt.title('Ripple da Corrente no Barramento')
plt.grid(True)
plt.legend()
plt.tight_layout()
plt.show()

print("Simulação concluída!")