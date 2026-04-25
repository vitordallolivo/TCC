#ifndef TELEMETRY_LOG_H
#define TELEMETRY_LOG_H

#include "Common_Types.h"
#include <stdint.h>

/* --- Auto-generated Struct: TELEMETRY_LOG --- */
typedef struct __attribute__((packed)) {

    uint32 timestamp;

    float load_cell_0;

    float load_cell_1;

    float load_cell_2;

    float load_cell_3;

    float load_cell_4;

    float load_cell_5;

    float v_batt;

    float i_batt;

    float soc;

    float temperature;

    float air_speed;

    float rpm;

    log_status status;

} TELEMETRY_LOG;

#endif // TELEMETRY_LOG_H