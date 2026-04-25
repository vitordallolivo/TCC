import sympy as sp

# Definição de cores para o terminal (apenas para organização do log)
GREEN = "\033[92m"
BLUE = "\033[94m"
MAGENTA = "\033[95m"
CYAN = "\033[96m"
RESET = "\033[0m"

def generate_smo_latex():
    # 1. DEFINIÇÃO DE SÍMBOLOS ATÔMICOS (Proteção contra TypeError)
    Ts = sp.Symbol('T_s', positive=True)
    Rs = sp.Symbol('R_s', positive=True)
    Ls = sp.Symbol('L_s', positive=True)
    Ks1 = sp.Symbol('K_{s1}', positive=True)
    Ks2 = sp.Symbol('K_{s2}', positive=True)
    Kt = sp.Symbol('K_t', positive=True)

    # Lados Esquerdos (Símbolos k+1)
    i_hat_next_a_lhs = sp.Symbol(r'\hat{i}_{\alpha,k+1}')
    i_hat_next_b_lhs = sp.Symbol(r'\hat{i}_{\beta,k+1}')
    e_hat_next_a_lhs = sp.Symbol(r'\hat{e}_{\alpha,k+1}')
    e_hat_next_b_lhs = sp.Symbol(r'\hat{e}_{\beta,k+1}')

    # Estados atuais e Medições (Instante k)
    i_hat_a = sp.Symbol(r'\hat{i}_{\alpha,k}')
    i_hat_b = sp.Symbol(r'\hat{i}_{\beta,k}')
    e_hat_a = sp.Symbol(r'\hat{e}_{\alpha,k}')
    e_hat_b = sp.Symbol(r'\hat{e}_{\beta,k}')
    v_a = sp.Symbol(r'v_{\alpha,k}')
    v_b = sp.Symbol(r'v_{\beta,k}')
    i_a = sp.Symbol(r'i_{\alpha,k}')
    i_b = sp.Symbol(r'i_{\beta,k}')

    # Função de Saturação
    sat = sp.Function(r'\text{Sat}')
    i_tilde_a = i_a - i_hat_a
    i_tilde_b = i_b - i_hat_b

    # 2. DEFINIÇÃO DAS CONSTANTES DE DISCRETIZAÇÃO (Ganhos)
    Phi_i = sp.Symbol(r'\Phi_i') # 1 - (Rs*Ts)/Ls
    Gamma_v = sp.Symbol(r'\Gamma_v') # Ts/Ls
    Gamma_s1 = sp.Symbol(r'\Gamma_{s1}') # Ts*Ks1
    Phi_e = sp.Symbol(r'\Phi_e') # 1 - Kt*Ts
    Gamma_s2 = sp.Symbol(r'\Gamma_{s2}') # Ts*Ks2

    Phi_i_val = 1 - (Rs * Ts) / Ls
    Gamma_v_val = Ts / Ls
    Gamma_s1_val = Ts * Ks1
    Phi_e_val = 1 - (Kt * Ts)
    Gamma_s2_val = Ts * Ks2

    # 3. EQUAÇÕES FINAIS (Lado Direito)
    i_hat_next_a_rhs = i_hat_a * Phi_i + Gamma_v * (v_a - e_hat_a) + Gamma_s1 * sat(i_tilde_a)
    i_hat_next_b_rhs = i_hat_b * Phi_i + Gamma_v * (v_b - e_hat_b) + Gamma_s1 * sat(i_tilde_b)
    
    e_hat_next_a_rhs = e_hat_a * Phi_e + Gamma_s2 * sat(i_tilde_a)
    e_hat_next_b_rhs = e_hat_b * Phi_e + Gamma_s2 * sat(i_tilde_b)

    # 4. TORQUE (Eq. 28)
    p = sp.Symbol('p')
    we = sp.Symbol(r'\omega_e')
    Te_lhs = sp.Symbol(r'\hat{T}_e')
    Te_rhs = (3*p / (4*we)) * (e_hat_a * i_a + e_hat_b * i_b)

    # --- FUNÇÃO DE IMPRESSÃO ---
    def print_eq(title, color, lhs, rhs):
        print(f"{color}% {title}{RESET}")
        print("\\begin{equation}")
        print(f"    {sp.latex(lhs)} = {sp.latex(rhs)}")
        print("\\end{equation}\n")

    # --- OUTPUTS ---
    print_eq("Definição das Constantes de Discretização", CYAN, Phi_i, Phi_i_val)
    print_eq("", CYAN, Gamma_v, Gamma_v_val)
    print_eq("", CYAN, Gamma_s1, Gamma_s1_val)
    print_eq("", CYAN, Phi_e, Phi_e_val)
    print_eq("", CYAN, Gamma_s2, Gamma_s2_val)

    print_eq("Estimação de Corrente Discreta (Eixo Alfa)", GREEN, i_hat_next_a_lhs, i_hat_next_a_rhs)
    print_eq("Estimação de Corrente Discreta (Eixo Beta)", GREEN, i_hat_next_b_lhs, i_hat_next_b_rhs)
    
    print_eq("Estimação de BEMF Discreta (Eixo Alfa)", BLUE, e_hat_next_a_lhs, e_hat_next_a_rhs)
    print_eq("Estimação de BEMF Discreta (Eixo Beta)", BLUE, e_hat_next_b_lhs, e_hat_next_b_rhs)
    
    print_eq("Cálculo do Torque Eletromagnético Estimado", MAGENTA, Te_lhs, sp.simplify(Te_rhs))

if __name__ == "__main__":
    generate_smo_latex()