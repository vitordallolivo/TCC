import numpy as np
import matplotlib.pyplot as plt

# ============================================================================
# 1. CLASSE DA PLANTA (MOTOR BLDC)
# ============================================================================
class BLDCMotor:
    def __init__(self, R, L, p, Ke, J, k_load, Ts):
        self.R, self.L, self.p, self.Ke = R, L, p, Ke
        self.J, self.k_load, self.Ts = J, k_load, Ts
        self.omega_m = 0.0
        self.theta_e = 0.0
        
    def f_trap(self, theta):
        """ Função trapezoidal da FEM (Normalizada) """
        th = np.mod(theta, 2 * np.pi)
        if th < np.pi/6: return (6/np.pi) * th
        elif th < 5*np.pi/6: return 1.0
        elif th < 7*np.pi/6: return -(6/np.pi) * (th - np.pi)
        elif th < 11*np.pi/6: return -1.0
        else: return (6/np.pi) * (th - 2*np.pi)

    def get_f_ab(self, theta):
        """ Transformada de Clarke da FEM Trapezoidal """
        f_al = self.f_trap(theta)
        f_be = (1/np.sqrt(3)) * (self.f_trap(theta - 2*np.pi/3) - self.f_trap(theta - 4*np.pi/3))
        return f_al, f_be

    def step(self, I_max):
        # Correntes senoidais (Simulando controle de corrente)
        i_al = I_max * np.sin(self.theta_e)
        i_be = -I_max * np.cos(self.theta_e)
        
        f_al, f_be = self.get_f_ab(self.theta_e)
        
        # Dinâmica de Torque e Velocidade
        Te = self.Ke * self.p * (f_al * i_al + f_be * i_be)
        Tl = self.k_load * (self.omega_m ** 2)
        
        self.omega_m += (1/self.J) * (Te - Tl) * self.Ts
        self.theta_e = np.mod(self.theta_e + (self.omega_m * self.p) * self.Ts, 2 * np.pi)
        
        # Tensões Reais (Entrada para os observadores)
        v_al = self.R * i_al + self.Ke * (self.omega_m * self.p) * f_al
        v_be = self.R * i_be + self.Ke * (self.omega_m * self.p) * f_be
        
        return i_al, i_be, v_al, v_be, Te, self.omega_m, self.theta_e

# ============================================================================
# 2. DERIVADA DA FEM (PARA A JACOBIANA DO EKF)
# ============================================================================
def get_df_ab(theta):
    def d_trap(th):
        th = np.mod(th, 2 * np.pi)
        if th < np.pi/6 or th > 11*np.pi/6: return 6/np.pi
        elif th < 5*np.pi/6: return 0.0
        elif th < 7*np.pi/6: return -6/np.pi
        else: return 0.0
    df_al = d_trap(theta)
    df_be = (1/np.sqrt(3)) * (d_trap(theta - 2*np.pi/3) - d_trap(theta - 4*np.pi/3))
    return df_al, df_be

# ============================================================================
# 3. SETUP DA SIMULAÇÃO
# ============================================================================
Ts = 100e-6
t = np.arange(0, 10.0, Ts)
N = len(t)
motor_params = {'R': 0.05, 'L': 0.0005, 'p': 12, 'Ke': 0.00513, 'J': 0.001, 'k_load': 5e-5}
motor = BLDCMotor(**motor_params, Ts=Ts)

plant_data = {'i_ab': np.zeros((2, N)), 'v_ab': np.zeros((2, N)), 
              'rpm': np.zeros(N), 'Te': np.zeros(N),
              'Tl': np.zeros(N),
              'Teste':np.zeros(N)
              }

# --- Novo Perfil de Corrente ---
I_profile = np.zeros(N)
for k in range(N):
    curr = t[k]
    # Troca de faixa a cada 2 segundos
    if curr < 2.0:
        target = 15.0
    elif curr < 4.0:
        target = 40.0
    elif curr < 6.0:
        target = 60.0
    elif curr < 8.0:
        target = 25.0
    else:
        target = 10.0
    
    # Suavização simples para não explodir o observador no degrau
    if k == 0:
        I_profile[k] = target
    else:
        # Filtro passa-baixa na referência para criar uma rampa suave
        I_profile[k] = I_profile[k-1] + 0.01 * (target - I_profile[k-1])


