/**
 * @file rpm.c
 * @brief RPM — Input Capture (TIM3 CH3/PB0), sensor independente do
 * acionamento (14 pólos -> 7 pulsos/volta), necessário no coast-down onde
 * o ZCP some. Conta pulsos numa janela fixa (SPMM_RPM_SAMPLE_MS) via ISR e
 * converte pra RPM. native_sim sintetiza omega perseguindo o throttle do ESC.
 */
#include "spmm.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rpm, LOG_LEVEL_INF);

/* rpm_milli = RPM * 1000, consultado por spmm_get_rpm/omega */
static atomic_t rpm_milli;

/** @brief Última rotação medida. @return rpm mecânico. */
float spmm_get_rpm(void)   { return (float)atomic_get(&rpm_milli) / 1000.0f; }
/** @brief Última rotação medida, em rad/s. @return omega mecânico. */
float spmm_get_omega(void) { return spmm_rpm_to_rads(spmm_get_rpm()); }

#define RPM_STACK_SIZE 1024
#define RPM_PRIORITY   6           /* MEDIA — abaixo da aquisicao (ver main.c) */
static K_THREAD_STACK_DEFINE(rpm_stack, RPM_STACK_SIZE);
static struct k_thread rpm_thread;

/**
 * @brief Converte a contagem de pulsos da janela em RPM e publica em rpm_milli.
 * @param pulses pulsos contados em SPMM_RPM_SAMPLE_MS
 */
static inline void rpm_publish(uint32_t pulses)
{
    /* RPM = (pulsos / pulsos_por_volta) * (60000 / janela_ms) */
    float rpm = ((float)pulses / (float)SPMM_RPM_PULSES_PER_REV)
                * (60000.0f / (float)SPMM_RPM_SAMPLE_MS);
    atomic_set(&rpm_milli, (atomic_val_t)(rpm * 1000.0f));
}

#ifndef CONFIG_ARCH_POSIX
/* -------------------- Placa real: TIM3 via MMIO direto ------------------ */
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>

#define TIM3_BASE        0x40000400UL
#define TIM3_INTFR       (*(volatile uint32_t *)(TIM3_BASE + 0x10))
#define TIM3_CHCTLR2     (*(volatile uint32_t *)(TIM3_BASE + 0x1C))
#define TIM3_CCER        (*(volatile uint32_t *)(TIM3_BASE + 0x20))
#define TIM3_PSC         (*(volatile uint32_t *)(TIM3_BASE + 0x28))
#define TIM3_ATRLR       (*(volatile uint32_t *)(TIM3_BASE + 0x2C))
#define TIM3_CH3CVR      (*(volatile uint32_t *)(TIM3_BASE + 0x3C))
#define TIM3_DMAINTENR   (*(volatile uint32_t *)(TIM3_BASE + 0x0C))
#define TIM3_CTLR1       (*(volatile uint32_t *)(TIM3_BASE + 0x00))

#define RCC_APB1PCENR    (*(volatile uint32_t *)(0x40021000UL + 0x1C))
#define RCC_APB1_TIM3EN  (1UL << 1)

#define TIM_CC3S_IC3_TI3 (1UL << 0)   /* CHCTLR2.CC3S = 01: IC3 mapeado em TI3 */
#define TIM_CCER_CC3E    (1UL << 8)
#define TIM_CCER_CC3P    (1UL << 9)   /* 0 = captura na borda de subida       */
#define TIM_DIER_CC3IE   (1UL << 3)
#define TIM_SR_CC3IF     (1UL << 3)
#define TIM_CTLR1_CEN    (1UL << 0)
#define TIM_CTLR1_ARPE   (1UL << 7)
#define TIM3_IRQN        45

#define RPM_GPIO_NODE DT_NODELABEL(rpm_capture_pin)
static const struct gpio_dt_spec rpm_pin =
    GPIO_DT_SPEC_GET_BY_IDX(RPM_GPIO_NODE, gpios, 0);

static atomic_t pulse_count;   /* incrementado na ISR a cada captura */

/**
 * @brief ISR de input capture do TIM3 CH3: limpa a flag e conta 1 pulso.
 * @param arg não usado
 */
