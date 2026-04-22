import sympy as sp

# 1. Definição das Variáveis Simbólicas
Ts, R, Ls, Ke = sp.symbols('T_s R L_s K_e')
# Use o prefixo 'r' antes das aspas para Raw String
ia_old, ib_old, we_old, te_old = sp.symbols(r'i_\alpha i_\beta \omega_e \theta_e')
va, vb = sp.symbols('v_alpha(k-1) v_beta(k-1)')

# Funções de forma da FEM (f_alpha e f_beta)
fa = sp.Function('f_alpha')(te_old)
fb = sp.Function('f_beta')(te_old)

# 2. Definição do Modelo Contínuo (Derivadas)
dia_dt = -(R/Ls)*ia_old - (Ke/Ls)*we_old*fa + (1/Ls)*va
dib_dt = -(R/Ls)*ib_old - (Ke/Ls)*we_old*fb + (1/Ls)*vb
dwe_dt = 0  # Aproximação clássica EKF para motores
dte_dt = we_old

# 3. Discretização de Euler: x(k) = x(k-1) + Ts * dx/dt
f1 = ia_old + Ts * dia_dt
f2 = ib_old + Ts * dib_dt
f3 = we_old + Ts * dwe_dt
f4 = te_old + Ts * dte_dt

f_matrix = sp.Matrix([f1, f2, f3, f4])
state_vector = sp.Matrix([ia_old, ib_old, we_old, te_old])

# 4. Cálculo da Jacobiana F
F_jacobian = f_matrix.jacobian(state_vector)

# --- OUTPUT EM LATEX ---
print("% --- Vetor de Funções f(x,u) ---")
print(sp.latex(f_matrix))
print("\n% --- Matriz Jacobiana F ---")
print(sp.latex(F_jacobian))