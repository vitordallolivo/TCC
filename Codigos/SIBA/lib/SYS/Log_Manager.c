#include "Log_Manager.h"
#include "OSSI.h"
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

    /* Registra thread no TCKEC */
    ossi_msg_t req = {
        .target   = TARGET_TCKEC,
        .req_type = REQ_TYPE_TCKEC_CREATE_THREAD,
    };
    req.payload.tckec_req.thread_id   = LOG_MANAGER_THREAD_ID;
    req.payload.tckec_req.entry_point = Log_Manager_thread_fn;
    req.payload.tckec_req.priority    = LOG_MANAGER_PRIO;
    req.payload.tckec_req.p1 = NULL;
    req.payload.tckec_req.p2 = NULL;
    req.payload.tckec_req.p3 = NULL;
    OSSI_Send(&req);

    LOG_INF("Log_Manager inicializado");
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
 * Log_Manager_thread_fn
 * Acorda a cada LOG_MANAGER_PERIOD_MS, monta o pacote e envia via OSSI→NCI.
 * O NCI TX vai serializar o raw[] diretamente para UDP.
 */
void Log_Manager_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    LOG_INF("Log_Manager thread iniciada (%d ms)", LOG_MANAGER_PERIOD_MS);

    _Static_assert(sizeof(TELEMETRY_LOG) <= 128,
                   "TELEMETRY_LOG excede payload do OSSI");

    while (1) {
        k_sleep(K_MSEC(LOG_MANAGER_PERIOD_MS));

        ossi_msg_t msg = {
            .target   = TARGET_NETWORK,
            .req_type = REQ_TYPE_NET_SEND_TELEMETRY,
        };

        k_mutex_lock(&log_mutex, K_FOREVER);
        current_log.timestamp = k_uptime_get_32();
        memcpy(msg.payload.raw, &current_log, sizeof(TELEMETRY_LOG));
        k_mutex_unlock(&log_mutex);

        if (OSSI_Send(&msg) != 0) {
            LOG_WRN("Log_Manager: fila NCI cheia, frame descartado");
        }
    }
}
