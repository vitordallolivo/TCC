#include "CMD_Core.h"
#include "CMD_Core_prm.h"
#include "CMD.h"
#include "HAL.h"
#include "Observer_Manager.h"
#include "Log_Manager.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(cmd_core, LOG_LEVEL_DBG);

/* Cache de config/validade por sensor — índice = target_type (0..4).
 * A fonte de verdade da CALIBRAÇÃO é o HAL (pode ter mudado por outra via);
 * aqui guarda-se defaults + faixa de validade. */
static SENSORS sensor_cfg[CMD_CORE_SENSOR_COUNT];

/* ---- Aplica/lê calibração no HAL (fonte única de verdade) --------------- */
static void apply_sensor_calib(int8_t tid, float linear, float offset)
{
    if (tid >= 0 && tid < CMD_CORE_SENSOR_COUNT) {
        HAL_ADC_Set_Calib((uint8_t)tid, linear, offset);
    }
}

static void read_sensor_calib(int8_t tid, float *linear, float *offset)
{
    if (tid >= 0 && tid < CMD_CORE_SENSOR_COUNT) {
        HAL_ADC_Get_Calib((uint8_t)tid, linear, offset);
    } else {
        *linear = 1.0f; *offset = 0.0f;
    }
}

/* ---- FUNC_START / STOP / RESET ----------------------------------------- */
static void handle_func_start(const CMD *cmd)
{
    /* A telemetria/observador já estão sempre ativos na SAME (é só leitura).
     * START (TARGET_DEFAULT) reinicia o estado do observador para começar
     * limpo uma nova aquisição. */
    if (cmd->target_id == TARGET_DEFAULT) {
        Observer_Reset();
        LOG_INF("CMD START: observador reiniciado");
    }
}

static void handle_func_stop(const CMD *cmd)
{
    ARG_UNUSED(cmd);
    /* SAME não aciona nada; STOP apenas zera o observador. */
    Observer_Reset();
    LOG_INF("CMD STOP: observador zerado");
}

static void handle_func_reset(const CMD *cmd)
{
    ARG_UNUSED(cmd);
    LOG_ERR("CMD RESET: reiniciando sistema...");
    extern void sys_reboot(int type);
    sys_reboot(0);
}

/* ---- Calibração de um sensor (currents/voltages) ----------------------- */
static void handle_sensor_calib(int8_t tid, const CMD *cmd)
{
    default_action act = (default_action)cmd->action_or_status;
    SENSORS *s = &sensor_cfg[tid];

    switch (act) {
        case DEFAULT_ACTION_SENSE_MAX_VALID:
            s->Max_Valid_Value = cmd->value;
            LOG_INF("SENSOR[%d]: MAX=%.4f", tid, (double)cmd->value);
            break;
        case DEFAULT_ACTION_SENSE_MIN_VALID:
            s->Minimum_Valid_Value = cmd->value;
            LOG_INF("SENSOR[%d]: MIN=%.4f", tid, (double)cmd->value);
            break;
        case DEFAULT_ACTION_SENSE_LINEAR_COEF: {
            float linear, offset;
            read_sensor_calib(tid, &linear, &offset);
            linear = cmd->value;
            apply_sensor_calib(tid, linear, offset);
            s->Linear_Coeff = linear; s->Offset_Coeff = offset;
            LOG_INF("SENSOR[%d]: linear=%.5f", tid, (double)cmd->value);
            break;
        }
        case DEFAULT_ACTION_SENSE_OFFSET_COEF: {
            float linear, offset;
            read_sensor_calib(tid, &linear, &offset);
            offset = cmd->value;
            apply_sensor_calib(tid, linear, offset);
            s->Linear_Coeff = linear; s->Offset_Coeff = offset;
            LOG_INF("SENSOR[%d]: offset=%.5f", tid, (double)cmd->value);
            break;
        }
        case DEFAULT_ACTION_SENSE_RESET_DEFAULT:
            s->Linear_Coeff = s->Linear_Coeff_Reset_Default;
            s->Offset_Coeff = s->Offset_Coeff_Reset_Default;
            apply_sensor_calib(tid, s->Linear_Coeff, s->Offset_Coeff);
            LOG_INF("SENSOR[%d]: reset default", tid);
            break;
        default:
            LOG_WRN("SENSOR[%d]: ação %d desconhecida", tid, act);
    }
}

