#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include "Log_Manager.h"
#include <stdint.h>
#include <stdbool.h>

void Battery_Manager_Init(void);
void Battery_Manager_Update(uint32_t sensor_id, float value);

/* Sobrescreve coeficientes do polinômio SOC via CMD */
void Battery_Manager_Set_Poly(uint8_t idx, float coeff);

/* Sobrescreve limite de temperatura */
void Battery_Manager_Set_Temp_Limit(float limit_c);

/* Liga/desliga relé de potência */
void Battery_Manager_Set_Relay(bool on);

#endif /* BATTERY_MANAGER_H */