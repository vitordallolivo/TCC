#include "CMD_Core.h"
#include "CMD_Core_prm.h"
#include "CMD.h"
#include "HAL.h"
#include "Battery_Manager.h"
#include "Propeller_Manager.h"
#include "Security_Manager.h"
#include "Log_Manager.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(cmd_core, LOG_LEVEL_DBG);

/* Config/calibração/faixa de cada sensor — índice = sensor_id. Preenchida por
 * CMD_Core_Init() a partir da tabela SIBA_SENSOR_TABLE (CMD_Core_prm.h). */
static SENSORS sensor_cfg[CMD_CORE_SENSOR_COUNT];

static void handle_func_start(const CMD *cmd)
{
    switch (cmd->target_id) {
        case TARGET_LOAD_CELL_0_enum ... TARGET_LOAD_CELL_5_enum:
            /* Células de carga iniciam automaticamente via HX711 thread */
            LOG_INF("CMD: START load_cell[%d] — já ativo", cmd->target_id);
            break;
        case TARGET_DEFAULT:
            /* START genérico/telemetria (ex.: `siba start telemetry` do app e o
             * CMD neutro do test_io) — a telemetria já está sempre ativa. */
            LOG_INF("CMD: START (telemetria/geral) — já ativo");
            break;
        default:
            LOG_WRN("CMD START: target %d não reconhecido", cmd->target_id);
    }
}

static void handle_func_stop(const CMD *cmd)
{
    if (cmd->target_id == TARGET_DEFAULT) {
        LOG_WRN("CMD STOP ESC: corte de emergência");
        Security_Manager_Trigger_Emergency();
    }
}

static void handle_func_esc(const CMD *cmd)
{
    /* value = throttle 0.0-100.0%
     * O pedido passa pelo Propeller_Manager (em vez de chamar o HAL
     * diretamente): ele arma o ESC se preciso e sobrescreve/reduz o PWM
     * sempre que a corrente do motor ultrapassar o limite configurado. */
    if (cmd->instruction == INSTR_WRITE) {
        Propeller_Manager_Request_Throttle(cmd->value);
    }
}

static void handle_func_battery(const CMD *cmd)
{
    battery_action act = (battery_action)cmd->action_or_status;
    switch (act) {
        case BAT_ACTION_P0_OVERWRITE ... BAT_ACTION_P4_OVERWRITE:
            Battery_Manager_Set_Poly(act - BAT_ACTION_P0_OVERWRITE, cmd->value);
            break;
        case BAT_ACTION_TEMP_LIMIT_OVERWRITE:
            Battery_Manager_Set_Temp_Limit(cmd->value);
            break;
        case BAT_ACTION_RELAY:
            Battery_Manager_Set_Relay(cmd->value > 0.5f);
            break;
        default:
            LOG_WRN("CMD BATTERY: ação %d desconhecida", act);
    }
}

/* Aplica Linear_Coeff/Offset_Coeff ao hardware do sensor `tid`, de forma
 * genérica — célula de carga (HX711) ou sensor analógico (ADC). `sensor_cfg[]`
 * (struct SENSORS, Common_Types.h/Log_Manager.h) é só um cache local; quem
 * guarda os valores de verdade é o HAL/Propeller_Manager (ver
 * read_sensor_calib abaixo). SENSOR_ID_SOC não tem hardware próprio
 * (calculado via polinômio pelo Battery_Manager) — não recebe calibração
 * linear/offset. */
static void apply_sensor_calib(int8_t tid, float linear, float offset)
{
    switch (tid) {
        case SENSOR_ID_LOAD_CELL_0 ... SENSOR_ID_LOAD_CELL_5:
            /* Taração/calibração de célula é decisão do Sys, não do HAL —
             * Propeller_Manager decide e só então aciona o hardware
             * (ver CLAUDE.md "o HAL é apenas questões de configuração"). */
            Propeller_Manager_Set_Cell_Calib((uint8_t)tid, linear, offset);
            break;

        case SENSOR_ID_V_BATT:
        case SENSOR_ID_I_BATT:
        case SENSOR_ID_TEMPERATURE_BATTERY:
        case SENSOR_ID_AIR_SPEED:
            HAL_ADC_Set_Calib((uint8_t)tid, linear, offset);
            break;

        default:
            break;
    }
}

