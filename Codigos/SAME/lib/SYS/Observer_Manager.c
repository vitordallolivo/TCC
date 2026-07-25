/**
 * @file Observer_Manager.c
 * @brief SMO + PLL do nó SAME, transcrito do laço numba de
 * test_coeff_auto.py::run_sim_core (só a parte do observador; a planta é o
 * motor real, medido pelos 2 ADCs). Tudo em float (FPU single-precision).
 *
 * Reconfiguração = reinício: CMD muda param via Observer_Set_*, aplicado no
 * topo do laço do ADC (Observer_Apply_Pending), que zera o estado.
 */

#include "Observer_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <math.h>

LOG_MODULE_REGISTER(observer, LOG_LEVEL_INF);

/* A libc mínima do Zephyr não define M_PI (não é padrão C). */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TWO_PI       6.283185307179586f
#define INV_TWO_PI   0.15915494309189535f
#define TWO_PI_3     2.0943951023931953f   /* 2π/3 */
#define FOUR_PI_3    4.1887902047863905f   /* 4π/3 */
#define INV_SQRT3    0.5773502691896258f
#define PLL_WN       (TWO_PI * 150.0f)      /* ω_n = 2π·150 (fixo, ver CLAUDE.md) */
#define PLL_ZETA     0.707f

/* Seletor da função de comutação (igual ao SMO_SWITCH_FUNC do script). */
enum { SW_SAT = 0, SW_TANH = 1, SW_SIGMOID = 2 };

/* ---- Defaults de fábrica (referência test_coeff_auto.py) ---------------- */
#define DEF_POLE_PAIRS   7.0f
#define DEF_RS           17.3e-3f
#define DEF_LS           7.0e-6f
#define DEF_KV           (310.0f / 1.7320508075688772f)  /* Kv já em estrela (Y) */
#define DEF_GAMMA1       5.0f      /* Ks1·Ts = 500000·10µs  (Γ_s1 > 0) */
#define DEF_GAMMA2      (-1.0f)    /* Ks2·Ts = -100000·10µs (Γ_s2 < 0) */
#define DEF_GAMMAT       0.05f     /* Kt·Ts  = 5000·10µs    (Γ_t  > 0) */
#define DEF_TORQUE_LPF   10000.0f  /* fc_torque do script */
#define DEF_BEMF_LPF     0.0f      /* 0 = desligado → PLL usa ê direto (== script) */
#define DEF_TS           10.0e-6f  /* 10 µs */
#define DEF_BL           18.0f
#define DEF_SWITCH       SW_TANH
#define DEF_BEMF_KIND    1         /* 1 = senoidal, 0 = trapezoidal */

/* ---- Parâmetros configuráveis (valores "crus" vindos por CMD) ----------- */
typedef struct {
    float pole_pairs;
    float Rs;
    float Ls;
    float Kv;
    float gamma1;      /* Γ_s1 (usado direto: î += Γ_s1·s) */
    float gamma2;      /* Γ_s2 (usado direto: ê += Γ_s2·s) */
    float gammat;      /* Γ_t  (Φ_e = 1 − Γ_t) */
    float torque_lpf;  /* fc do LPF de torque [Hz] */
    float bemf_lpf;    /* fc do LPF de BEMF antes da PLL [Hz]; ≤0 = desligado */
    float ts;          /* passo de amostragem [s] */
    float bl;          /* camada de contorno */
    int   switch_func;
    int   bemf_kind;
} obs_params;

/* ---- Constantes de discretização (pré-calculadas de obs_params) --------- */
typedef struct {
    float Phi_i;       /* 1 − Rs·Ts/Ls */
    float Gamma_v;     /* Ts/Ls */
    float Gamma_s1;    /* = gamma1 */
    float Gamma_s2;    /* = gamma2 */
    float Phi_e;       /* 1 − gammat */
    float alpha_tq;    /* LPF de torque */
    float alpha_bemf;  /* LPF de BEMF (0 = desligado) */
    float Kp_pll;      /* 2ζω_n */
    float Ki_pll;      /* ω_n² */
    float Ke;          /* Ke_mec/p */
    float inv_p;       /* 1/p */
    float ts;          /* cópia rápida do passo */
    float bl;
    int   switch_func;
    int   bemf_kind;
} obs_const;

