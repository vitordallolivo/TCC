import numpy as np
import matplotlib.pyplot as plt
import scipy.signal as signal

# Flags de configuração dos blocos de visualização
CONFIG_PLOT_SMO = True
CONFIG_PLOT_SMO_BMF = True
CONFIG_PLOT_PROPELLER_COEFFICIENT = True
CONFIG_PLOT_THETA_COMPLETE = True

# ============================================================================
# 1. PARÂMETROS DO MOTOR, CARGA E BATERIA
# ============================================================================
R_s = 0.000_05          # Ohms
L_s = 0.000_5         # Henrys
P = 24               # Polos
p = P / 2            # Pares de polos
Kv = 310.0           # RPM/V
J = 0.0001            # kg.m^2
k_load = 25.1e-6      # Coeficiente da hélice
V_bus = 30.0         # 6S LiPo

# Constantes de FEM
Ke_mec = 60.0 / (2 * np.pi * Kv)
Ke = Ke_mec / p     

# Janela de tempo estendida com alta taxa de amostragem para o PWM
Ts = 10e-6          # 10 microsegundos (100 kHz)
t_end = 5.0         # Simulação por 6 segundos completos
t = np.arange(0, t_end, Ts)
N = len(t)

# ============================================================================
# 2. FUNÇÕES AUXILIARES
# ============================================================================
def f_a(theta):
    th = np.mod(theta, 2 * np.pi)
    if th < np.pi/6: return (6/np.pi) * th
    elif th < 5*np.pi/6: return 1.0
    elif th < 7*np.pi/6: return -(6/np.pi) * (th - np.pi)
    elif th < 11*np.pi/6: return -1.0
    else: return (6/np.pi) * (th - 2*np.pi)

def sat(x, epsilon=0.6):
    return np.clip(x / epsilon, -1.0, 1.0)

# ============================================================================
# 3. SIMULAÇÃO DA PLANTA REAL (AGORA COM DINÂMICA DE INDUTÂNCIA REAL)
# ============================================================================
x_true = np.zeros((4, N))
u_input = np.zeros((2, N))
y_meas = np.zeros((2, N))
torque_real = np.zeros(N)
load_real = np.zeros(N)
e_real = np.zeros((2, N))

ia_log = np.zeros(N)
ib_log = np.zeros(N)
ic_log = np.zeros(N)

# Estados elétricos dinâmicos reais da planta (correntes integradas passo a passo)
i_a_real, i_b_real, i_c_real = 0.0, 0.0, 0.0

# Perfil de escada com múltiplos patamares de RPM
RPM_target_profile = np.zeros(N)
for k in range(N):
    curr_t = t[k]
    if curr_t < 0.2:    target = 0.0
    elif curr_t < 1.2:  target = 1200.0  
    elif curr_t < 2.4:  target = 3000.0  
    elif curr_t < 3.6:  target = 1500.0  
    elif curr_t < 4.8:  target = 910.0  
    else:               target = 400.0   
    RPM_target_profile[k] = target

# Inicialização de estados mecânicos
omega_m, theta_e = 0.0, 0.0

# Ganhos do Controlador PI de Velocidade (Gera comando de tensão)
Kp_w = 20.1   
Ki_w = 1.5    
integral_w = 0.0
V_max = V_bus 

f_pwm = 8_000.0  # 20 kHz para chaveamento realístico
duty_cycle = 0.0

