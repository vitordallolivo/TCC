#ifndef OSSI_H
#define OSSI_H

#include <zephyr/kernel.h>
#include <stdint.h>

/* siba_wq — única fila de trabalho do sistema; sensores/CMDs/log TX rodam
 * como k_work nela. Só NCI RX/HAL ADC/HAL HX711 continuam como threads
 * bloqueantes de verdade (precisam bloquear em hardware). Substitui o
 * modelo antigo de 4 k_msgq + 5 threads de polling, economizando ~23 KB. */
extern struct k_work_q siba_wq;

/* @brief Inicializa e inicia o siba_wq. Chamar em main() antes de outro Init. */
void OSSI_Init(void);

/*
 * @brief Submete atualização de sensor pros managers (Log/Battery/
 * Propeller/Security). Thread-safe e ISR-safe.
 * @param sensor_id ver SENSOR_ID_* em Log_Manager.h
 * @param value valor já convertido
 */
void OSSI_Submit_Sensor(uint32_t sensor_id, float value);

/*
 * @brief Submete pacote CMD recebido pela rede pro despacho no CMD_Core.
 * @param buf bytes do pacote (copiados internamente)
 * @param len tamanho de buf
 */
void OSSI_Submit_CMD(const uint8_t *buf, size_t len);

#endif /* OSSI_H */
