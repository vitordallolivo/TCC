/*
 * Test_Shell.c — diagnóstico direto no Zephyr Shell (mesmo UART do console).
 * Comandos top-level (sem prefixo "siba"). Nunca passa por CMD_Core.
 */
#include "Test_Shell.h"
#include "HAL.h"
#include "NCI.h"
#include "NVM.h"
#include "Battery_Manager.h"
#include "Propeller_Manager.h"
#include "Security_Manager.h"
#include "Log_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/shell/shell.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

/* ── tele ─────────────────────────────────────────────────────────────────── */
static int cmd_tele(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    TELEMETRY_LOG snap = Log_Manager_Get_Snapshot();
    shell_print(sh, "t=%u ms  status=%d  error_code=0x%08X",
                snap.timestamp, (int)snap.status, snap.error_code);
    shell_print(sh, "  cells[0..5] = %.2f %.2f %.2f %.2f %.2f %.2f g",
                (double)snap.load_cell_0, (double)snap.load_cell_1,
                (double)snap.load_cell_2, (double)snap.load_cell_3,
                (double)snap.load_cell_4, (double)snap.load_cell_5);
    shell_print(sh, "  v_batt=%.2fV  i_batt=%.2fA  soc=%.1f%%",
                (double)snap.v_batt, (double)snap.i_batt, (double)snap.soc);
    shell_print(sh, "  temp_batt=%.1fC  temp_motor=%.1fC",
                (double)snap.temperature_battery, (double)snap.temperature_motor);
    shell_print(sh, "  air_speed=%.2fm/s  rpm=%.1f",
                (double)snap.air_speed, (double)snap.rpm);

    bool armed     = HAL_ESC_Is_Armed();
    float duty     = HAL_ESC_Get_Throttle_Pct();
    float pulse_ms = (HAL_ESC_PWM_MIN_US + (duty / 100.0f) *
                     (HAL_ESC_PWM_MAX_US - HAL_ESC_PWM_MIN_US)) / 1000.0f;
    shell_print(sh, "  ESC: %s  duty=%.1f%%  pulso=%.3f ms",
                armed ? "ARMADO" : "disarm", (double)duty, (double)pulse_ms);

    shell_print(sh, "  reles: pre-carga=%s  potencia=%s  (espera de %u ms)",
                HAL_Relay_Precharge_Closed() ? "FECHADO" : "aberto",
                HAL_Relay_Power_Closed()     ? "FECHADO" : "aberto",
                Battery_Manager_Get_Precharge_Ms());
    shell_print(sh, "  emergencia: botao=%s  parada=%s  sobrescrita=%s",
                HAL_Emergency_Is_Pressed()             ? "APERTADO" : "solto",
                Security_Manager_Is_Emergency_Active() ? "ATIVA"    : "livre",
                Security_Manager_Get_Emergency_Override() ? "SIM"   : "nao");
    return 0;
}
SHELL_CMD_REGISTER(tele, NULL, "snapshot do TELEMETRY_LOG", cmd_tele);

/* ── sensors ─────────────────────────────────────────────────────────────── */
static int cmd_sensors(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    shell_print(sh, "=== HX711 (raw / gramas) ===");
    for (uint8_t i = 0; i < 6; i++) {
        shell_print(sh, "  [%u] raw=%d  g=%.2f", i,
                    HAL_HX711_Get_Raw(i), (double)HAL_HX711_Get_Grams(i));
    }
    shell_print(sh, "=== ADC (raw 12b / fisico) ===");
    shell_print(sh, "  v_batt      raw=%u -> %.3f V",
                HAL_ADC2_Get(HAL_ADC2_IDX_V_BATT),    (double)HAL_ADC_Get_VBatt());
    shell_print(sh, "  i_batt      raw=%u -> %.3f A",
                HAL_ADC2_Get(HAL_ADC2_IDX_I_BATT),    (double)HAL_ADC_Get_IBatt());
    shell_print(sh, "  temp_batt   raw=%u -> %.1f C",
                HAL_ADC1_Get(HAL_ADC1_IDX_TEMP_BATT), (double)HAL_ADC_Get_Temperature_Batt());
    shell_print(sh, "  temp_motor  raw=%u -> %.1f C",
                HAL_ADC1_Get(HAL_ADC1_IDX_TEMP_MOTOR),(double)HAL_ADC_Get_Temperature_Motor());
    shell_print(sh, "  air_speed   raw=%u -> %.3f m/s",
                HAL_ADC1_Get(HAL_ADC1_IDX_AIR_SPEED), (double)HAL_ADC_Get_Air_Speed());
    shell_print(sh, "=== Misc ===");
    shell_print(sh, "  soc=%.1f%%  cells=%u  relay=%s  rpm=%.1f",
                (double)Log_Manager_Get_Snapshot().soc,
                Battery_Manager_Get_Cell_Count(),
                Battery_Manager_Get_Relay() ? "on" : "off",
                (double)HAL_RPM_Get());
    return 0;
}
SHELL_CMD_REGISTER(sensors, NULL, "raw+calibrado de todos os sensores", cmd_sensors);

