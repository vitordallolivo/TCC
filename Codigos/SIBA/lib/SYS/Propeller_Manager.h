#ifndef PROPELLER_MANAGER_H
#define PROPELLER_MANAGER_H

#include <stdint.h>

/* Limite de corrente do motor — padrão definido em 90A, próximo do teto
 * de 100A do sensor de corrente (margem de segurança). Ajustável via
 * CMD/shell (Propeller_Manager_Set_Max_Current). */
#define PROPELLER_DEFAULT_MAX_CURRENT_A   90.0f

/* Passo de variação do throttle ao reduzir/restaurar (% por atualização) */
#define PROPELLER_THROTTLE_STEP_PCT        2.0f

void Propeller_Manager_Init(void);

/* Recebe atualizações de sensores relevantes (corrente do motor — i_batt;
 * RPM/forças no futuro, para cálculo de Ct/Cp da hélice — ver TODO) */
void Propeller_Manager_Update(uint32_t sensor_id, float value);

/* @brief Aplica o pedido de throttle, mas reduz o PWM se a corrente do
 * motor ultrapassar o limite (nunca chamar HAL_ESC_Set_Throttle direto). */
int Propeller_Manager_Request_Throttle(float percent);

/* Sobrescreve o limite máximo de corrente do motor via CMD */
void Propeller_Manager_Set_Max_Current(float amps);

/* Células de carga: taração/calibração são decisão do Sys, não do HAL —
 * CMD_Core/Test_Shell chamam aqui, nunca HAL_HX711_* diretamente. */
void Propeller_Manager_Tare_Cell(uint8_t idx);                              /* tara = raw (zera) */
void Propeller_Manager_Set_Cell_Calib(uint8_t idx, float scale, float tare_raw);
void Propeller_Manager_Calibrate_Cell_Weight(uint8_t idx, float known_weight); /* ganho por peso */

/* TODO: Propeller_Manager_thread_fn / cálculo de Ct/Cp a partir de
 * força (load cells) + RPM — ver lib/SYS/Propeller_Manager.h no CLAUDE.md */

#endif /* PROPELLER_MANAGER_H */
