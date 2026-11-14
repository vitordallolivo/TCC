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

/* ------------------------------------------------------------------------ *
 * OBS_IMPL — variante do caminho quente (só Observer_Step muda; parâmetros,
 * CMD e diagnóstico são idênticos nas duas).
 *
 *   0 = V0: transcrição direta do script — 6x f_shape() com sinf(), tanhf()
 *           na comutação, floorf() no wrap, 1/BL e 1/inv_p dentro do laço.
 *           Mantido como BASELINE do A/B (não otimizar aqui).
 *   5 = V5: LUT512 + interpolação, identidade g_beta == -cos(theta),
 *           comutação sigmoide, 2 sincos (PLL antes / torque depois do
 *           update), Kt e inv_bl pré-calculados, wrap sem floorf.
 *
 * Catálogo por mudança e plano de validação: CLAUDE.md.
 * ------------------------------------------------------------------------ */
#ifndef OBS_IMPL
#define OBS_IMPL 5
#endif

/* A troca tanh -> sigmoide é uma SEGUNDA otimização, independente da LUT, e é
 * OPT-IN. Medido em tests/observer_golden (Γ do autotuning, 3 patamares):
 *
 *   LUT + identidade + 2 sincos (tanh dos dois lados) -> 0,0015 % no torque
 *   só trocar tanh por sigmoide                       -> 2,03 %   no torque
 *
 * Ou seja, 100 % do desvio do V5 vem da comutação, nenhum da LUT — e 2,03 %
 * estoura a tolerância de 1 % do plano de validação (o catálogo estimava
 * 0,34 %; a medição no ISA real desmentiu). Os Γ do smo_gains.json foram
 * otimizados com SMO_SWITCH_FUNC=1 (tanh), então a sigmoide só faz sentido
 * DEPOIS de reotimizar com SMO_SWITCH_FUNC=2.
 *
 * Default = tanh: fica numericamente indistinguível do V0 e ainda assim 2,6x
 * mais rápido (1027 -> 394 instret), o que já destrava ~190 kSps. Para ligar
 * a sigmoide (183 instret, ~252 kSps) depois de reotimizar os Γ:
 *     -DOBS_KEEP_TANH=0 -DDEF_SWITCH=SW_SIGMOID
 */
#ifndef OBS_KEEP_TANH
#define OBS_KEEP_TANH 1
#endif

#if OBS_IMPL >= 5
#include "Observer_Sin_LUT.h"
#endif

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
#define DEF_RS           8.69e-3f   /* R_Y = R_LL/2 (TCC, tab. de resistência) */
#define DEF_LS           6.95e-6f   /* L_Y = L_LL/2 (TCC) */
#define DEF_KV           310.0f     /* Kv de catálogo já é o valor a usar: sem /√3 */
/* Γ do autotuning (autotune_cost.py, differential-evolution, Ts_sim = 10 µs
 * == DEF_TS). Fonte: Aplicacao_Pi/Scripts/smo_gains.json.
 *
 * Os defaults antigos (Γ1=5, Γ2=−1, Γt=0,05) NÃO faziam o observador
 * convergir: medidos em tests/observer_golden contra uma planta com torque
 * real de 0,612 N·m, davam 0,96 N·m a 1000 rpm e 1,87 N·m a 3000 rpm — erro
 * de 57 % e 205 %, e dependente da rotação quando o torque real é constante.
 * O `î` simplesmente não seguia o `i`.
 *
 * Estes vêm da simulação COM O ADC MODELADO (quantização de 12 bits, retenção
 * no frame, filtro antialiasing e o skew entre as 3 conversões) — que é o que
 * o observador de fato enxerga — e com a métrica de custo `rmse`.
 *
 * Γ1 e Γ2 batem no teto do otimizador em todas as configurações testadas; quem
 * se move é Γt, e ele é bem sensível ao que se está otimizando:
 *
 *   planta ideal   + huber : Γt = 0,066771   (Φ_e = 0,933)
 *   planta com ADC + huber : Γt = 0,029435   (Φ_e = 0,971)
 *   planta com ADC + rmse  : Γt = 0,013139   (Φ_e = 0,987)  <- este
 *
 * A direção é coerente: quanto mais realista a entrada (quantização e skew) e
 * quanto mais o custo pesa o TORQUE de fato (ver a nota sobre o Huber em
 * autotune_cost.py), mais lento o polo da BEMF precisa ser.
 *
 * ⚠️ Γ carrega Ts embutido (Γ = K·Ts). Se OBS_TS mudar, estes valores têm de
 * ser regerados com `autotune_cost.py --Ts <novo>` — não reescalados à mão.
 *
 * ⚠️ Γ1 e Γ2 param no LIMITE do otimizador (BOUNDS_LOG dá Γ1 ≤ 10^1,5 e
 * |Γ2| ≤ 10), não num ótimo interior. Alargar exige subir BL junto, porque o
 * limite de chattering é BL > Γ1/(1+Φi) — com Γ1 = 31,62 e BL = 18 a folga é
 * de só 1,12×. */
