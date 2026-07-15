#ifndef OBSERVER_MANAGER_H
#define OBSERVER_MANAGER_H

#include "Common_Types.h"   /* target_type */
#include <stdint.h>
#include <stdbool.h>

/*
 * Observer_Manager — Observador por Modos Deslizantes (SMO) + PLL, refencial
 * αβ. Equações em test_coeff_auto.py / SAME/CLAUDE.md "SMO — equações".
 * Observer_Step roda na thread do ADC; um CMD só marca reconfig pendente,
 * aplicada por Observer_Apply_Pending (recalcula constantes, zera estado).
 * Ganhos na forma Γ (Gamma = K·Ts, invariante ao passo): Γ_s1>0, Γ_s2<0, Γ_t>0.
 */

/* @brief Aplica defaults e zera estado. Chamar em main() antes do HAL. */
void Observer_Init(void);

/* @brief Zera só o estado dinâmico (î,ê,I_pll,θ̂,filtros), mantém parâmetros. */
void Observer_Reset(void);

/*
 * @brief Agenda mudança de um parâmetro/ganho do observador (via CMD_Core).
 * Não toca no estado ao vivo — só marca reconfig pendente.
 * @param target TARGET_* do CMD (Common_Types.h)
 * @param value novo valor
 * @return true se o target pertence ao observador
 */
bool Observer_Set_Param(target_type target, float value);

/*
 * @brief Restaura o default de fábrica de um target.
 * @param target TARGET_* do CMD
 * @return true se tratado
 */
bool Observer_Reset_Param_Default(target_type target);

/* @brief Aplica reconfig pendente (recalcula constantes + zera estado).
 * Chamada no topo do laço da thread do ADC; barata se nada mudou. */
void Observer_Apply_Pending(void);

/*
 * @brief Executa um passo do observador (referencial αβ).
 * @param v_alpha, v_beta tensão de fase em αβ [V]
 * @param i_alpha, i_beta corrente de fase em αβ [A]
 */
void Observer_Step(float v_alpha, float v_beta, float i_alpha, float i_beta);

/* @brief Saídas do último passo (lidas na mesma thread do ADC, sem lock). */
float Observer_Get_Rpm(void);            /* rpm mecânico (→ rpm_same) */
float Observer_Get_Te(void);             /* torque eletromagnético [N·m] */
float Observer_Get_Torque_Helice(void);  /* torque na hélice [N·m] */

#endif /* OBSERVER_MANAGER_H */
