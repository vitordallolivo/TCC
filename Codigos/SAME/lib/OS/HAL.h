#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include "HAL_prm.h"
#include "Common_Types.h"   /* target_type */

/* HAL da SAME — só aquisição, não aciona o motor. ADC1=i_a,i_b,i_c,
 * ADC2=V_a,V_b,V_c. A thread do ADC calibra, faz Clarke, roda um passo do
 * observador e publica telemetria via OSSI_Submit_Sensor. */

#define HAL_ADC_PRIO     7      /* > prio da same_wq (5): o passo do SMO não espera */
#define HAL_STACK_SIZE   2048

/* Tensão de referência do ADC (mV) e fundo de escala 12 bits. */
#define HAL_ADC_VREF_MV     3300U
#define HAL_ADC_FULL_SCALE  4096U

/* Índice do canal na sequência de cada ADC (ver adc_ch_cfg em HAL.c). */
#define HAL_ADC1_NUM_CH   3    /* correntes */
#define HAL_ADC2_NUM_CH   3    /* tensões */

/* @brief Inicializa ADCs e sobe a thread de aquisição. @return 0 ou erro. */
int  HAL_Init(void);
/* @brief Inicializa só os ADCs (chamado por HAL_Init). @return 0 ou erro. */
int  HAL_ADC_Init(void);
/* @brief Aplica os defaults de calibração (SAME_SENSOR_TABLE) ao array interno.
 * Chamar em main() ANTES de NVM_Init(), para que o NVM possa sobrescrever
 * com valores salvos antes de HAL_ADC_Init() ligar o DMA. */
void HAL_Calib_Init_Defaults(void);

/* Calibração afim por sensor: y = linear·(mV no pino) + offset. */
/* @param sensor_id índice (target_type) @param linear ganho @param offset deslocamento */
void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset);
/* @param sensor_id índice @param linear [out] ganho @param offset [out] deslocamento */
void HAL_ADC_Get_Calib(uint8_t sensor_id, float *linear, float *offset);
/* Pausa/retoma o callback de ISR do ADC — usar ao redor de mudanças de sensor. */
void HAL_ADC_Pause(void);
void HAL_ADC_Resume(void);

/*
 * @brief true depois que o offset do sensor Hall de corrente foi levantado no
 * boot e o observador foi liberado.
 *
 * Enquanto for false, `rpm_same` e `torque_eletrico` da telemetria NÃO são
 * válidos: o observador está parado de propósito. O motivo está em HAL.c —
 * o repouso do SS49E varia alguns mV entre energizações, e essa componente é
 * comum às três fases, então atravessa a Clarke como sequência zero e vira
 * BEMF espúria no SMO.
 */
bool HAL_Calibration_Done(void);

/*
 * @brief Refaz a calibração de offset do zero (espera de estabilização +
 * acumulação), com o ESC parado. Sem isto, uma calibração reprovada só se
 * recupera reiniciando a placa. Ligada ao FUNC_START/TARGET_DEFAULT do CMD.
 */
void HAL_Calibration_Restart(void);

#endif /* HAL_H */