# --- Loop da Planta Atualizado ---
for k in range(N):
    # Passamos o valor do perfil para o step
    i_al, i_be, v_al, v_be, Te, om_m, th_e = motor.step(I_profile[k])
    
    plant_data['i_ab'][:, k] = [i_al, i_be]
    plant_data['v_ab'][:, k] = [v_al, v_be]
    plant_data['rpm'][k] = om_m * (30/np.pi)
    plant_data['Te'][k] = Te
    plant_data['Tl'][k] = motor.k_load * (om_m ** 2)


# ============================================================================
# 4. OBSERVADOR EKF
# ============================================================================
def run_EKF(d, p_m):
    R, L, Ke, p, J = p_m['R'], p_m['L'], p_m['Ke'], p_m['p'], p_m['J']
    x = np.zeros((4, N))  # [i_al, i_be, we, th]
    Te_est = np.zeros(N)
    H = np.array([[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0]])

    P = np.diag([0.1, 0.1, 1000.0, 0.1])
    Q = np.diag([1e-2, 1e-2, 0.016, 100e-6]) 
    R_cov = np.eye(2) * 0.01
    

    for k in range(1, N):
        v = d['v_ab'][:, k-1]
        i_al, i_be, we, th = x[:, k-1]
        f_al, f_be = motor.get_f_ab(th)
        
        Te_prev = Ke * p * (f_al * i_al + f_be * i_be) # Torque calculado no passo anterior
        Tl_prev = motor.k_load * ((we/p)**2)

        # Predição de estado
        x_p = x[:, k-1] + Ts * np.array([
            -(R/L)*i_al - (Ke/L)*we*f_al + v[0]/L,
            -(R/L)*i_be - (Ke/L)*we*f_be + v[1]/L,
            (p/J) * (Te_prev - Tl_prev), 
            we
        ])
        
        # Matriz Jacobiana F
        df_al, df_be = get_df_ab(th)
        F = np.eye(4) + Ts * np.array([
            [-R/L, 0, -Ke*f_al/L, -Ke*we*df_al/L],
            [0, -R/L, -Ke*f_be/L, -Ke*we*df_be/L],
            [(p**2 * Ke * f_al)/J, (p**2 * Ke * f_be)/J, -2*motor.k_load*we/(J*p), 0], 
            [0, 0, 1, 0]
        ])
        
        # Correção
        Pp = F @ P @ F.T + Q
        K = Pp @ H.T @ np.linalg.inv(H @ Pp @ H.T + R_cov)
        x[:, k] = x_p + K @ (d['i_ab'][:, k] - x_p[:2])
        x[3, k] = np.mod(x[3, k], 2*np.pi)
        P = (np.eye(4) - K @ H) @ Pp
        
        # Torque Estimado EKF
        fa_h, fb_h = motor.get_f_ab(x[3, k])
        Te_est[k] = Ke * p * (fa_h * x[0, k] + fb_h * x[1, k])
        
    return (x[2,:]/p) * (30/np.pi), Te_est  # Retorna RPM e Torque