/* ── errs ────────────────────────────────────────────────────────────────── */
struct error_bit_info { uint32_t bit; const char *name; const char *descr; };

static const struct error_bit_info error_bits[] = {
    { ERROR_BATT_LOW_SOC,                  "BATT_LOW_SOC",                  "SOC abaixo do limite" },
    { ERROR_BATT_POLY_MISCONFIGURED,       "BATT_POLY_MISCONFIGURED",       "polinomio SOC invalido" },
    { ERROR_BATT_LOW_VOLTAGE,              "BATT_LOW_VOLTAGE",              "tensao por celula baixa" },
    { ERROR_BATT_RELAY_MISMATCH,           "BATT_RELAY_MISMATCH",           "corrente com relay off" },
    { ERROR_BATT_OVER_TEMPERATURE,         "BATT_OVER_TEMPERATURE",         "temp bateria alta" },
    { ERROR_PROP_LOAD_CELL_NOT_CONFIGURED, "PROP_CELL_NOT_CONFIGURED",      "celula sem calibracao" },
    { ERROR_PROP_LOAD_CELL_NOT_CONNECTED,  "PROP_CELL_NOT_CONNECTED",       "HX711 nao responde" },
    { ERROR_PROP_OVERCURRENT,              "PROP_OVERCURRENT",              "corrente motor alta" },
    { ERROR_SEC_VOLTAGE_OUT_OF_RANGE,      "SEC_VOLTAGE_OUT_OF_RANGE",      "v_batt fora dos limites" },
    { ERROR_SEC_CURRENT_EXCESSIVE,         "SEC_CURRENT_EXCESSIVE",         "i_batt excessivo" },
    { ERROR_SEC_RPM_EXCESSIVE,             "SEC_RPM_EXCESSIVE",             "rpm excessivo" },
    { ERROR_SEC_EMERGENCY_ACTIVE,          "SEC_EMERGENCY_ACTIVE",          "emergency stop ativo" },
    { ERROR_OS_GPIO_INIT_FAILED,           "OS_GPIO_INIT_FAILED",           "GPIO nao inicializou" },
};

static int cmd_errs(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    uint32_t code = Security_Manager_Get_Error_Code();
    shell_print(sh, "error_code = 0x%08X", code);
    if (code == 0U) { shell_print(sh, "  (sem erros)"); return 0; }
    for (size_t i = 0; i < ARRAY_SIZE(error_bits); i++) {
        if (code & error_bits[i].bit) {
            shell_print(sh, "  [bit%2u] %-28s %s",
                        (unsigned)(31u - __builtin_clz(error_bits[i].bit)),
                        error_bits[i].name, error_bits[i].descr);
        }
    }
    return 0;
}
SHELL_CMD_REGISTER(errs, NULL, "error_code decodificado bit a bit", cmd_errs);

/* ── rpm ─────────────────────────────────────────────────────────────────── */
static int cmd_rpm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    shell_print(sh, "RPM = %.1f", (double)HAL_RPM_Get());
    return 0;
}
SHELL_CMD_REGISTER(rpm, NULL, "RPM atual (TIM3 CH3)", cmd_rpm);

/* ── esc ─────────────────────────────────────────────────────────────────── */
static int cmd_esc(const struct shell *sh, size_t argc, char **argv)
{
    float pct = strtof(argv[1], NULL);
    int r = Propeller_Manager_Request_Throttle(pct);
    if (r == -EPERM) {
        shell_error(sh, "ESC nao armado — use 'arm' ou comande via rede");
    } else {
        float duty     = HAL_ESC_Get_Throttle_Pct();
        float pulse_ms = (HAL_ESC_PWM_MIN_US + (duty / 100.0f) *
                         (HAL_ESC_PWM_MAX_US - HAL_ESC_PWM_MIN_US)) / 1000.0f;
        shell_print(sh, "ESC: duty=%.1f%%  pulso=%.3f ms", (double)duty, (double)pulse_ms);
    }
    return 0;
}
SHELL_CMD_ARG_REGISTER(esc, NULL, "<0-100> — throttle %%", cmd_esc, 2, 0);

