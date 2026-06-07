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
#include "Security_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(propeller_mgr, LOG_LEVEL_DBG);

static struct k_mutex prop_mutex;

static float max_current_a      = PROPELLER_DEFAULT_MAX_CURRENT_A;
static float last_current_a     = 0.0f;
static float requested_throttle = 0.0f;  /* último pedido do usuário (%)     */
static float applied_throttle   = 0.0f;  /* throttle de fato aplicado no ESC */

/* ---------------------------------------------------------------------
 * Células de carga — taração/calibração e inferência de conectividade
 * são responsabilidade do Sys (Propeller_Manager), não do HAL: o HAL
 * só expõe leitura bruta/configuração de hardware (ver CLAUDE.md
 * "o HAL é apenas questões de configuração").
 *
 * "Configurada" = usuário já escreveu uma calibração explícita via CMD/
 * shell (Linear_Coeff/Offset_Coeff != default — ver Propeller_Manager_Set_Cell_Calib).
 * "Conectada" = HX711 responde algo diferente de "todos os bits em 1"
 * (DOUT flutuando em pull-up = HX711 ausente/desconectado — sign-extend
 * de 24 bits todos '1' vira -1 em int32_t, daí a checagem `raw == -1`).
 * --------------------------------------------------------------------- */
#define PROPELLER_HX711_DISCONNECTED_RAW   (-1)

static bool cell_configured[HX711_COUNT];

/* Atualiza o bit agregado ERROR_PROP_LOAD_CELL_NOT_CONFIGURED/_NOT_CONNECTED
 * a partir do estado de TODAS as células — basta uma estar com problema
 * para o bit ficar setado (loga só na borda, ver Error_Detect/Error_Erase). */
static void check_load_cells_health(void)
{
    bool any_unconfigured = false;
    bool any_disconnected = false;

    for (uint8_t i = 0; i < HX711_COUNT; i++) {
        if (!cell_configured[i]) {
            any_unconfigured = true;
        }
        if (HAL_HX711_Get_Raw(i) == PROPELLER_HX711_DISCONNECTED_RAW) {
            any_disconnected = true;
        }
    }

    if (any_unconfigured) {
        if (Security_Manager_Error_Detect(ERROR_PROP_LOAD_CELL_NOT_CONFIGURED)) {
            LOG_WRN("Propeller: ha celula(s) de carga sem calibracao (ver 'siba prop tare/calib')");
        }
    } else {
        Security_Manager_Error_Erase(ERROR_PROP_LOAD_CELL_NOT_CONFIGURED);
    }

    if (any_disconnected) {
        if (Security_Manager_Error_Detect(ERROR_PROP_LOAD_CELL_NOT_CONNECTED)) {
            LOG_ERR("Propeller: celula(s) de carga desconectada(s) (HX711 retornando todos os bits em 1)");
        }
    } else {
        Security_Manager_Error_Erase(ERROR_PROP_LOAD_CELL_NOT_CONNECTED);
    }
}

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

    for (uint8_t i = 0; i < HX711_COUNT; i++) {
        cell_configured[i] = false;
    }

    LOG_INF("Propeller_Manager inicializado (limite de corrente = %.1fA)",
            (double)max_current_a);
}

/* Tara a célula `idx` — ponto de entrada único (Sys/Propeller decide
 * QUANDO/SE tarar; HAL só executa a operação de hardware). */
void Propeller_Manager_Tare_Cell(uint8_t idx)
{
    if (idx >= HX711_COUNT) {
        LOG_WRN("Propeller: tare — indice de celula invalido (%u)", idx);
        return;
    }

    HAL_HX711_Tare(idx);
    LOG_INF("Propeller: celula[%u] tarada", idx);
}

/* Calibra a célula `idx` (Linear_Coeff/Offset_Coeff) — marca a célula
 * como "configurada" para Error_Erase(ERROR_PROP_LOAD_CELL_NOT_CONFIGURED). */
void Propeller_Manager_Set_Cell_Calib(uint8_t idx, float linear, float offset)
{
    if (idx >= HX711_COUNT) {
        LOG_WRN("Propeller: calib — indice de celula invalido (%u)", idx);
        return;
    }

    HAL_HX711_Set_Calib(idx, linear, offset);
    cell_configured[idx] = true;
    LOG_INF("Propeller: celula[%u] calibrada (linear=%.4f offset=%.4f)",
            idx, (double)linear, (double)offset);
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
    /* Células de carga: a cada amostra (qualquer uma das 6), reavalia a
     * "saúde" do conjunto — configuração e conectividade (ver CLAUDE.md
     * "o sys deve tentar inferir se a célula tá conectada"). */
    if (sensor_id <= SENSOR_ID_LOAD_CELL_5) {
        check_load_cells_health();
        return;
    }

    if (sensor_id != SENSOR_ID_I_BATT) {
        return;
    }

    k_mutex_lock(&prop_mutex, K_FOREVER);
    last_current_a = value;

    if (last_current_a > max_current_a) {
        if (Security_Manager_Error_Detect(ERROR_PROP_OVERCURRENT)) {
            LOG_ERR("Propeller: sobrecorrente — %.1fA > limite %.1fA",
                    (double)last_current_a, (double)max_current_a);
        }

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
    } else {
        Security_Manager_Error_Erase(ERROR_PROP_OVERCURRENT);

        if (applied_throttle < requested_throttle) {
            /* Corrente normalizada: retoma gradualmente em direção ao pedido
             * original do usuário (sem ultrapassar o limite de novo). */
            float restored = applied_throttle + PROPELLER_THROTTLE_STEP_PCT;
            if (restored > requested_throttle) restored = requested_throttle;
            apply_throttle(restored);
        }
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
