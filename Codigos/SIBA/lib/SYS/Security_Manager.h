#ifndef SECURITY_MANAGER_H
#define SECURITY_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "Error_Codes.h"   /* siba_error_bit_t — GERADO do SIBA.yaml (error_bits) */

/* O enum de códigos de erro (siba_error_bit_t) agora é GERADO de
 * SIBA.yaml (seção `error_bits`) em lib/SYS/Error_Codes.h — fonte única
 * compartilhada com a GUI (Communication_Data/errors.py). Não edite à mão. */

void Security_Manager_Init(void);
void Security_Manager_Check(uint32_t sensor_id, float value);
void Security_Manager_Trigger_Emergency(void);

/* @return true se a chamada mudou o estado do bit (borda), pra managers
 * logarem só na transição. */
bool Security_Manager_Error_Detect(siba_error_bit_t bit);
bool Security_Manager_Error_Erase(siba_error_bit_t bit);

/* Código agregado atual — exposto em TELEMETRY_LOG.error_code (HEX) */
uint32_t Security_Manager_Get_Error_Code(void);

#endif /* SECURITY_MANAGER_H */
