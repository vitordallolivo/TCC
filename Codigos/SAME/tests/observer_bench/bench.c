/**
 * @file bench.c
 * @brief Conta instruções retiradas (CSR minstret) por chamada de
 * Observer_Step, em rv32imafc/ilp32f — o ISA do CH32V307 — sob
 * qemu-system-riscv32.
 *
 * Por que não medir no native_sim: native_sim é x86_64. Ele valida a
 * matemática (tests/observer_golden), não o custo. E por que instret e não
 * ciclos: o QEMU não modela latência de FDIV/FSQRT, wait state de flash nem
 * misprediction. instret é um piso — o que ele responde é "quanto CPI médio
 * cada variante aguenta antes de estourar o período do frame do ADC".
 * O número de ciclo real só sai com GPIO toggle na placa.
 *
 * A medição é feita DEPOIS de a PLL travar: com |ê| abaixo do limiar o passo
 * pula o sincos inteiro e o custo medido sairia otimista demais.
 */

#include "Observer_Manager.h"
#include <stdint.h>

/* ---- UART 16550 do -machine virt ---- */
#define UART0        ((volatile uint8_t *)0x10000000)
#define UART_THR     0
#define UART_LSR     5
#define UART_LSR_THRE 0x20

static void putch(char c)
{
    while (!(UART0[UART_LSR] & UART_LSR_THRE)) { }
    UART0[UART_THR] = (uint8_t)c;
}

static void puts_(const char *s) { while (*s) putch(*s++); }

