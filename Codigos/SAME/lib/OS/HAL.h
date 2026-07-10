#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include "HAL_prm.h"
#include "Common_Types.h"   /* target_type */

/* ===========================================================
 * HAL da SAME — SÓ aquisição (a SAME não aciona o motor). Dois ADCs
 * convencionais do CH32V307 (driver custom drivers/adc/adc_ch32v307.c):
 *   ADC1 (DMA1 ch1) → 3 tensões de fase   : CH0 PA0=V_a, CH1 PA1=V_b, CH2 PA2=V_c
 *   ADC2 (DMA1 ch2) → 3 correntes de fase : CH3 PA3=i_a, CH4 PA4=i_b, CH5 PA5=i_c
 *
 * A thread do ADC bloqueia em adc_read_async/k_poll (evento "conversão
 * pronta"), aplica calibração, faz Clarke (αβ), roda UM passo do observador
 * (Observer_Step) e, decimado, publica os 13 campos do TELEMETRY_LOG via
 * OSSI_Submit_Sensor. É a única thread de aquisição.
 * =========================================================== */

#define HAL_ADC_PRIO     7      /* > prio da same_wq (5): o passo do SMO não espera */
#define HAL_STACK_SIZE   2048

/* Tensão de referência do ADC (mV) e fundo de escala 12 bits. */
#define HAL_ADC_VREF_MV     3300U
#define HAL_ADC_FULL_SCALE  4096U

/* Índice do canal na sequência de cada ADC (ver adc_ch_cfg em HAL.c). */
#define HAL_ADC1_NUM_CH   3    /* tensões */
#define HAL_ADC2_NUM_CH   3    /* correntes */

/* API pública */
int  HAL_Init(void);
int  HAL_ADC_Init(void);

/* Calibração afim por sensor (índice = target_type 0..CMD_CORE_SENSOR_COUNT-1):
 * y = linear·(mV no pino) + offset. Fonte única de verdade no HAL. */
void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset);
void HAL_ADC_Get_Calib(uint8_t sensor_id, float *linear, float *offset);

#endif /* HAL_H */
