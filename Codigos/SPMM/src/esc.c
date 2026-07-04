/*
 * ESC — PWM de servo (TIM4 CH1 / PB6) para o throttle do ESC comercial.
 * O SPMM NAO gera PWM trifasico; so comanda o throttle (1000-2000us @ 50Hz).
 *
 * Reaproveita o padrao de PWM da SIBA (HAL_ESC_*), reconfigurado para servo.
 * Em native_sim (CONFIG_ARCH_POSIX) nao ha TIM4 -> throttle synthetic (so
 * guarda o valor, para rpm.c/acquisition.c "seguirem" o throttle).
 */
#include "spmm.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(esc, LOG_LEVEL_INF);

static bool     esc_armed;
static uint32_t esc_throttle_us = SPMM_ESC_MIN_US;

uint32_t esc_get_throttle_us(void) { return esc_throttle_us; }

#ifndef CONFIG_ARCH_POSIX
/* -------------------- Placa real: TIM4/PWM via API generica -------------- */
#include <zephyr/drivers/pwm.h>

static const struct pwm_dt_spec esc_pwm = PWM_DT_SPEC_GET(DT_NODELABEL(esc_motor));

static int esc_apply_us(uint32_t us)
{
    return pwm_set_dt(&esc_pwm, PWM_USEC(SPMM_ESC_PERIOD_US), PWM_USEC(us));
}

int esc_init(void)
{
    if (!pwm_is_ready_dt(&esc_pwm)) {
        LOG_ERR("ESC PWM nao pronto");
        return -ENODEV;
    }
    esc_armed = false;
    esc_throttle_us = SPMM_ESC_ARM_US;
    int r = esc_apply_us(SPMM_ESC_ARM_US);
    LOG_INF("ESC: PWM servo inicializado (%dus)", SPMM_ESC_ARM_US);
    return r;
}
#else
/* -------------------- native_sim: throttle synthetic -------------------- */
static int esc_apply_us(uint32_t us) { ARG_UNUSED(us); return 0; }

int esc_init(void)
{
    esc_armed = false;
    esc_throttle_us = SPMM_ESC_ARM_US;
    LOG_INF("ESC: modo synthetic (native_sim) — sem TIM4");
    return 0;
}
#endif /* CONFIG_ARCH_POSIX */

void esc_arm(void)
{
    /* Sequencia de arm: pulso minimo por 2s (padrao dos ESCs RC) */
    esc_throttle_us = SPMM_ESC_MIN_US;
    esc_apply_us(SPMM_ESC_MIN_US);
    k_sleep(K_MSEC(2000));
    esc_armed = true;
    LOG_INF("ESC: armado");
}

void esc_disarm(void)
{
    esc_throttle_us = SPMM_ESC_ARM_US;
    esc_apply_us(SPMM_ESC_ARM_US);
    esc_armed = false;
    LOG_INF("ESC: desarmado");
}

int esc_set_throttle_us(uint32_t us)
{
    if (!esc_armed) {
        LOG_WRN("ESC: throttle ignorado — nao armado");
        return -EPERM;
    }
    if (us < SPMM_ESC_MIN_US) us = SPMM_ESC_MIN_US;
    if (us > SPMM_ESC_MAX_US) us = SPMM_ESC_MAX_US;
    esc_throttle_us = us;
    return esc_apply_us(us);
}

int esc_ramp_us(uint32_t target_us, uint32_t secs)
{
    if (!esc_armed) {
        LOG_WRN("ESC: ramp ignorado — nao armado");
        return -EPERM;
    }
    if (target_us < SPMM_ESC_MIN_US) target_us = SPMM_ESC_MIN_US;
    if (target_us > SPMM_ESC_MAX_US) target_us = SPMM_ESC_MAX_US;
    if (secs == 0) return esc_set_throttle_us(target_us);

    const uint32_t steps    = secs * 20U;          /* 20 passos/s (50ms)       */
    const int32_t  start_us = (int32_t)esc_throttle_us;
    const int32_t  delta    = (int32_t)target_us - start_us;

    for (uint32_t i = 1; i <= steps; i++) {
        int32_t us = start_us + (delta * (int32_t)i) / (int32_t)steps;
        esc_set_throttle_us((uint32_t)us);
        k_sleep(K_MSEC(50));
    }
    return esc_set_throttle_us(target_us);
}
