/*
 * Test_Shell.c — SIBA Board
 *
 * Comandos de teste registrados no Zephyr Shell (CONFIG_SHELL), usando o
 * mesmo UART do console (zephyr,console / zephyr,shell-uart, 115200 8N1).
 * O subsistema de shell já cuida de UART, prompt, histórico, autocompletar
 * e tokenização — não há necessidade de thread, polling ou parser próprios.
 *
 * Permite exercitar manualmente as mesmas ações que hoje só chegam via CMD
 * pela rede UDP — útil para validar HX711/ESC/RPM/Battery/Propeller sem
 * depender do link com o PC.
 *
 * A maioria dos comandos monta uma struct CMD e injeta em fila_cmd via OSSI
 * (mesmo caminho que NCI_RX usa ao receber um pacote — ver NCI.c), reusando
 * o CMD_Core como dispatcher único. Ações sem equivalente em CMD.h (tara
 * avulsa de célula, contagem de células, limite de corrente do motor) chamam
 * o HAL/managers diretamente.
 *
 * Digite "siba" + TAB no terminal serial para ver a lista de subcomandos.
 */

#include "Test_Shell.h"
#include "OSSI.h"
#include "CMD.h"
#include "HAL.h"
#include "Battery_Manager.h"
#include "Propeller_Manager.h"
#include "Log_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* ===========================================================
 * Injeta um CMD em fila_cmd via OSSI — mesmo caminho do NCI_RX
 * (ver lib/OS/NCI.c), reaproveitando o dispatcher do CMD_Core.
 * =========================================================== */
static void shell_send_cmd(func_type func, instruct_type instr, int8_t target_id,
                           float value, int8_t action)
{
    CMD cmd = {
        .function         = func,
        .instruction      = instr,
        .target_id        = target_id,
        .value            = value,
        .action_or_status = action,
    };

    ossi_msg_t msg = { .target = TARGET_CMD, .req_type = REQ_TYPE_CMD_PARSE };

    memcpy(msg.payload.cmd_req.buffer, &cmd, sizeof(cmd));
    msg.payload.cmd_req.len = sizeof(cmd);

    OSSI_Send(&msg);
}

static int cmd_read(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    TELEMETRY_LOG snap = Log_Manager_Get_Snapshot();

    shell_print(sh, "t=%u ms  status=%d", snap.timestamp, (int)snap.status);
    shell_print(sh, "  load_cell[0..5] = %.2f %.2f %.2f %.2f %.2f %.2f",
                (double)snap.load_cell_0, (double)snap.load_cell_1, (double)snap.load_cell_2,
                (double)snap.load_cell_3, (double)snap.load_cell_4, (double)snap.load_cell_5);
    shell_print(sh, "  v_batt=%.2fV  i_batt=%.2fA  soc=%.1f%%  temp_batt=%.1fC",
                (double)snap.v_batt, (double)snap.i_batt, (double)snap.soc,
                (double)snap.temperature_battery);
    shell_print(sh, "  air_speed=%.2f m/s   rpm=%.1f", (double)snap.air_speed, (double)snap.rpm);

    return 0;
}

static int cmd_rpm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "RPM = %.1f", (double)HAL_RPM_Get());
    return 0;
}

static int cmd_esc(const struct shell *sh, size_t argc, char **argv)
{
    float pct = strtof(argv[1], NULL);

    Propeller_Manager_Request_Throttle(pct);
    shell_print(sh, "ESC: throttle pedido = %.1f%%", (double)pct);
    return 0;
}

static int cmd_arm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    HAL_ESC_Arm();
    shell_print(sh, "ESC armado");
    return 0;
}

static int cmd_disarm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    HAL_ESC_Disarm();
    shell_print(sh, "ESC desarmado");
    return 0;
}

static int cmd_start(const struct shell *sh, size_t argc, char **argv)
{
    int id = atoi(argv[1]);

    if (id < 0 || id > 5) {
        shell_error(sh, "start: id deve ser 0-5");
        return -EINVAL;
    }

    shell_send_cmd(FUNC_START, INSTR_NONE,
                   (int8_t)(TARGET_LOAD_CELL_0_enum + id), 0.0f, 0);
    return 0;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_send_cmd(FUNC_STOP, INSTR_NONE, TARGET_DEFAULT, 0.0f, 0);
    return 0;
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_send_cmd(FUNC_RESET, INSTR_NONE, TARGET_DEFAULT, 0.0f, 0);
    return 0;
}

static int cmd_tare(const struct shell *sh, size_t argc, char **argv)
{
    int id = atoi(argv[1]);

    if (id < 0 || id > 5) {
        shell_error(sh, "tare: id deve ser 0-5");
        return -EINVAL;
    }

    HAL_HX711_Tare((uint8_t)id);
    shell_print(sh, "Celula[%d]: tarada", id);
    return 0;
}

