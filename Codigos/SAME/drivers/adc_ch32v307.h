/**
 * @file adc_ch32v307.h
 * @brief API pública estendida do driver ADC CH32V307 — fora da interface
 *        Zephyr padrão (struct adc_driver_api).
 *
 * Inclua em código que precise registrar um callback de frame: o callback
 * é chamado DIRETAMENTE DA ISR de DMA sem nenhum overhead de kernel.
 */
#ifndef DRIVERS_ADC_CH32V307_H
#define DRIVERS_ADC_CH32V307_H

#include <zephyr/device.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Assinatura do callback de frame ADC (chamado em contexto de ISR).
 *
 * @param frame    Ponteiro para o início da metade estável do buffer de DMA
 *                 ping-pong.  Cada word de 32 bits contém um par simultâneo:
 *                 bits [15:0]  = código bruto do ADC1 (canal i)
 *                 bits [31:16] = código bruto do ADC2 (canal i)
 * @param nch      Número de canais (= número de words em @p frame).
 * @param cal_master Offset de calibração HW do ADC1 (LSBs, subtrai do raw).
 * @param cal_slave  Offset de calibração HW do ADC2 (LSBs, subtrai do raw).
 * @param user     Ponteiro opaco passado em adc_ch32v307_set_frame_callback().
 *
 * @note  Esta função é chamada em ISR. Não use primitivas de kernel que
 *        possam bloquear (k_mutex_lock, k_sem_take, k_sleep, etc.).
 *        k_work_submit(), k_uptime_get_32() e operações atômicas são seguras.
 */
typedef void (*adc_ch32v307_frame_cb_t)(const uint32_t *frame, uint8_t nch,
                                        int32_t cal_master, int32_t cal_slave,
                                        void *user);

/**
 * @brief Registra (ou remove) um callback de frame para o ADC em modo dual.
 *
 * Quando um callback está registrado, a ISR de DMA chama-o diretamente ao
 * invés de dar k_sem_give — eliminando o custo de context-switch do caminho
 * crítico ADC → Clarke → Observer.
 *
 * Passar @p cb = NULL remove o callback e restaura o comportamento padrão
 * (semáforo → adc_read bloqueante).
 *
 * @param dev  Device ADC1 (mestre do par dual).
 * @param cb   Função callback, ou NULL para desregistrar.
 * @param user Ponteiro opaco repassado a @p cb em cada chamada.
 */
void adc_ch32v307_set_frame_callback(const struct device *dev,
                                     adc_ch32v307_frame_cb_t cb,
                                     void *user);

#ifdef __cplusplus
}
#endif
#endif /* DRIVERS_ADC_CH32V307_H */
