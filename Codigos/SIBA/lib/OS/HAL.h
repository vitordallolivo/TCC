#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include "HAL_prm.h"

#define HAL_ADC_PRIO     7
#define HAL_HX711_PRIO   6
#define HAL_STACK_SIZE   2048

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
 * suporta. Por isso todo canal abaixo vive em PA0-PA7 (HX711 foi
 * remapeado para a Porta E para abrir espaço — ver HAL.c).
 *
 * Pinagem REAL aplicada conforme esquemático (ver HW.md "Pinagem real"):
 *
 * idx | canal ADC1 | pino | grandeza              | sensor
 * ----|------------|------|-----------------------|------------------
 *  0  | CH0        | PA0  | tensão de bateria     | v_batt
 *  1  | CH1        | PA1  | corrente de bateria   | i_batt
 *  2  | CH2        | PA2  | velocidade do ar      | air_speed (Pitot)
 *  3  | CH6        | PA6  | temperatura bateria   | temperature_battery
 *      (uso DUPLO — mesmo circuito de Ensaio_Bateria_Termistor_1)
 *
 * Os pinos PA3/PA4/PA7 (Ensaio_Bateria_Voltage/DAC/Termistor_2) são da
 * bancada de ensaio — não entram nesse scan nem no TELEMETRY_LOG (ver
 * CLAUDE.md). PA8 (Ensaio_Bateria_Corrente) está fora do range do ADC1 —
 * ver HAL_RESERVED_PA8_IDX abaixo.
 * =========================================================== */
#define HAL_ADC_IDX_V_BATT       0
#define HAL_ADC_IDX_I_BATT       1
#define HAL_ADC_IDX_AIR_SPEED    2
#define HAL_ADC_IDX_TEMP_BATT    3

/* Tensão de referência do ADC (mV) — Vref interno CH32V307 */
#define HAL_ADC_VREF_MV          3300U
#define HAL_ADC_FULL_SCALE       4096U   /* 12 bits */

/* ===========================================================
 * PA8 — "Ensaio_Bateria_Corrente" (esquemático). Fora do range físico do
 * ADC1 (CH0-CH7 = PA0-PA7) e do mux do ADC2 suportado pelo driver — função
 * exata ainda não confirmada. Reservado no .dts (`pa8_reserved`) + slot de
 * índice aqui ("espaço de memória" para uso futuro — sem leitura ainda).
 * =========================================================== */
#define HAL_RESERVED_PA8_IDX     0

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
 * I2C — I2C1 remapeado para PB8(SCL)/PB9(SDA) (remap=1)
 *
 * Infra MÍNIMA e crua (ler/escrever N bytes num endereço, scan do
 * barramento) — substituirá o HX711 no futuro; quem decide o protocolo
 * de cada dispositivo é código que ainda será escrito por cima disso.
 * Usa a API genérica i2c_* do Zephyr sobre o driver wch,i2c já existente.
 * =========================================================== */
#define HAL_I2C_SCAN_ADDR_MIN   0x03U
#define HAL_I2C_SCAN_ADDR_MAX   0x77U

/* ===========================================================
 * DAC — DAC1 canal 1 (PA5 / DAC_OUT1)
 *
 * Não existe driver/binding Zephyr para o DAC do CH32V307 — acesso via
 * MMIO bruto, mesmo padrão usado para RPM/TIM3 (ver HAL_RPM_* em HAL.c).
 * =========================================================== */
#define HAL_DAC_MAX_VALUE       4095U    /* 12 bits */

/* ===========================================================
 * API pública
 * =========================================================== */

/* Inicialização global do HAL — chamada em main() */
int  HAL_Init(void);

/* ADC — leitura bruta (índice na sequência, ver HAL_ADC_IDX_*) */
int      HAL_ADC_Init(void);
uint16_t HAL_ADC_Get(uint8_t channel);

/* ADC — grandezas já calibradas (publicadas também via OSSI→SYS).
 * Sem fórmula física embutida: cada uma é só
 *   y = Linear_Coeff * (tensão_no_pino_em_mV) + Offset_Coeff
 * (mesmíssimo padrão do HX711/`HAL_HX711_Set_Calib` — ver
 * HAL_ADC_Set_Calib). O usuário calibra linear/offset via CMD/shell
 * (`siba sensor <id> linear|offset`) com os valores reais da bancada,
 * sem depender de constantes de hardware incertas (divisor, ACS758,
 * NTC, MPXV7002DP — removidas, ver TODO em CLAUDE.md). */
float HAL_ADC_Get_VBatt(void);
float HAL_ADC_Get_IBatt(void);
float HAL_ADC_Get_Temperature_Batt(void);
float HAL_ADC_Get_Air_Speed(void);

/* ADC — calibração (Linear_Coeff/Offset_Coeff aplicados como
 * y = Linear_Coeff * tensão_no_pino_mV + Offset_Coeff, mesmo padrão do HX711).
 * sensor_id: SENSOR_ID_V_BATT/I_BATT/TEMPERATURE_BATTERY/AIR_SPEED (Log_Manager.h) */
void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset);

/* ADC — lê o par (linear, offset) atualmente aplicado. HAL é a fonte única
 * de verdade da calibração — quem for alterar só UM dos dois coeficientes
 * (CMD_Core/shell) deve ler aqui primeiro e reescrever ambos, senão o campo
 * não tocado é perdido (sobrescrito com o default). Para sensor_id fora do
 * range, devolve (1.0, 0.0). */
void HAL_ADC_Get_Calib(uint8_t sensor_id, float *linear, float *offset);

/* HX711 — modelo canônico: grams = (raw - tare)/scale (um só offset = tare,
 * em counts; scale = counts/unidade). Ver HAL.c. */
int     HAL_HX711_Init(void);
int32_t HAL_HX711_Get_Raw(uint8_t index);      /* valor bruto 24 bits */
float   HAL_HX711_Get_Grams(uint8_t index);    /* (raw - tare)/scale  */
void    HAL_HX711_Set_Calib(uint8_t index, float scale, float tare_raw);

/* HX711 — lê o par canônico (scale, tare_raw) atualmente aplicado. */
void    HAL_HX711_Get_Calib(uint8_t index, float *scale, float *tare_raw);
void    HAL_HX711_Tare(uint8_t index);          /* tara = raw atual (zera) */
/* Calibra a escala por peso conhecido: scale = (raw - tare)/known_weight. */
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