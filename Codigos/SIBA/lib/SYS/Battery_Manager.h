#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include "Log_Manager.h"
#include <stdint.h>
#include <stdbool.h>

void Battery_Manager_Init(void);
void Battery_Manager_Update(uint32_t sensor_id, float value);

/* Sobrescreve coeficientes do polinômio SOC via CMD */
void Battery_Manager_Set_Poly(uint8_t idx, float coeff);

/* Sobrescreve a contagem de células via CMD (0 = volta a auto-detectar
 * pela tensão usando a lookup table interna, ver Battery_Manager.c) */
void Battery_Manager_Set_Cell_Count(uint8_t cells);

/* Contagem de células em uso no momento (definida pelo usuário ou
 * auto-detectada pela última leitura de v_batt) */
uint8_t Battery_Manager_Get_Cell_Count(void);

/* Sobrescreve limite de temperatura */
void Battery_Manager_Set_Temp_Limit(float limit_c);

/* Liga/desliga relé de potência */
void Battery_Manager_Set_Relay(bool on);

#endif /* BATTERY_MANAGER_H */