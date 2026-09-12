#ifndef SECURITY_MANAGER_H
#define SECURITY_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "Error_Codes.h"   /* same_error_bit_t — GERADO do SAME.yaml (error_bits) */

/*
 * Security_Manager — diagnóstico do nó SAME. Diferente da SIBA, a SAME só
 * OBSERVA o motor (não aciona o ESC), então NÃO há parada de emergência: este
 * manager só AGREGA condições de erro (1 bit por condição, same_error_bit_t) e
 * expõe o uint32_t que vai em TELEMETRY_LOG.error_code. A GUI decodifica em
 * pt-BR a partir da MESMA seção `error_bits` do SAME.yaml (errors.py).
 *
 * Padrão idêntico ao da SIBA: os checadores marcam/limpam bits via
 * Error_Detect/_Erase (só logam na transição de borda).
 */

/* @brief Zera o código de erro agregado. Chamar em main() antes do HAL. */
void Security_Manager_Init(void);

/* @return true se a chamada mudou o estado do bit (borda) — pra logar só na
 * transição. */
bool Security_Manager_Error_Detect(same_error_bit_t bit);
bool Security_Manager_Error_Erase(same_error_bit_t bit);

/* @brief Seta o bit se `cond`, senão limpa — açúcar pros checadores. */
void Security_Manager_Error_Set(same_error_bit_t bit, bool cond);

/* @brief Código de erro agregado atual — exposto em TELEMETRY_LOG.error_code. */
uint32_t Security_Manager_Get_Error_Code(void);

/* @brief true quando calibração feita + rede online (nenhum bit de MCU_READY_MASK ativo).
 * Reflete o LED verde (PB4). */
bool Security_Manager_Is_MCU_Ready(void);

/*
 * @brief Checa a sanidade das medidas elétricas de fase (desequilíbrio de
 * corrente/tensão, sobrecorrente, faixa do sensor). Chamada no ritmo de
 * publicação da telemetria pela thread do HAL.
 * @param Va,Vb,Vc tensões de fase [V]
 * @param ia,ib,ic correntes de fase [A]
 */
void Security_Manager_Check_Electrical(float Va, float Vb, float Vc,
                                       float ia, float ib, float ic);

/*
 * @brief Checa a saúde do observador (BEMF baixa, PLL sem lock, divergência,
 * RPM implausível) e a validade da configuração (params físicos, sinais Γ).
 * Lê os getters do Observer_Manager. Mesma thread do HAL.
 */
void Security_Manager_Check_Observer(void);

#endif /* SECURITY_MANAGER_H */