/* ---- FUNC_SENSOR: calibração OU parâmetro/ganho do observador ---------- */
static void handle_func_sensor(const CMD *cmd)
{
    int8_t tid = cmd->target_id;
    default_action act = (default_action)cmd->action_or_status;

    /* Parâmetros do motor + ganhos Γ + LPFs + Ts_adc → observador.
     * (POLES_PAIRS..TS_ADC são contíguos no enum, ver Common_Types.h.) */
    if (tid >= TARGET_POLES_PAIRS && tid <= TARGET_TS_ADC) {
        bool ok;
        if (act == DEFAULT_ACTION_VALUE_RESET_DEFAULT) {
            ok = Observer_Reset_Param_Default((target_type)tid);
        } else { /* DEFAULT_ACTION_SET_VALUE (padrão da GUI) */
            ok = Observer_Set_Param((target_type)tid, cmd->value);
        }
        if (!ok) LOG_WRN("Observer: target %d não tratado", tid);
        return;
    }

    /* Período da telemetria UDP (ms). */
    if (tid == TARGET_TS_UDP) {
        if (act == DEFAULT_ACTION_VALUE_RESET_DEFAULT) {
            Log_Manager_Set_Period_Ms(LOG_MANAGER_PERIOD_MS);
        } else {
            Log_Manager_Set_Period_Ms((uint32_t)cmd->value);
        }
        return;
    }

    /* Sensores calibráveis (currents/voltages), índice = target 0..4. */
    if (tid >= 0 && tid < CMD_CORE_SENSOR_COUNT) {
        handle_sensor_calib(tid, cmd);
        return;
    }

    LOG_WRN("CMD SENSOR: target_id %d inválido", tid);
}

void CMD_Core_Dispatch(const uint8_t *buf, size_t len)
{
    if (len < sizeof(CMD)) {
        LOG_WRN("CMD_Core: pacote curto (%d bytes)", (int)len);
        return;
    }

    CMD cmd;
    memcpy(&cmd, buf, sizeof(CMD));

    LOG_DBG("CMD: func=%d instr=%d target=%d val=%.4f act=%d",
            cmd.function, cmd.instruction, cmd.target_id,
            (double)cmd.value, cmd.action_or_status);

    switch (cmd.function) {
        case FUNC_START:  handle_func_start(&cmd);  break;
        case FUNC_STOP:   handle_func_stop(&cmd);   break;
        case FUNC_RESET:  handle_func_reset(&cmd);  break;
        case FUNC_SENSOR: handle_func_sensor(&cmd); break;
        default:
            LOG_WRN("CMD_Core: função %d desconhecida", cmd.function);
    }
}

const SENSORS *CMD_Core_Get_Sensor_Cfg(uint32_t id)
{
    if (id >= CMD_CORE_SENSOR_COUNT) return NULL;
    return &sensor_cfg[id];
}

bool CMD_Core_Validate_Sensor(uint32_t id, float value)
{
    if (id >= CMD_CORE_SENSOR_COUNT) return true;
    return (value >= sensor_cfg[id].Minimum_Valid_Value &&
            value <= sensor_cfg[id].Max_Valid_Value);
}

void CMD_Core_Init(void)
{
    /* Preenche sensor_cfg[] a partir de SAME_SENSOR_TABLE (CMD_Core_prm.h):
     * defaults de calibração afim + faixa de validade. */
#define X(TID, DEF_LIN, DEF_OFF, MINV, MAXV)                       \
    sensor_cfg[TID].SensorTarget               = (TID);            \
    sensor_cfg[TID].Linear_Coeff               = (DEF_LIN);        \
    sensor_cfg[TID].Offset_Coeff               = (DEF_OFF);        \
    sensor_cfg[TID].Linear_Coeff_Reset_Default = (DEF_LIN);        \
    sensor_cfg[TID].Offset_Coeff_Reset_Default = (DEF_OFF);        \
    sensor_cfg[TID].Minimum_Valid_Value        = (MINV);           \
    sensor_cfg[TID].Max_Valid_Value            = (MAXV);
    SAME_SENSOR_TABLE(X)
#undef X

    LOG_INF("CMD_Core inicializado (%d sensores)", CMD_CORE_SENSOR_COUNT);
}