/* ---- Estado dinâmico (zerado no reinício) ------------------------------- */
typedef struct {
    float i_hat_al, i_hat_be;   /* î_αβ */
    float e_hat_al, e_hat_be;   /* ê_αβ */
    float e_lpf_al, e_lpf_be;   /* ê filtrado (se bemf_lpf ativo) */
    float integral_pll;         /* integrador da PLL */
    float th_est;               /* θ̂ elétrico */
    float tq_lpf;               /* torque filtrado */
} obs_state;

/* Cópia AO VIVO usada só pela thread do ADC (sem lock). */
static obs_params live_params;
static obs_const  live_const;
static obs_state  live_state;

/* Saídas do último passo (lidas na mesma thread do ADC). */
static float out_rpm;
static float out_te;
static float out_torque_helice;

/* Saúde do observador (lida pelo Security_Manager, mesma thread do ADC). */
static float out_amp_e;      /* amplitude da BEMF estimada (|ê|) — lock da PLL */
static float out_phase_err;  /* erro de fase da PLL no último passo */

/* Reconfiguração pendente: escrita pela same_wq, aplicada pela thread do ADC. */
static obs_params    pending_params;
static atomic_t      reconfig_pending;
static struct k_mutex cfg_mutex;

/* ======================================================================== *
 * Helpers (always_inline, float puro)                                      *
 * ======================================================================== */

/**
 * @brief Forma normalizada (±1) da BEMF — igual a f_shape() do script.
 * @param theta ângulo elétrico (rad)
 * @param kind 1 = senoidal, 0 = trapezoidal
 * @return valor normalizado em [-1, 1]
 */
static inline float __attribute__((always_inline))
f_shape(float theta, int kind)
{
    if (kind == 1) {
        return sinf(theta);
    }
    /* trapezoidal: normaliza para [0,2π) */
    float th = theta - TWO_PI * floorf(theta * INV_TWO_PI);
    if (th < (float)(M_PI / 6.0))         return (6.0f / (float)M_PI) * th;
    else if (th < (float)(5.0*M_PI/6.0))  return 1.0f;
    else if (th < (float)(7.0*M_PI/6.0))  return -(6.0f/(float)M_PI) * (th - (float)M_PI);
    else if (th < (float)(11.0*M_PI/6.0)) return -1.0f;
    else                                  return (6.0f/(float)M_PI) * (th - TWO_PI);
}

/**
 * @brief Função de comutação s(e) com camada de contorno.
 * @param e erro de corrente
 * @param inv_bl 1/BL (largura da camada de contorno)
 * @param kind SW_SAT, SW_TANH ou SW_SIGMOID
 * @return valor de comutação em [-1, 1]
 */
static inline float __attribute__((always_inline))
switch_fn(float e, float inv_bl, int kind)
{
    float x = e * inv_bl;
    if (kind == SW_TANH) {
        return tanhf(x);
    } else if (kind == SW_SAT) {
        if (x > 1.0f) return 1.0f;
        if (x < -1.0f) return -1.0f;
        return x;
    } else { /* sigmoide algébrica */
        return x / (1.0f + fabsf(x));
    }
}

/**
 * @brief Envolve um ângulo em [0, 2*pi), sem laço (trata transitório grande).
 * @param th ângulo em rad (qualquer valor)
 * @return ângulo equivalente em [0, 2*pi)
 */
static inline float __attribute__((always_inline))
wrap_2pi(float th)
{
    th -= TWO_PI * floorf(th * INV_TWO_PI);
    return th;
}

