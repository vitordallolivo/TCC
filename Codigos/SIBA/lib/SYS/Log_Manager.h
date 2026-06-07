#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include "Common_Types.h"
#include <stdint.h>

/* Auto-generated — edite SIBA.yaml, não este arquivo */

/* ===========================================================
 * Mapeamento sensor_id (OSSI sys_req) → campo do TELEMETRY_LOG
 * Gerado a partir da ordem dos campos em TELEMETRY_LOG
 * (exclui timestamp e status, que não são sensores).
 * =========================================================== */
#define SENSOR_ID_LOAD_CELL_0 0U
#define SENSOR_ID_LOAD_CELL_1 1U
#define SENSOR_ID_LOAD_CELL_2 2U
#define SENSOR_ID_LOAD_CELL_3 3U
#define SENSOR_ID_LOAD_CELL_4 4U
#define SENSOR_ID_LOAD_CELL_5 5U
#define SENSOR_ID_V_BATT 6U
#define SENSOR_ID_I_BATT 7U
#define SENSOR_ID_SOC 8U
#define SENSOR_ID_TEMPERATURE_BATTERY 9U
#define SENSOR_ID_AIR_SPEED 10U
#define SENSOR_ID_RPM 11U

/* ===========================================================
 * Structs geradas a partir do SIBA.yaml
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

    float load_cell_0;

    float load_cell_1;

    float load_cell_2;

    float load_cell_3;

    float load_cell_4;

    float load_cell_5;

    float v_batt;

    float i_batt;

    float soc;

    float temperature_battery;

    float air_speed;

    float rpm;

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
 * System_Manager_thread_fn a cada LOG_MANAGER_PERIOD_MS) */
void Log_Manager_Transmit(void);

#define LOG_MANAGER_THREAD_ID   40
#define LOG_MANAGER_PRIO         6
#define LOG_MANAGER_PERIOD_MS  100   /* 10 Hz */

#endif /* LOG_MANAGER_H */
