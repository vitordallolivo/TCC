#ifndef OSSI_H
#define OSSI_H

#include <zephyr/kernel.h>
#include <stdint.h>

/* same_wq — única workqueue do sistema (mesmo modelo da SIBA). Só duas
 * threads bloqueiam em I/O: HAL ADC (roda o observador inline e publica
 * telemetria) e NCI RX (recebe CMD). O observador não passa pela fila —
 * roda no ritmo do ADC; a fila só serializa telemetria e CMDs (~10 Hz). */
extern struct k_work_q same_wq;

/* @brief Inicializa e inicia a same_wq. Chamar em main() antes de outro Init. */
void OSSI_Init(void);

/*
 * @brief Publica um campo do TELEMETRY_LOG pra escrita assíncrona no log.
 * Thread-safe e ISR-safe.
 * @param sensor_id ver SENSOR_ID_* em Log_Manager.h
 * @param value valor já calculado
 */
void OSSI_Submit_Sensor(uint32_t sensor_id, float value);

/*
 * @brief Submete um pacote CMD recebido pela rede pra despacho no CMD_Core.
 * @param buf bytes do pacote (copiados internamente)
 * @param len tamanho de buf
 */
void OSSI_Submit_CMD(const uint8_t *buf, size_t len);

#endif /* OSSI_H */