/**
 * @brief Recalcula as constantes de discretização a partir dos parâmetros
 * "crus" (motor + ganhos), pra uso no caminho quente de Observer_Step.
 * @param p [in] parâmetros configuráveis (motor, ganhos Γ, LPFs, Ts)
 * @param c [out] constantes pré-calculadas
 */
static void Observer_Recompute(const obs_params *p, obs_const *c)
{
    const float ts = p->ts;
    c->ts       = ts;
    c->Phi_i    = 1.0f - (p->Rs * ts / p->Ls);
    c->Gamma_v  = ts / p->Ls;
    c->Gamma_s1 = p->gamma1;               /* Γ usados DIRETO (ver CLAUDE.md) */
    c->Gamma_s2 = p->gamma2;
    c->Phi_e    = 1.0f - p->gammat;

    float wt = TWO_PI * p->torque_lpf * ts;
    c->alpha_tq = wt / (1.0f + wt);

    if (p->bemf_lpf > 0.0f) {
        float wb = TWO_PI * p->bemf_lpf * ts;
        c->alpha_bemf = wb / (1.0f + wb);
    } else {
        c->alpha_bemf = 0.0f;              /* desligado → PLL usa ê direto */
    }

    c->Kp_pll = 2.0f * PLL_ZETA * PLL_WN;
    c->Ki_pll = PLL_WN * PLL_WN;

    float ke_mec = (p->Kv > 0.0f) ? (60.0f / (TWO_PI * p->Kv)) : 0.0f;
    c->inv_p = (p->pole_pairs > 0.0f) ? (1.0f / p->pole_pairs) : 0.0f;
    c->Ke    = ke_mec * c->inv_p;

    c->bl          = p->bl;
    c->switch_func = p->switch_func;
    c->bemf_kind   = p->bemf_kind;
}

/**
 * @brief Zera só o estado dinâmico (î, ê, PLL, θ̂, filtros, saídas) sem
 * mexer nos parâmetros/constantes.
 */
void Observer_Reset(void)
{
    live_state = (obs_state){0};
    out_rpm = out_te = out_torque_helice = 0.0f;
}

/**
 * @brief Aplica os defaults de fábrica, recalcula as constantes e zera o
 * estado. Chamar em main() antes do HAL.
 */
void Observer_Init(void)
{
    k_mutex_init(&cfg_mutex);

    live_params = (obs_params){
        .pole_pairs = DEF_POLE_PAIRS,
        .Rs         = DEF_RS,
        .Ls         = DEF_LS,
        .Kv         = DEF_KV,
        .gamma1     = DEF_GAMMA1,
        .gamma2     = DEF_GAMMA2,
        .gammat     = DEF_GAMMAT,
        .torque_lpf = DEF_TORQUE_LPF,
        .bemf_lpf   = DEF_BEMF_LPF,
        .ts         = DEF_TS,
        .bl         = DEF_BL,
        .switch_func = DEF_SWITCH,
        .bemf_kind   = DEF_BEMF_KIND,
    };
    pending_params = live_params;
    Observer_Recompute(&live_params, &live_const);
    Observer_Reset();
    atomic_clear(&reconfig_pending);

    LOG_INF("Observer init: p=%d Rs=%.4f L=%.2eH Kv=%.1f Ts=%.1fus",
            (int)live_params.pole_pairs, (double)live_params.Rs,
            (double)live_params.Ls, (double)live_params.Kv,
            (double)(live_params.ts * 1e6f));
}

/**
 * @brief Devolve o valor de fábrica de um target (pra VALUE_RESET_DEFAULT).
 * @param t target do CMD
 * @param out [out] valor default (TS_ADC em µs)
 * @return true se `t` pertence ao observador
 */