/* Lê o par (linear, offset) hoje aplicado no HAL/Propeller_Manager para o
 * sensor `tid` — HAL é a fonte única de verdade da calibração (pode ter
 * sido alterada pelo shell `siba sensor`, fora do cache sensor_cfg[]).
 * Usado para alterações PARCIAIS (só linear ou só offset): lê o par atual,
 * troca apenas o campo pedido e regrava os dois — sem isso, alterar um
 * coeficiente sobrescreveria o outro com o default (1.0/0.0). */
static void read_sensor_calib(int8_t tid, float *linear, float *offset)
{
    switch (tid) {
        case SENSOR_ID_LOAD_CELL_0 ... SENSOR_ID_LOAD_CELL_5:
            HAL_HX711_Get_Calib((uint8_t)tid, linear, offset);
            break;

        case SENSOR_ID_V_BATT:
        case SENSOR_ID_I_BATT:
        case SENSOR_ID_TEMPERATURE_BATTERY:
        case SENSOR_ID_AIR_SPEED:
            HAL_ADC_Get_Calib((uint8_t)tid, linear, offset);
            break;

        default:
            *linear = 1.0f;
            *offset = 0.0f;
            break;
    }
}

static void handle_func_sensor(const CMD *cmd)
{
    int8_t tid = cmd->target_id;
    if (tid < 0 || tid >= 11) {
        LOG_WRN("CMD SENSOR: target_id %d inválido", tid);
        return;
    }

    default_action act = (default_action)cmd->action_or_status;

    /* ---- Célula de carga (HX711): modelo CANÔNICO grams=(raw-tare)/scale ----
     * Diferente do ADC (afim y=linear·x+offset): a célula tem UM só offset (o
     * tare, em counts) e a "escala" é derivada de um PESO conhecido — não é um
     * ganho digitado à mão. Por isso:
     *   OFFSET  -> TARA (tare = raw atual; zera; `value` ignorado)
     *   LINEAR  -> calibra a escala pelo PESO conhecido em `value`
     *              (scale = (raw-tare)/value  =>  leitura passa a valer o peso)
     *   RESET   -> restaura defaults e re-tara (tratado no bloco genérico) */
    if (tid >= SENSOR_ID_LOAD_CELL_0 && tid <= SENSOR_ID_LOAD_CELL_5 &&
        (act == DEFAULT_ACTION_SENSE_OFFSET_COEF ||
         act == DEFAULT_ACTION_SENSE_LINEAR_COEF)) {
        switch (act) {
            case DEFAULT_ACTION_SENSE_OFFSET_COEF:
                Propeller_Manager_Tare_Cell((uint8_t)tid);
                LOG_INF("CMD SENSOR[%d]: TARA (offset = raw atual)", tid);
                break;
            case DEFAULT_ACTION_SENSE_LINEAR_COEF:
                Propeller_Manager_Calibrate_Cell_Weight((uint8_t)tid, cmd->value);
                LOG_INF("CMD SENSOR[%d]: calib por peso=%.3f", tid, (double)cmd->value);
                break;
            default:
                break;  /* MAX/MIN/RESET tratados no bloco genérico abaixo */
        }
        return;
    }

    SENSORS *s = &sensor_cfg[tid];

    switch (act) {
        case DEFAULT_ACTION_SENSE_MAX_VALID:
            s->Max_Valid_Value = cmd->value;
            LOG_INF("CMD SENSOR[%d]: limite MÁX = %.4f", tid, (double)cmd->value);
            break;

        case DEFAULT_ACTION_SENSE_MIN_VALID:
            s->Minimum_Valid_Value = cmd->value;
            LOG_INF("CMD SENSOR[%d]: limite MÍN = %.4f", tid, (double)cmd->value);
            break;

        case DEFAULT_ACTION_SENSE_LINEAR_COEF: {
            float linear, offset;
            read_sensor_calib(tid, &linear, &offset);
            linear = cmd->value;
            apply_sensor_calib(tid, linear, offset);
            s->Linear_Coeff = linear;
            s->Offset_Coeff = offset;
            LOG_INF("CMD SENSOR[%d]: linear=%.4f", tid, (double)cmd->value);
            break;
        }

        case DEFAULT_ACTION_SENSE_OFFSET_COEF: {
            float linear, offset;
            read_sensor_calib(tid, &linear, &offset);
            offset = cmd->value;
            apply_sensor_calib(tid, linear, offset);
            s->Linear_Coeff = linear;
            s->Offset_Coeff = offset;
            LOG_INF("CMD SENSOR[%d]: offset=%.4f", tid, (double)cmd->value);
            break;
        }

        case DEFAULT_ACTION_SENSE_RESET_DEFAULT:
            s->Linear_Coeff = s->Linear_Coeff_Reset_Default;
            s->Offset_Coeff = s->Offset_Coeff_Reset_Default;
            apply_sensor_calib(tid, s->Linear_Coeff, s->Offset_Coeff);
            if (tid <= 5) {
                Propeller_Manager_Tare_Cell((uint8_t)tid);
            }
            LOG_INF("CMD SENSOR[%d]: reset default", tid);
            break;

        default:
            LOG_WRN("CMD SENSOR: ação %d desconhecida", act);
    }
}