/* ── arm / disarm ────────────────────────────────────────────────────────── */
/* Armar a bancada é a cadeia inteira, na ordem: fecha pré-carga, espera os
 * capacitores do ESC carregarem, fecha potência e só então arma o ESC.
 * Desarmar percorre o caminho inverso. O ESC ainda é armado direto no HAL,
 * sem passar pelo Propeller_Manager — o limite de corrente só atua sobre
 * pedidos de throttle, não sobre a armação. */
static int cmd_arm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);

    if (!HAL_Relay_Power_Closed()) {
        shell_print(sh, "energizando: pre-carga -> %u ms -> potencia, depois ESC",
                    Battery_Manager_Get_Precharge_Ms());
    }

    if (Security_Manager_Arm_Bench() != 0) {
        shell_error(sh, "recusado — botao de emergencia apertado "
                        "(ou sobrescreva por comando de rede)");
        return -EPERM;
    }

    shell_print(sh, "bancada armada — pre-carga=%s potencia=%s ESC=%s",
                HAL_Relay_Precharge_Closed() ? "fechado" : "aberto",
                HAL_Relay_Power_Closed()     ? "fechado" : "aberto",
                HAL_ESC_Is_Armed()           ? "armado"  : "disarm");
    return 0;
}
SHELL_CMD_REGISTER(arm, NULL,
                   "arma a bancada: pre-carga -> potencia -> ESC (bloqueia)",
                   cmd_arm);

static int cmd_disarm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);

    Security_Manager_Shutdown_Bench();

    shell_print(sh, "bancada desarmada — pre-carga=%s potencia=%s ESC=%s",
                HAL_Relay_Precharge_Closed() ? "fechado" : "aberto",
                HAL_Relay_Power_Closed()     ? "fechado" : "aberto",
                HAL_ESC_Is_Armed()           ? "armado"  : "disarm");
    return 0;
}
SHELL_CMD_REGISTER(disarm, NULL,
                   "desarma a bancada: ESC -> potencia -> pre-carga",
                   cmd_disarm);

/* ── estop ───────────────────────────────────────────────────────────────── */
static int cmd_estop(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    shell_warn(sh, "ESTOP: corte de emergencia");
    Security_Manager_Trigger_Emergency();
    return 0;
}
SHELL_CMD_REGISTER(estop, NULL, "emergency stop (Security_Manager)", cmd_estop);

#ifdef CONFIG_ARCH_POSIX
/* ── btn (só native_sim) ─────────────────────────────────────────────────── */
static int cmd_btn(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    bool pressed = (strcmp(argv[1], "on") == 0);
    HAL_Emergency_Sim_Set_Pressed(pressed);
    shell_print(sh, "botao de emergencia simulado: %s",
                pressed ? "APERTADO" : "solto");
    return 0;
}
SHELL_CMD_ARG_REGISTER(btn, NULL, "on|off — finge o botao de emergencia (sim)",
                       cmd_btn, 2, 0);
#endif /* CONFIG_ARCH_POSIX */

/* ── reboot ──────────────────────────────────────────────────────────────── */
static int cmd_reboot(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    shell_warn(sh, "REBOOT...");
    sys_reboot(SYS_REBOOT_COLD);
    return 0;
}
SHELL_CMD_REGISTER(reboot, NULL, "cold reboot", cmd_reboot);

/* ── tare ────────────────────────────────────────────────────────────────── */
static int cmd_tare(const struct shell *sh, size_t argc, char **argv)
{
    int id = atoi(argv[1]);
    if (id < 0 || id > 5) { shell_error(sh, "id deve ser 0-5"); return -EINVAL; }
    Propeller_Manager_Tare_Cell((uint8_t)id);
    shell_print(sh, "celula[%d] tarada", id);
    return 0;
}
SHELL_CMD_ARG_REGISTER(tare, NULL, "<0-5> — tara celula de carga", cmd_tare, 2, 0);

/* ── calib ───────────────────────────────────────────────────────────────── */
static bool sid_is_cell(int id) { return id >= 0 && id <= 5; }
static bool sid_is_adc(int id)
{
    return id == SENSOR_ID_V_BATT || id == SENSOR_ID_I_BATT ||
           id == SENSOR_ID_TEMPERATURE_BATTERY || id == SENSOR_ID_AIR_SPEED ||
           id == SENSOR_ID_TEMPERATURE_MOTOR;
}

