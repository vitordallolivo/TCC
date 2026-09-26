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

/* Encerra a emergência SEM reenergizar nada: o ESC segue desarmado e os dois
 * relés abertos. Só devolve o sistema ao estado em que aceita comandos, para
 * que soltar o botão de emergência não obrigue a reiniciar a placa. */
void Security_Manager_Clear_Emergency(void);

/* @return true se a chamada mudou o estado do bit (borda), pra managers
 * logarem só na transição. */
bool Security_Manager_Error_Detect(siba_error_bit_t bit);
bool Security_Manager_Error_Erase(siba_error_bit_t bit);

/* Código agregado atual — exposto em TELEMETRY_LOG.error_code (HEX) */
uint32_t Security_Manager_Get_Error_Code(void);

/* true enquanto houver parada de emergência em vigor. */
bool Security_Manager_Is_Emergency_Active(void);

/* ── Cadeia de potência da bancada ─────────────────────────────────────── *
 * Armar exige DUAS condições simultâneas: o supervisório ter declarado que
 * está tudo certo (FUNC_START / INSTR_WRITE / TARGET_DEFAULT / STATUS_OK) e
 * o botão físico de emergência liberado. O botão pode ser sobrescrito por
 * comando (STATUS_EMERGENCY_OVERRIDE) — nesse caso o supervisório precisa
 * enviar os dois: primeiro a sobrescrita, depois o STATUS_OK.
 * A sobrescrita vive só em RAM: reiniciar devolve o botão ao comando.      */
int  Security_Manager_Arm_Bench(void);        /* 0 = armada, -EPERM = recusada */
void Security_Manager_Shutdown_Bench(void);   /* desarma o ESC e abre os relés */

void Security_Manager_Set_Emergency_Override(bool on);
bool Security_Manager_Get_Emergency_Override(void);

/* Botão apertado, já descontada a sobrescrita — é este o estado que a
 * varredura de segurança usa, não HAL_Emergency_Is_Pressed() direto. */
bool Security_Manager_Button_Pressed(void);

#endif /* SECURITY_MANAGER_H */