for k in range(N):
    # Malha Externa de Velocidade
    rpm_real = omega_m * (30.0 / np.pi)
    erro_rpm = RPM_target_profile[k] - rpm_real
    
    integral_w += erro_rpm * Ts
    integral_w = np.clip(integral_w, -V_max/Ki_w, V_max/Ki_w) 
    
    V_cmd = Kp_w * erro_rpm + Ki_w * integral_w
    V_cmd = np.clip(V_cmd, 0.0, V_max)
    duty_cycle = V_cmd / V_bus
    
    # Gerador de PWM interno à planta
    pwm_state = 1.0 if (np.mod(t[k], 1.0/f_pwm) < duty_cycle / f_pwm) else 0.0
    V_active = V_bus * pwm_state

    # Back-EMF Real das Fases (Planta)
    e_a = Ke_mec * omega_m * f_a(theta_e)
    e_b = Ke_mec * omega_m * f_a(theta_e - 2.0 * np.pi / 3.0)
    e_c = Ke_mec * omega_m * f_a(theta_e - 4.0 * np.pi / 3.0)
    
    e_real[0, k] = e_a
    e_real[1, k] = (1.0 / np.sqrt(3.0)) * (e_b - e_c)

    # Inversor Real de 6 Passos aplicando as equações diferenciais elétricas (di/dt)
    th = np.mod(theta_e, 2 * np.pi)
    
    if th >= np.pi/6 and th < np.pi/2:          # Passo 1: A+, B-, C Flutuante
        v_a, v_b = V_active, 0.0
        di_a = (1.0 / (2.0 * L_s)) * ((v_a - v_b) - 2.0 * R_s * i_a_real - (e_a - e_b))
        i_a_real += di_a * Ts
        i_b_real = -i_a_real
        i_c_real = 0.0
        v_c = R_s * i_a_real + L_s * di_a + e_c 
    elif th >= np.pi/2 and th < 5*np.pi/6:      # Passo 2: A+, C-, B Flutuante
        v_a, v_c = V_active, 0.0
        di_a = (1.0 / (2.0 * L_s)) * ((v_a - v_c) - 2.0 * R_s * i_a_real - (e_a - e_c))
        i_a_real += di_a * Ts
        i_c_real = -i_a_real
        i_b_real = 0.0
        v_b = R_s * i_a_real + L_s * di_a + e_b
    elif th >= 5*np.pi/6 and th < 7*np.pi/6:    # Passo 3: B+, C-, A Flutuante
        v_b, v_c = V_active, 0.0
        di_b = (1.0 / (2.0 * L_s)) * ((v_b - v_c) - 2.0 * R_s * i_b_real - (e_b - e_c))
        i_b_real += di_b * Ts
        i_c_real = -i_b_real
        i_a_real = 0.0
        v_a = R_s * i_b_real + L_s * di_b + e_a
    elif th >= 7*np.pi/6 and th < 3*np.pi/2:    # Passo 4: B+, A-, C Flutuante
        v_b, v_a = V_active, 0.0
        di_b = (1.0 / (2.0 * L_s)) * ((v_b - v_a) - 2.0 * R_s * i_b_real - (e_b - e_a))
        i_b_real += di_b * Ts
        i_a_real = -i_b_real
        i_c_real = 0.0
        v_c = R_s * i_b_real + L_s * di_b + e_c
    elif th >= 3*np.pi/2 and th < 11*np.pi/6:   # Passo 5: C+, A-, B Flutuante
        v_c, v_a = V_active, 0.0
        di_c = (1.0 / (2.0 * L_s)) * ((v_c - v_a) - 2.0 * R_s * i_c_real - (e_c - e_a))
        i_c_real += di_c * Ts
        i_a_real = -i_c_real
        i_b_real = 0.0
        v_b = R_s * i_c_real + L_s * di_c + e_b
    else:                                       # Passo 6: C+, B-, A Flutuante
        v_c, v_b = V_active, 0.0
        di_c = (1.0 / (2.0 * L_s)) * ((v_c - v_b) - 2.0 * R_s * i_c_real - (e_c - e_b))
        i_c_real += di_c * Ts
        i_b_real = -i_c_real
        i_a_real = 0.0
        v_a = R_s * i_c_real + L_s * di_c + e_a

    ia_log[k], ib_log[k], ic_log[k] = i_a_real, i_b_real, i_c_real
    i_al_new, i_be_new = i_a_real, (1.0 / np.sqrt(3.0)) * (i_b_real - i_c_real)

    # Cálculo correto da tensão Fase-Neutro
    v_n = (1.0 / 3.0) * ((v_a + v_b + v_c) - (e_a + e_b + e_c))
    v_an, v_bn, v_cn = v_a - v_n, v_b - v_n, v_c - v_n

    # Dinâmica mecânica acoplada ao torque real gerado
    Te = Ke_mec * (i_a_real * f_a(theta_e) + i_b_real * f_a(theta_e - 2.0*np.pi/3.0) + i_c_real * f_a(theta_e - 4.0*np.pi/3.0))
    Tl = k_load * (omega_m ** 2)
    
    omega_m += (1.0 / J) * (Te - Tl) * Ts
    theta_e = np.mod(theta_e + (omega_m * p) * Ts, 2.0 * np.pi)
    
    x_true[:, k] = [i_al_new, i_be_new, (omega_m * p), theta_e]
    u_input[:, k] = [v_an, (1.0 / np.sqrt(3.0)) * (v_bn - v_cn)]
    y_meas[:, k] = [i_al_new + np.random.normal(0, 0.02), i_be_new + np.random.normal(0, 0.02)]
    torque_real[k], load_real[k] = Te, Tl