static int cmd_calib(const struct shell *sh, size_t argc, char **argv)
{
    int id = atoi(argv[1]);
    if (!sid_is_cell(id) && !sid_is_adc(id)) {
        shell_error(sh, "uso: calib <id> linear|offset <val> | calib <id> reset");
        shell_error(sh, "  ids validos: 0-5 (celulas) | 6=v_batt 7=i_batt 9=temp_batt 10=air_speed 12=temp_motor");
        return -EINVAL;
    }

    if (!strcmp(argv[2], "linear") && argc == 4) {
        float val = strtof(argv[3], NULL);
        if (sid_is_cell(id)) {
            Propeller_Manager_Calibrate_Cell_Weight((uint8_t)id, val);
            shell_print(sh, "celula[%d]: peso ref=%.3f", id, (double)val);
        } else {
            float lin, off;
            HAL_ADC_Get_Calib((uint8_t)id, &lin, &off);
            HAL_ADC_Set_Calib((uint8_t)id, val, off);
            shell_print(sh, "sensor[%d]: linear=%.6f", id, (double)val);
        }
    } else if (!strcmp(argv[2], "offset") && argc == 4) {
        float val = strtof(argv[3], NULL);
        if (sid_is_cell(id)) {
            Propeller_Manager_Tare_Cell((uint8_t)id);
            shell_print(sh, "celula[%d]: tarada (offset=raw atual)", id);
        } else {
            float lin, off;
            HAL_ADC_Get_Calib((uint8_t)id, &lin, &off);
            HAL_ADC_Set_Calib((uint8_t)id, lin, val);
            shell_print(sh, "sensor[%d]: offset=%.6f", id, (double)val);
        }
    } else if (!strcmp(argv[2], "reset") && argc == 3) {
        if (sid_is_cell(id)) {
            Propeller_Manager_Set_Cell_Calib((uint8_t)id, 1.0f, 0.0f);
            Propeller_Manager_Tare_Cell((uint8_t)id);
        } else {
            HAL_ADC_Set_Calib((uint8_t)id, 1.0f, 0.0f);
        }
        shell_print(sh, "sensor[%d]: calibracao resetada", id);
    } else {
        shell_error(sh, "uso: calib <id> linear <val> | calib <id> offset <val> | calib <id> reset");
        return -EINVAL;
    }
    return 0;
}
SHELL_CMD_ARG_REGISTER(calib, NULL,
    "<id> linear|offset <val> | <id> reset — calibracao sensor/celula",
    cmd_calib, 3, 1);

/* ── nvm ─────────────────────────────────────────────────────────────────── */
static int cmd_nvm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);

    static const char * const names[NVM_SENSOR_COUNT] = {
        "load_cell_0", "load_cell_1", "load_cell_2",
        "load_cell_3", "load_cell_4", "load_cell_5",
        "v_batt", "i_batt", "soc",
        "temp_batt", "air_speed", "rpm", "temp_motor",
    };

    shell_print(sh, "NVM EEPROM  write_count=%u", NVM_Get_Write_Count());
    shell_print(sh, "  id  %-14s  linear            offset", "sensor");
    for (uint8_t i = 0; i < NVM_SENSOR_COUNT; i++) {
        float lin = 0.0f, off = 0.0f;
        NVM_Get_Sensor_Calib(i, &lin, &off);
        shell_print(sh, "  %2u  %-14s  %16.6f  %16.6f",
                    i, names[i], (double)lin, (double)off);
    }
    return 0;
}
SHELL_CMD_REGISTER(nvm, NULL, "calibracao salva no EEPROM (todos os 13 sensores)", cmd_nvm);

/* ── mon ─────────────────────────────────────────────────────────────────── */
static int cmd_mon(const struct shell *sh, size_t argc, char **argv)
{
    bool en = atoi(argv[1]) != 0;
    NCI_Set_Status_Print(en);
    shell_print(sh, "monitor UART: %s", en ? "ON" : "OFF");
    return 0;
}
SHELL_CMD_ARG_REGISTER(mon, NULL, "<0|1> — liga/desliga print periodico UART", cmd_mon, 2, 0);

