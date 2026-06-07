#include "Log_Manager.h"
#include "OSSI.h"
#include "Security_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(log_manager, LOG_LEVEL_DBG);

static TELEMETRY_LOG current_log;
static struct k_mutex log_mutex;

void Log_Manager_Init(void)
{
    k_mutex_init(&log_mutex);

    k_mutex_lock(&log_mutex, K_FOREVER);
    memset(&current_log, 0, sizeof(current_log));
    current_log.status = LOG_VALID;
    k_mutex_unlock(&log_mutex);

    LOG_INF("Log_Manager inicializado (TX a cada %d ms via Sys)",
            LOG_MANAGER_PERIOD_MS);
}

void Log_Manager_Update(uint32_t sensor_id, float value)
{
    k_mutex_lock(&log_mutex, K_FOREVER);
    switch (sensor_id) {
        case SENSOR_ID_LOAD_CELL_0:   current_log.load_cell_0        = value; break;
        case SENSOR_ID_LOAD_CELL_1:   current_log.load_cell_1        = value; break;
        case SENSOR_ID_LOAD_CELL_2:   current_log.load_cell_2        = value; break;
        case SENSOR_ID_LOAD_CELL_3:   current_log.load_cell_3        = value; break;
        case SENSOR_ID_LOAD_CELL_4:   current_log.load_cell_4        = value; break;
        case SENSOR_ID_LOAD_CELL_5:   current_log.load_cell_5        = value; break;
        case SENSOR_ID_V_BATT:        current_log.v_batt             = value; break;
        case SENSOR_ID_I_BATT:        current_log.i_batt             = value; break;
        case SENSOR_ID_SOC:           current_log.soc                = value; break;
        case SENSOR_ID_TEMPERATURE_BATTERY:  current_log.temperature_battery= value; break;
        case SENSOR_ID_AIR_SPEED:     current_log.air_speed          = value; break;
        case SENSOR_ID_RPM:           current_log.rpm                = value; break;
        default:
            LOG_WRN("Log_Manager: sensor_id desconhecido %d", sensor_id);
            break;
    }
    k_mutex_unlock(&log_mutex);
}

TELEMETRY_LOG Log_Manager_Get_Snapshot(void)
{
    TELEMETRY_LOG snap;
    k_mutex_lock(&log_mutex, K_FOREVER);
    memcpy(&snap, &current_log, sizeof(snap));
    k_mutex_unlock(&log_mutex);
    return snap;
}

/*
 * Log_Manager_Transmit
 * Monta o pacote TELEMETRY_LOG e envia via OSSI→NCI (UDP). Chamada
 * periodicamente (a cada LOG_MANAGER_PERIOD_MS) pela thread única do
 * Sys — ver Sys_thread_fn.
 */
void Log_Manager_Transmit(void)
{
    _Static_assert(sizeof(TELEMETRY_LOG) <= 128,
                   "TELEMETRY_LOG excede payload do OSSI");

    ossi_msg_t msg = {
        .target   = TARGET_NETWORK,
        .req_type = REQ_TYPE_NET_SEND_TELEMETRY,
    };

    k_mutex_lock(&log_mutex, K_FOREVER);
    current_log.timestamp  = k_uptime_get_32();
    current_log.error_code = Security_Manager_Get_Error_Code();
    memcpy(msg.payload.raw, &current_log, sizeof(TELEMETRY_LOG));
    k_mutex_unlock(&log_mutex);

    if (OSSI_Send(&msg) != 0) {
        LOG_WRN("Log_Manager: fila NCI cheia, frame descartado");
    }
}