static bool default_for_target(target_type t, float *out)
{
    switch (t) {
        case TARGET_POLES_PAIRS:          *out = DEF_POLE_PAIRS; return true;
        case TARGET_RESISTANCE:           *out = DEF_RS;         return true;
        case TARGET_INDUCTANCE:           *out = DEF_LS;         return true;
        case TARGET_KV_RATING:            *out = DEF_KV;         return true;
        case TARGET_GAMMA1:               *out = DEF_GAMMA1;     return true;
        case TARGET_GAMMA2:               *out = DEF_GAMMA2;     return true;
        case TARGET_GAMMAT:               *out = DEF_GAMMAT;     return true;
        case TARGET_BEMF_LPF_FREQUENCY:   *out = DEF_BEMF_LPF;   return true;
        case TARGET_TORQUE_LPF_FREQUENCY: *out = DEF_TORQUE_LPF; return true;
        case TARGET_TS_ADC:               *out = DEF_TS * 1e6f;  return true; /* em µs */
        default: return false;
    }
}

/**
 * @brief Grava um target em pending_params. Deve ser chamado com cfg_mutex
 * travado.
 * @param t target do CMD
 * @param value novo valor (TS_ADC em µs, convertido pra segundos aqui)
 * @return true se `t` pertence ao observador
 */
static bool write_pending(target_type t, float value)
{
    switch (t) {
        case TARGET_POLES_PAIRS:          pending_params.pole_pairs = value; break;
        case TARGET_RESISTANCE:           pending_params.Rs         = value; break;
        case TARGET_INDUCTANCE:           pending_params.Ls         = value; break;
        case TARGET_KV_RATING:            pending_params.Kv         = value; break;
        case TARGET_GAMMA1:               pending_params.gamma1     = value; break;
        case TARGET_GAMMA2:               pending_params.gamma2     = value; break;
        case TARGET_GAMMAT:               pending_params.gammat     = value; break;
        case TARGET_BEMF_LPF_FREQUENCY:   pending_params.bemf_lpf   = value; break;
        case TARGET_TORQUE_LPF_FREQUENCY: pending_params.torque_lpf = value; break;
        case TARGET_TS_ADC:               pending_params.ts         = value * 1e-6f; break; /* µs→s */
        default: return false;
    }
    return true;
}

/**
 * @brief Agenda mudança de um parâmetro/ganho (marca reconfig pendente;
 * não toca no estado ao vivo).
 * @param target TARGET_* do CMD
 * @param value novo valor
 * @return true se target pertence ao observador
 */
bool Observer_Set_Param(target_type target, float value)
{
    k_mutex_lock(&cfg_mutex, K_FOREVER);
    bool handled = write_pending(target, value);
    if (handled) {
        atomic_set(&reconfig_pending, 1);
    }
    k_mutex_unlock(&cfg_mutex);
    if (handled) {
        LOG_INF("Observer: target %d <- %.6f (reinício pendente)", target, (double)value);
    }
    return handled;
}

/**
 * @brief Restaura o valor de fábrica de um target (agenda via Observer_Set_Param).
 * @param target TARGET_* do CMD
 * @return true se tratado
 */
bool Observer_Reset_Param_Default(target_type target)
{
    float def;
    if (!default_for_target(target, &def)) {
        return false;
    }
    /* TS_ADC guarda em µs no default helper; write_pending reconverte. */
    return Observer_Set_Param(target, def);
}

/**
 * @brief Aplica reconfig pendente (se houver): recalcula constantes e
 * zera o estado. Chamada no topo do laço da thread do ADC.
 */
void Observer_Apply_Pending(void)
{
    if (!atomic_get(&reconfig_pending)) {
        return;
    }
    k_mutex_lock(&cfg_mutex, K_FOREVER);
    live_params = pending_params;
    atomic_clear(&reconfig_pending);
    k_mutex_unlock(&cfg_mutex);

    Observer_Recompute(&live_params, &live_const);
    Observer_Reset();   /* reinicia o observador ao mudar uma variável dele */
    LOG_INF("Observer: reconfigurado e reiniciado");
}

/**
 * @brief Executa um passo do SMO+PLL (referencial αβ). Caminho quente, sem
 * lock, estado/constantes em registradores de FP.
 * @param v_alpha, v_beta tensão de fase em αβ (V)
 * @param i_alpha, i_beta corrente de fase em αβ (A)
 */
