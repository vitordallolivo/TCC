#include "CMD_Core.h"
#include "CMD.h"
#include "HAL.h"
#include "Battery_Manager.h"
#include "Propeller_Manager.h"
#include "Security_Manager.h"
#include "Log_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(cmd_core, LOG_LEVEL_DBG);

/* Tabela de calibração dos sensores (6 células + pitot + v_batt + i_batt) */
static SENSORS sensor_cfg[11];  /* índice = target_id */

static void handle_func_start(const CMD *cmd)
{
    switch (cmd->target_id) {
        case TARGET_LOAD_CELL_0_enum ... TARGET_LOAD_CELL_5_enum:
            /* Células de carga iniciam automaticamente via HX711 thread */
            LOG_INF("CMD: START load_cell[%d] — já ativo", cmd->target_id);
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
    SENSORS *s = &sensor_cfg[tid];

    switch (act) {
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
 * CMD_Core_thread_fn
 * Consome fila_cmd, desserializa struct CMD do buffer binário e executa.
 */
void CMD_Core_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    ossi_msg_t msg;
    CMD cmd;

    LOG_INF("CMD_Core thread iniciada");

    while (1) {
        if (k_msgq_get(&fila_cmd, &msg, K_FOREVER) != 0) continue;

        if (msg.payload.cmd_req.len < sizeof(CMD)) {
            LOG_WRN("CMD_Core: pacote curto (%d bytes)", msg.payload.cmd_req.len);
            continue;
        }

        memcpy(&cmd, msg.payload.cmd_req.buffer, sizeof(CMD));

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
}

void CMD_Core_Init(void)
{
    /* Inicializa tabela de sensores com defaults */
    for (int i = 0; i < 11; i++) {
        sensor_cfg[i].Linear_Coeff              = 1.0f;
        sensor_cfg[i].Offset_Coeff              = 0.0f;
        sensor_cfg[i].Linear_Coeff_Reset_Default= 1.0f;
        sensor_cfg[i].Offset_Coeff_Reset_Default= 0.0f;
        sensor_cfg[i].Max_Valid_Value            = 1e6f;
        sensor_cfg[i].Minimum_Valid_Value        = -1e6f;
    }

    ossi_msg_t req = {
        .target   = TARGET_TCKEC,
        .req_type = REQ_TYPE_TCKEC_CREATE_THREAD,
    };
    req.payload.tckec_req.thread_id   = CMD_CORE_THREAD_ID;
    req.payload.tckec_req.entry_point = CMD_Core_thread_fn;
    req.payload.tckec_req.priority    = CMD_CORE_PRIO;
    req.payload.tckec_req.p1 = NULL;
    req.payload.tckec_req.p2 = NULL;
    req.payload.tckec_req.p3 = NULL;
    OSSI_Send(&req);

    LOG_INF("CMD_Core inicializado");
}
