#include "Log_Manager.h"
#include "NCI.h"
#include "OSSI.h"
#include "Security_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(log_manager, LOG_LEVEL_DBG);

static TELEMETRY_LOG current_log;
static struct k_mutex log_mutex;

/* 10 Hz TX via k_timer → k_work → NCI_Transmit.
 * O work item roda no siba_wq (via OSSI) — serializado com o despacho de
 * sensores e CMDs, sem concorrência com Log_Manager_Update. */
static struct k_work log_tx_work;

static void log_tx_work_fn(struct k_work *w)
{
    ARG_UNUSED(w);
    Log_Manager_Transmit();
}

static void log_tx_timer_fn(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_work_submit_to_queue(&siba_wq, &log_tx_work);
}

K_TIMER_DEFINE(log_tx_timer, log_tx_timer_fn, NULL);

void Log_Manager_Init(void)
{
    k_mutex_init(&log_mutex);
    k_work_init(&log_tx_work, log_tx_work_fn);

    k_mutex_lock(&log_mutex, K_FOREVER);
    memset(&current_log, 0, sizeof(current_log));
    current_log.status = LOG_VALID;
    k_mutex_unlock(&log_mutex);

    k_timer_start(&log_tx_timer,
                  K_MSEC(LOG_MANAGER_PERIOD_MS),
                  K_MSEC(LOG_MANAGER_PERIOD_MS));

    LOG_INF("Log_Manager inicializado (TX a cada %d ms via timer)", LOG_MANAGER_PERIOD_MS);
}

void Log_Manager_Update(uint32_t sensor_id, float value)
{
    k_mutex_lock(&log_mutex, K_FOREVER);
    switch (sensor_id) {
        case SENSOR_ID_LOAD_CELL_0:          current_log.load_cell_0          = value; break;
        case SENSOR_ID_LOAD_CELL_1:          current_log.load_cell_1          = value; break;
        case SENSOR_ID_LOAD_CELL_2:          current_log.load_cell_2          = value; break;
        case SENSOR_ID_LOAD_CELL_3:          current_log.load_cell_3          = value; break;
        case SENSOR_ID_LOAD_CELL_4:          current_log.load_cell_4          = value; break;
        case SENSOR_ID_LOAD_CELL_5:          current_log.load_cell_5          = value; break;
        case SENSOR_ID_V_BATT:               current_log.v_batt               = value; break;
        case SENSOR_ID_I_BATT:               current_log.i_batt               = value; break;
        case SENSOR_ID_SOC:                  current_log.soc                  = value; break;
        case SENSOR_ID_TEMPERATURE_BATTERY:  current_log.temperature_battery  = value; break;
        case SENSOR_ID_AIR_SPEED:            current_log.air_speed            = value; break;
        case SENSOR_ID_RPM:                  current_log.rpm                  = value; break;
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

void Log_Manager_Transmit(void)
{
    TELEMETRY_LOG snap;

    k_mutex_lock(&log_mutex, K_FOREVER);
    current_log.timestamp  = k_uptime_get_32();
    current_log.error_code = Security_Manager_Get_Error_Code();
    memcpy(&snap, &current_log, sizeof(snap));
    k_mutex_unlock(&log_mutex);

    NCI_Transmit(&snap, sizeof(snap));
}
