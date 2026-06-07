#include "Security_Manager.h"
#include "HAL.h"
#include "Log_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(security_mgr, LOG_LEVEL_WRN);

/* Limites operacionais */
#define LIMIT_V_BATT_MIN   18.0f   /* 6S * 3.0V/célula */
#define LIMIT_V_BATT_MAX   25.2f   /* 6S * 4.2V/célula */
#define LIMIT_I_BATT_MAX   60.0f   /* A — ajuste conforme BMS */
#define LIMIT_RPM_MAX   20000.0f

static bool emergency_active = false;

void Security_Manager_Init(void)
{
    emergency_active = false;
    LOG_INF("Security_Manager inicializado");
}

void Security_Manager_Trigger_Emergency(void)
{
    if (emergency_active) return;
    emergency_active = true;

    LOG_ERR("SECURITY: EMERGÊNCIA — ESC desarmado imediatamente!");
    HAL_ESC_Disarm();
}

void Security_Manager_Check(uint32_t sensor_id, float value)
{
    if (emergency_active) return;

    switch (sensor_id) {
        case SENSOR_ID_V_BATT:
            if (value < LIMIT_V_BATT_MIN || value > LIMIT_V_BATT_MAX) {
                LOG_ERR("SECURITY: tensão fora do limite: %.2fV", (double)value);
                Security_Manager_Trigger_Emergency();
            }
            break;
        case SENSOR_ID_I_BATT:
            if (value > LIMIT_I_BATT_MAX) {
                LOG_ERR("SECURITY: corrente excessiva: %.1fA", (double)value);
                Security_Manager_Trigger_Emergency();
            }
            break;
        case SENSOR_ID_RPM:
            if (value > LIMIT_RPM_MAX) {
                LOG_ERR("SECURITY: RPM excessivo: %.0f", (double)value);
                Security_Manager_Trigger_Emergency();
            }
            break;
        default:
            break;
    }
}
