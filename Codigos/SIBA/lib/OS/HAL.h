#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include "HAL_prm.h"

#define HAL_ADC_PRIO     7
#define HAL_HX711_PRIO   6
#define HAL_STACK_SIZE   2048

/* HX711 x6 — PD_SCK (clock, saída) / DOUT (dado serial, entrada). */
#define HX711_COUNT 6

/* Ganho: 128 = canal A / 1 pulso extra
 *        64  = canal B / 3 pulsos extra
 *        32  = canal A / 2 pulsos extra  */
#define HX711_GAIN_PULSES 1   /* 128x — padrão para strain gauge */

/* ADC1 (PA0-PA7, único scan DMA): CH0=v_batt, CH1=i_batt, CH2=air_speed
 * (Pitot), CH6=temperature_battery. PA3/PA4/PA7 são da bancada de ensaio,
 * fora do TELEMETRY_LOG; PA8 fora do range do ADC1 (ver HAL_RESERVED_PA8_IDX). */
#define HAL_ADC_IDX_V_BATT       0
#define HAL_ADC_IDX_I_BATT       1
#define HAL_ADC_IDX_AIR_SPEED    2
#define HAL_ADC_IDX_TEMP_BATT    3

/* Tensão de referência do ADC (mV) — Vref interno CH32V307 */
#define HAL_ADC_VREF_MV          3300U
#define HAL_ADC_FULL_SCALE       4096U   /* 12 bits */

/* PA8 "Ensaio_Bateria_Corrente" — fora do range do ADC1, reservado sem leitura. */
#define HAL_RESERVED_PA8_IDX     0

/* ESC via TIM4 CH1 (PB6): 50 Hz, pulso 1000-2000µs (padrão RC). */
#define HAL_ESC_PWM_PERIOD_US   20000U
#define HAL_ESC_PWM_MIN_US       1000U
#define HAL_ESC_PWM_MAX_US       2000U
#define HAL_ESC_PWM_ARM_US       1000U   /* pulso de arm */

/* RPM via TIM3 CH3 (PB0), modo capture. */
#define HAL_RPM_PULSES_PER_REV  14U      /* pólos do motor / 2 */
#define HAL_RPM_SAMPLE_MS       100U     /* janela de medição   */

/* I2C1 remapeado PB8(SCL)/PB9(SDA) — infra crua (ler/escrever/scan), sem
 * decisão de protocolo. Substituirá o HX711 no futuro. */
#define HAL_I2C_SCAN_ADDR_MIN   0x03U
#define HAL_I2C_SCAN_ADDR_MAX   0x77U

/* DAC1 canal 1 (PA5/DAC_OUT1) — sem driver Zephyr, acesso via MMIO bruto. */
#define HAL_DAC_MAX_VALUE       4095U    /* 12 bits */

/* API pública */

/* @brief Inicialização global do HAL. Chamar em main(). */
int  HAL_Init(void);

/* ADC — leitura bruta (índice, ver HAL_ADC_IDX_*). */
int      HAL_ADC_Init(void);
uint16_t HAL_ADC_Get(uint8_t channel);

/* Grandezas calibradas: y = Linear_Coeff*(mV no pino) + Offset_Coeff,
 * calibrado via CMD/shell (`siba sensor <id> linear|offset`). */
float HAL_ADC_Get_VBatt(void);
float HAL_ADC_Get_IBatt(void);
float HAL_ADC_Get_Temperature_Batt(void);
float HAL_ADC_Get_Air_Speed(void);

/* @brief Ajusta calibração afim de um sensor ADC.
 * @param sensor_id SENSOR_ID_V_BATT/I_BATT/TEMPERATURE_BATTERY/AIR_SPEED */
void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset);

/* @brief Lê a calibração atual; fora do range devolve (1.0, 0.0).
 * Ao alterar só um coeficiente, ler aqui primeiro e reescrever os dois. */
void HAL_ADC_Get_Calib(uint8_t sensor_id, float *linear, float *offset);

/* HX711 — modelo canônico grams = (raw - tare)/scale. */
int     HAL_HX711_Init(void);
int32_t HAL_HX711_Get_Raw(uint8_t index);      /* valor bruto 24 bits */
float   HAL_HX711_Get_Grams(uint8_t index);    /* (raw - tare)/scale  */
void    HAL_HX711_Set_Calib(uint8_t index, float scale, float tare_raw);

void    HAL_HX711_Get_Calib(uint8_t index, float *scale, float *tare_raw);
void    HAL_HX711_Tare(uint8_t index);          /* tara = raw atual (zera) */
/* @brief scale = (raw - tare)/known_weight. */
void    HAL_HX711_Calibrate_Weight(uint8_t index, float known_weight);

/* PWM / ESC */
int  HAL_ESC_Init(void);
int  HAL_ESC_Set_Throttle(float percent);   /* 0.0 – 100.0 */
void HAL_ESC_Arm(void);
void HAL_ESC_Disarm(void);

/* RPM */
int      HAL_RPM_Init(void);
float    HAL_RPM_Get(void);

/* I2C — infra crua (sem decisão de protocolo — isso fica para quem for
 * implementar o dispositivo específico por cima). Retornam 0 em sucesso
 * ou um errno negativo (ver i2c_read/i2c_write/i2c_write_read do Zephyr). */
int HAL_I2C_Init(void);
int HAL_I2C_Read(uint16_t addr, uint8_t *buf, size_t len);
int HAL_I2C_Write(uint16_t addr, const uint8_t *buf, size_t len);
int HAL_I2C_Scan(uint8_t *found_addrs, size_t max, size_t *out_count);

/* DAC — canal 1 (PA5/DAC_OUT1), valor bruto 12 bits (0-4095) */
int  HAL_DAC_Init(void);
void HAL_DAC_Set_Value(uint16_t value_12bit);


#endif /* HAL_H */