void __attribute__((hot, flatten))
Observer_Step(float v_alpha, float v_beta, float i_alpha, float i_beta)
{
    /* Constantes → registradores */
    register const float Phi_i    = live_const.Phi_i;
    register const float Gamma_v  = live_const.Gamma_v;
    register const float Gamma_s1 = live_const.Gamma_s1;
    register const float Gamma_s2 = live_const.Gamma_s2;
    register const float Phi_e    = live_const.Phi_e;
    register const float alpha_tq = live_const.alpha_tq;
    register const float a_bemf   = live_const.alpha_bemf;
    register const float Kp_pll   = live_const.Kp_pll;
    register const float Ki_pll   = live_const.Ki_pll;
    register const float Ke       = live_const.Ke;
    register const float inv_p    = live_const.inv_p;
    register const float ts       = live_const.ts;
    register const float inv_bl   = (live_const.bl != 0.0f) ? (1.0f / live_const.bl) : 0.0f;
    const int sw   = live_const.switch_func;
    const int kind = live_const.bemf_kind;

    /* Estado → registradores */
    register float i_hat_al = live_state.i_hat_al;
    register float i_hat_be = live_state.i_hat_be;
    register float e_hat_al = live_state.e_hat_al;
    register float e_hat_be = live_state.e_hat_be;
    register float integral_pll = live_state.integral_pll;
    register float th_est   = live_state.th_est;
    register float tq_lpf   = live_state.tq_lpf;

    /* ── SMO: erro de corrente + comutação ── */
    register float s_al = switch_fn(i_alpha - i_hat_al, inv_bl, sw);
    register float s_be = switch_fn(i_beta  - i_hat_be, inv_bl, sw);

    /* ── Observador de corrente (Γ_s1) ── */
    i_hat_al = Phi_i * i_hat_al + Gamma_v * (v_alpha - e_hat_al) + Gamma_s1 * s_al;
    i_hat_be = Phi_i * i_hat_be + Gamma_v * (v_beta  - e_hat_be) + Gamma_s1 * s_be;

    /* ── Observador de BEMF (Γ_s2, polo Φ_e) ── */
    e_hat_al = Phi_e * e_hat_al + Gamma_s2 * s_al;
    e_hat_be = Phi_e * e_hat_be + Gamma_s2 * s_be;

    /* ── LPF opcional de BEMF antes da PLL (a_bemf==0 → passa direto) ── */
    register float pll_e_al = e_hat_al;
    register float pll_e_be = e_hat_be;
    if (a_bemf > 0.0f) {
        register float el = live_state.e_lpf_al + a_bemf * (e_hat_al - live_state.e_lpf_al);
        register float eb = live_state.e_lpf_be + a_bemf * (e_hat_be - live_state.e_lpf_be);
        live_state.e_lpf_al = el;
        live_state.e_lpf_be = eb;
        pll_e_al = el;
        pll_e_be = eb;
    }

    /* ── PLL ── */
    register float amp_E = sqrtf(pll_e_al * pll_e_al + pll_e_be * pll_e_be);
    register float erro_fase;
    if (amp_E > 1.0f) {
        register float g_al = f_shape(th_est, kind);
        register float g_be = INV_SQRT3 * (f_shape(th_est - TWO_PI_3, kind)
                                         - f_shape(th_est - FOUR_PI_3, kind));
        erro_fase = (g_al * pll_e_be - g_be * pll_e_al) / amp_E;
    } else {
        erro_fase = 0.0f;
    }

    integral_pll += erro_fase * ts;
    register float omega_est_elec = Kp_pll * erro_fase + Ki_pll * integral_pll;
    th_est = wrap_2pi(th_est + omega_est_elec * ts);

    /* θ_out = θ̂ (compensação de fase do front-end analógico fica desligada
     * por padrão — ver test_coeff_auto.py phase_mode==3). */
    register float th_out = th_est;

    /* ── Reconstrução / torque (Te = 1.5·Ke·p·(f_a·î_α + f_b·î_β)) ── */
    register float fa = f_shape(th_out, kind);
    register float fb = INV_SQRT3 * (f_shape(th_out - TWO_PI_3, kind)
                                   - f_shape(th_out - FOUR_PI_3, kind));
    register float p_pairs = (inv_p > 0.0f) ? (1.0f / inv_p) : 0.0f;
    register float t_inst = 1.5f * Ke * p_pairs * (fa * i_hat_al + fb * i_hat_be);
    tq_lpf = alpha_tq * t_inst + (1.0f - alpha_tq) * tq_lpf;

    /* ── Estado de volta à memória ── */
    live_state.i_hat_al = i_hat_al;
    live_state.i_hat_be = i_hat_be;
    live_state.e_hat_al = e_hat_al;
    live_state.e_hat_be = e_hat_be;
    live_state.integral_pll = integral_pll;
    live_state.th_est   = th_est;
    live_state.tq_lpf   = tq_lpf;

    /* ── Saídas (rad/s elétrico → rpm mecânico) ── */
    register float omega_mech = omega_est_elec * inv_p;   /* rad/s mecânico */
    out_rpm = omega_mech * (30.0f / (float)M_PI);          /* rad/s → rpm */
    out_te  = tq_lpf;
    out_amp_e     = amp_E;      /* saúde: amplitude da BEMF (lock da PLL) */
    out_phase_err = erro_fase;  /* saúde: erro de fase da PLL */
    /* torque_helice: em regime a carga da hélice = torque eletromagnético
     * estimado. Quando B/τc (atrito, compartilhados com a SPMM) forem
     * embarcados, subtrair (B·ω+τc). Por ora, espelha Te. */
    out_torque_helice = tq_lpf;
}

