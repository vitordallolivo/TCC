#ifndef NVM_H
#define NVM_H

/**
 * @file NVM.h
 * @brief Persistência em EEPROM I2C — calibração de sensores e parâmetros do
 * observador. API genérica: dispositivo I2C e endereço passados em NVM_Init(),
 * permitindo reusar na SIBA sem mudança de interface.
 *
 * Layout do EEPROM (FT24C32A-ETR-T, 4 KB, páginas de 32 bytes):
 *   0x000 (1 pág.) — Header: magic, versão, write_count, CRCs
 *   0x020 (2 pág.) — Calibração: NVM_SENSOR_COUNT × {id, linear, offset}
 *   0x060 (2 pág.) — Observador: NVM_OBS_PARAM_COUNT × {id, valor}
 */

#include <zephyr/drivers/i2c.h>
#include "Common_Types.h"
#include <stdint.h>

/* Número de sensores calibráveis (TARGET_CURRENT_A .. TARGET_VOLTAGE_C). */
#define NVM_SENSOR_COUNT     6U
/* Parâmetros do observador: POLES_PAIRS..TS_ADC (inclusive). */
#define NVM_OBS_PARAM_COUNT 10U

/** Entrada de calibração de sensor gravada no EEPROM. */
typedef struct __attribute__((packed)) {
    uint8_t id;      /**< target_type do sensor */
    float   linear;  /**< ganho (coeficiente linear) */
    float   offset;  /**< deslocamento (coeficiente de offset) */
} nvm_sensor_t;

/** Entrada de parâmetro do observador gravada no EEPROM. */
typedef struct __attribute__((packed)) {
    uint8_t id;      /**< target_type do parâmetro */
    float   value;   /**< valor atual (TARGET_TS_ADC em µs, igual ao CMD) */
} nvm_param_t;

/**
 * @brief Inicializa o NVM: sonda o EEPROM e carrega valores salvos, ou grava
 * defaults na primeira execução. Deve ser chamado APÓS HAL_Init() (para que
 * hal_adc_calib_init já tenha rodado) e ANTES de NCI_Set_Connection_Status().
 *
 * @param i2c_dev  dispositivo I2C Zephyr (ex.: DEVICE_DT_GET(DT_NODELABEL(i2c2)))
 * @param i2c_addr endereço I2C 7-bit do EEPROM (FT24C32A padrão: 0x50)
 * @return 0 em caso de sucesso; negativo se o EEPROM não responde
 */
int NVM_Init(const struct device *i2c_dev, uint8_t i2c_addr);

/**
 * @brief Persiste a calibração atual (todos os sensores). Chamar em CMD_Core
 * após confirmar uma mudança de calibração. Roda na same_wq (thread-safe).
 * Custo: ≈ 2 páginas × 5 ms + 1 página de header = ~15 ms.
 * @return 0 ok; negativo em caso de erro I2C
 */
int NVM_Save_Calibration(void);

/**
 * @brief Persiste os parâmetros do observador. Chamar em CMD_Core após
 * Observer_Set_Param() confirmar.
 * @return 0 ok; negativo em caso de erro I2C
 */
int NVM_Save_Observer(void);

/**
 * @brief Atualiza o cache local de calibração de um sensor.
 * Chamar junto com HAL_ADC_Set_Calib, antes de NVM_Save_Calibration().
 * @param sensor_id índice do sensor (0 .. NVM_SENSOR_COUNT-1)
 * @param linear    ganho
 * @param offset    deslocamento
 */
void NVM_Set_Sensor_Calib(uint8_t sensor_id, float linear, float offset);

/**
 * @brief Atualiza o cache local de um parâmetro do observador.
 * Chamar junto com Observer_Set_Param(), antes de NVM_Save_Observer().
 * @param target  TARGET_POLES_PAIRS .. TARGET_TS_ADC
 * @param value   valor (TARGET_TS_ADC em µs, igual ao que o CMD envia)
 */
void NVM_Set_Observer_Param(target_type target, float value);

/**
 * @brief Retorna o total de ciclos de gravação de página acumulados.
 * Endurance típico do FT24C32A: 1 000 000 ciclos por byte.
 */
uint32_t NVM_Get_Write_Count(void);

#endif /* NVM_H */
