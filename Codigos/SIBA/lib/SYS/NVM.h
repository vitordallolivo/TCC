#ifndef NVM_H
#define NVM_H

/**
 * @file NVM.h
 * @brief Persistência de calibração de sensores e parâmetros do observador
 * em EEPROM I2C (FT24C32A-ETR-T, 4 KB, páginas de 32 bytes).
 *
 * Layout v2 do EEPROM:
 *   0x000 (1 pág. / 32 B)  — Header: magic, versão, write_count, CRCs
 *   0x020 (6 pág. / 192 B) — Calibração: NVM_SENSOR_COUNT × {id, linear, offset}
 *   0x0E0 (2 pág. / 64 B)  — Observador: NVM_OBS_PARAM_COUNT × {target, valor}
 * A v1 (só 13 sensores) é migrada no boot sem perder a calibração.
 */

#include <zephyr/drivers/i2c.h>
#include <stdint.h>
#include "CMD_Core_prm.h"   /* CMD_CORE_SENSOR_COUNT */
#include "Common_Types.h"   /* target_type */

/* 1:1 com CMD_CORE_SENSOR_COUNT (19: inclui SOC/RPM e as 6 fases). */
#define NVM_SENSOR_COUNT     ((uint8_t)CMD_CORE_SENSOR_COUNT)
/* TARGET_POLES_PAIRS..TARGET_TS_ADC. */
#define NVM_OBS_PARAM_COUNT  10U

/** Entrada de calibração gravada no EEPROM. */
typedef struct __attribute__((packed)) {
    uint8_t id;      /**< SENSOR_ID_* */
    float   linear;  /**< ganho (HX711: scale; ADC: coef. linear) */
    float   offset;  /**< deslocamento (HX711: tare_raw; ADC: coef. offset) */
} nvm_sensor_t;

/** Parâmetro do observador gravado no EEPROM. */
typedef struct __attribute__((packed)) {
    uint8_t id;      /**< TARGET_* */
    float   value;   /**< valor (TS_ADC em µs) */
} nvm_param_t;

/**
 * @brief Inicializa o NVM: sonda o EEPROM, restaura calibração e observador
 * salvos (migra a v1) ou grava os defaults na primeira execução.
 * Chamar depois de Observer_Init e do HAL.
 * @param i2c_dev  dispositivo I2C2 Zephyr
 * @param i2c_addr endereço 7-bit do EEPROM (FT24C32A: 0x50)
 * @return 0 OK; negativo se EEPROM não responde
 */
int NVM_Init(const struct device *i2c_dev, uint8_t i2c_addr);

/** @brief Grava a calibração de todos os sensores (~7 páginas, ~35 ms). */
int NVM_Save_Calibration(void);

/** @brief Grava os parâmetros do observador (~3 páginas, ~15 ms). */
int NVM_Save_Observer(void);

/** @brief Atualiza o cache de calibração de um sensor (antes de salvar). */
void NVM_Set_Sensor_Calib(uint8_t sensor_id, float linear, float offset);

/** @brief Atualiza o cache de um parâmetro do observador (antes de salvar). */
void NVM_Set_Observer_Param(target_type target, float value);

/** @brief Total de ciclos de gravação acumulados. */
uint32_t NVM_Get_Write_Count(void);

/** @brief Lê a calibração em cache (o que foi salvo/restaurado do EEPROM). */
void NVM_Get_Sensor_Calib(uint8_t sensor_id, float *linear, float *offset);

#endif /* NVM_H */
