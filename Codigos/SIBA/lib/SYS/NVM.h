#ifndef NVM_H
#define NVM_H

/**
 * @file NVM.h
 * @brief Persistência de calibração de sensores em EEPROM I2C (FT24C32A-ETR-T,
 * 4 KB, páginas de 32 bytes). Portado da SAME; sem bloco de observador (SIBA
 * não tem SMO). NVM_SENSOR_COUNT = CMD_CORE_SENSOR_COUNT (13 slots).
 *
 * Layout do EEPROM:
 *   0x000 (1 pág. / 32 B) — Header: magic, versão, write_count, CRC
 *   0x020 (4 pág. / 128 B) — Calibração: NVM_SENSOR_COUNT × {id, linear, offset}
 */

#include <zephyr/drivers/i2c.h>
#include <stdint.h>

/* 1:1 com CMD_CORE_SENSOR_COUNT; inclui slots de SOC e RPM (identidade). */
#define NVM_SENSOR_COUNT  13U

/** Entrada de calibração gravada no EEPROM. */
typedef struct __attribute__((packed)) {
    uint8_t id;      /**< SENSOR_ID_* */
    float   linear;  /**< ganho (HX711: scale; ADC: coef. linear) */
    float   offset;  /**< deslocamento (HX711: tare_raw; ADC: coef. offset) */
} nvm_sensor_t;

/**
 * @brief Inicializa o NVM: sonda o EEPROM, restaura calibração salva ou grava
 * defaults de fábrica na primeira execução.
 * @param i2c_dev  dispositivo I2C2 Zephyr
 * @param i2c_addr endereço 7-bit do EEPROM (FT24C32A: 0x50)
 * @return 0 OK; negativo se EEPROM não responde
 */
int NVM_Init(const struct device *i2c_dev, uint8_t i2c_addr);

/**
 * @brief Persiste calibração atual de todos os sensores no EEPROM.
 * Custo: ~4 páginas × 5 ms + 1 página header = ~25 ms.
 * @return 0 OK; negativo em erro I2C
 */
int NVM_Save_Calibration(void);

/**
 * @brief Atualiza cache local de calibração de um sensor.
 * Chamar antes de NVM_Save_Calibration().
 */
void NVM_Set_Sensor_Calib(uint8_t sensor_id, float linear, float offset);

/** @brief Total de ciclos de gravação acumulados. */
uint32_t NVM_Get_Write_Count(void);

#endif /* NVM_H */
