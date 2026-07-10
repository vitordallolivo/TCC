#ifndef CMD_CORE_PRM_H
#define CMD_CORE_PRM_H

#include "Common_Types.h"   /* target_type */

/* ===========================================================
 * CMD_Core_prm.h — Tabela de configuração BÁSICA dos SENSORES da SAME
 * (mesmo papel do CMD_Core_prm.h da SIBA).
 *
 * Os "sensores" da SAME são as entradas dos DOIS ADCs: 3 correntes de fase
 * (ADC2) e as tensões de fase (ADC1). Cada linha traz, para um sensor:
 *   - defaults de calibração afim  y = LINEAR·(mV no pino) + OFFSET
 *     (restaurados no RESET; mesmo modelo do ADC da SIBA — sem constante
 *     física fixa no meio: o usuário calibra LINEAR/OFFSET na bancada)
 *   - faixa válida [MIN, MAX] usada para marcar o `status` do TELEMETRY_LOG
 *     (LOG_VALID / LOG_INVALID) — a SAME não tem Security_Manager, então não
 *     há "bit de erro", só a marcação de validade.
 *
 * A tabela é INDEXADA pelo próprio `target_type` do CMD (Common_Types.h): os
 * 5 primeiros enums — TARGET_CURRENT_A..TARGET_VOLTAGE_BC — são exatamente os
 * sensores calibráveis, nesta ordem (0..4). Assim o índice do array = target,
 * como na SIBA (índice = sensor_id).
 *
 * ⚠ DECISÃO DE HW EM ABERTO (ver SAME/CLAUDE.md): o protocolo expõe só DUAS
 * calibrações de tensão (VOLTAGE_AB, VOLTAGE_BC) mas o ADC1 lê TRÊS tensões
 * de fase (V_a,V_b,V_c). Como os três divisores são hardware idêntico, o HAL
 * calibra V_a←VOLTAGE_AB, V_b←V_c←VOLTAGE_BC (ver ADC_CH_* em HAL.c). Se um
 * dia houver um TARGET_VOLTAGE_CA no SAME.yaml, basta acrescentar a linha.
 * =========================================================== */

/* Nº de sensores calibráveis (target 0..CMD_CORE_SENSOR_COUNT-1). */
#define CMD_CORE_SENSOR_COUNT   5

/* Sentinela "sem limite" para a faixa de validade. */
#define CMD_CORE_NO_LIMIT       1.0e30f

/*        X(TARGET,             DEF_LINEAR, DEF_OFFSET, MIN_VALID,          MAX_VALID) */
#define SAME_SENSOR_TABLE(X)                                                                      \
    X(TARGET_CURRENT_A,         1.0f,       0.0f,      -CMD_CORE_NO_LIMIT,  CMD_CORE_NO_LIMIT)     \
    X(TARGET_CURRENT_B,         1.0f,       0.0f,      -CMD_CORE_NO_LIMIT,  CMD_CORE_NO_LIMIT)     \
    X(TARGET_CURRENT_C,         1.0f,       0.0f,      -CMD_CORE_NO_LIMIT,  CMD_CORE_NO_LIMIT)     \
    X(TARGET_VOLTAGE_AB,        1.0f,       0.0f,      -CMD_CORE_NO_LIMIT,  CMD_CORE_NO_LIMIT)     \
    X(TARGET_VOLTAGE_BC,        1.0f,       0.0f,      -CMD_CORE_NO_LIMIT,  CMD_CORE_NO_LIMIT)

#endif /* CMD_CORE_PRM_H */
