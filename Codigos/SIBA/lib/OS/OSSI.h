#ifndef OSSI_H
#define OSSI_H

#include <zephyr/kernel.h>
#include <stdint.h>

/* siba_wq — única fila de trabalho do sistema. Toda a lógica de despacho
 * (sensores, CMDs, log TX) roda como k_work items nessa fila, numa thread
 * dedicada de prioridade 5.
 *
 * Threads de I/O bloqueante (NCI RX, HAL ADC, HAL HX711) continuam como
 * threads Zephyr normais porque precisam bloquear em primitivas de hardware
 * (recvfrom, k_poll/adc_read_async, bit-bang GPIO). Tudo o mais vira
 * k_work item aqui — sem polling, sem contexto de thread desnecessário.
 *
 * Substitui o modelo anterior de 4 k_msgq + 5 threads de polling (TCKEC,
 * Sys, CMD_Core, NCI TX, HAL RPM) — economiza ~23 KB de RAM:
 *   - 20 KB pool TCKEC (managed_stacks[10][2048]) → eliminado
 *   - 11 KB buffers dos 4 k_msgq grandes (ossi_msg_t 136B × 20 itens × 4)
 *   - substituídos por 8 KB de stacks estáticas (4 threads × 2048B) +
 *     ~400 B de filas pequenas (sensor_queue 40×8 + cmd_queue 4×12) */
extern struct k_work_q siba_wq;

/* Inicializa e inicia o siba_wq. Chamar em main() antes de qualquer Init. */
void OSSI_Init(void);

/* Submete atualização de sensor para despacho pelos managers (Log, Battery,
 * Propeller, Security). Thread-safe e ISR-safe — pode ser chamada de
 * qualquer contexto (thread HAL, ISR de timer). */
void OSSI_Submit_Sensor(uint32_t sensor_id, float value);

/* Submete pacote CMD recebido pela rede para despacho pelo CMD_Core.
 * `buf` é copiado internamente. Thread-safe. */
void OSSI_Submit_CMD(const uint8_t *buf, size_t len);

#endif /* OSSI_H */
