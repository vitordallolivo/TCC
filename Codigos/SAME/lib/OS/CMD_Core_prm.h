#ifndef CMD_CORE_PRM_H
#define CMD_CORE_PRM_H

#include "Common_Types.h"      /* target_type */

/* Auto-generated — edite SAME.yaml, não este arquivo.
 * Defaults de calibração (y=linear*x+offset) e faixa válida por sensor,
 * indexado pelo target/sensor (cmd->target_id).
 */

/* Nº de entradas (target/sensor 0..CMD_CORE_SENSOR_COUNT-1). */
#define CMD_CORE_SENSOR_COUNT   6

/* Sentinela "sem limite" para a faixa de validade. */
#define CMD_CORE_NO_LIMIT       1.0e30f

/*        X(TARGET, DEF_LINEAR, DEF_OFFSET, MIN_VALID, MAX_VALID) */
#define SAME_SENSOR_TABLE(X) \
    X(TARGET_CURRENT_A, 0.0592593f, -132.64f, -70.0f, 70.0f) \
    X(TARGET_CURRENT_B, 0.0592593f, -132.64f, -70.0f, 70.0f) \
    X(TARGET_CURRENT_C, 0.0592593f, -132.64f, -70.0f, 70.0f) \
    X(TARGET_VOLTAGE_A, 0.0183333f, 0.0f, -1.0f, 65.0f) \
    X(TARGET_VOLTAGE_B, 0.0183333f, 0.0f, -1.0f, 65.0f) \
    X(TARGET_VOLTAGE_C, 0.0183333f, 0.0f, -1.0f, 65.0f)

#endif /* CMD_CORE_PRM_H */
