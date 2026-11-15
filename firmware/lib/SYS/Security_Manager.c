/**
 * @file Security_Manager.c
 * @brief Código de erro agregado e parada de emergência da SIBA.
 */
#include "Security_Manager.h"
#include "CMD_Core.h"        /* CMD_Core_Get_Sensor_Cfg — faixa central (CMD_Core_prm.h) */
#include "Battery_Manager.h" /* Battery_Manager_Set_Relay — pré-carga + potência */
#include "HAL.h"
#include "Log_Manager.h"
#include "Observer_Manager.h"   /* saúde do SMO/PLL */
#include "Security_prm.h"       /* SM_* — gerado do SIBA.yaml (security_limits) */
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(security_mgr, LOG_LEVEL_WRN);

/* LED vermelho (PA15): MCU ready — apagado em emergência ou boot sem relay. */
static const struct gpio_dt_spec led_red =
    GPIO_DT_SPEC_GET(DT_NODELABEL(led_red), gpios);

/* Os limites operacionais NÃO vivem mais aqui — vêm da tabela central
 * SIBA_SENSOR_TABLE em CMD_Core_prm.h (via CMD_Core_Get_Sensor_Cfg).
 * Para mudar um limite, edite a linha do sensor lá. */

static bool emergency_active = false;

/* Quem disparou a emergência em vigor. Só a emergência levantada pelo botão
 * se encerra sozinha quando o botão é solto; as disparadas por limite de
 * sensor continuam valendo enquanto o sensor estiver fora da faixa. */
static bool emergency_by_button = false;

/* Sobrescrita do botão físico, comandada pelo supervisório. Só em RAM. */
static bool emergency_override = false;

/* Código de erro agregado. Atômico: além da siba_wq, a fila do ADC (fases e
 * observador) também seta/limpa bits. Acesso sempre por Error_Detect/Erase/Set. */
static atomic_t error_code = ATOMIC_INIT(0);

/** @brief Zera o estado de emergência; pré-seta ERROR_MCU_POWER_READY (relay aberto ao boot). */
void Security_Manager_Init(void)
{
    emergency_active    = false;
    emergency_by_button = false;
    emergency_override  = false;
    atomic_set(&error_code, 0);
    /* Relay não está energizado ao boot — seta bit para a GUI saber. */
    atomic_or(&error_code, (atomic_val_t)ERROR_MCU_POWER_READY);

    if (gpio_is_ready_dt(&led_red)) {
        gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    }

    LOG_INF("Security_Manager inicializado");
}

/**
 * @brief Seta um bit de erro, se ainda não estiver setado (borda de subida).
 * @param bit bit a setar
 * @return true se mudou o estado (borda), false se já estava setado
 */
bool Security_Manager_Error_Detect(siba_error_bit_t bit)
{
    if ((uint32_t)atomic_or(&error_code, (atomic_val_t)bit) & (uint32_t)bit) {
        return false; /* já estava setado — sem mudança de borda */
    }
    return true;
}

/**
 * @brief Limpa um bit de erro, se estiver setado (borda de descida).
 * @param bit bit a limpar
 * @return true se mudou o estado (borda), false se já estava limpo
 */
bool Security_Manager_Error_Erase(siba_error_bit_t bit)
{
    if (!((uint32_t)atomic_and(&error_code, ~(atomic_val_t)bit) & (uint32_t)bit)) {
        return false; /* já estava limpo — sem mudança de borda */
    }
    return true;
}

/** @brief Seta (cond=true) ou limpa (cond=false) um bit de erro. */
void Security_Manager_Error_Set(siba_error_bit_t bit, bool cond)
{
    if (cond) {
        Security_Manager_Error_Detect(bit);
    } else {
        Security_Manager_Error_Erase(bit);
    }
}

/**
 * @brief Checa as fases do motor (portado da SAME). Só diagnóstico: não
 * dispara emergência. Chamada pela fila do ADC a cada publicação (10 Hz).
 * Desequilíbrio (soma ≈ 0) fica de fora, como na SAME: com i_c derivada a
 * soma das correntes é zero por construção e as tensões são contra o terra.
 * @param Va,Vb,Vc tensões de fase [V]; ia,ib,ic correntes de fase [A]
 */