#define DEF_GAMMA1       31.62f       /* Γ_s1 > 0 (TCC, APC 15x6E) */
#define DEF_GAMMA2      (-10.00f)     /* Γ_s2 < 0 (TCC, APC 15x6E) */
#define DEF_GAMMAT       0.154f       /* Γ_t  > 0 (TCC, APC 15x6E) */
#define DEF_TORQUE_LPF   10000.0f  /* fc_torque do script */
#define DEF_BEMF_LPF     0.0f      /* 0 = desligado → PLL usa ê direto (== script) */
#define DEF_TS           10.0e-6f  /* 10 µs */
#define DEF_BL           18.0f
/* Filtro passa-baixas das SAÍDAS publicadas (rpm e torque), igual ao
 * fc_plot do script (test_coeff_auto.py). Não entra na malha do observador. */
#define OBS_OUT_LPF_HZ   2.0f

/* tanh nas duas variantes (== SMO_SWITCH_FUNC=1 do test_coeff_auto.py, que é
 * com o que os Γ do smo_gains.json foram otimizados). A sigmoide é opt-in —
 * ver a nota de OBS_KEEP_TANH acima. */
#ifndef DEF_SWITCH
#define DEF_SWITCH       SW_TANH
#endif

#define DEF_BEMF_KIND    1         /* 1 = senoidal, 0 = trapezoidal */

#if OBS_IMPL >= 5
/* O V5 troca as 6 avaliações de f_shape() pelo par (sin, -cos), o que vale
 * SÓ para BEMF senoidal:
 *     (1/raiz(3))*(sin(th-2pi/3) - sin(th-4pi/3)) == -cos(th)
 * Para trapezoidal a identidade é FALSA e o erro não aparece nem em
 * compilação nem em runtime — vira ângulo e torque errados, silenciosamente.
 * O caminho para trapezoidal é LUT com 3 consultas de f_shape, não voltar
 * para libm. Ver CLAUDE.md. */
_Static_assert(DEF_BEMF_KIND == 1,
    "OBS_IMPL>=5: o par (sin, -cos) assume BEMF senoidal. "
    "Para trapezoidal, usar a LUT com 3 consultas de f_shape.");
#endif

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
    float alpha_out;   /* LPF das saídas publicadas (OBS_OUT_LPF_HZ) */
    float Kp_pll;      /* 2ζω_n */
    float Ki_pll;      /* ω_n² */
    float Ke;          /* Ke_mec/p */
    float Kt;          /* 1.5·ke_mec — o p de Te=1.5·Ke·p cancela com Ke=ke_mec/p */
    float inv_p;       /* 1/p */
    float ts;          /* cópia rápida do passo */
    float bl;
    float inv_bl;      /* 1/BL (fora do laço: era uma FDIV por passo no V0) */
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

