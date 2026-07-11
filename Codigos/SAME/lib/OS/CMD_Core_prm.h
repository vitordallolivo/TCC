#ifndef CMD_CORE_PRM_H
#define CMD_CORE_PRM_H

#include "Common_Types.h"      /* target_type */

/* Auto-generated — edite SAME.yaml, não este arquivo.
 *
 * CMD_Core_prm.h — configuração básica dos sensores calibráveis de SAME.
 * Fonte única de verdade: bloco `sensor_defaults` do SAME.yaml.
 *   - defaults de calibração afim (y = linear*x + offset), restaurados no RESET
 *   - faixa válida [min, max]
 *
 * A tabela é INDEXADA pelo próprio target/sensor — mesmo índice usado por
 * cmd->target_id (ver Common_Types.h / Log_Manager.h).
 */

/* Nº de entradas (target/sensor 0..CMD_CORE_SENSOR_COUNT-1). */
#define CMD_CORE_SENSOR_COUNT   6

/* Sentinela "sem limite" para a faixa de validade. */
#define CMD_CORE_NO_LIMIT       1.0e30f

/*        X(TARGET, DEF_LINEAR, DEF_OFFSET, MIN_VALID, MAX_VALID) */
#define SAME_SENSOR_TABLE(X) \
    X(TARGET_CURRENT_A, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT) \
    X(TARGET_CURRENT_B, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT) \
    X(TARGET_CURRENT_C, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT) \
    X(TARGET_VOLTAGE_A, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT) \
    X(TARGET_VOLTAGE_B, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT) \
    X(TARGET_VOLTAGE_C, 1.0f, 0.0f, -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT)

#endif /* CMD_CORE_PRM_H */
