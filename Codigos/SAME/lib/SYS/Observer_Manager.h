#ifndef OBSERVER_MANAGER_H
#define OBSERVER_MANAGER_H

#include "Common_Types.h"   /* target_type */
#include <stdint.h>
#include <stdbool.h>

/* ===========================================================
 * Observer_Manager — Observador por Modos Deslizantes (SMO) + PLL
 *
 * Embarca as equações de ../Aplicacao_Pi/Scripts/test_coeff_auto.py (ver
 * SAME/CLAUDE.md "SMO — equações passo a passo"). Estados no referencial
 * estacionário αβ: correntes estimadas î_αβ e BEMF estimada ê_αβ; a PLL
 * extrai θ̂/ω da BEMF; o torque sai da reconstrução analítica da BEMF.
 *
 * FLUXO DE EVENTOS (ver OSSI.h):
 *   - Observer_Step() roda na thread do ADC, colado ao evento "conversão dos
 *     2 ADCs pronta" (ritmo Ts_adc). Sem fila, sem lock no caminho quente.
 *   - Um CMD que muda um parâmetro/ganho chama um Observer_Set_*; isso só
 *     marca uma reconfiguração PENDENTE (com os novos valores). A própria
 *     thread do ADC aplica no topo do laço via Observer_Apply_Pending(), que
 *     RECALCULA as constantes de discretização e ZERA o estado — ou seja,
 *     "reinicia o observador" quando uma variável dele muda, como exigido
 *     (î/ê/PLL/θ̂/filtros voltam a zero para não arrastar transitório com o
 *     modelo antigo).
 *
 * Ganhos na forma Γ (Gamma = K·Ts, invariante ao passo) — é o que a GUI
 * envia por CMD. As equações de atualização usam Γ DIRETAMENTE
 * (î += Γ_s1·s, ê += Γ_s2·s, Φ_e = 1−Γ_t); K = Γ/Ts só importa para
 * interpretação. Sinais fixos: Γ_s1 > 0, Γ_s2 < 0, Γ_t > 0.
 * =========================================================== */

/* Defaults + zera estado. Chamar em main() antes do HAL. */
void Observer_Init(void);

/* Zera SÓ o estado dinâmico (î,ê,I_pll,θ̂,filtros) sem mexer nos parâmetros.
 * Usado internamente ao reconfigurar; exposto para start/reset via CMD. */
void Observer_Reset(void);

/* Setters chamados pelo CMD_Core (thread same_wq). NÃO tocam no estado ao
 * vivo — marcam reconfiguração pendente; a thread do ADC aplica e reinicia.
 * `target` é o TARGET_* do CMD (Common_Types.h). Retorna true se o target
 * pertence ao observador (params do motor, ganhos Γ, LPFs, Ts_adc). */
bool Observer_Set_Param(target_type target, float value);

/* Restaura o valor default de fábrica do `target` (ação
 * DEFAULT_ACTION_VALUE_RESET_DEFAULT). Retorna true se tratado. */
bool Observer_Reset_Param_Default(target_type target);

/* Aplica reconfiguração pendente (recalcula constantes + reinicia estado).
 * Chamado no topo do laço da thread do ADC. Barato quando nada mudou. */
void Observer_Apply_Pending(void);

/* UM passo do observador (referencial αβ). Entradas em unidades físicas:
 * tensões de fase (V) e correntes de fase (A) já em αβ (Clarke). Atualiza o
 * estado interno e recalcula as saídas (rpm/Te/torque_helice). */
void Observer_Step(float v_alpha, float v_beta, float i_alpha, float i_beta);

/* Saídas do último passo (lidas na MESMA thread do ADC, sem lock). */
float Observer_Get_Rpm(void);            /* rpm mecânico (→ rpm_same) */
float Observer_Get_Te(void);             /* torque eletromagnético [N·m] */
float Observer_Get_Torque_Helice(void);  /* torque na hélice [N·m] */

#endif /* OBSERVER_MANAGER_H */
