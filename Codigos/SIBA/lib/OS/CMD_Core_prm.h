#ifndef CMD_CORE_PRM_H
#define CMD_CORE_PRM_H

#include "Log_Manager.h"        /* SENSOR_ID_* */
#include "Security_Manager.h"   /* siba_error_bit_t */

/* ===========================================================
 * CMD_Core_prm.h — Configuração BÁSICA e padronizada dos sensores
 *
 * Fonte única de verdade dos parâmetros de cada sensor do SIBA:
 *   - defaults de calibração (linear/offset restaurados no RESET)
 *   - faixa válida [min, max] (limites operacionais / de segurança)
 *   - bit de erro (siba_error_bit_t) a marcar quando o valor sai da faixa
 *
 * Tudo vive na TABELA X-macro `SIBA_SENSOR_TABLE` abaixo — para
 * mudar um limite/default de um sensor, edite SÓ a linha dele. A tabela
 * alimenta automaticamente:
 *   - CMD_Core_Init()               (preenche sensor_cfg[] — defaults e faixa)
 *   - CMD_Core_Validate_Sensor()    (checa faixa e devolve o bit de erro)
 *   - Security_Manager_Check()      (reação de segurança usa a MESMA faixa)
 *
 * Assim não há mais limites "mágicos" espalhados (antes LIMIT_V_BATT_* etc.
 * viviam hardcoded no Security_Manager.c) nem init de sensor duplicado.
 *
 * Semântica das colunas por TIPO de sensor:
 *   - Célula de carga (HX711, modelo canônico grams=(raw-tare)/scale):
 *       DEF_LINEAR = scale default (counts/un);  DEF_OFFSET = tare default (counts)
 *   - ADC (v_batt/i_batt/temp/air_speed, modelo afim y=linear·x+offset):
 *       DEF_LINEAR = ganho;                       DEF_OFFSET = offset afim
 *   - SOC/RPM: não têm calibração linear/offset própria (DEF_* = neutro)
 *
 *   ERR_BIT = 0  -> sensor sem verificação de faixa (não gera erro).
 * =========================================================== */

/* Nº de entradas de sensor_cfg[] (0 .. CMD_CORE_SENSOR_COUNT-1). Cobre
 * load_cell_0..5, v_batt, i_batt, soc, temperature_battery, air_speed, rpm. */
#define CMD_CORE_SENSOR_COUNT   12

/* Sentinela "sem faixa" — quando não se quer checar min/max daquele sensor. */
#define CMD_CORE_NO_LIMIT       1.0e30f

/*        X(SENSOR_ID,                     DEF_LINEAR, DEF_OFFSET,  MIN_VALID,   MAX_VALID,   ERR_BIT) */
#define SIBA_SENSOR_TABLE(X)                                                                              \
    X(SENSOR_ID_LOAD_CELL_0,               1.0f,       0.0f,       -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0)                              \
    X(SENSOR_ID_LOAD_CELL_1,               1.0f,       0.0f,       -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0)                              \
    X(SENSOR_ID_LOAD_CELL_2,               1.0f,       0.0f,       -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0)                              \
    X(SENSOR_ID_LOAD_CELL_3,               1.0f,       0.0f,       -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0)                              \
    X(SENSOR_ID_LOAD_CELL_4,               1.0f,       0.0f,       -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0)                              \
    X(SENSOR_ID_LOAD_CELL_5,               1.0f,       0.0f,       -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0)                              \
    X(SENSOR_ID_V_BATT,                    0.008f,     0.0f,        0.0f,      72.0f,      ERROR_SEC_VOLTAGE_OUT_OF_RANGE)              \
    X(SENSOR_ID_I_BATT,                    0.020f,     0.0f,       -CMD_CORE_NO_LIMIT, 60.0f, ERROR_SEC_CURRENT_EXCESSIVE)               \
    X(SENSOR_ID_SOC,                       1.0f,       0.0f,        0.0f,       100.0f,     0)                                           \
    X(SENSOR_ID_TEMPERATURE_BATTERY,       0.040f,     0.0f,       -40.0f,      85.0f,      ERROR_BATT_OVER_TEMPERATURE)                 \
    X(SENSOR_ID_AIR_SPEED,                 1.0f,       0.0f,       -CMD_CORE_NO_LIMIT, CMD_CORE_NO_LIMIT, 0)                              \
    X(SENSOR_ID_RPM,                       1.0f,       0.0f,       -CMD_CORE_NO_LIMIT, 20000.0f, ERROR_SEC_RPM_EXCESSIVE)

#endif /* CMD_CORE_PRM_H */