static int cmd_sensor(const struct shell *sh, size_t argc, char **argv)
{
    int id = atoi(argv[1]);

    if (!strcmp(argv[2], "linear") && argc == 4) {
        shell_send_cmd(FUNC_SENSOR, INSTR_WRITE, (int8_t)id, strtof(argv[3], NULL),
                       DEFAULT_ACTION_SENSE_LINEAR_COEF);

    } else if (!strcmp(argv[2], "offset") && argc == 4) {
        shell_send_cmd(FUNC_SENSOR, INSTR_WRITE, (int8_t)id, strtof(argv[3], NULL),
                       DEFAULT_ACTION_SENSE_OFFSET_COEF);

    } else if (!strcmp(argv[2], "reset") && argc == 3) {
        shell_send_cmd(FUNC_SENSOR, INSTR_WRITE, (int8_t)id, 0.0f,
                       DEFAULT_ACTION_SENSE_RESET_DEFAULT);

    } else {
        shell_error(sh, "uso: sensor <id> linear|offset <val> | sensor <id> reset");
        return -EINVAL;
    }

    return 0;
}

static int cmd_batt_poly(const struct shell *sh, size_t argc, char **argv)
{
    int   idx = atoi(argv[1]);
    float val = strtof(argv[2], NULL);

    if (idx < 0 || idx > 4) {
        shell_error(sh, "batt poly: idx deve ser 0-4");
        return -EINVAL;
    }

    shell_send_cmd(FUNC_BATTERY, INSTR_WRITE, 0, val,
                   (int8_t)(BAT_ACTION_P0_OVERWRITE + idx));
    return 0;
}

static int cmd_batt_templimit(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);

    float val = strtof(argv[1], NULL);

    shell_send_cmd(FUNC_BATTERY, INSTR_WRITE, 0, val, BAT_ACTION_TEMP_LIMIT_OVERWRITE);
    shell_print(sh, "Battery: limite de temperatura = %.1f", (double)val);
    return 0;
}

static int cmd_batt_relay(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);

    bool on = !strcmp(argv[1], "on");

    shell_send_cmd(FUNC_BATTERY, INSTR_WRITE, 0, on ? 1.0f : 0.0f, BAT_ACTION_RELAY);
    shell_print(sh, "Rele de potencia: %s", on ? "ligado" : "desligado");
    return 0;
}

static int cmd_batt_cells(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);

    int n = atoi(argv[1]);

    if (n < 0 || n > 255) {
        shell_error(sh, "batt cells: valor invalido");
        return -EINVAL;
    }

    Battery_Manager_Set_Cell_Count((uint8_t)n);
    shell_print(sh, "Battery: contagem de celulas = %d (0 = auto-detectar)", n);
    return 0;
}

static int cmd_prop_maxcurrent(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);

    float amps = strtof(argv[1], NULL);

    Propeller_Manager_Set_Max_Current(amps);
    shell_print(sh, "Propeller: limite de corrente = %.1f A", (double)amps);
    return 0;
}

/* ===========================================================
 * Árvore de (sub)comandos — "siba <grupo> <ação> [args...]"
 * =========================================================== */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_batt,
    SHELL_CMD_ARG(poly, NULL,
                  "<0-4> <val> — sobrescreve coef. P<n> do polinomio SOC",
                  cmd_batt_poly, 3, 0),
    SHELL_CMD_ARG(templimit, NULL,
                  "<val> — sobrescreve limite de temperatura da bateria",
                  cmd_batt_templimit, 2, 0),
    SHELL_CMD_ARG(relay, NULL,
                  "on|off — liga/desliga rele de potencia",
                  cmd_batt_relay, 2, 0),
    SHELL_CMD_ARG(cells, NULL,
                  "<n> — fixa contagem de celulas (0 = auto)",
                  cmd_batt_cells, 2, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_prop,
    SHELL_CMD_ARG(maxcurrent, NULL,
                  "<amps> — sobrescreve limite de corrente do motor",
                  cmd_prop_maxcurrent, 2, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_siba,
    SHELL_CMD_ARG(read, NULL,
                  "snapshot do TELEMETRY_LOG", cmd_read, 1, 0),
    SHELL_CMD_ARG(rpm, NULL,
                  "RPM atual (HAL_RPM_Get)", cmd_rpm, 1, 0),
    SHELL_CMD_ARG(esc, NULL,
                  "<0-100> — throttle do ESC %% (Propeller_Manager)", cmd_esc, 2, 0),
    SHELL_CMD_ARG(arm, NULL,
                  "arma o ESC (HAL direto)", cmd_arm, 1, 0),
    SHELL_CMD_ARG(disarm, NULL,
                  "desarma o ESC (HAL direto)", cmd_disarm, 1, 0),
    SHELL_CMD_ARG(start, NULL,
                  "<0-5> — FUNC_START da celula N (via CMD_Core)", cmd_start, 2, 0),
    SHELL_CMD_ARG(stop, NULL,
                  "FUNC_STOP / emergency stop (via CMD_Core)", cmd_stop, 1, 0),
    SHELL_CMD_ARG(reset, NULL,
                  "FUNC_RESET — reboot do board (via CMD_Core)", cmd_reset, 1, 0),
    SHELL_CMD_ARG(tare, NULL,
                  "<0-5> — tara a celula de carga N (HAL direto)", cmd_tare, 2, 0),
    SHELL_CMD_ARG(sensor, NULL,
                  "<id> linear|offset <val> | <id> reset — calibracao (FUNC_SENSOR)",
                  cmd_sensor, 3, 1),
    SHELL_CMD(batt, &sub_batt, "comandos da bateria (FUNC_BATTERY)", NULL),
    SHELL_CMD(prop, &sub_prop, "comandos do Propeller_Manager", NULL),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(siba, &sub_siba, "Comandos de teste do SIBA — ver HAL/CMD_Core", NULL);
