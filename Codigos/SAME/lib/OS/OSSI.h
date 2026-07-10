#ifndef OSSI_H
#define OSSI_H

#include <zephyr/kernel.h>
#include <stdint.h>

/* same_wq — única workqueue do sistema. Espelha o modelo da SIBA: em vez de
 * várias threads de polling, tudo o que é "reação a um evento" (um CMD que
 * chegou pela rede, uma amostra pronta para virar telemetria) roda como
 * k_work item numa thread dedicada.
 *
 * Só duas threads de I/O bloqueante existem, cada uma bloqueando numa
 * primitiva de hardware/SO:
 *   - HAL ADC thread   → bloqueia em adc_read_async/k_poll (evento "conversão
 *     dos 2 ADCs pronta"); roda o passo do OBSERVADOR (SMO+PLL) inline e, a
 *     cada N amostras, publica a telemetria via OSSI_Submit_Sensor.
 *   - NCI RX thread    → bloqueia em recvfrom (evento "CMD recebido") e
 *     submete via OSSI_Submit_CMD.
 *
 * O observador NÃO passa pela fila: ele roda no ritmo do ADC (Ts_adc), colado
 * ao evento de conversão. A fila serializa só a telemetria e os CMDs — o que
 * é lento (~10 Hz) e não pode concorrer com a escrita do log. */
extern struct k_work_q same_wq;

/* Inicializa e inicia a same_wq. Chamar em main() antes de qualquer Init. */
void OSSI_Init(void);

/* Publica um campo já calculado do TELEMETRY_LOG (sensor_id em Log_Manager.h)
 * para ser escrito no log pela same_wq. Chamado pela thread do ADC (após o
 * passo do observador). Thread-safe e ISR-safe. */
void OSSI_Submit_Sensor(uint32_t sensor_id, float value);

/* Submete um pacote CMD recebido pela rede para despacho pelo CMD_Core.
 * `buf` é copiado internamente. Thread-safe. */
void OSSI_Submit_CMD(const uint8_t *buf, size_t len);

#endif /* OSSI_H */