/** @brief Rotação mecânica estimada. @return rpm mecânico (rpm_same). */
float Observer_Get_Rpm(void)            { return out_rpm; }
/** @brief Torque eletromagnético estimado. @return Te em N·m. */
float Observer_Get_Te(void)             { return out_te; }
/** @brief Torque na hélice (hoje espelha Te). @return N·m. */
float Observer_Get_Torque_Helice(void)  { return out_torque_helice; }

/** @brief Amplitude da BEMF estimada do último passo. @return |ê|. */
float Observer_Get_Bemf_Amplitude(void) { return out_amp_e; }
/** @brief Erro de fase da PLL do último passo. @return rad (0 se BEMF baixa). */
float Observer_Get_Phase_Error(void)    { return out_phase_err; }

/**
 * @brief Verifica se o estado dinâmico do observador é todo finito.
 * @return true se î,ê,PLL,θ̂,torque são finitos (false = divergiu → NaN/Inf).
 */
bool Observer_State_Is_Finite(void)
{
    return isfinite(live_state.i_hat_al) && isfinite(live_state.i_hat_be)
        && isfinite(live_state.e_hat_al) && isfinite(live_state.e_hat_be)
        && isfinite(live_state.integral_pll) && isfinite(live_state.th_est)
        && isfinite(live_state.tq_lpf);
}

/**
 * @brief Sanidade física dos parâmetros do motor/observador.
 * @return true se Rs,Ls,pares_de_polos,Kv,Ts são todos > 0.
 */
bool Observer_Params_Physical_Valid(void)
{
    return live_params.Rs > 0.0f && live_params.Ls > 0.0f
        && live_params.pole_pairs > 0.0f && live_params.Kv > 0.0f
        && live_params.ts > 0.0f;
}

/**
 * @brief Checa a regra fixa de sinais dos ganhos Γ (ver CLAUDE.md).
 * @return true se Γ1 > 0, Γ2 < 0 e Γt > 0.
 */
bool Observer_Gamma_Signs_Valid(void)
{
    return live_params.gamma1 > 0.0f && live_params.gamma2 < 0.0f
        && live_params.gammat > 0.0f;
}