/* Valores αβ do último Observer_Step_ABC (lidos na same_wq, sem lock). */
static float out_v_alpha;
static float out_v_beta;
static float out_i_alpha;
static float out_i_beta;

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
#if OBS_IMPL < 5
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
#endif /* OBS_IMPL < 5 */

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
#if OBS_KEEP_TANH
    if (kind == SW_TANH) {
        return tanhf(x);
    }
#else
    /* Sob V5 o ramo tanh NÃO é compilado — é o que tira tanhf (e a bagagem de
     * libm que vem junto) do binário e paga a flash da LUT. switch_func não
     * tem target de CMD, então a escolha é de compilação; um SW_TANH residual
     * cai na sigmoide, que é a mesma forma (ver CLAUDE.md). */
#endif
    if (kind == SW_SAT) {
        if (x > 1.0f) return 1.0f;
        if (x < -1.0f) return -1.0f;
        return x;
    }
    /* sigmoide algébrica */
    return x / (1.0f + fabsf(x));
}

/**
 * @brief Envolve um ângulo em [0, 2*pi), sem laço (trata transitório grande).
 * @param th ângulo em rad (qualquer valor)
 * @return ângulo equivalente em [0, 2*pi)
 */
static inline float __attribute__((always_inline))
wrap_2pi(float th)
{
#if OBS_IMPL < 5
    th -= TWO_PI * floorf(th * INV_TWO_PI);
    return th;
#else
    /* Caminho comum: θ avança ω_e·Ts < 2π por passo, então um único
     * ajuste resolve — dois testes e uma subtração, sem chamada. */
    if (th >= TWO_PI) {
        th -= TWO_PI;
        if (th < TWO_PI) return th;
    } else if (th >= 0.0f) {
        return th;
    } else {
        th += TWO_PI;
        if (th >= 0.0f) return th;
    }
    /* Fallback para transitório grande (ω_e explodindo antes do lock).
     * Trunca com cast pra int em vez de floorf(): é o floorf que arrasta
     * libm de volta e come a economia de flash da LUT. */
    th -= TWO_PI * (float)(int32_t)(th * INV_TWO_PI);
    if (th < 0.0f) th += TWO_PI;
    return th;
#endif
}

#if OBS_IMPL >= 5
/**
 * @brief sin/cos de um ângulo por LUT512 com interpolação linear.
 *
 * O cosseno sai da MESMA tabela deslocando o índice de N/4 (+π/2), então as
 * duas saídas custam dois pares de leituras e um mul-add cada — nenhuma
 * chamada a libm. Ver Observer_Sin_LUT.h.
 *
 * @param th ângulo em rad; deve ser >= 0 (wrap_2pi garante). O AND com
 *           OBS_LUT_MASK cobre o caso de borda th == 2π exato (arredondamento).
 * @param s [out] sin(th)
 * @param c [out] cos(th)
 */