static void tim3_capture_isr(const void *arg)
{
    ARG_UNUSED(arg);
    if (TIM3_INTFR & TIM_SR_CC3IF) {
        (void)TIM3_CH3CVR;             /* leitura limpa overcapture (CC3OF)   */
        TIM3_INTFR &= ~TIM_SR_CC3IF;   /* limpa flag de captura               */
        atomic_inc(&pulse_count);
    }
}

/**
 * @brief Configura o GPIO PB0 e o TIM3 CH3 em input capture, e conecta a ISR.
 * @return 0 em sucesso, erro do gpio_pin_configure_dt
 */
int rpm_init(void)
{
    int err = gpio_pin_configure_dt(&rpm_pin, GPIO_INPUT);
    if (err) {
        LOG_ERR("RPM: GPIO PB0 falhou (%d)", err);
        return err;
    }
    RCC_APB1PCENR |= RCC_APB1_TIM3EN;

    TIM3_CTLR1     = 0;
    TIM3_PSC       = 0;
    TIM3_ATRLR     = 0xFFFF;
    TIM3_CHCTLR2   = (TIM3_CHCTLR2 & ~0x3u) | TIM_CC3S_IC3_TI3;
    TIM3_CCER      = (TIM3_CCER & ~(TIM_CCER_CC3E | TIM_CCER_CC3P)) | TIM_CCER_CC3E;
    TIM3_DMAINTENR |= TIM_DIER_CC3IE;
    TIM3_INTFR     = 0;
    TIM3_CTLR1     = TIM_CTLR1_ARPE | TIM_CTLR1_CEN;

    IRQ_CONNECT(TIM3_IRQN, 1, tim3_capture_isr, NULL, 0);
    irq_enable(TIM3_IRQN);

    LOG_INF("RPM: TIM3 CH3 input capture inicializado (PB0)");
    return 0;
}

/**
 * @brief Thread (hardware): a cada janela, lê e zera o contador de pulsos e
 * publica o RPM. Nunca retorna.
 * @param p1, p2, p3 não usados (assinatura exigida por k_thread_create)
 */
static void rpm_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    LOG_INF("RPM: thread iniciada");
    while (1) {
        atomic_clear(&pulse_count);
        k_sleep(K_MSEC(SPMM_RPM_SAMPLE_MS));
        rpm_publish((uint32_t)atomic_get(&pulse_count));
    }
}

#else /* CONFIG_ARCH_POSIX — synthetic: omega persegue o throttle */

#define RPM_SIM_MAX_RPM   9000.0f   /* @ throttle 2000us (arbitrario)         */
#define RPM_SIM_LAG       0.15f     /* fracao do erro corrigida por amostra   */

/** @brief native_sim: stub (não há TIM3 real). @return sempre 0. */
int rpm_init(void)
{
    LOG_INF("RPM: modo synthetic (native_sim) — sem TIM3");
    return 0;
}

/**
 * @brief Thread (native_sim): persegue o throttle do ESC com atraso de 1ª
 * ordem (inclui coast-down). Nunca retorna.
 * @param p1, p2, p3 não usados (assinatura exigida por k_thread_create)
 */
static void rpm_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    LOG_INF("RPM: thread synthetic iniciada");
    float rpm = 0.0f;
    while (1) {
        k_sleep(K_MSEC(SPMM_RPM_SAMPLE_MS));
        uint32_t us = esc_get_throttle_us();
        float frac = 0.0f;
        if (us > SPMM_ESC_MIN_US) {
            frac = (float)(us - SPMM_ESC_MIN_US) /
                   (float)(SPMM_ESC_MAX_US - SPMM_ESC_MIN_US);
        }
        float target = frac * RPM_SIM_MAX_RPM;
        rpm += (target - rpm) * RPM_SIM_LAG;   /* 1a ordem (inclui coast-down) */
        atomic_set(&rpm_milli, (atomic_val_t)(rpm * 1000.0f));
    }
}
#endif /* CONFIG_ARCH_POSIX */

/** @brief Cria a thread de medição de RPM (prioridade média). */
void rpm_start(void)
{
    k_thread_create(&rpm_thread, rpm_stack, RPM_STACK_SIZE,
                    rpm_thread_fn, NULL, NULL, NULL,
                    RPM_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&rpm_thread, "rpm");
}
