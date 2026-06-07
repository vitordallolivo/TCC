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
/* Só registrada em native_sim (CONFIG_ARCH_POSIX) — alimenta o adc_emul
 * com sinais sintéticos (ver HAL.c::hal_adc_stimulus_thread_fn /
 * native_sim.overlay). Usa um slot do pool TCKEC (folga de 3, ver CLAUDE.md). */
#define HAL_THREAD_ADC_STIMULUS_ID 33

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
 * ADC — canais analógicos (scan circular único via DMA, ADC1)
 *
 * O periférico ADC1 do CH32V307 só alcança os canais 0-7 (pinos PA0-PA7);
 * canais 8+ pertencem ao mux do ADC2, que o driver custom adc_ch32v307 não
 * suporta. Por isso todo canal novo abaixo vive em PA0-PA7 (HX711 foi
 * remapeado para a Porta E para abrir espaço — ver HAL.c).
 *
 * idx | canal ADC1 | pino | grandeza              | sensor
 * ----|------------|------|-----------------------|------------------
 *  0  | CH0        | PA0  | expansão genérica     | —
 *  1  | CH1        | PA1  | tensão de bateria     | divisor resistivo
 *  2  | CH2        | PA2  | corrente de bateria   | ACS758 (Hall, analógico)
 *  3  | CH3        | PA3  | temperatura bateria   | termistor NTC (lookup table)
 *  4  | CH4        | PA4  | velocidade do ar      | MPXV7002DP (pressão diferencial)
 *
 * ⚠ Pinagem placeholder — confirmar no esquemático (ver HW.md).
 * =========================================================== */
#define HAL_ADC_IDX_EXPANSION    0
#define HAL_ADC_IDX_V_BATT       1
#define HAL_ADC_IDX_I_BATT       2
#define HAL_ADC_IDX_TEMP_BATT    3
#define HAL_ADC_IDX_AIR_SPEED    4

/* Tensão de referência do ADC (mV) — Vref interno CH32V307 */
#define HAL_ADC_VREF_MV          3300U
#define HAL_ADC_FULL_SCALE       4096U   /* 12 bits */

/* --- Divisor de tensão de bateria (V_BATT) ---
 * Vbat = Vadc * (R1 + R2) / R2
 * Ajustar R1/R2 conforme o divisor real do esquemático.
 * Valores abaixo assumem divisor para 6S LiPo (~25.2V) → Vadc max ~3.0V */
#define HAL_VBATT_DIVIDER_R1_KOHM   68.0f
#define HAL_VBATT_DIVIDER_R2_KOHM   10.0f

/* --- Sensor de corrente ACS758 (analógico, efeito Hall) ---
 * Iout = (Vadc - Vzero) / sensibilidade
 * Variante 50A bidirecional: Vzero = Vcc/2 (~1.65V em 3.3V), 40 mV/A.
 * Ajustar conforme a variante real do ACS758 usada (ex.: 50U, 100U, 150B...). */
#define HAL_ACS758_VZERO_MV       1650.0f
#define HAL_ACS758_SENS_MV_PER_A    40.0f

/* --- MPXV7002DP (pressão diferencial ±2 kPa) → velocidade do ar ---
 * Vout = Vs * (0.2 * P_kPa + 0.5)   →   P_kPa = (Vout/Vs - 0.5) / 0.2
 * v = sqrt(2 * |P| / rho_ar), rho_ar ≈ 1.225 kg/m³ (ao nível do mar) */
#define HAL_MPXV7002DP_VS_MV     5000.0f
#define HAL_AIR_DENSITY_KG_M3       1.225f

/* --- Termistor NTC (temperatura da bateria) ---
 * Conversão por lookup table (tensão ADC → °C), ver hal_ntc_lut[] em HAL.c.
 * Montagem assumida: divisor NTC + resistor fixo, NTC para GND. */
#define HAL_NTC_PULLUP_KOHM      10.0f

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

/* ADC — leitura bruta (índice na sequência, ver HAL_ADC_IDX_*) */
int      HAL_ADC_Init(void);
uint16_t HAL_ADC_Get(uint8_t channel);

/* ADC — grandezas físicas já convertidas (publicadas também via OSSI→SYS) */
float HAL_ADC_Get_VBatt(void);          /* V   — divisor de tensão        */
float HAL_ADC_Get_IBatt(void);          /* A   — ACS758                    */
float HAL_ADC_Get_Temperature_Batt(void);/* °C  — termistor NTC (lookup)   */
float HAL_ADC_Get_Air_Speed(void);      /* m/s — MPXV7002DP (pressão diff) */

/* ADC — calibração (Linear_Coeff/Offset_Coeff aplicados como
 * y = Linear_Coeff * grandeza_fisica + Offset_Coeff, mesmo padrão do HX711).
 * sensor_id: SENSOR_ID_V_BATT/I_BATT/TEMPERATURE_BATTERY/AIR_SPEED (Log_Manager.h) */
void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset);

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