static inline void __attribute__((always_inline))
sincos_lut(float th, float *s, float *c)
{
    float x = th * ((float)OBS_LUT_N / TWO_PI);   /* rad → índice fracionário */
    uint32_t i = (uint32_t)x;
    float frac = x - (float)i;

    uint32_t is = i & OBS_LUT_MASK;
    uint32_t ic = (i + OBS_LUT_QUARTER) & OBS_LUT_MASK;

    float s0 = obs_sin_lut[is], s1 = obs_sin_lut[is + 1];
    float c0 = obs_sin_lut[ic], c1 = obs_sin_lut[ic + 1];

    *s = s0 + frac * (s1 - s0);
    *c = c0 + frac * (c1 - c0);
}
#endif /* OBS_IMPL >= 5 */

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

    float wo = TWO_PI * OBS_OUT_LPF_HZ * ts;
    c->alpha_out = wo / (1.0f + wo);

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

    /* Te = 1.5·Ke·p com Ke = ke_mec/p ⇒ o p cancela. Kt = 1.5·ke_mec é
     * algebricamente idêntico ao que o V0 calcula no laço, sem a FDIV do
     * round-trip 1/inv_p. Consequência: pole_pairs deixa de zerar Te quando
     * p ≤ 0 (só afeta rpm_same, via inv_p) — daí a guarda explícita abaixo,
     * que preserva o comportamento do V0. Ver CLAUDE.md. */
    c->Kt = (p->pole_pairs > 0.0f) ? (1.5f * ke_mec) : 0.0f;

    c->bl          = p->bl;
    c->inv_bl      = (p->bl != 0.0f) ? (1.0f / p->bl) : 0.0f;
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

    LOG_INF("Observer init (V%d): p=%d Rs=%.4f L=%.2eH Kv=%.1f Ts=%.1fus",
            OBS_IMPL,
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

#if OBS_IMPL < 5
/**
 * @brief V0 — executa um passo do SMO+PLL (referencial αβ). Caminho quente,
 * sem lock, estado/constantes em registradores de FP.
 *
 * BASELINE do A/B contra o V5: transcrição direta do laço do
 * test_coeff_auto.py. Não otimizar — é o que dá sentido ao número de
 * referência (ver CLAUDE.md e tests/observer_golden/).
 *
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
    /* Saídas publicadas passam pelo LPF de exibição (OBS_OUT_LPF_HZ). */
    out_rpm += live_const.alpha_out * (omega_mech * (30.0f / (float)M_PI) - out_rpm);
    out_te  += live_const.alpha_out * (tq_lpf - out_te);
    out_amp_e     = amp_E;      /* saúde: amplitude da BEMF (lock da PLL) */
    out_phase_err = erro_fase;  /* saúde: erro de fase da PLL */
    /* torque_helice: em regime a carga da hélice = torque eletromagnético
     * estimado. Quando B/τc (atrito do conjunto) forem
     * embarcados, subtrair (B·ω+τc). Por ora, espelha Te. */
    out_torque_helice = out_te;
}

#else /* OBS_IMPL >= 5 */

/**
 * @brief V5 — um passo do SMO+PLL (referencial αβ), caminho quente otimizado.
 *
 * Mesmas equações do V0; o que muda é como f(·) é avaliada:
 *
 *  - identidade (só para BEMF senoidal, travada pelo _Static_assert acima):
 *        g_α = sin θ ,  g_β = (1/√3)(sin(θ−2π/3) − sin(θ−4π/3)) ≡ −cos θ
 *    → as 6 avaliações de f_shape() viram 2 sincos.
 *  - os 2 sincos ficam em POSIÇÕES DIFERENTES de propósito: a PLL usa θ̂
 *    ANTES do update, o torque usa θ_out DEPOIS. Reusar um só introduziria
 *    atraso de uma amostra (0,53 % no torque a 3000 rpm) para economizar ~5
 *    instruções — não vale (ver CLAUDE.md).
 *  - Kt e inv_bl vêm de obs_const (duas FDIV a menos por passo).
 *
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
    register const float Kt       = live_const.Kt;      /* 1.5·ke_mec */
    register const float inv_p    = live_const.inv_p;
    register const float ts       = live_const.ts;
    register const float inv_bl   = live_const.inv_bl;
    const int sw = live_const.switch_func;

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

    /* ── PLL (1º sincos: θ̂ ANTES do update) ── */
    register float amp_E = sqrtf(pll_e_al * pll_e_al + pll_e_be * pll_e_be);
    register float erro_fase;
    if (amp_E > 1.0f) {
        float sin_th, cos_th;
        sincos_lut(th_est, &sin_th, &cos_th);
        /* (g_α·ê_β − g_β·ê_α)/|ê| com g_α=sin, g_β=−cos */
        erro_fase = (sin_th * pll_e_be + cos_th * pll_e_al) / amp_E;
    } else {
        erro_fase = 0.0f;
    }

    integral_pll += erro_fase * ts;
    register float omega_est_elec = Kp_pll * erro_fase + Ki_pll * integral_pll;
    th_est = wrap_2pi(th_est + omega_est_elec * ts);

    /* θ_out = θ̂ (compensação de fase do front-end analógico fica desligada
     * por padrão — ver test_coeff_auto.py phase_mode==3). */
    register float th_out = th_est;

    /* ── Torque (2º sincos: θ_out DEPOIS do update) ──
     * Te = 1.5·Ke·p·(f_a·î_α + f_b·î_β), f_a = sin θ_out, f_b = −cos θ_out. */
    float sin_o, cos_o;
    sincos_lut(th_out, &sin_o, &cos_o);
    register float t_inst = Kt * (sin_o * i_hat_al - cos_o * i_hat_be);
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
    register float omega_mech = omega_est_elec * inv_p;
    /* Saídas publicadas passam pelo LPF de exibição (OBS_OUT_LPF_HZ). */
    out_rpm += live_const.alpha_out * (omega_mech * (30.0f / (float)M_PI) - out_rpm);
    out_te  += live_const.alpha_out * (tq_lpf - out_te);
    out_amp_e     = amp_E;
    out_phase_err = erro_fase;
    out_torque_helice = out_te;
}