# ============================================================================
# 5. OBSERVADOR SMO
# ============================================================================
def run_SMO(d, p_m):
    R, L, Ke, p = p_m['R'], p_m['L'], p_m['Ke'], p_m['p']
    i_h, e_h = np.zeros((2, N)), np.zeros((2, N))
    rpm_est, Te_est = np.zeros(N), np.zeros(N)
    
    # Constante de Torque (Kt) para o referencial dq
    # Para BLDC, Kt aproximado no referencial dq é 1.5 * Ke * p
    Kt = 1.5 * Ke * p 
    
    K_smo = 40.0
    fc_fem = 800.0 # Hz (Com fc alta, o phi_delay é quase zero)
    alpha_e = (2 * np.pi * fc_fem) * Ts / (1 + (2 * np.pi * fc_fem) * Ts)
    alpha_w = 0.001
    alpha_tq = 0.02 
    
    th_p = 0.0
    z_al, z_be = 0.0, 0.0
    
    for k in range(1, N):
        v = d['v_ab'][:, k-1]
        i_real = d['i_ab'][:, k]
        
        # 1. Estimador de Corrente (Alpha-Beta)
        i_h[0, k] = i_h[0, k-1] + Ts * (-(R/L)*i_h[0, k-1] + (v[0] - e_h[0, k-1] - z_al)/L)
        i_h[1, k] = i_h[1, k-1] + Ts * (-(R/L)*i_h[1, k-1] + (v[1] - e_h[1, k-1] - z_be)/L)
        
        # 2. Sliding Mode (tanh para reduzir chattering)
        z_al = K_smo * np.tanh(2.0 * (i_h[0, k] - i_real[0]))
        z_be = K_smo * np.tanh(2.0 * (i_h[1, k] - i_real[1]))
        
        # 3. Filtro da Back-EMF
        e_h[0, k] = e_h[0, k-1] + alpha_e * (z_al - e_h[0, k-1])
        e_h[1, k] = e_h[1, k-1] + alpha_e * (z_be - e_h[1, k-1])
        
        # 4. Ângulo e Velocidade
        th_raw = np.arctan2(-e_h[0, k], e_h[1, k])
        dth = np.mod(th_raw - th_p + np.pi, 2*np.pi) - np.pi
        w_el_est = dth / Ts
        
        rpm_inst = (w_el_est / p) * (30 / np.pi)
        rpm_est[k] = rpm_est[k-1] + alpha_w * (rpm_inst - rpm_est[k-1])
        th_p = th_raw
        
        # --- 5. TRANSFORMADA DE PARK (O PONTO CHAVE) ---
        # Compensamos a fase antes da transformada para alinhar os eixos
        phi_delay = np.arctan((rpm_est[k]*p*np.pi/30) / (2 * np.pi * fc_fem))
        th_park = np.mod(th_raw + phi_delay, 2 * np.pi)
        
        # Matriz de Park: i_dq = P(theta) * i_alphabeta
        # i_d = i_al * cos(th) + i_be * sin(th)
        # i_q = -i_al * sin(th) + i_be * cos(th)
        i_d =  i_real[0] * np.cos(th_park) + i_real[1] * np.sin(th_park)
        i_q = -i_real[0] * np.sin(th_park) + i_real[1] * np.cos(th_park)
        
        # 6. Torque Estimado via Eixo Q
        # Em controle vetorial ideal, i_d deve ser 0. 
        # O torque eletromagnético segue i_q.
        t_dq = Kt * i_q
        
        # Filtro de torque
        Te_est[k] = alpha_tq * t_dq + (1 - alpha_tq) * Te_est[k-1]
        
    return rpm_est, Te_est
# ============================================================================
# 6. PLOTS COMPARATIVOS
# ============================================================================
rpm_ekf, tq_ekf = run_EKF(plant_data, motor_params)
rpm_smo, tq_smo = run_SMO(plant_data, motor_params)
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10), sharex=True)

# --- Subplot 1: Velocidade ---
ax1.plot(t, plant_data['rpm'], 'k', label='RPM Real', linewidth=2)
ax1.plot(t, abs(rpm_ekf), 'r--', label='RPM EKF', alpha=0.8)
ax1.plot(t, rpm_smo, 'b-', label='RPM SMO', alpha=0.8)
ax1.set_ylabel("Velocidade [RPM]")
ax1.legend(loc='upper right')
ax1.grid(True, alpha=0.3)

# --- Subplot 2: Torques ---
# Torque Eletromagnético Real (Preto com ripple)
ax2.plot(t, plant_data['Te'], 'k', label='Te Real (Eletromagnético)', alpha =0.6, linewidth=1)

# Torque de Carga Mecânica (Verde - Linha suave que representa a carga)
ax2.plot(t, plant_data['Tl'], 'g', label='Tl Real (Carga Mecânica)', linewidth=2)

# Estimativas
ax2.plot(t, abs(tq_ekf), 'r--', label='Te Estimado EKF', alpha=0.9)
ax2.plot(t, abs(tq_smo), 'b-', label='Te Estimado SMO', alpha=0.9)

ax2.set_ylabel("Torque [N.m]")
ax2.set_xlabel("Tempo [s]")
ax2.legend(loc='upper right')
ax2.grid(True, alpha=0.3)

# Zoom no final para ver o regime permanente
plt.tight_layout()
plt.show()