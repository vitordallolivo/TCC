/*
 * Propeller_Manager.c — SIBA Board
 *
 * Responsável por proteger o conjunto motor/ESC/hélice contra sobrecorrente:
 * monitora i_batt (corrente do motor) e nunca permite que ela ultrapasse
 * PROPELLER_DEFAULT_MAX_CURRENT_A — se isso acontecer, o throttle pedido
 * pelo usuário (via CMD FUNC_ESC) é sobrescrito/reduzido progressivamente
 * até a corrente normalizar, e só então o pedido original é retomado.
 *
 * Cálculo de Ct/Cp da hélice (força + RPM) continua como TODO — ver CLAUDE.md.
 */

#include "Propeller_Manager.h"
#include "HAL.h"
#include "Log_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(propeller_mgr, LOG_LEVEL_DBG);

static struct k_mutex prop_mutex;

static float max_current_a      = PROPELLER_DEFAULT_MAX_CURRENT_A;
static float last_current_a     = 0.0f;
static float requested_throttle = 0.0f;  /* último pedido do usuário (%)     */
static float applied_throttle   = 0.0f;  /* throttle de fato aplicado no ESC */

/* Aplica o throttle no ESC, armando-o automaticamente se necessário
 * (mesmo comportamento que CMD_Core::handle_func_esc tinha antes). */
static void apply_throttle(float pct)
{
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    int r = HAL_ESC_Set_Throttle(pct);
    if (r == -EPERM) {
        LOG_WRN("Propeller: ESC não armado, armando agora...");
        HAL_ESC_Arm();
        r = HAL_ESC_Set_Throttle(pct);
    }

    if (r == 0) {
        applied_throttle = pct;
    }
}

void Propeller_Manager_Init(void)
{
    k_mutex_init(&prop_mutex);

    max_current_a      = PROPELLER_DEFAULT_MAX_CURRENT_A;
    last_current_a     = 0.0f;
    requested_throttle = 0.0f;
    applied_throttle   = 0.0f;

    LOG_INF("Propeller_Manager inicializado (limite de corrente = %.1fA)",
            (double)max_current_a);
}

int Propeller_Manager_Request_Throttle(float percent)
{
    k_mutex_lock(&prop_mutex, K_FOREVER);

    requested_throttle = percent;
    float target = requested_throttle;

    /* Se a corrente já está acima do limite, não permite AUMENTAR o
     * throttle — mantém o que está aplicado (a redução fica a cargo de
     * Propeller_Manager_Update, disparada pelas leituras de i_batt). */
    if (last_current_a > max_current_a && target > applied_throttle) {
        LOG_WRN("Propeller: pedido de throttle %.1f%% retido — corrente %.1fA > limite %.1fA",
                (double)percent, (double)last_current_a, (double)max_current_a);
        target = applied_throttle;
    }

    apply_throttle(target);

    k_mutex_unlock(&prop_mutex);
    return 0;
}

void Propeller_Manager_Update(uint32_t sensor_id, float value)
{
    if (sensor_id != SENSOR_ID_I_BATT) {
        return;
    }

    k_mutex_lock(&prop_mutex, K_FOREVER);
    last_current_a = value;

    if (last_current_a > max_current_a) {
        /* Corrente acima do permitido: sobrescreve o pedido do usuário,
         * reduzindo o PWM do ESC em passos até a corrente normalizar. */
        float reduced = applied_throttle - PROPELLER_THROTTLE_STEP_PCT;
        if (reduced < 0.0f) reduced = 0.0f;

        if (reduced != applied_throttle) {
            LOG_WRN("Propeller: corrente %.1fA > limite %.1fA — throttle %.1f%% -> %.1f%%",
                    (double)last_current_a, (double)max_current_a,
                    (double)applied_throttle, (double)reduced);
            apply_throttle(reduced);
        }
    } else if (applied_throttle < requested_throttle) {
        /* Corrente normalizada: retoma gradualmente em direção ao pedido
         * original do usuário (sem ultrapassar o limite de novo). */
        float restored = applied_throttle + PROPELLER_THROTTLE_STEP_PCT;
        if (restored > requested_throttle) restored = requested_throttle;
        apply_throttle(restored);
    }

    k_mutex_unlock(&prop_mutex);
}

void Propeller_Manager_Set_Max_Current(float amps)
{
    if (amps <= 0.0f) {
        LOG_WRN("Propeller: limite de corrente inválido (%.1fA)", (double)amps);
        return;
    }

    k_mutex_lock(&prop_mutex, K_FOREVER);
    max_current_a = amps;
    k_mutex_unlock(&prop_mutex);

    LOG_INF("Propeller: limite de corrente ajustado para %.1fA", (double)amps);
}