void Security_Manager_Check_Electrical(float Va, float Vb, float Vc,
                                       float ia, float ib, float ic)
{
    Security_Manager_Error_Set(ERROR_MEAS_OVERCURRENT,
        fabsf(ia) > SM_OVERCURRENT_A || fabsf(ib) > SM_OVERCURRENT_A
                                     || fabsf(ic) > SM_OVERCURRENT_A);

    bool range = !isfinite(Va) || !isfinite(Vb) || !isfinite(Vc)
              || !isfinite(ia) || !isfinite(ib) || !isfinite(ic)
              || fabsf(Va) > SM_SENSOR_ABS_MAX || fabsf(Vb) > SM_SENSOR_ABS_MAX
              || fabsf(Vc) > SM_SENSOR_ABS_MAX || fabsf(ia) > SM_SENSOR_ABS_MAX
              || fabsf(ib) > SM_SENSOR_ABS_MAX || fabsf(ic) > SM_SENSOR_ABS_MAX;
    Security_Manager_Error_Set(ERROR_MEAS_SENSOR_RANGE, range);
}

/**
 * @brief Saúde do observador e validade da configuração (portado da SAME).
 * BEMF baixa / PLL sem lock não viram bit na SIBA (estado normal com o motor
 * parado; apagariam o LED de pronto). Os getters seguem disponíveis.
 */
void Security_Manager_Check_Observer(void)
{
    Security_Manager_Error_Set(ERROR_OBS_DIVERGED, !Observer_State_Is_Finite());
    Security_Manager_Error_Set(ERROR_OBS_RPM_IMPLAUSIBLE,
        fabsf(Observer_Get_Rpm()) > SM_RPM_MAX);
    Security_Manager_Error_Set(ERROR_CFG_MOTOR_PARAM_INVALID,
        !Observer_Params_Physical_Valid());
    Security_Manager_Error_Set(ERROR_CFG_GAMMA_SIGN,
        !Observer_Gamma_Signs_Valid());
}

/** @brief Código de erro agregado atual. @return bitmask (siba_error_bit_t). */
uint32_t Security_Manager_Get_Error_Code(void)
{
    return (uint32_t)atomic_get(&error_code);
}

/** @brief Há parada de emergência em vigor? @return true se sim. */
bool Security_Manager_Is_Emergency_Active(void)
{
    return emergency_active;
}

/**
 * @brief Dispara a parada de emergência: desarma o ESC e abre os dois relés
 * (potência primeiro, depois pré-carga — ordem imposta por HAL_Relay_Deenergize).
 * O comando passa por Battery_Manager_Set_Relay para que o estado publicado na
 * telemetria acompanhe o que o hardware realmente fez.
 * Idempotente — chamadas subsequentes são no-op enquanto ativa.
 */
void Security_Manager_Trigger_Emergency(void)
{
    if (emergency_active) return;
    emergency_active = true;

    Security_Manager_Error_Detect(ERROR_SEC_EMERGENCY_ACTIVE);

    LOG_ERR("SECURITY: EMERGENCIA — ESC desarmado e reles abertos!");
    HAL_ESC_Disarm();
    Battery_Manager_Set_Relay(false);
}

/**
 * @brief Encerra a emergência em vigor. NÃO reenergiza: o ESC continua
 * desarmado e os relés abertos; reenergizar e rearmar seguem sendo ato
 * explícito do operador pela aplicação de supervisão. Serve para que soltar o
 * botão de emergência devolva a placa ao estado operacional sem reiniciá-la.
 */
void Security_Manager_Clear_Emergency(void)
{
    if (!emergency_active) return;
    emergency_active    = false;
    emergency_by_button = false;

    Security_Manager_Error_Erase(ERROR_SEC_EMERGENCY_ACTIVE);
    LOG_WRN("SECURITY: emergencia encerrada — reenergizar e rearmar pela GUI");
}

/* ── Sobrescrita do botão de emergência ──────────────────────────────── */

void Security_Manager_Set_Emergency_Override(bool on)
{
    if (on == emergency_override) return;
    emergency_override = on;

    if (on) {
        LOG_WRN("SECURITY: botao de emergencia SOBRESCRITO por comando");
    } else {
        LOG_WRN("SECURITY: botao de emergencia restaurado ao comando");
    }
}

bool Security_Manager_Get_Emergency_Override(void)
{
    return emergency_override;
}

bool Security_Manager_Button_Pressed(void)
{
    return !emergency_override && HAL_Emergency_Is_Pressed();
}

/* ── Cadeia de potência da bancada ───────────────────────────────────── */

/**
 * @brief Arma a bancada inteira: fecha a pré-carga, espera os capacitores do
 * ESC carregarem, fecha a potência e arma o ESC. Bloqueia a thread chamadora
 * durante a espera de pré-carga.
 *
 * Recusa enquanto o botão de emergência estiver apertado (e não sobrescrito).
 * Se houver parada em vigor mas o botão já estiver liberado, armar é o ato
 * deliberado que a encerra — sem isso, sair de uma parada disparada por
 * comando exigiria reiniciar a placa.
 *
 * @return 0 se armou; -EPERM se o botão barrou.
 */
