#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <zephyr/kernel.h>

/* ===========================================================
 * IDs de Thread para o TCKEC
 * =========================================================== */
#define HAL_THREAD_ADC_ID    30
#define HAL_THREAD_HX711_ID  31
#define HAL_THREAD_RPM_ID    32

#define HAL_ADC_PRIO         7
#define HAL_HX711_PRIO       6
#define HAL_RPM_PRIO         6
#define HAL_STACK_SIZE       2048

/* ===========================================================
 * HX711 — 6 células de carga
 * Ajuste os GPIOs conforme o seu esquemático.
 * Formato: { port, pin }
 *
 * PD_SCK = clock de aquisição (saída do MCU)
 * DOUT   = dado serial       (entrada no MCU)
 * =========================================================== */
#define HX711_COUNT 6

/* Ganho: 128 = canal A / 1 pulso extra
 *        64  = canal B / 3 pulsos extra
 *        32  = canal A / 2 pulsos extra  */
#define HX711_GAIN_PULSES 1   /* 128x — padrão para strain gauge */

/* ===========================================================
 * ADC — canais analógicos
 * =========================================================== */
/* Canal 0 (PA0) reservado para expansão analógica futura      */
/* Tensão de bateria e corrente são medidos externamente        */

/* ===========================================================
 * PWM — ESC via TIM4 CH1 (PB6)
 * Período 20ms (50Hz), pulso 1000-2000µs (padrão RC)
 * =========================================================== */
#define HAL_ESC_PWM_PERIOD_US   20000U
#define HAL_ESC_PWM_MIN_US       1000U
#define HAL_ESC_PWM_MAX_US       2000U
#define HAL_ESC_PWM_ARM_US       1000U   /* pulso de arm */

/* ===========================================================
 * RPM — TIM3 CH3 (PB0) modo capture
 * =========================================================== */
#define HAL_RPM_PULSES_PER_REV  14U      /* pólos do motor / 2 */
#define HAL_RPM_SAMPLE_MS       100U     /* janela de medição   */

/* ===========================================================
 * API pública
 * =========================================================== */

/* Inicialização global do HAL — chamada em main() */
int  HAL_Init(void);

/* ADC */
int      HAL_ADC_Init(void);
uint16_t HAL_ADC_Get(uint8_t channel);

/* HX711 */
int     HAL_HX711_Init(void);
int32_t HAL_HX711_Get_Raw(uint8_t index);      /* valor bruto 24 bits */
float   HAL_HX711_Get_Grams(uint8_t index);    /* após linear/offset  */
void    HAL_HX711_Set_Calib(uint8_t index, float scale, float offset);
void    HAL_HX711_Tare(uint8_t index);

/* PWM / ESC */
int  HAL_ESC_Init(void);
int  HAL_ESC_Set_Throttle(float percent);   /* 0.0 – 100.0 */
void HAL_ESC_Arm(void);
void HAL_ESC_Disarm(void);

/* RPM */
int      HAL_RPM_Init(void);
float    HAL_RPM_Get(void);

/* Entry points das threads (registradas no TCKEC) */
void HAL_thread_ADC_fn  (void *p1, void *p2, void *p3);
void HAL_thread_HX711_fn(void *p1, void *p2, void *p3);
void HAL_thread_RPM_fn  (void *p1, void *p2, void *p3);

#endif /* HAL_H */