static void putu(uint32_t v)
{
    char b[12];
    int n = 0;
    if (!v) { putch('0'); return; }
    while (v) { b[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) putch(b[--n]);
}

static inline uint32_t rd_instret(void)
{
    uint32_t v;
    __asm__ volatile("csrr %0, minstret" : "=r"(v));
    return v;
}

/* mcycle no QEMU: existe, mas NÃO é microarquitetural — o QEMU não modela o
 * pipeline de 3 estágios do QingKe V4F, BHT/BTB/RAS, wait state de flash nem
 * latência de FDIV/FSQRT. Lido aqui só para deixar isso explícito na saída
 * (vem praticamente colado no instret). Além disso o CH32V307 NÃO IMPLEMENTA
 * mcycle/minstret: a lista de CSR do QingKe V4 (manual V1.5, tabela 8-1) vai
 * de mstatus/misa a pmpaddr + CSRs da WCH, sem nenhum contador de performance.
 * Ciclo real na placa se mede com o SysTick (STK, 64 bits @ HCLK,
 * 0xE000F008) — ver tests/README.md. */
static inline uint32_t rd_cycle(void)
{
    uint32_t v;
    __asm__ volatile("csrr %0, mcycle" : "=r"(v));
    return v;
}

/* ---- Estímulo sem libm ----------------------------------------------------
 * Rotação por recorrência (multiplicação complexa por e^{j·dθ}): dá sin/cos do
 * ângulo que avança sem chamar sinf, que é justamente o que está sob medição.
 * Deriva lentamente em amplitude, irrelevante para contar instruções.
 * Mesma planta do harness de equivalência: v = R_s·i + L_s·di/dt + e.        */

#define PL_TS       1.0e-5f
#define PL_P        7.0f
#define PL_RS       17.3e-3f
#define PL_LS       7.0e-6f
#define PL_KE_MEC   0.053351f          /* 60/(2π·(310/√3)) */
#define PL_I_AMP    8.0f
#define PL_RPM      3000.0f

/* cos/sin de dθ = ω_e·Ts, com ω_e = 3000·2π/60·7 = 2199.11 rad/s
 * → dθ = 0.0219911 rad */
#define D_COS       0.99975824f
#define D_SIN       0.021989327f
/* defasagem de carga: cos/sin de 0.30 rad */
#define PHI_COS     0.95533649f
#define PHI_SIN     0.29552021f

static float c_th = 1.0f, s_th = 0.0f;   /* cos θ, sin θ */

static void stim_next(float *v_al, float *v_be, float *i_al, float *i_be)
{
    const float w_m = PL_RPM * (6.2831853f / 60.0f);
    const float w_e = w_m * PL_P;
    const float E   = PL_KE_MEC * w_m;

    /* θ_i = θ − φ */
    const float s_i = s_th * PHI_COS - c_th * PHI_SIN;
    const float c_i = c_th * PHI_COS + s_th * PHI_SIN;

    const float e_al =  E * s_th;
    const float e_be = -E * c_th;

    *i_al =  PL_I_AMP * s_i;
    *i_be = -PL_I_AMP * c_i;

    const float di_al =  PL_I_AMP * w_e * c_i;
    const float di_be =  PL_I_AMP * w_e * s_i;

    *v_al = PL_RS * (*i_al) + PL_LS * di_al + e_al;
    *v_be = PL_RS * (*i_be) + PL_LS * di_be + e_be;

    /* avança θ */
    const float ns = s_th * D_COS + c_th * D_SIN;
    const float nc = c_th * D_COS - s_th * D_SIN;
    s_th = ns; c_th = nc;
}

/* Referência para descontar o custo do laço + do estímulo, que não é do
 * observador. `noinline` + volatile para o compilador não engolir a chamada. */
static volatile float sink;
static void __attribute__((noinline))
step_null(float a, float b, float c, float d) { sink = a + b + c + d; }

#define WARMUP  20000u   /* passos até a PLL travar (|ê| acima do limiar) */
#define MEASURE  2000u

void bench_main(void)
{
    float v_al, v_be, i_al, i_be;

    Observer_Init();
    /* Γ do autotuning — é o regime em que o observador de fato opera; com os
     * DEF_GAMMA* de fábrica a PLL não trava e o custo medido seria de um
     * caminho que o firmware não percorre. */
    Observer_Set_Param(TARGET_GAMMA1,  31.622776f);
    Observer_Set_Param(TARGET_GAMMA2, -10.0f);
    Observer_Set_Param(TARGET_GAMMAT,   0.06677100f);
    Observer_Apply_Pending();

    for (uint32_t n = 0; n < WARMUP; n++) {
        stim_next(&v_al, &v_be, &i_al, &i_be);
        Observer_Step(v_al, v_be, i_al, i_be);
    }

    /* laço de referência (estímulo + chamada vazia) */
    uint32_t i0 = rd_instret(), c0 = rd_cycle();
    for (uint32_t n = 0; n < MEASURE; n++) {
        stim_next(&v_al, &v_be, &i_al, &i_be);
        step_null(v_al, v_be, i_al, i_be);
    }
    uint32_t i1 = rd_instret(), c1 = rd_cycle();

    /* laço real */
    uint32_t i2 = rd_instret(), c2 = rd_cycle();
    for (uint32_t n = 0; n < MEASURE; n++) {
        stim_next(&v_al, &v_be, &i_al, &i_be);
        Observer_Step(v_al, v_be, i_al, i_be);
    }
    uint32_t i3 = rd_instret(), c3 = rd_cycle();

    const uint32_t instr_ovh = (i1 - i0) / MEASURE;
    const uint32_t instr_tot = (i3 - i2) / MEASURE;
    const uint32_t cyc_ovh   = (c1 - c0) / MEASURE;
    const uint32_t cyc_tot   = (c3 - c2) / MEASURE;

    puts_("OBS_IMPL="); putu((uint32_t)OBS_IMPL);
    puts_(" tanh=");    putu((uint32_t)OBS_KEEP_TANH);
    puts_(" instret_step="); putu(instr_tot - instr_ovh);
    puts_(" mcycle_step=");  putu(cyc_tot - cyc_ovh);
    puts_(" overhead=");     putu(instr_ovh);
    /* Sanidade: se a PLL não travou, o custo não é o do caminho real. */
    puts_(" rpm=");          putu((uint32_t)Observer_Get_Rpm());
    puts_(" ampE=");         putu((uint32_t)Observer_Get_Bemf_Amplitude());
    puts_("\n");
}
