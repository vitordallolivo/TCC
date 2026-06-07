#ifndef SECURITY_MANAGER_H
#define SECURITY_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

/* ===========================================================
 * Código de erro/alarme agregado — uint32_t, 1 bit por condição
 *
 * Cada manager (Battery/Propeller/Security/OS) detecta suas próprias
 * condições e reporta aqui via Security_Manager_Error_Detect/_Erase
 * (mesmo padrão Error_Detect/Error_Erase do CMP_BackgroundHandler:
 * o bit fica setado enquanto a condição persistir e é limpo assim
 * que ela deixa de ocorrer — sem reentrância/spam de log a cada
 * leitura, só na transição). O agregado vai para TELEMETRY_LOG.error_code
 * (ver SIBA.yaml/Log_Manager) — exposto em HEX para a interface.
 * =========================================================== */
typedef enum {
    /* --- Battery_Manager --- */
    ERROR_BATT_LOW_SOC              = (1U << 0),  /* SOC abaixo do limite de alerta        */
    ERROR_BATT_POLY_MISCONFIGURED   = (1U << 1),  /* polinômio SOC produz valores absurdos */
    ERROR_BATT_LOW_VOLTAGE          = (1U << 2),  /* tensão por célula próxima do limite   */
    ERROR_BATT_RELAY_MISMATCH       = (1U << 3),  /* corrente flui com o relé desligado    */
    ERROR_BATT_OVER_TEMPERATURE     = (1U << 4),  /* temperatura acima do limite (alarme)  */

    /* --- Propeller_Manager --- */
    ERROR_PROP_LOAD_CELL_NOT_CONFIGURED = (1U << 8),  /* célula sem calibração válida      */
    ERROR_PROP_LOAD_CELL_NOT_CONNECTED  = (1U << 9),  /* HX711 não responde (leitura ~0xFF)*/
    ERROR_PROP_OVERCURRENT              = (1U << 10), /* corrente do motor > limite        */

    /* --- Security_Manager --- */
    ERROR_SEC_VOLTAGE_OUT_OF_RANGE  = (1U << 16), /* v_batt fora dos limites operacionais  */
    ERROR_SEC_CURRENT_EXCESSIVE     = (1U << 17), /* i_batt acima do limite de segurança   */
    ERROR_SEC_RPM_EXCESSIVE         = (1U << 18), /* rpm acima do limite de segurança      */
    ERROR_SEC_EMERGENCY_ACTIVE      = (1U << 19), /* emergency stop disparado              */

    /* --- OS / genéricos --- */
    ERROR_OS_GPIO_INIT_FAILED       = (1U << 24), /* algum módulo GPIO não inicializou     */
} siba_error_bit_t;

void Security_Manager_Init(void);
void Security_Manager_Check(uint32_t sensor_id, float value);
void Security_Manager_Trigger_Emergency(void);

/* ---------------------------------------------------------------------
 * Error_Detect / Error_Erase — agregação central do código de erro.
 *
 * Retornam true quando a chamada efetivamente mudou o estado do bit
 * (borda de subida/descida) — útil para os managers logarem só na
 * transição, evitando spam de LOG_ERR a cada leitura de sensor.
 * --------------------------------------------------------------------- */
bool Security_Manager_Error_Detect(siba_error_bit_t bit);
bool Security_Manager_Error_Erase(siba_error_bit_t bit);

/* Código agregado atual — exposto em TELEMETRY_LOG.error_code (HEX) */
uint32_t Security_Manager_Get_Error_Code(void);

#endif /* SECURITY_MANAGER_H */
