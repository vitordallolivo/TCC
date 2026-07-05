#include "Security_Manager.h"
#include "CMD_Core.h"        /* CMD_Core_Get_Sensor_Cfg — faixa central (CMD_Core_prm.h) */
#include "HAL.h"
#include "Log_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(security_mgr, LOG_LEVEL_WRN);

/* Os limites operacionais NÃO vivem mais aqui — vêm da tabela central
 * SIBA_SENSOR_TABLE em CMD_Core_prm.h (via CMD_Core_Get_Sensor_Cfg).
 * Para mudar um limite, edite a linha do sensor lá. */

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
    /* Sensores de SEGURANÇA: sair da faixa dispara parada de emergência.
     * A faixa [min,max] e o bit de erro vêm da tabela central (CMD_Core_prm.h),
     * não mais de #defines locais. Assim mudar um limite é editar UMA linha. */
    siba_error_bit_t bit;
    switch (sensor_id) {
        case SENSOR_ID_V_BATT: bit = ERROR_SEC_VOLTAGE_OUT_OF_RANGE; break;
        case SENSOR_ID_I_BATT: bit = ERROR_SEC_CURRENT_EXCESSIVE;    break;
        case SENSOR_ID_RPM:    bit = ERROR_SEC_RPM_EXCESSIVE;        break;
        default: return;   /* sensor não é crítico de segurança */
    }

    const SENSORS *cfg = CMD_Core_Get_Sensor_Cfg(sensor_id);
    if (!cfg) return;

    if (value < cfg->Minimum_Valid_Value || value > cfg->Max_Valid_Value) {
        if (Security_Manager_Error_Detect(bit)) {
            LOG_ERR("SECURITY: sensor_id %u fora da faixa [%.2f, %.2f]: %.2f",
                    sensor_id, (double)cfg->Minimum_Valid_Value,
                    (double)cfg->Max_Valid_Value, (double)value);
        }
        if (!emergency_active) {
            Security_Manager_Trigger_Emergency();
        }
    } else {
        Security_Manager_Error_Erase(bit);
    }
}
