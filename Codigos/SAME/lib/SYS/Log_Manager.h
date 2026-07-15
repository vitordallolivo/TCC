#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include "Common_Types.h"
#include <stdint.h>

/* Auto-generated — edite SAME.yaml, não este arquivo */

/* Mapeamento sensor_id (OSSI sys_req) → campo do TELEMETRY_LOG. */
#define SENSOR_ID_V_A 0U
#define SENSOR_ID_V_B 1U
#define SENSOR_ID_V_C 2U
#define SENSOR_ID_I_A 3U
#define SENSOR_ID_I_B 4U
#define SENSOR_ID_I_C 5U
#define SENSOR_ID_V_ALPHA 6U
#define SENSOR_ID_V_BETA 7U
#define SENSOR_ID_I_ALPHA 8U
#define SENSOR_ID_I_BETA 9U
#define SENSOR_ID_RPM_SAME 10U
#define SENSOR_ID_TORQUE_ELETRICO 11U
#define SENSOR_ID_TORQUE_HELICE 12U

/* ===========================================================
 * Structs geradas a partir do SAME.yaml
 * =========================================================== */
typedef struct __attribute__((packed)) {

    target_type SensorTarget;

    float Linear_Coeff;

    float Offset_Coeff;

    float Max_Valid_Value;

    float Minimum_Valid_Value;

    float Linear_Coeff_Reset_Default;

    float Offset_Coeff_Reset_Default;

} SENSORS;

typedef struct __attribute__((packed)) {

    uint32_t timestamp;

    float V_a;

    float V_b;

    float V_c;

    float i_a;

    float i_b;

    float i_c;

    float V_alpha;

    float V_beta;

    float i_alpha;

    float i_beta;

    float rpm_same;

    float torque_eletrico;

    float torque_helice;

    log_status status;

} TELEMETRY_LOG;

/* ===========================================================
 * API do Log Manager (implementação manual em lib/SYS/Log_Manager.c)
 * =========================================================== */

/* Inicializa o log e registra thread no TCKEC */
void Log_Manager_Init(void);

/* Atualiza um campo do log a partir do sensor_id */
void Log_Manager_Update(uint32_t sensor_id, float value);

/* Entry point da thread (registrada no TCKEC) */
void Log_Manager_thread_fn(void *p1, void *p2, void *p3);

/* Retorna cópia thread-safe do último log */
TELEMETRY_LOG Log_Manager_Get_Snapshot(void);

/* Serializa e envia o snapshot atual via OSSI→NCI (chamado por
 * Sys_thread_fn a cada LOG_MANAGER_PERIOD_MS) */
void Log_Manager_Transmit(void);

/* Reprograma o período do timer de TX em runtime (ex.: TARGET_TS_UDP) */
void Log_Manager_Set_Period_Ms(uint32_t period_ms);

#define LOG_MANAGER_THREAD_ID   40
#define LOG_MANAGER_PRIO         6
#define LOG_MANAGER_PERIOD_MS  100   /* 10 Hz */

#endif /* LOG_MANAGER_H */