# ============================================================================
# 4. IMPLEMENTAÇÃO CORRIGIDA: FESM + PLL EM ALPHA-BETA (MALHA FECHADA REAL)
# ============================================================================
i_hat = np.zeros((2, N))
E_ex = np.zeros((2, N))          # [alpha, beta]
theta_est = np.zeros(N)
omega_est = np.zeros(N)
torque_est_lpf = np.zeros(N)

# Ganhos do Observador de Modos Deslizantes Estendido (FESM)
Ks1 = 80_000.0   
Ks2 = -80_000.0  

Phi_i = 1.0 - (R_s * Ts) / L_s
Gamma_v = Ts / L_s
Gamma_s1 = Ks1 * Ts
g_s2 = Ks2 * Ts

# Filtro passa-baixas para o torque médio da hélice
fc_tq = 5000.0
alpha_lpf_tq = (2 * np.pi * fc_tq * Ts) / (1 + 2 * np.pi * fc_tq * Ts)

# Sintonia do PLL (Segunda Ordem Amortecida)
w_n = 2 * np.pi * 500.0  
zeta = 0.707             
Kp_pll = 2 * zeta * w_n
Ki_pll = w_n**2
integral_pll = 0.0
th_est_prev = 0.0 

# Filtro de velocidade mecânica
fc_w_pll = 15.0  
alpha_w_pll = (2 * np.pi * fc_w_pll * Ts) / (1 + 2 * np.pi * fc_w_pll * Ts)
omega_m_filt_prev = 0.0