int Security_Manager_Arm_Bench(void)
{
    if (Security_Manager_Button_Pressed()) {
        LOG_ERR("SECURITY: armar recusado — botao de emergencia apertado");
        return -EPERM;
    }

    if (emergency_active) {
        Security_Manager_Clear_Emergency();
    }

    if (!HAL_Relay_Power_Closed()) {
        Battery_Manager_Set_Relay(true);
    }
    /* O supervisório repete o "pronto": armar de novo zeraria o throttle. */
    if (!HAL_ESC_Is_Armed()) {
        HAL_ESC_Arm();
    }

    LOG_INF("SECURITY: bancada armada (pre-carga=%d potencia=%d ESC=%d)",
            (int)HAL_Relay_Precharge_Closed(), (int)HAL_Relay_Power_Closed(),
            (int)HAL_ESC_Is_Armed());
    return 0;
}

/**
 * @brief Desliga a bancada: desarma o ESC e abre os dois relés (potência
 * depois pré-carga). Não é emergência — não seta bit de erro nem retém nada.
 */
void Security_Manager_Shutdown_Bench(void)
{
    LOG_INF("SECURITY: desligando bancada — ESC e reles");
    HAL_ESC_Disarm();
    Battery_Manager_Set_Relay(false);
}

/**
 * @brief Verifica se o valor está dentro dos limites operacionais (tabela
 * central); seta/limpa o bit de erro correspondente e dispara emergência
 * se estiver fora da faixa.
 * @param sensor_id SENSOR_ID_V_BATT/I_BATT/RPM (outros são ignorados)
 * @param value valor do sensor
 */
void Security_Manager_Check(uint32_t sensor_id, float value)
{
    /* Sinal de parada de emergência (PD1, 0 V = emergência), lido a cada despacho de sensor —
     * antes de qualquer filtro por sensor_id, para que a varredura aconteça
     * em todo item da fila e não só nos três sensores críticos.
     * Soltar o botão encerra a emergência (ver Clear_Emergency) — sem
     * reenergizar nada, e sem exigir que a placa seja reiniciada. */
    if (Security_Manager_Button_Pressed()) {
        if (!emergency_active) {
            LOG_ERR("SECURITY: sinal de emergencia acionado (PD1)");
            emergency_by_button = true;
            Security_Manager_Trigger_Emergency();
        }
    } else if (emergency_by_button) {
        /* Botão solto e foi ele quem disparou — libera sem reiniciar a placa.
         * Se houver outra condição de risco ainda ativa (temperatura, faixa de
         * sensor), o próximo despacho a redispara em milissegundos. */
        Security_Manager_Clear_Emergency();
    }

    /* LED externo de pronto (PE15): mesma condição do LED vermelho. */
    HAL_Ready_Led_Set(!emergency_active && atomic_get(&error_code) == 0);

    /* Sensores de SEGURANÇA: sair da faixa dispara parada de emergência.
     * A faixa [min,max] e o bit de erro vêm da tabela central (CMD_Core_prm.h),
     * não mais de #defines locais. Assim mudar um limite é editar UMA linha. */
    siba_error_bit_t bit;
    switch (sensor_id) {
#if HAL_ENABLE_ADC
        case SENSOR_ID_V_BATT: bit = ERROR_SEC_VOLTAGE_OUT_OF_RANGE; break;
        case SENSOR_ID_I_BATT: bit = ERROR_SEC_CURRENT_EXCESSIVE;    break;
#endif
        /* Com o ADC desligado (HAL_DISABLE_ADC em HAL_prm.h) v_batt/i_batt
         * não são medidos: a checagem de faixa da bateria fica desabilitada
         * para não disparar emergência sobre um valor que não existe. */
        case SENSOR_ID_RPM:    bit = ERROR_SEC_RPM_EXCESSIVE;        break;
        default: return;   /* sensor não é crítico de segurança */
    }

    const SENSORS *cfg = CMD_Core_Get_Sensor_Cfg(sensor_id);
    if (!cfg) return;

    if (value < cfg->Minimum_Valid_Value || value > cfg->Max_Valid_Value) {
        if (Security_Manager_Error_Detect(bit)) {
            LOG_ERR("SECURITY: sensor_id %u fora da faixa [%.2f, %.2f]: %.2f",
                    sensor_id, (double)cfg->Minimum_Valid_Value,
                    (double)cfg->Max_Valid_Value, (double)value);
        }
        if (!emergency_active) {
            Security_Manager_Trigger_Emergency();
        }
    } else {
        Security_Manager_Error_Erase(bit);
    }

    /* LED vermelho: MCU ready = sem emergência e sem erros críticos. */
    gpio_pin_set_dt(&led_red, (!emergency_active && atomic_get(&error_code) == 0) ? 1 : 0);
}
