#ifndef ERROR_CODES_H
#define ERROR_CODES_H

/* Auto-generated — edite SAME.yaml (seção `error_bits`), não este arquivo.
 *
 * Código de erro agregado: 1 bit por condição de erro/alarme. Cada manager
 * reporta via Security_Manager_Error_Detect/_Erase (só loga na transição). O
 * agregado (uint32_t) vai pro TELEMETRY_LOG.error_code e a GUI decodifica em
 * pt-BR (Communication_Data/errors.py, que lê a MESMA seção `error_bits`). */

#include <stdint.h>

typedef enum {
    ERROR_MEAS_CURRENT_IMBALANCE = (1U << 0),  /* Medição: As correntes das 3 fases não somam zero — sensor de corrente com falha, fase aberta ou calibração fora */
    ERROR_MEAS_VOLTAGE_IMBALANCE = (1U << 1),  /* Medição: As tensões das 3 fases não somam zero — divisor/fase com problema ou calibração fora */
    ERROR_MEAS_OVERCURRENT = (1U << 2),  /* Medição: Corrente de fase acima do limite seguro */
    ERROR_MEAS_SENSOR_RANGE = (1U << 3),  /* Medição: Leitura de tensão/corrente fora da faixa válida do sensor (saturado ou desconectado) */
    ERROR_OBS_BEMF_TOO_LOW = (1U << 8),  /* Observador: Força contraeletromotriz baixa demais para o observador travar — motor parado/muito lento; rotação e torque estimados não são confiáveis */
    ERROR_OBS_PLL_UNLOCKED = (1U << 9),  /* Observador: PLL sem sincronismo (erro de fase alto) — ângulo/rotação estimados não confiáveis */
    ERROR_OBS_DIVERGED = (1U << 10),  /* Observador: Observador divergiu (valores não finitos) — reduza os ganhos Γ ou o passo Ts */
    ERROR_OBS_RPM_IMPLAUSIBLE = (1U << 11),  /* Observador: Rotação estimada acima do limite plausível para este motor */
    ERROR_CFG_MOTOR_PARAM_INVALID = (1U << 16),  /* Configuração: Parâmetro do motor inválido (R, L, pares de polos, Kv ou Ts ≤ 0) — configure antes de estimar */
    ERROR_CFG_GAMMA_SIGN = (1U << 17),  /* Configuração: Sinais dos ganhos Γ incorretos (regra fixa: Γ1 > 0, Γ2 < 0, Γt > 0) */
    ERROR_SYS_ADC_READ_FAILED = (1U << 24),  /* Sistema: Falha na leitura do ADC (aquisição elétrica interrompida) */
    ERROR_SYS_ADC_FRAME_DROP = (1U << 25),  /* Sistema: Frames do ADC descartados — a ISR não acompanhou a taxa de conversão; as amostras do observador estão deslocadas no tempo */
    ERROR_CAL_IN_PROGRESS = (1U << 26),  /* Calibração: Calibração de offset em andamento (aguardando o sensor Hall estabilizar, com o ESC parado) — rotação e torque ainda não são válidos */
    ERROR_CAL_FAILED = (1U << 27),  /* Calibração: Calibração de offset falhou: ruído alto demais nas correntes com o ESC parado — ESC energizado durante a calibração, sensor mal alimentado ou terra aberto */
    ERROR_SYS_NET_DISCONNECTED = (1U << 28),  /* Sistema: Rede desconectada — PHY sem link ou interface Ethernet não inicializada; telemetria não está chegando ao PC */
} same_error_bit_t;

#endif /* ERROR_CODES_H */
