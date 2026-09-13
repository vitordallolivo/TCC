/**
 * @file Propeller_Manager.c
 * @brief Protege motor/ESC/hélice contra sobrecorrente: monitora i_batt e
 * nunca deixa passar de PROPELLER_DEFAULT_MAX_CURRENT_A, reduzindo o
 * throttle progressivamente até normalizar e só então retomando o pedido
 * original. Cálculo de Ct/Cp da hélice ainda é TODO.
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

/* Taração/calibração e conectividade das células são do Sys, não do HAL.
 * "Configurada" = calibração explícita já gravada via CMD/shell.
 * "Conectada" = HX711 não devolve raw==-1 (24 bits em 1 = DOUT flutuando,
 * sensor ausente). */
#define PROPELLER_HX711_DISCONNECTED_RAW   (-1)

static bool cell_configured[HX711_COUNT];

/**
 * @brief Reavalia todas as 6 células e atualiza os bits agregados
 * ERROR_PROP_LOAD_CELL_NOT_CONFIGURED/_NOT_CONNECTED (basta uma com
 * problema pra setar o bit; loga só na borda).
 */
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

/**
 * @brief Aplica o throttle no ESC (sem armar — caller é responsável por
 * garantir que o ESC já está armado antes de chamar).
 * @param pct throttle desejado; clampado em [0, 100]
 */
static void apply_throttle(float pct)
{
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    if (HAL_ESC_Set_Throttle(pct) == 0) {
        applied_throttle = pct;
    }
}

/** @brief Zera estado (mutex, throttle, limite default, células) do manager. */
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

/**
 * @brief Tara a célula `idx` (ponto de entrada único; HAL só executa).
 * @param idx índice da célula (0..HX711_COUNT-1)
 */
void Propeller_Manager_Tare_Cell(uint8_t idx)
{
    if (idx >= HX711_COUNT) {
        LOG_WRN("Propeller: tare — indice de celula invalido (%u)", idx);
        return;
    }

    HAL_HX711_Tare(idx);
    LOG_INF("Propeller: celula[%u] tarada", idx);
}

/**
 * @brief Ajuste direto de scale/tare de uma célula; marca como configurada.
 * @param idx índice da célula
 * @param scale contagens por unidade
 * @param tare_raw zero de referência (contagens)
 */
void Propeller_Manager_Set_Cell_Calib(uint8_t idx, float scale, float tare_raw)
{
    if (idx >= HX711_COUNT) {
        LOG_WRN("Propeller: calib — indice de celula invalido (%u)", idx);
        return;
    }

    HAL_HX711_Set_Calib(idx, scale, tare_raw);
    cell_configured[idx] = true;
    LOG_INF("Propeller: celula[%u] calibrada (scale=%.4f tare=%d)",
            idx, (double)scale, (int)tare_raw);
}

/**
 * @brief Calibra a escala de uma célula por peso conhecido (com a célula
 * já tarada); marca a célula como configurada.
 * @param idx índice da célula
 * @param known_weight peso aplicado, na unidade desejada
 */
void Propeller_Manager_Calibrate_Cell_Weight(uint8_t idx, float known_weight)
{
    if (idx >= HX711_COUNT) {
        LOG_WRN("Propeller: calib peso — indice de celula invalido (%u)", idx);
        return;
    }

    HAL_HX711_Calibrate_Weight(idx, known_weight);
    cell_configured[idx] = true;
    LOG_INF("Propeller: celula[%u] calibrada por peso conhecido=%.3f",
            idx, (double)known_weight);
}

/**
 * @brief Aplica o pedido de throttle do usuário, retendo aumentos se a
 * corrente já estiver acima do limite (Propeller_Manager_Update reduz).
 * @param percent throttle pedido (0-100%)
 * @return 0 em sucesso, -EPERM se ESC não armado
 */
int Propeller_Manager_Request_Throttle(float percent)
{
    if (!HAL_ESC_Is_Armed()) {
        LOG_WRN("Propeller: throttle ignorado — ESC nao armado (arme via CMD de rede ou 'arm')");
        return -EPERM;
    }

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

/**
 * @brief Recebe atualizações de células de carga (reavalia saúde) e de
 * i_batt (protege contra sobrecorrente, reduzindo/restaurando o throttle).
 * @param sensor_id SENSOR_ID_LOAD_CELL_0..5 ou SENSOR_ID_I_BATT (outros
 * são ignorados)
 * @param value valor do sensor
 */
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

/**
 * @brief Sobrescreve o limite máximo de corrente do motor.
 * @param amps novo limite (A); valores <= 0 são ignorados
 */
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
