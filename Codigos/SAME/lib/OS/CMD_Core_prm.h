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

/* Lei de conversão da leitura crua (x, em mV no pino) para grandeza física:
 *
 *   SENSOR_LAW_AFFINE : y = linear*(x - zero) + offset
 *   SENSOR_LAW_SQRT   : y = linear*sqrt(x - zero) + offset
 *
 * Com zero = 0 a afim é idêntica ao y = linear*x + offset de sempre, então
 * nada do que já estava calibrado muda. A raiz existe para o tubo de Pitot:
 * por Bernoulli a velocidade cresce com a RAIZ da pressão diferencial
 * (v = sqrt(2*dP/rho)), de modo que reta nenhuma descreve esse sensor.
 * Abaixo de `zero` a raiz satura em 0 — pressão negativa ali é escoamento
 * invertido ou tomada entupida, não velocidade. */
typedef enum {
    SENSOR_LAW_AFFINE = 0,
    SENSOR_LAW_SQRT   = 1,
} sensor_law_t;

/*        X(TARGET, DEF_LINEAR, DEF_OFFSET, MIN_VALID, MAX_VALID, DEF_ZERO, LAW) */
#define SAME_SENSOR_TABLE(X) \
    X(TARGET_CURRENT_A, 0.0784f, -136.315f, -70.0f, 70.0f, 0.0f, SENSOR_LAW_AFFINE) \
    X(TARGET_CURRENT_B, 0.0784f, -137.987f, -70.0f, 70.0f, 0.0f, SENSOR_LAW_AFFINE) \
    X(TARGET_CURRENT_C, 0.0784f, -136.657f, -70.0f, 70.0f, 0.0f, SENSOR_LAW_AFFINE) \
    X(TARGET_VOLTAGE_A, 0.0183333f, 0.0f, -1.0f, 65.0f, 0.0f, SENSOR_LAW_AFFINE) \
    X(TARGET_VOLTAGE_B, 0.0183333f, 0.0f, -1.0f, 65.0f, 0.0f, SENSOR_LAW_AFFINE) \
    X(TARGET_VOLTAGE_C, 0.0183333f, 0.0f, -1.0f, 65.0f, 0.0f, SENSOR_LAW_AFFINE)

#endif /* CMD_CORE_PRM_H */