#endif /* OBS_IMPL */

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

/* Clarke 3-termos amplitude-invariante — usada em Observer_Step_ABC. */
#define TWO_THIRDS_V   0.6666666666666666f
#define INV_SQRT3_V    0.5773502691896258f

/**
 * @brief Clarke transform (amplitude-invariante, 3 termos) + Observer_Step.
 * Computa αβ a partir de V/A por fase e avança o SMO. Armazena os αβ para os
 * getters, para o HAL publicar na telemetria.
 */
void Observer_Step_ABC(float Va, float Vb, float Vc, float ia, float ib, float ic)
{
    float v_al = TWO_THIRDS_V * (Va - 0.5f * Vb - 0.5f * Vc);
    float v_be = INV_SQRT3_V  * (Vb - Vc);
    float i_al = TWO_THIRDS_V * (ia - 0.5f * ib - 0.5f * ic);
    float i_be = INV_SQRT3_V  * (ib - ic);

    out_v_alpha = v_al;
    out_v_beta  = v_be;
    out_i_alpha = i_al;
    out_i_beta  = i_be;

    Observer_Step(v_al, v_be, i_al, i_be);
}

float Observer_Get_V_Alpha(void) { return out_v_alpha; }
float Observer_Get_V_Beta(void)  { return out_v_beta;  }
float Observer_Get_I_Alpha(void) { return out_i_alpha; }
float Observer_Get_I_Beta(void)  { return out_i_beta;  }

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

/**
 * @brief Lê o valor atual de um parâmetro do observador a partir de
 * pending_params (inclui CMDs ainda não aplicados pelo laço do ADC).
 * TARGET_TS_ADC é devolvido em µs (igual ao formato do CMD).
 * @param target TARGET_* do parâmetro
 * @param value  [out] valor atual
 * @return true se o target pertence ao observador
 */
bool Observer_Get_Param(target_type target, float *value)
{
    k_mutex_lock(&cfg_mutex, K_FOREVER);
    bool ok = true;
    switch (target) {
        case TARGET_POLES_PAIRS:          *value = pending_params.pole_pairs;       break;
        case TARGET_RESISTANCE:           *value = pending_params.Rs;               break;
        case TARGET_INDUCTANCE:           *value = pending_params.Ls;               break;
        case TARGET_KV_RATING:            *value = pending_params.Kv;               break;
        case TARGET_GAMMA1:               *value = pending_params.gamma1;           break;
        case TARGET_GAMMA2:               *value = pending_params.gamma2;           break;
        case TARGET_GAMMAT:               *value = pending_params.gammat;           break;
        case TARGET_BEMF_LPF_FREQUENCY:   *value = pending_params.bemf_lpf;        break;
        case TARGET_TORQUE_LPF_FREQUENCY: *value = pending_params.torque_lpf;      break;
        case TARGET_TS_ADC:               *value = pending_params.ts * 1e6f;       break;
        default: ok = false; break;
    }
    k_mutex_unlock(&cfg_mutex);
    return ok;
}