/* ── batt ────────────────────────────────────────────────────────────────── */
static int cmd_batt_poly(const struct shell *sh, size_t argc, char **argv)
{
    int idx = atoi(argv[1]);
    if (idx < 0 || idx > 4) { shell_error(sh, "idx 0-4"); return -EINVAL; }
    Battery_Manager_Set_Poly((uint8_t)idx, strtof(argv[2], NULL));
    shell_print(sh, "batt poly P%d=%.6f", idx, (double)strtof(argv[2], NULL));
    return 0;
}
static int cmd_batt_templimit(const struct shell *sh, size_t argc, char **argv)
{
    Battery_Manager_Set_Temp_Limit(strtof(argv[1], NULL));
    shell_print(sh, "batt templimit=%.1f C", (double)strtof(argv[1], NULL));
    return 0;
}
static int cmd_batt_relay(const struct shell *sh, size_t argc, char **argv)
{
    bool on = !strcmp(argv[1], "on");
    Battery_Manager_Set_Relay(on);
    shell_print(sh, "relay: %s", on ? "LIGADO" : "desligado");
    return 0;
}
static int cmd_batt_cells(const struct shell *sh, size_t argc, char **argv)
{
    int n = atoi(argv[1]);
    if (n < 0 || n > 255) { shell_error(sh, "0-255"); return -EINVAL; }
    Battery_Manager_Set_Cell_Count((uint8_t)n);
    shell_print(sh, "cells=%d (0=auto)", n);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_batt,
    SHELL_CMD_ARG(poly,      NULL, "<0-4> <val>", cmd_batt_poly,      3, 0),
    SHELL_CMD_ARG(templimit, NULL, "<C>",          cmd_batt_templimit, 2, 0),
    SHELL_CMD_ARG(relay,     NULL, "on|off",       cmd_batt_relay,     2, 0),
    SHELL_CMD_ARG(cells,     NULL, "<n|0=auto>",   cmd_batt_cells,     2, 0),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(batt, &sub_batt, "bateria — poly/templimit/relay/cells", NULL);

/* ── prop ────────────────────────────────────────────────────────────────── */
static int cmd_prop_maxcurrent(const struct shell *sh, size_t argc, char **argv)
{
    Propeller_Manager_Set_Max_Current(strtof(argv[1], NULL));
    shell_print(sh, "prop maxcurrent=%.1f A", (double)strtof(argv[1], NULL));
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_prop,
    SHELL_CMD_ARG(maxcurrent, NULL, "<A>", cmd_prop_maxcurrent, 2, 0),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(prop, &sub_prop, "helice — maxcurrent", NULL);

/* ── i2c ─────────────────────────────────────────────────────────────────── */
#define I2C_MAX_BYTES 32

static int cmd_i2c_scan(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    uint8_t addrs[16]; size_t count = 0;
    HAL_I2C_Scan(addrs, ARRAY_SIZE(addrs), &count);
    shell_print(sh, "I2C scan: %u dispositivo(s)", (unsigned)count);
    for (size_t i = 0; i < count; i++) shell_print(sh, "  0x%02X", addrs[i]);
    return 0;
}
static int cmd_i2c_read(const struct shell *sh, size_t argc, char **argv)
{
    uint16_t addr = (uint16_t)strtoul(argv[1], NULL, 0);
    size_t   len  = (size_t)strtoul(argv[2], NULL, 0);
    uint8_t  buf[I2C_MAX_BYTES];
    if (len == 0 || len > sizeof(buf)) { shell_error(sh, "len 1-%u", (unsigned)sizeof(buf)); return -EINVAL; }
    int ret = HAL_I2C_Read(addr, buf, len);
    if (ret) { shell_error(sh, "erro %d", ret); return ret; }
    shell_fprintf(sh, SHELL_NORMAL, "0x%02X:", addr);
    for (size_t i = 0; i < len; i++) shell_fprintf(sh, SHELL_NORMAL, " %02X", buf[i]);
    shell_print(sh, "");
    return 0;
}
static int cmd_i2c_write(const struct shell *sh, size_t argc, char **argv)
{
    uint16_t addr = (uint16_t)strtoul(argv[1], NULL, 0);
    size_t   len  = argc - 2;
    uint8_t  buf[I2C_MAX_BYTES];
    if (len == 0 || len > sizeof(buf)) { shell_error(sh, "bytes 1-%u", (unsigned)sizeof(buf)); return -EINVAL; }
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)strtoul(argv[2 + i], NULL, 0);
    int ret = HAL_I2C_Write(addr, buf, len);
    if (ret) { shell_error(sh, "erro %d", ret); return ret; }
    shell_print(sh, "i2c write 0x%02X: %u B OK", addr, (unsigned)len);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_i2c,
    SHELL_CMD_ARG(scan,  NULL, "lista dispositivos",              cmd_i2c_scan,  1, 0),
    SHELL_CMD_ARG(read,  NULL, "<addr> <len>",                    cmd_i2c_read,  3, 0),
    SHELL_CMD_ARG(write, NULL, "<addr> <b0> [b1...]",             cmd_i2c_write, 3, I2C_MAX_BYTES - 1),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(i2c, &sub_i2c, "I2C2 (PB10/PB11) — scan/read/write", NULL);
