#include "CMD_Core.h"
#include "CMD.h"
#include "HAL.h"
#include "Battery_Manager.h"
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
        case TARGET_LOAD_CELL_0_enum ... TARGET_LOAD_CELL_6_enum:
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
    /* value = throttle 0.0-100.0% */
    if (cmd->instruction == INSTR_WRITE) {
        int r = HAL_ESC_Set_Throttle(cmd->value);
        if (r == -EPERM) {
            LOG_WRN("CMD ESC: ESC não armado, armando agora...");
            HAL_ESC_Arm();
            HAL_ESC_Set_Throttle(cmd->value);
        }
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
        case DEFAULT_ACTION_SENSE_LINEAR_COEF:
            s->Linear_Coeff = cmd->value;
            /* Aplica no HX711 se for célula de carga */
            if (tid <= 5) {
                HAL_HX711_Set_Calib((uint8_t)tid, s->Linear_Coeff, s->Offset_Coeff);
            }
            LOG_INF("CMD SENSOR[%d]: linear=%.4f", tid, (double)cmd->value);
            break;

        case DEFAULT_ACTION_SENSE_OFFSET_COEF:
            s->Offset_Coeff = cmd->value;
            if (tid <= 5) {
                HAL_HX711_Set_Calib((uint8_t)tid, s->Linear_Coeff, s->Offset_Coeff);
            }
            LOG_INF("CMD SENSOR[%d]: offset=%.4f", tid, (double)cmd->value);
            break;

        case DEFAULT_ACTION_SENSE_RESET_DEFAULT:
            s->Linear_Coeff = s->Linear_Coeff_Reset_Default;
            s->Offset_Coeff = s->Offset_Coeff_Reset_Default;
            if (tid <= 5) {
                HAL_HX711_Set_Calib((uint8_t)tid, s->Linear_Coeff, s->Offset_Coeff);
                HAL_HX711_Tare((uint8_t)tid);
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
