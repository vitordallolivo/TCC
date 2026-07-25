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

/* ---- Saúde do observador (consumida pelo Security_Manager) ------------- */
/* @brief Amplitude da BEMF estimada |ê| do último passo (lock da PLL). */
float Observer_Get_Bemf_Amplitude(void);
/* @brief Erro de fase da PLL do último passo (rad; 0 se BEMF abaixo do limiar). */
float Observer_Get_Phase_Error(void);
/* @brief true se o estado dinâmico (î,ê,PLL,θ̂,torque) é finito (não divergiu). */
bool  Observer_State_Is_Finite(void);
/* @brief true se os parâmetros físicos são válidos (Rs,Ls,p,Kv,Ts > 0). */
bool  Observer_Params_Physical_Valid(void);
/* @brief true se os sinais dos ganhos Γ estão corretos (Γ1>0, Γ2<0, Γt>0). */
bool  Observer_Gamma_Signs_Valid(void);

#endif /* OBSERVER_MANAGER_H */
