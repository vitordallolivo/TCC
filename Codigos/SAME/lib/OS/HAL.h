#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include "HAL_prm.h"
#include "Common_Types.h"   /* target_type */

/* HAL da SAME — só aquisição, não aciona o motor. ADC1=V_a,V_b,V_c,
 * ADC2=i_a,i_b,i_c. A thread do ADC calibra, faz Clarke, roda um passo do
 * observador e publica telemetria via OSSI_Submit_Sensor. */

#define HAL_ADC_PRIO     7      /* > prio da same_wq (5): o passo do SMO não espera */
#define HAL_STACK_SIZE   2048

/* Tensão de referência do ADC (mV) e fundo de escala 12 bits. */
#define HAL_ADC_VREF_MV     3300U
#define HAL_ADC_FULL_SCALE  4096U

/* Índice do canal na sequência de cada ADC (ver adc_ch_cfg em HAL.c). */
#define HAL_ADC1_NUM_CH   3    /* tensões */
#define HAL_ADC2_NUM_CH   3    /* correntes */

/* @brief Inicializa ADCs e sobe a thread de aquisição. @return 0 ou erro. */
int  HAL_Init(void);
/* @brief Inicializa só os ADCs (chamado por HAL_Init). @return 0 ou erro. */
int  HAL_ADC_Init(void);

/* Calibração afim por sensor: y = linear·(mV no pino) + offset. */
/* @param sensor_id índice (target_type) @param linear ganho @param offset deslocamento */
void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset);
/* @param sensor_id índice @param linear [out] ganho @param offset [out] deslocamento */
void HAL_ADC_Get_Calib(uint8_t sensor_id, float *linear, float *offset);

#endif /* HAL_H */
