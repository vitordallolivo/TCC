#ifndef PROPELLER_MANAGER_H
#define PROPELLER_MANAGER_H

#include <stdint.h>

/* Limite de corrente do motor — ajustar conforme o ESC/motor real.
 * Mantido abaixo de LIMIT_I_BATT_MAX (Security_Manager.c) para que o
 * Propeller_Manager reduza o throttle ANTES de uma parada de emergência. */
#define PROPELLER_DEFAULT_MAX_CURRENT_A   55.0f

/* Passo de variação do throttle ao reduzir/restaurar (% por atualização) */
#define PROPELLER_THROTTLE_STEP_PCT        2.0f

void Propeller_Manager_Init(void);

/* Recebe atualizações de sensores relevantes (corrente do motor — i_batt;
 * RPM/forças no futuro, para cálculo de Ct/Cp da hélice — ver TODO) */
void Propeller_Manager_Update(uint32_t sensor_id, float value);

/* Ponto de entrada para pedidos de throttle (substitui chamada direta a
 * HAL_ESC_Set_Throttle em CMD_Core): aplica o pedido do usuário, mas
 * sobrescreve/reduz o PWM do ESC sempre que a corrente do motor ultrapassar
 * o limite configurado — a corrente do motor NUNCA pode passar do limite. */
int Propeller_Manager_Request_Throttle(float percent);

/* Sobrescreve o limite máximo de corrente do motor via CMD */
void Propeller_Manager_Set_Max_Current(float amps);

/* TODO: Propeller_Manager_thread_fn / cálculo de Ct/Cp a partir de
 * força (load cells) + RPM — ver lib/SYS/Propeller_Manager.h no CLAUDE.md */

#endif /* PROPELLER_MANAGER_H */