static void handle_func_reset(const CMD *cmd)
{
    ARG_UNUSED(cmd);
    LOG_ERR("CMD RESET: reiniciando sistema...");
    /* Em Zephyr, sys_reboot(SYS_REBOOT_COLD) requer <zephyr/sys/reboot.h> */
    extern void sys_reboot(int type);
    sys_reboot(0);
}

/*
 * CMD_Core_Dispatch
 * Desserializa struct CMD do buffer binário e executa o handler.
 * Chamado pelo cmd_dispatch_fn do OSSI (siba_wq).
 */
void CMD_Core_Dispatch(const uint8_t *buf, size_t len)
{
    if (len < sizeof(CMD)) {
        LOG_WRN("CMD_Core: pacote curto (%d bytes)", (int)len);
        return;
    }

    CMD cmd;
    memcpy(&cmd, buf, sizeof(CMD));

    LOG_DBG("CMD: func=%d instr=%d target=%d val=%.3f act=%d",
            cmd.function, cmd.instruction, cmd.target_id,
            (double)cmd.value, cmd.action_or_status);

    switch (cmd.function) {
        case FUNC_START:   handle_func_start(&cmd);   break;
        case FUNC_STOP:    handle_func_stop(&cmd);    break;
        case FUNC_RESET:   handle_func_reset(&cmd);   break;
        case FUNC_SENSOR:  handle_func_sensor(&cmd);  break;
        case FUNC_ESC:     handle_func_esc(&cmd);     break;
        case FUNC_BATTERY: handle_func_battery(&cmd); break;
        default:
            LOG_WRN("CMD_Core: função %d desconhecida", cmd.function);
    }
}

/* Bit de erro por sensor, na MESMA ordem da tabela — usado por
 * CMD_Core_Validate_Sensor. Índice = sensor_id. */
static const uint32_t sensor_err_bit[CMD_CORE_SENSOR_COUNT] = {
#define X(ID, DEF_LIN, DEF_OFF, MINV, MAXV, ERRBIT)   [ID] = (uint32_t)(ERRBIT),
    SIBA_SENSOR_TABLE(X)
#undef X
};

const SENSORS *CMD_Core_Get_Sensor_Cfg(uint32_t id)
{
    if (id >= CMD_CORE_SENSOR_COUNT) return NULL;
    return &sensor_cfg[id];
}

uint32_t CMD_Core_Validate_Sensor(uint32_t id, float value)
{
    if (id >= CMD_CORE_SENSOR_COUNT) return 0U;
    uint32_t bit = sensor_err_bit[id];
    if (bit == 0U) return 0U;   /* sensor sem verificação de faixa */
    if (value < sensor_cfg[id].Minimum_Valid_Value ||
        value > sensor_cfg[id].Max_Valid_Value) {
        return bit;             /* fora da faixa -> reporta o bit configurado */
    }
    return 0U;                  /* dentro da faixa */
}

void CMD_Core_Init(void)
{
    /* Inicializa a tabela de sensores a partir de SIBA_SENSOR_TABLE
     * (CMD_Core_prm.h) — defaults de calibração + faixa válida.
     * Sem thread — despacho via OSSI_Submit_CMD → siba_wq (ver OSSI.c). */
#define X(ID, DEF_LIN, DEF_OFF, MINV, MAXV, ERRBIT)                 \
    sensor_cfg[ID].Linear_Coeff               = (DEF_LIN);          \
    sensor_cfg[ID].Offset_Coeff               = (DEF_OFF);          \
    sensor_cfg[ID].Linear_Coeff_Reset_Default = (DEF_LIN);          \
    sensor_cfg[ID].Offset_Coeff_Reset_Default = (DEF_OFF);          \
    sensor_cfg[ID].Minimum_Valid_Value        = (MINV);             \
    sensor_cfg[ID].Max_Valid_Value            = (MAXV);
    SIBA_SENSOR_TABLE(X)
#undef X

    LOG_INF("CMD_Core inicializado");
}
