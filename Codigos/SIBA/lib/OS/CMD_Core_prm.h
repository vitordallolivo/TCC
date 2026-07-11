#ifndef CMD_CORE_PRM_H
#define CMD_CORE_PRM_H

#include "Log_Manager.h"       /* SENSOR_ID_* */
#include "Security_Manager.h"  /* siba_error_bit_t */

/* Auto-generated — edite SIBA.yaml, não este arquivo.
 *
 * CMD_Core_prm.h — configuração básica dos sensores calibráveis de SIBA.
 * Fonte única de verdade: bloco `sensor_defaults` do SIBA.yaml.
 *   - defaults de calibração afim (y = linear*x + offset), restaurados no RESET
 *   - faixa válida [min, max] + bit de erro (siba_error_bit_t) a marcar
 *     quando o valor sai da faixa
 *
 * A tabela é INDEXADA pelo próprio target/sensor — mesmo índice usado por
 * cmd->target_id (ver Common_Types.h / Log_Manager.h).
 */

/* Nº de entradas (target/sensor 0..CMD_CORE_SENSOR_COUNT-1). */
#define CMD_CORE_SENSOR_COUNT   12

/* Sentinela "sem limite" para a faixa de validade. */
#define CMD_CORE_NO_LIMIT       1.0e30f

/*        X(TARGET, DEF_LINEAR, DEF_OFFSET, MIN_VALID, MAX_VALID, ERR_BIT) */
#define SIBA_SENSOR_TABLE(X) \
    X(SENSOR_ID_LOAD_CELL_0, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0) \
    X(SENSOR_ID_LOAD_CELL_1, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0) \
    X(SENSOR_ID_LOAD_CELL_2, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0) \
    X(SENSOR_ID_LOAD_CELL_3, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0) \
    X(SENSOR_ID_LOAD_CELL_4, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0) \
    X(SENSOR_ID_LOAD_CELL_5, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0) \
    X(SENSOR_ID_V_BATT, 0.008f, 0.0f, 0.0f, 72.0f, ERROR_SEC_VOLTAGE_OUT_OF_RANGE) \
    X(SENSOR_ID_I_BATT, 0.02f, 0.0f, -CMD_CORE_NO_LIMIT, 60.0f, ERROR_SEC_CURRENT_EXCESSIVE) \
    X(SENSOR_ID_SOC, 1.0f, 0.0f, 0.0f, 100.0f, 0) \
    X(SENSOR_ID_TEMPERATURE_BATTERY, 0.04f, 0.0f, -40.0f, 85.0f, ERROR_BATT_OVER_TEMPERATURE) \
    X(SENSOR_ID_AIR_SPEED, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0) \
    X(SENSOR_ID_RPM, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, 20000.0f, ERROR_SEC_RPM_EXCESSIVE)

#endif /* CMD_CORE_PRM_H */
