/**
 * @file Log_Manager.c
 * @brief Mantém o TELEMETRY_LOG e transmite por UDP a cada Ts_udp (10 Hz
 * default). k_timer dispara k_work na same_wq -> NCI_Transmit, serializado
 * com telemetria (OSSI) e CMDs. Thread do ADC atualiza via OSSI_Submit_Sensor.
 */

#include "Log_Manager.h"
#include "NCI.h"
#include "OSSI.h"
#include "Security_Manager.h"   /* error_code agregado */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(log_manager, LOG_LEVEL_INF);

static TELEMETRY_LOG current_log;
static struct k_mutex log_mutex;

static struct k_work  log_tx_work;
static struct k_timer log_tx_timer;

/**
 * @brief Item de trabalho que efetivamente transmite o log. Roda na same_wq.
 * @param w item de trabalho (não usado)
 */
static void log_tx_work_fn(struct k_work *w)
{
    ARG_UNUSED(w);
    Log_Manager_Transmit();
}

/**
 * @brief Callback do k_timer: só agenda log_tx_work na same_wq (não
 * transmite direto, pra não rodar em contexto de interrupção do timer).
 * @param t timer que disparou (não usado)
 */
static void log_tx_timer_fn(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_work_submit_to_queue(&same_wq, &log_tx_work);
}

/**
 * @brief Inicializa o log (status LOG_VALID) e arranca o timer de TX
 * periódico. Chamar em main() antes do HAL.
 */
void Log_Manager_Init(void)
{
    k_mutex_init(&log_mutex);
    k_work_init(&log_tx_work, log_tx_work_fn);
    k_timer_init(&log_tx_timer, log_tx_timer_fn, NULL);

    k_mutex_lock(&log_mutex, K_FOREVER);
    memset(&current_log, 0, sizeof(current_log));
    current_log.status = LOG_VALID;
    k_mutex_unlock(&log_mutex);

    k_timer_start(&log_tx_timer,
                  K_MSEC(LOG_MANAGER_PERIOD_MS),
                  K_MSEC(LOG_MANAGER_PERIOD_MS));

    LOG_INF("Log_Manager inicializado (TX a cada %d ms)", LOG_MANAGER_PERIOD_MS);
}

/**
 * @brief Reconfigura o período de TX do log (via CMD TARGET_TS_UDP).
 * @param period_ms novo período em ms; 0 é ignorado (mantém o atual)
 */
void Log_Manager_Set_Period_Ms(uint32_t period_ms)
{
    if (period_ms == 0) return;
    k_timer_start(&log_tx_timer, K_MSEC(period_ms), K_MSEC(period_ms));
    LOG_INF("Log_Manager: período de TX = %u ms", period_ms);
}

/**
 * @brief Atualiza um campo do TELEMETRY_LOG corrente. Thread-safe.
 * @param sensor_id ver SENSOR_ID_* em Log_Manager.h
 * @param value valor a gravar; sensor_id desconhecido só loga um aviso
 */
void Log_Manager_Update(uint32_t sensor_id, float value)
{
    k_mutex_lock(&log_mutex, K_FOREVER);
    switch (sensor_id) {
        case SENSOR_ID_V_A:              current_log.V_a             = value; break;
        case SENSOR_ID_V_B:              current_log.V_b             = value; break;
        case SENSOR_ID_V_C:              current_log.V_c             = value; break;
        case SENSOR_ID_I_A:              current_log.i_a             = value; break;
        case SENSOR_ID_I_B:              current_log.i_b             = value; break;
        case SENSOR_ID_I_C:              current_log.i_c             = value; break;
        case SENSOR_ID_V_ALPHA:          current_log.V_alpha         = value; break;
        case SENSOR_ID_V_BETA:           current_log.V_beta          = value; break;
        case SENSOR_ID_I_ALPHA:          current_log.i_alpha         = value; break;
        case SENSOR_ID_I_BETA:           current_log.i_beta          = value; break;
        case SENSOR_ID_RPM_SAME:         current_log.rpm_same        = value; break;
        case SENSOR_ID_TORQUE_ELETRICO:  current_log.torque_eletrico = value; break;
        case SENSOR_ID_TORQUE_HELICE:    current_log.torque_helice   = value; break;
        default:
            LOG_WRN("Log_Manager: sensor_id desconhecido %u", sensor_id);
            break;
    }
    k_mutex_unlock(&log_mutex);
}

/**
 * @brief Copia o TELEMETRY_LOG corrente (thread-safe).
 * @return cópia do snapshot atual
 */
TELEMETRY_LOG Log_Manager_Get_Snapshot(void)
{
    TELEMETRY_LOG snap;
    k_mutex_lock(&log_mutex, K_FOREVER);
    memcpy(&snap, &current_log, sizeof(snap));
    k_mutex_unlock(&log_mutex);
    return snap;
}

/**
 * @brief Marca timestamp/status/error_code e transmite o log corrente (NCI).
 */
void Log_Manager_Transmit(void)
{
    TELEMETRY_LOG snap;

    k_mutex_lock(&log_mutex, K_FOREVER);
    current_log.timestamp  = k_uptime_get_32();
    current_log.status     = LOG_VALID;
    current_log.error_code = Security_Manager_Get_Error_Code();
    memcpy(&snap, &current_log, sizeof(snap));
    k_mutex_unlock(&log_mutex);

    NCI_Transmit(&snap, sizeof(snap));
}
