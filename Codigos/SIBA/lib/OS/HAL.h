#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include "HAL_prm.h"

#define HAL_ADC_PRIO     7
#define HAL_HX711_PRIO   6
#define HAL_STACK_SIZE   2048

/* HX711 x6 — PD_SCK (clock, saída PE0-5) / DOUT (dado, entrada PE6-11). */
#define HX711_COUNT 6
#define HX711_GAIN_PULSES 1   /* 128x — canal A, padrão para strain gauge */

/* ── ADC1 master (PA): 3 canais ────────────────────────────────────────── *
 * idx 0 = PA0/CH0  → Pitot (air_speed)                                    *
 * idx 1 = PA1/CH1  → termistor bateria (temperature_battery)              *
 * idx 2 = PA3/CH3  → termistor motor  (temperature_motor)                 */
#define HAL_ADC1_IDX_AIR_SPEED    0
#define HAL_ADC1_IDX_TEMP_BATT    1
#define HAL_ADC1_IDX_TEMP_MOTOR   2

/* ── ADC2 slave (PC, dual simultâneo com ADC1): 3 canais ───────────────── *
 * idx 0 = PC3/CH13 → tensão bateria (v_batt)                              *
 * idx 1 = PC4/CH14 → corrente bateria (i_batt)                            *
 * idx 2 = PC2/CH12 → reserva                                               */
#define HAL_ADC2_IDX_V_BATT       0
#define HAL_ADC2_IDX_I_BATT       1
#define HAL_ADC2_IDX_SPARE        2

#define HAL_ADC_CHANNELS_PER_ADC  3U
#define HAL_ADC_VREF_MV           3300U
#define HAL_ADC_FULL_SCALE        4096U   /* 12 bits */

/* ESC via TIM4 CH1 (PB6): 50 Hz, pulso 1000-2000 µs (padrão RC). */
#define HAL_ESC_PWM_PERIOD_US   20000U
#define HAL_ESC_PWM_MIN_US       1000U
#define HAL_ESC_PWM_MAX_US       2000U
#define HAL_ESC_PWM_ARM_US       1000U

/* RPM via TIM3 CH3 (PB0), modo input capture. */
#define HAL_RPM_PULSES_PER_REV  14U
#define HAL_RPM_SAMPLE_MS       100U

/* I2C2 (PB10=SCL, PB11=SDA) — EEPROM + expansão. */
#define HAL_I2C_SCAN_ADDR_MIN   0x03U
#define HAL_I2C_SCAN_ADDR_MAX   0x77U

/* O tempo de pré-carga NÃO vive aqui: é parâmetro de bancada, decidido pela
 * camada System e passado em HAL_Relay_Energize(). Ver Battery_Manager. */

/* ── API pública ──────────────────────────────────────────────────────── */

int HAL_Init(void);

/* ADC — dual simultâneo ADC1+ADC2. */
int      HAL_ADC_Init(void);
uint16_t HAL_ADC1_Get(uint8_t idx);  /* ADC1: HAL_ADC1_IDX_* */
uint16_t HAL_ADC2_Get(uint8_t idx);  /* ADC2: HAL_ADC2_IDX_* */

float HAL_ADC_Get_VBatt(void);
float HAL_ADC_Get_IBatt(void);
float HAL_ADC_Get_Temperature_Batt(void);
float HAL_ADC_Get_Temperature_Motor(void);
float HAL_ADC_Get_Air_Speed(void);

void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset);
void HAL_ADC_Get_Calib(uint8_t sensor_id, float *linear, float *offset);

/* HX711 */
int     HAL_HX711_Init(void);
int32_t HAL_HX711_Get_Raw(uint8_t index);
float   HAL_HX711_Get_Grams(uint8_t index);
void    HAL_HX711_Set_Calib(uint8_t index, float scale, float tare_raw);
void    HAL_HX711_Get_Calib(uint8_t index, float *scale, float *tare_raw);
void    HAL_HX711_Tare(uint8_t index);
void    HAL_HX711_Calibrate_Weight(uint8_t index, float known_weight);

/* ESC */
int  HAL_ESC_Init(void);
int  HAL_ESC_Set_Throttle(float percent);
void HAL_ESC_Arm(void);
void HAL_ESC_Disarm(void);
bool  HAL_ESC_Is_Armed(void);
float HAL_ESC_Get_Throttle_Pct(void);

/* RPM */
int   HAL_RPM_Init(void);
float HAL_RPM_Get(void);

/* I2C2 — infra crua (protocolo fica para NVM e quem usar por cima). */
int HAL_I2C_Init(void);
int HAL_I2C_Read(uint16_t addr, uint8_t *buf, size_t len);
int HAL_I2C_Write(uint16_t addr, const uint8_t *buf, size_t len);
int HAL_I2C_Scan(uint8_t *found_addrs, size_t max, size_t *out_count);

/* Relés — sequência obrigatória.
 * HAL_Relay_Energize: fecha pre-carga → aguarda `precharge_ms` → fecha
 * potência. O HAL não escolhe esse tempo: recebe o que a camada de cima
 * mandar, e só garante a ordem dos contatos.
 * HAL_Relay_Deenergize: abre potência → abre pre-carga (sem delay).
 * Ambas bloqueiam a thread chamadora durante a sequência. */
int  HAL_Relay_Init(void);
void HAL_Relay_Energize(uint32_t precharge_ms);
void HAL_Relay_Deenergize(void);
bool HAL_Relay_Is_Energized(void);
/* true se QUALQUER um dos dois contatos estiver fechado — inclusive durante
 * a pré-carga, quando a sequência ainda não terminou mas já há corrente. */
bool HAL_Relay_Any_Closed(void);
/* Estado de cada contato isoladamente, para diagnóstico. */
bool HAL_Relay_Precharge_Closed(void);
bool HAL_Relay_Power_Closed(void);

/* Botão de emergência (PD2, entrada) + LED externo de pronto (PE15, saída).
 * A inversão do botão (0 = apertado) está declarada no DTS como
 * GPIO_ACTIVE_LOW; HAL_Emergency_Is_Pressed já devolve lógica positiva.
 * Falha de leitura do pino é reportada como "apertado" (falha segura). */
int  HAL_Emergency_Init(void);
bool HAL_Emergency_Is_Pressed(void);
void HAL_Ready_Led_Set(bool on);
#ifdef CONFIG_ARCH_POSIX
/* Só native_sim: finge o botão, para exercitar a recusa de armar sem o pino
 * físico. Não existe no build da placa. */
void HAL_Emergency_Sim_Set_Pressed(bool pressed);
#endif

#endif /* HAL_H */