for k in range(1, N):
    # 1. Erros de estimação de corrente
    err_al = y_meas[0, k] - i_hat[0, k-1]
    err_be = y_meas[1, k] - i_hat[1, k-1]
    
    s_al = sat(err_al, epsilon=0.6)
    s_be = sat(err_be, epsilon=0.6)
    
    # 2. Atualização preditiva de corrente
    i_hat[0, k] = Phi_i * i_hat[0, k-1] + Gamma_v * (u_input[0, k-1] - E_ex[0, k-1]) + Gamma_s1 * s_al
    i_hat[1, k] = Phi_i * i_hat[1, k-1] + Gamma_v * (u_input[1, k-1] - E_ex[1, k-1]) + Gamma_s1 * s_be
    
    # 3. Componentes brutas da Back-EMF Estendida
    E_ex_raw_al = E_ex[0, k-1] + g_s2 * s_al
    E_ex_raw_be = E_ex[1, k-1] + g_s2 * s_be
    E_ex[:, k] = [E_ex_raw_al, E_ex_raw_be]
    
    # 4. Detector de Erro Angular por Produto Cruzado Corrigido
    amp_E = np.sqrt(E_ex_raw_al**2 + E_ex_raw_be**2)
    if amp_E > 0.5:
        # CORREÇÃO DA IDENTIDADE: sin(theta - th_est) = sin(theta)*cos(th_est) - cos(theta)*sin(th_est)
        # Sendo E_beta proporcional a sin(theta) e E_alpha a cos(theta):
        erro_fase_pll = (E_ex_raw_be * np.cos(th_est_prev) - E_ex_raw_al * np.sin(th_est_prev)) / amp_E
    else:
        erro_fase_pll = 0.0
        
    # 5. Filtro de Loop do PLL com Proteção Anti-Windup Interna
    integral_pll += erro_fase_pll * Ts
    
    # Saturação do integrador baseada na velocidade máxima elétrica esperada (ex: p * w_max)
    limite_integrador = 4000.0 
    integral_pll = np.clip(integral_pll, -limite_integrador/Ki_pll, limite_integrador/Ki_pll)
    
    omega_est_e = Kp_pll * erro_fase_pll + Ki_pll * integral_pll
    
    # Conversão e filtragem da velocidade mecânica
    omega_m_raw = omega_est_e / p
    omega_m_filt = alpha_w_pll * omega_m_raw + (1 - alpha_w_pll) * omega_m_filt_prev
    omega_m_filt_prev = omega_m_filt
    
    omega_est[k] = omega_m_filt
    
    # Integração contínua da malha fechada do PLL
    theta_est[k] = np.mod(th_est_prev + omega_est_e * Ts, 2 * np.pi)
    th_est_prev = theta_est[k]
    
    # --- FILTRO ANTI-WINDUP DE ESTADO ---
    w_safe = abs(omega_est[k]) if abs(omega_est[k]) > 5.0 else 5.0 
    rpm_estimado_abs = w_safe * (30.0 / np.pi)
    
    if rpm_estimado_abs < 5.0:
        E_ex[:, k] = 0.0
        integral_pll = 0.0  # Mantém o PLL limpo até haver tensão induzida suficiente
        t_inst = 0.0
    else:
        potencia_em_alpha_beta = (E_ex[0, k] * i_hat[0, k] + E_ex[1, k] * i_hat[1, k])
        t_inst = potencia_em_alpha_beta / w_safe
        
    torque_est_lpf[k] = alpha_lpf_tq * abs(t_inst) + (1 - alpha_lpf_tq) * torque_est_lpf[k-1]

# ============================================================================
# 5. PROCESSAMENTO E VISUALIZAÇÃO
# ============================================================================
if CONFIG_PLOT_SMO:
    plt.figure(1, figsize=(12, 10))
    
    plt.subplot(3, 1, 1)
    plt.plot(t, RPM_target_profile, 'k--', label='RPM Alvo')
    plt.plot(t, x_true[2, :]/p * (30/np.pi), 'b-', label='RPM Real')
    plt.plot(t, omega_est * (30/np.pi), 'r-', alpha=0.7, label='RPM Estimado (SMO)')
    plt.title('Desempenho com Planta Dinâmica Real e Topologia do Artigo'); plt.ylabel('RPM'); plt.grid(True); plt.legend()

    plt.subplot(3, 1, 2)
    plt.plot(t, torque_real, 'b-', alpha=0.4, label='Te Real (Física)')
    plt.plot(t, torque_est_lpf, 'r-', linewidth=2.0, label='Te Estimado Corrigido')
    plt.plot(t, load_real, 'k', label='Curva de Carga da Hélice')
    plt.ylim(0,max(torque_real))
    plt.ylabel('Torque (N.m)'); plt.grid(True); plt.legend()

    plt.subplot(3, 1, 3)
    plt.plot(t, ia_log, label='i_fase A', alpha=0.8)
    plt.plot(t, ib_log, label='i_fase B', alpha=0.8)
    plt.plot(t, ic_log, label='i_fase C', alpha=0.8)
    plt.title('Correntes Dinâmicas Reais da Planta (Com Efeito Exponencial de L/R)'); plt.xlabel('Tempo [s]'); plt.ylabel('Corrente [A]'); plt.grid(True); plt.legend()
    plt.tight_layout()

