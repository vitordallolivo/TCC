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

/* Código de erro agregado — Sys roda em uma única thread (ver CLAUDE.md),
 * então não há concorrência real entre os managers; ainda assim o acesso
 * é sempre feito através de Error_Detect/Error_Erase/Get_Error_Code. */
static uint32_t error_code = 0U;

void Security_Manager_Init(void)
{
    emergency_active = false;
    error_code       = 0U;
    LOG_INF("Security_Manager inicializado");
}

bool Security_Manager_Error_Detect(siba_error_bit_t bit)
{
    if (error_code & (uint32_t)bit) {
        return false; /* já estava setado — sem mudança de borda */
    }
    error_code |= (uint32_t)bit;
    return true;
}

bool Security_Manager_Error_Erase(siba_error_bit_t bit)
{
    if (!(error_code & (uint32_t)bit)) {
        return false; /* já estava limpo — sem mudança de borda */
    }
    error_code &= ~(uint32_t)bit;
    return true;
}

uint32_t Security_Manager_Get_Error_Code(void)
{
    return error_code;
}

void Security_Manager_Trigger_Emergency(void)
{
    if (emergency_active) return;
    emergency_active = true;

    Security_Manager_Error_Detect(ERROR_SEC_EMERGENCY_ACTIVE);

    LOG_ERR("SECURITY: EMERGÊNCIA — ESC desarmado imediatamente!");
    HAL_ESC_Disarm();
}

/*
 * Security_Manager_Check
 *
 * Verifica se o valor recebido faz sentido dentro dos limites
 * operacionais (padrão Error_Detect/Error_Erase do CMP_BackgroundHandler:
 * seta o bit enquanto a condição persiste, limpa assim que ela cessa —
 * loga só na borda, nunca a cada amostra).
 */
void Security_Manager_Check(uint32_t sensor_id, float value)
{
    switch (sensor_id) {
        case SENSOR_ID_V_BATT:
            if (value < LIMIT_V_BATT_MIN || value > LIMIT_V_BATT_MAX) {
                if (Security_Manager_Error_Detect(ERROR_SEC_VOLTAGE_OUT_OF_RANGE)) {
                    LOG_ERR("SECURITY: tensão fora do limite: %.2fV", (double)value);
                }
                if (!emergency_active) {
                    Security_Manager_Trigger_Emergency();
                }
            } else {
                Security_Manager_Error_Erase(ERROR_SEC_VOLTAGE_OUT_OF_RANGE);
            }
            break;

        case SENSOR_ID_I_BATT:
            if (value > LIMIT_I_BATT_MAX) {
                if (Security_Manager_Error_Detect(ERROR_SEC_CURRENT_EXCESSIVE)) {
                    LOG_ERR("SECURITY: corrente excessiva: %.1fA", (double)value);
                }
                if (!emergency_active) {
                    Security_Manager_Trigger_Emergency();
                }
            } else {
                Security_Manager_Error_Erase(ERROR_SEC_CURRENT_EXCESSIVE);
            }
            break;

        case SENSOR_ID_RPM:
            if (value > LIMIT_RPM_MAX) {
                if (Security_Manager_Error_Detect(ERROR_SEC_RPM_EXCESSIVE)) {
                    LOG_ERR("SECURITY: RPM excessivo: %.0f", (double)value);
                }
                if (!emergency_active) {
                    Security_Manager_Trigger_Emergency();
                }
            } else {
                Security_Manager_Error_Erase(ERROR_SEC_RPM_EXCESSIVE);
            }
            break;

        default:
            break;
    }
}
