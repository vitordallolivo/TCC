/**
 * @file Security_Manager.c
 * @brief Diagnóstico agregado do nó SAME (sem parada de emergência — a SAME só
 * observa). Espelha o padrão Error_Detect/_Erase/Get_Error_Code da SIBA; os
 * bits (same_error_bit_t) são gerados de SAME.yaml (Error_Codes.h).
 */
#include "Security_Manager.h"
#include "Security_prm.h"       /* limiares SM_* — GERADOS de SAME.yaml */
#include "Observer_Manager.h"   /* getters de saúde do SMO/PLL */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <math.h>

LOG_MODULE_REGISTER(security_mgr, LOG_LEVEL_WRN);

/* Limiares dos checadores (SM_*) vêm de Security_prm.h, gerado da seção
 * `security_limits` do SAME.yaml. Desequilíbrio: |soma das 3 fases| aceitável =
 * SM_IMBAL_ABS + SM_IMBAL_FRAC·(soma dos módulos) — o termo relativo cobre o
 * resíduo do sampling sequencial dos canais dentro de cada ADC (as 3 fases não
 * são amostradas no MESMO instante). */

/* Código de erro agregado. Escrito só pela thread do HAL (checadores + falha
 * de ADC); lido pela same_wq no Log_Manager_Transmit. uint32_t alinhado →
 * leitura/escrita atômica na plataforma; sem lock (mesmo modelo da SIBA). */
static uint32_t error_code;

/** @brief Zera o código de erro agregado. */
void Security_Manager_Init(void)
{
    error_code = 0U;
    LOG_INF("Security_Manager (SAME) inicializado");
}

/**
 * @brief Seta um bit de erro se ainda não estiver setado (borda de subida).
 * @param bit bit a setar
 * @return true se mudou o estado (borda)
 */
bool Security_Manager_Error_Detect(same_error_bit_t bit)
{
    if (error_code & (uint32_t)bit) {
        return false;
    }
    error_code |= (uint32_t)bit;
    LOG_WRN("SAME: erro ATIVADO (bit 0x%08x)", (uint32_t)bit);
    return true;
}

/**
 * @brief Limpa um bit de erro se estiver setado (borda de descida).
 * @param bit bit a limpar
 * @return true se mudou o estado (borda)
 */
bool Security_Manager_Error_Erase(same_error_bit_t bit)
{
    if (!(error_code & (uint32_t)bit)) {
        return false;
    }
    error_code &= ~(uint32_t)bit;
    LOG_INF("SAME: erro resolvido (bit 0x%08x)", (uint32_t)bit);
    return true;
}

/**
 * @brief Seta o bit quando `cond` é verdadeiro, senão limpa (só age na borda).
 * @param bit bit de erro
 * @param cond condição do erro
 */
void Security_Manager_Error_Set(same_error_bit_t bit, bool cond)
{
    if (cond) {
        Security_Manager_Error_Detect(bit);
    } else {
        Security_Manager_Error_Erase(bit);
    }
}

/** @brief Código de erro agregado atual. @return bitmask (same_error_bit_t). */
uint32_t Security_Manager_Get_Error_Code(void)
{
    return error_code;
}

/**
 * @brief Checa a sanidade das medidas elétricas de fase.
 * @param Va,Vb,Vc tensões de fase [V]
 * @param ia,ib,ic correntes de fase [A]
 */
void Security_Manager_Check_Electrical(float Va, float Vb, float Vc,
                                       float ia, float ib, float ic)
{
    /* Desequilíbrio de corrente: num motor em estrela sem neutro, i_a+i_b+i_c≈0.
     * Fugir disso indica sensor com falha, fase aberta ou calibração fora. */
    float sum_i = ia + ib + ic;
    float mag_i = fabsf(ia) + fabsf(ib) + fabsf(ic);
    Security_Manager_Error_Set(ERROR_MEAS_CURRENT_IMBALANCE,
        fabsf(sum_i) > (SM_IMBAL_ABS_I + SM_IMBAL_FRAC * mag_i));

    /* Desequilíbrio de tensão: mesma ideia para as tensões de fase. */
    float sum_v = Va + Vb + Vc;
    float mag_v = fabsf(Va) + fabsf(Vb) + fabsf(Vc);
    Security_Manager_Error_Set(ERROR_MEAS_VOLTAGE_IMBALANCE,
        fabsf(sum_v) > (SM_IMBAL_ABS_V + SM_IMBAL_FRAC * mag_v));

    /* Sobrecorrente de fase. */
    Security_Manager_Error_Set(ERROR_MEAS_OVERCURRENT,
        fabsf(ia) > SM_OVERCURRENT_A || fabsf(ib) > SM_OVERCURRENT_A
                                     || fabsf(ic) > SM_OVERCURRENT_A);

    /* Fora de faixa / não finito = sensor saturado ou desconectado. */
    bool range = !isfinite(Va) || !isfinite(Vb) || !isfinite(Vc)
              || !isfinite(ia) || !isfinite(ib) || !isfinite(ic)
              || fabsf(Va) > SM_SENSOR_ABS_MAX || fabsf(Vb) > SM_SENSOR_ABS_MAX
              || fabsf(Vc) > SM_SENSOR_ABS_MAX || fabsf(ia) > SM_SENSOR_ABS_MAX
              || fabsf(ib) > SM_SENSOR_ABS_MAX || fabsf(ic) > SM_SENSOR_ABS_MAX;
    Security_Manager_Error_Set(ERROR_MEAS_SENSOR_RANGE, range);
}

/**
 * @brief Checa a saúde do observador e a validade da configuração.
 */
void Security_Manager_Check_Observer(void)
{
    /* BEMF baixa demais: PLL não trava (motor parado/muito lento). */
    bool bemf_low = Observer_Get_Bemf_Amplitude() <= SM_BEMF_MIN;
    Security_Manager_Error_Set(ERROR_OBS_BEMF_TOO_LOW, bemf_low);

    /* PLL sem lock: só faz sentido avaliar com BEMF suficiente. */
    Security_Manager_Error_Set(ERROR_OBS_PLL_UNLOCKED,
        !bemf_low && fabsf(Observer_Get_Phase_Error()) > SM_PLL_ERR_MAX);

    /* Divergência numérica (ganhos Γ/Ts inadequados). */
    Security_Manager_Error_Set(ERROR_OBS_DIVERGED, !Observer_State_Is_Finite());

    /* RPM implausível. */
    Security_Manager_Error_Set(ERROR_OBS_RPM_IMPLAUSIBLE,
        fabsf(Observer_Get_Rpm()) > SM_RPM_MAX);

    /* Configuração do observador. */
    Security_Manager_Error_Set(ERROR_CFG_MOTOR_PARAM_INVALID,
        !Observer_Params_Physical_Valid());
    Security_Manager_Error_Set(ERROR_CFG_GAMMA_SIGN,
        !Observer_Gamma_Signs_Valid());
}