if CONFIG_PLOT_SMO_BMF:
    plt.figure(2, figsize=(11, 7))
    macro_mask = (t >= 2.0) & (t <= 2.05) 
    
    # Subplot 1: Eixo Alpha (Corrigido para E_ex)
    plt.subplot(2, 1, 1)
    plt.plot(t[macro_mask], e_real[0, macro_mask], 'b', linewidth=2, label='e_alpha Real')
    plt.plot(t[macro_mask], E_ex[0, macro_mask], 'r--', linewidth=1, alpha=0.5, label='E_ex_alpha Bruta SMO')
    plt.title('Validação de Back-EMF'); plt.ylabel('Tensão [V]'); plt.grid(True); plt.legend()
    
    # Subplot 2: Eixo Beta (Corrigido para E_ex)
    plt.subplot(2, 1, 2)
    plt.plot(t[macro_mask], e_real[1, macro_mask], 'b', linewidth=2, label='e_beta Real')
    plt.plot(t[macro_mask], E_ex[1, macro_mask], 'r--', linewidth=1, alpha=0.5, label='E_ex_beta Bruta SMO')
    plt.xlabel('Tempo [s]'); plt.ylabel('Tensão [V]'); plt.grid(True); plt.legend()
    
    plt.tight_layout()

if CONFIG_PLOT_PROPELLER_COEFFICIENT:
    fc = 2.0; fs = 1.0 / Ts; nyq = 0.5 * fs; low = fc / nyq
    b, a = signal.butter(1, low, btype='low') 
    tq_real_lp = signal.lfilter(b, a, torque_real)

    threshold_tq_margin = 0.02
    min_omega = 400.0  
    janela_minima = 50 
    k_identificado_real, k_identificado_smo, rpm_estabilizado = [], [], []
    contador_estavel = 0

    for k in range(1, N):
        delta_tq_lp = abs(tq_real_lp[k] - tq_real_lp[k-1])
        if delta_tq_lp <= threshold_tq_margin and x_true[2, k] > min_omega:
            contador_estavel += 1
        else:
            contador_estavel = 0 

        if contador_estavel >= janela_minima and contador_estavel % 200 == 0:
            w_m_r = x_true[2, k] / p
            w_m_s = omega_est[k]
            if w_m_r > 1.0:
                k_r = tq_real_lp[k] / (w_m_r**2)
                k_s = torque_est_lpf[k] / (w_m_s**2)
                if k_r > 0 and k_s > 0:
                    k_identificado_real.append(k_r)
                    k_identificado_smo.append(k_s)
                    rpm_estabilizado.append(w_m_r * (30/np.pi))

    media_real = np.mean(k_identificado_real) if k_identificado_real else 0
    media_smo = np.mean(k_identificado_smo) if k_identificado_smo else 0

    print("=" * 70)
    print("          ALGORITMO DE IDENTIFICAÇÃO DO COEFICIENTE DA HÉLICE       ")
    print("=" * 70)
    print(f"k_Load de Projeto (Nominal): {k_load:.4e}")
    print(f"k_Real Identificado Médio:   {media_real:.4e} (Erro: {abs(media_real-k_load)/k_load*100 if k_load!=0 else 0:.2f}%)")
    print(f"k_SMO Identificado Médio:    {media_smo:.4e} (Erro: {abs(media_smo-k_load)/k_load*100 if k_load!=0 else 0:.2f}%)")
    print("=" * 70)

if CONFIG_PLOT_THETA_COMPLETE:
    plt.figure(4, figsize=(12, 7))
    plt.subplot(2, 1, 1)
    plt.plot(t, x_true[3, :], 'b', label='Theta Real', alpha=0.5)
    plt.plot(t, theta_est, 'r--', label='Theta Estimado (PLL)', alpha=0.6)
    plt.title('Rastreamento de Ângulo de Posição via Topologia do Artigo')
    plt.ylabel('Ângulo [rad]'); plt.grid(True); plt.legend(loc='upper right')
    
    erro_fase = x_true[3, :] - theta_est
    erro_fase_wrap = np.mod(erro_fase + np.pi, 2 * np.pi) - np.pi
    erro_fase_graus = erro_fase_wrap * (180.0 / np.pi)
    
    plt.subplot(2, 1, 2)
    plt.plot(t, erro_fase_graus, 'g', alpha=0.7, label='Erro de Fase Absoluto')
    plt.axhline(y=0, color='k', linestyle='--', linewidth=1.2)
    plt.xlabel('Tempo [s]'); plt.ylabel('Erro [Graus Elétricos]'); plt.grid(True); plt.legend(loc='upper right')
    plt.tight_layout()

plt.show()