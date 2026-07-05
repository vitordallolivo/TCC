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
 * IMPORTANTE — camadas: o shell é Application e fala diretamente com a
 * camada Sys (managers) ou, quando inevitável, com o HAL — NUNCA injeta
 * CMD em fila_cmd/CMD_Core. CMD existe só para o protocolo de rede com o
 * PC (ver CLAUDE.md "Application -> Sys -> OS"); reusar esse caminho aqui
 * exporia o OS para a Application e duplicaria/desalinharia a lógica dos
 * managers (ex.: taração de célula é decisão do Sys, não do HAL).
 *
 * Digite "siba" + TAB no terminal serial para ver a lista de subcomandos.
 */

#include "Test_Shell.h"
#include "HAL.h"
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

static int cmd_read(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    TELEMETRY_LOG snap = Log_Manager_Get_Snapshot();

    shell_print(sh, "t=%u ms  status=%d  error_code=0x%08X",
                snap.timestamp, (int)snap.status, snap.error_code);
    shell_print(sh, "  load_cell[0..5] = %.2f %.2f %.2f %.2f %.2f %.2f",
                (double)snap.load_cell_0, (double)snap.load_cell_1, (double)snap.load_cell_2,
                (double)snap.load_cell_3, (double)snap.load_cell_4, (double)snap.load_cell_5);
    shell_print(sh, "  v_batt=%.2fV  i_batt=%.2fA  soc=%.1f%%  temp_batt=%.1fC",
                (double)snap.v_batt, (double)snap.i_batt, (double)snap.soc,
                (double)snap.temperature_battery);
    shell_print(sh, "  air_speed=%.2f m/s   rpm=%.1f", (double)snap.air_speed, (double)snap.rpm);

    return 0;
}

/* ===========================================================
 * siba sensors — lista verbosa de TODOS os sensores: valor
 * convertido (mesmo dado que vai pro TELEMETRY_LOG) + leitura
 * bruta direto do HAL, útil para diagnosticar hardware vs.
 * conversão/calibração.
 * =========================================================== */
static int cmd_sensors(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "=== Celulas de carga (HX711, bruto / convertido em gramas) ===");
    for (uint8_t i = 0; i < 6; i++) {
        shell_print(sh, "  [%u] raw=%d  grams=%.2f", i,
                    HAL_HX711_Get_Raw(i), (double)HAL_HX711_Get_Grams(i));
    }

    shell_print(sh, "=== ADC (bruto 12 bits / valor fisico convertido) ===");
    shell_print(sh, "  v_batt           raw=%u  -> %.2f V",
                HAL_ADC_Get(HAL_ADC_IDX_V_BATT),    (double)HAL_ADC_Get_VBatt());
    shell_print(sh, "  i_batt           raw=%u  -> %.2f A",
                HAL_ADC_Get(HAL_ADC_IDX_I_BATT),    (double)HAL_ADC_Get_IBatt());
    shell_print(sh, "  temperature_batt raw=%u  -> %.1f C",
                HAL_ADC_Get(HAL_ADC_IDX_TEMP_BATT), (double)HAL_ADC_Get_Temperature_Batt());
    shell_print(sh, "  air_speed        raw=%u  -> %.2f m/s",
                HAL_ADC_Get(HAL_ADC_IDX_AIR_SPEED), (double)HAL_ADC_Get_Air_Speed());

    shell_print(sh, "=== Outros ===");
    shell_print(sh, "  soc   = %.1f %% (cell_count=%u)",
                (double)Log_Manager_Get_Snapshot().soc, Battery_Manager_Get_Cell_Count());
    shell_print(sh, "  rpm   = %.1f", (double)HAL_RPM_Get());
    shell_print(sh, "  rele  = %s (comandado)", Battery_Manager_Get_Relay() ? "ligado" : "desligado");

    return 0;
}

/* ===========================================================
 * siba error — mostra o uint32_t agregado pelo Security_Manager
 * (mesmo campo TELEMETRY_LOG.error_code, em HEX) já decodificado
 * bit a bit — ver Security_Manager.h "siba_error_bit_t".
 * =========================================================== */
struct error_bit_info {
    uint32_t            bit;
    const char         *name;
    const char         *descr;
};

static const struct error_bit_info error_bits[] = {
    { ERROR_BATT_LOW_SOC,                "BATT_LOW_SOC",                "SOC abaixo do limite de alerta" },
    { ERROR_BATT_POLY_MISCONFIGURED,     "BATT_POLY_MISCONFIGURED",     "polinomio SOC produz valores absurdos" },
    { ERROR_BATT_LOW_VOLTAGE,            "BATT_LOW_VOLTAGE",            "tensao por celula proxima do limite" },
    { ERROR_BATT_RELAY_MISMATCH,         "BATT_RELAY_MISMATCH",         "corrente flui com o role desligado" },
    { ERROR_BATT_OVER_TEMPERATURE,       "BATT_OVER_TEMPERATURE",       "temperatura da bateria acima do limite" },
    { ERROR_PROP_LOAD_CELL_NOT_CONFIGURED, "PROP_LOAD_CELL_NOT_CONFIGURED", "celula(s) sem calibracao valida" },
    { ERROR_PROP_LOAD_CELL_NOT_CONNECTED,  "PROP_LOAD_CELL_NOT_CONNECTED",  "HX711 nao responde (leitura ~0xFF)" },
    { ERROR_PROP_OVERCURRENT,            "PROP_OVERCURRENT",            "corrente do motor acima do limite" },
    { ERROR_SEC_VOLTAGE_OUT_OF_RANGE,    "SEC_VOLTAGE_OUT_OF_RANGE",    "v_batt fora dos limites operacionais" },
    { ERROR_SEC_CURRENT_EXCESSIVE,       "SEC_CURRENT_EXCESSIVE",       "i_batt acima do limite de seguranca" },
    { ERROR_SEC_RPM_EXCESSIVE,           "SEC_RPM_EXCESSIVE",           "rpm acima do limite de seguranca" },
    { ERROR_SEC_EMERGENCY_ACTIVE,        "SEC_EMERGENCY_ACTIVE",        "emergency stop disparado" },
    { ERROR_OS_GPIO_INIT_FAILED,         "OS_GPIO_INIT_FAILED",         "algum modulo GPIO nao inicializou" },
};

static int cmd_error(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    uint32_t code = Security_Manager_Get_Error_Code();
    shell_print(sh, "error_code = 0x%08X", code);

    if (code == 0U) {
        shell_print(sh, "  (nenhuma condicao de erro ativa)");
        return 0;
    }

    for (size_t i = 0; i < ARRAY_SIZE(error_bits); i++) {
        if (code & error_bits[i].bit) {
            shell_print(sh, "  [bit %2u] %-30s — %s",
                        (unsigned)(31u - __builtin_clz(error_bits[i].bit)),
                        error_bits[i].name, error_bits[i].descr);
        }
    }
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

    /* Não há ação real a tomar: as células de carga já rodam
     * automaticamente via thread HX711 desde o boot — mesmo
     * comportamento de FUNC_START em CMD_Core (ver CLAUDE.md). */
    shell_print(sh, "Celula[%d]: ja ativa (thread HX711 roda desde o boot)", id);
    return 0;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_warn(sh, "STOP: corte de emergencia");
    Security_Manager_Trigger_Emergency();
    return 0;
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_warn(sh, "RESET: reiniciando sistema...");
    sys_reboot(SYS_REBOOT_COLD);
    return 0;
}

static int cmd_tare(const struct shell *sh, size_t argc, char **argv)
{
    int id = atoi(argv[1]);

    if (id < 0 || id > 5) {
        shell_error(sh, "tare: id deve ser 0-5");
        return -EINVAL;
    }

    Propeller_Manager_Tare_Cell((uint8_t)id);
    shell_print(sh, "Celula[%d]: tarada", id);
    return 0;
}

/* ===========================================================
 * siba sensor <id> linear|offset <val> | <id> reset
 *
 * Calibração é decisão do Sys, nunca do HAL puro: células de carga
 * (id 0-5) passam por Propeller_Manager_Set_Cell_Calib (que também
 * marca a célula como configurada — ver ERROR_PROP_LOAD_CELL_NOT_CONFIGURED);
 * sensores analógicos (id 6/7/9/10 — v_batt/i_batt/temp/air_speed) vão
 * direto para HAL_ADC_Set_Calib (ADC não tem lógica de Sys própria).
 * SENSOR_ID_SOC (8) e SENSOR_ID_RPM (11) não têm calibração linear/offset
 * (SOC é um polinômio — ver "siba batt poly"; RPM é contagem de pulsos).
 * =========================================================== */
static bool sensor_id_is_load_cell(int id)
{
    return (id >= SENSOR_ID_LOAD_CELL_0 && id <= SENSOR_ID_LOAD_CELL_5);
}

static bool sensor_id_is_adc(int id)
{
    return (id == SENSOR_ID_V_BATT || id == SENSOR_ID_I_BATT ||
            id == SENSOR_ID_TEMPERATURE_BATTERY || id == SENSOR_ID_AIR_SPEED);
}

static int cmd_sensor(const struct shell *sh, size_t argc, char **argv)
{
    int id = atoi(argv[1]);

    if (!sensor_id_is_load_cell(id) && !sensor_id_is_adc(id)) {
        shell_error(sh, "sensor: id deve ser 0-7,9,10 (celulas 0-5 ou ADC v_batt/i_batt/temp/air_speed)");
        return -EINVAL;
    }

    if (!strcmp(argv[2], "linear") && argc == 4) {
        float val = strtof(argv[3], NULL);
        float linear, offset;
        if (sensor_id_is_load_cell(id)) {
            /* Célula: `linear <peso>` = calibra a escala por PESO conhecido
             * (modelo canônico do HX711), não um ganho digitado. */
            Propeller_Manager_Calibrate_Cell_Weight((uint8_t)id, val);
            shell_print(sh, "sensor[%d]: calibrado por peso conhecido=%.3f", id, (double)val);
        } else {
            HAL_ADC_Get_Calib((uint8_t)id, &linear, &offset);
            HAL_ADC_Set_Calib((uint8_t)id, val, offset);
            shell_print(sh, "sensor[%d]: linear=%.4f", id, (double)val);
        }

    } else if (!strcmp(argv[2], "offset") && argc == 4) {
        float val = strtof(argv[3], NULL);
        float linear, offset;
        if (sensor_id_is_load_cell(id)) {
            /* Célula: `offset` = TARA (offset = raw atual); `val` ignorado. */
            Propeller_Manager_Tare_Cell((uint8_t)id);
            shell_print(sh, "sensor[%d]: TARADO (offset = raw atual)", id);
        } else {
            HAL_ADC_Get_Calib((uint8_t)id, &linear, &offset);
            HAL_ADC_Set_Calib((uint8_t)id, linear, val);
            shell_print(sh, "sensor[%d]: offset=%.4f", id, (double)val);
        }

    } else if (!strcmp(argv[2], "reset") && argc == 3) {
        if (sensor_id_is_load_cell(id)) {
            Propeller_Manager_Set_Cell_Calib((uint8_t)id, 1.0f, 0.0f);
            Propeller_Manager_Tare_Cell((uint8_t)id);
        } else {
            HAL_ADC_Set_Calib((uint8_t)id, 1.0f, 0.0f);
        }
        shell_print(sh, "sensor[%d]: calibracao restaurada", id);

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

    Battery_Manager_Set_Poly((uint8_t)idx, val);
    shell_print(sh, "Battery: P%d = %.6f", idx, (double)val);
    return 0;
}

static int cmd_batt_templimit(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);

    float val = strtof(argv[1], NULL);

    Battery_Manager_Set_Temp_Limit(val);
    shell_print(sh, "Battery: limite de temperatura = %.1f", (double)val);
    return 0;
}

static int cmd_batt_relay(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);

    bool on = !strcmp(argv[1], "on");

    Battery_Manager_Set_Relay(on);
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
 * siba i2c scan|read|write — infra crua do I2C1 (PB8/PB9 remap,
 * HAL_I2C_*). Sem decisão de protocolo — útil para checar a fiação/
 * endereço de um dispositivo novo antes de implementar o driver dele.
 * =========================================================== */
#define I2C_SHELL_MAX_BYTES 32

static int cmd_i2c_scan(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    uint8_t addrs[16];
    size_t count = 0;

    HAL_I2C_Scan(addrs, ARRAY_SIZE(addrs), &count);

    shell_print(sh, "I2C scan (0x%02X-0x%02X): %u dispositivo(s)",
                HAL_I2C_SCAN_ADDR_MIN, HAL_I2C_SCAN_ADDR_MAX, (unsigned)count);
    for (size_t i = 0; i < count && i < ARRAY_SIZE(addrs); i++) {
        shell_print(sh, "  0x%02X", addrs[i]);
    }
    return 0;
}

static int cmd_i2c_read(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);

    unsigned long addr = strtoul(argv[1], NULL, 0);
    unsigned long len  = strtoul(argv[2], NULL, 0);
    uint8_t buf[I2C_SHELL_MAX_BYTES];

    if (len == 0 || len > sizeof(buf)) {
        shell_error(sh, "i2c read: len deve ser 1-%u", (unsigned)sizeof(buf));
        return -EINVAL;
    }

    int ret = HAL_I2C_Read((uint16_t)addr, buf, (size_t)len);
    if (ret != 0) {
        shell_error(sh, "i2c read 0x%02X: erro %d", (unsigned)addr, ret);
        return ret;
    }

    shell_fprintf(sh, SHELL_NORMAL, "0x%02X:", (unsigned)addr);
    for (unsigned long i = 0; i < len; i++) {
        shell_fprintf(sh, SHELL_NORMAL, " %02X", buf[i]);
    }
    shell_print(sh, "");
    return 0;
}

static int cmd_i2c_write(const struct shell *sh, size_t argc, char **argv)
{
    unsigned long addr = strtoul(argv[1], NULL, 0);
    size_t len = argc - 2;
    uint8_t buf[I2C_SHELL_MAX_BYTES];

    if (len == 0 || len > sizeof(buf)) {
        shell_error(sh, "i2c write: numero de bytes deve ser 1-%u", (unsigned)sizeof(buf));
        return -EINVAL;
    }

    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)strtoul(argv[2 + i], NULL, 0);
    }

    int ret = HAL_I2C_Write((uint16_t)addr, buf, len);
    if (ret != 0) {
        shell_error(sh, "i2c write 0x%02X: erro %d", (unsigned)addr, ret);
        return ret;
    }

    shell_print(sh, "i2c write 0x%02X: %u byte(s) OK", (unsigned)addr, (unsigned)len);
    return 0;
}

/* ===========================================================
 * siba dac <0-4095> — DAC1 canal 1 (PA5/DAC_OUT1), valor bruto 12 bits.
 * =========================================================== */
static int cmd_dac(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);

    long val = strtol(argv[1], NULL, 0);

    if (val < 0 || val > HAL_DAC_MAX_VALUE) {
        shell_error(sh, "dac: valor deve ser 0-%u", HAL_DAC_MAX_VALUE);
        return -EINVAL;
    }

    HAL_DAC_Set_Value((uint16_t)val);
    shell_print(sh, "DAC: %ld / %u", val, HAL_DAC_MAX_VALUE);
    return 0;
}

/* ===========================================================
 * Árvore de (sub)comandos — "siba <grupo> <ação> [args...]"
 * =========================================================== */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_batt,
    SHELL_CMD_ARG(poly, NULL,
                  "<0-4> <val> — sobrescreve coef. P<n> do polinomio SOC (Battery_Manager direto)",
                  cmd_batt_poly, 3, 0),
    SHELL_CMD_ARG(templimit, NULL,
                  "<val> — sobrescreve limite de temperatura da bateria (Battery_Manager direto)",
                  cmd_batt_templimit, 2, 0),
    SHELL_CMD_ARG(relay, NULL,
                  "on|off — liga/desliga rele de potencia (Battery_Manager direto)",
                  cmd_batt_relay, 2, 0),
    SHELL_CMD_ARG(cells, NULL,
                  "<n> — fixa contagem de celulas, 0 = auto (Battery_Manager direto)",
                  cmd_batt_cells, 2, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_prop,
    SHELL_CMD_ARG(maxcurrent, NULL,
                  "<amps> — sobrescreve limite de corrente do motor (Propeller_Manager direto)",
                  cmd_prop_maxcurrent, 2, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_i2c,
    SHELL_CMD_ARG(scan, NULL,
                  "varre o barramento (HAL_I2C_SCAN_ADDR_MIN-MAX) listando enderecos que respondem",
                  cmd_i2c_scan, 1, 0),
    SHELL_CMD_ARG(read, NULL,
                  "<addr> <len> — le len bytes do endereco addr (HAL_I2C_Read)",
                  cmd_i2c_read, 3, 0),
    SHELL_CMD_ARG(write, NULL,
                  "<addr> <byte0> [byte1...] — escreve bytes no endereco addr (HAL_I2C_Write)",
                  cmd_i2c_write, 3, I2C_SHELL_MAX_BYTES - 1),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_siba,
    SHELL_CMD_ARG(read, NULL,
                  "snapshot do TELEMETRY_LOG (Log_Manager)", cmd_read, 1, 0),
    SHELL_CMD_ARG(sensors, NULL,
                  "lista verbosa: valor bruto + convertido de cada sensor (HAL direto)",
                  cmd_sensors, 1, 0),
    SHELL_CMD_ARG(error, NULL,
                  "mostra error_code agregado (Security_Manager) decodificado bit a bit",
                  cmd_error, 1, 0),
    SHELL_CMD_ARG(rpm, NULL,
                  "RPM atual (HAL direto)", cmd_rpm, 1, 0),
    SHELL_CMD_ARG(esc, NULL,
                  "<0-100> — throttle do ESC %% (Propeller_Manager direto)", cmd_esc, 2, 0),
    SHELL_CMD_ARG(arm, NULL,
                  "arma o ESC (HAL direto)", cmd_arm, 1, 0),
    SHELL_CMD_ARG(disarm, NULL,
                  "desarma o ESC (HAL direto)", cmd_disarm, 1, 0),
    SHELL_CMD_ARG(start, NULL,
                  "<0-5> — celula de carga N (informativo: ja roda automaticamente)", cmd_start, 2, 0),
    SHELL_CMD_ARG(stop, NULL,
                  "emergency stop (Security_Manager direto)", cmd_stop, 1, 0),
    SHELL_CMD_ARG(reset, NULL,
                  "reboot do board (sys_reboot direto)", cmd_reset, 1, 0),
    SHELL_CMD_ARG(tare, NULL,
                  "<0-5> — tara a celula de carga N (Propeller_Manager direto)", cmd_tare, 2, 0),
    SHELL_CMD_ARG(sensor, NULL,
                  "<id> linear|offset <val> | <id> reset — calibracao (Propeller/HAL_ADC direto)",
                  cmd_sensor, 3, 1),
    SHELL_CMD(batt, &sub_batt, "comandos da bateria (Battery_Manager direto)", NULL),
    SHELL_CMD(prop, &sub_prop, "comandos do Propeller_Manager", NULL),
    SHELL_CMD(i2c, &sub_i2c, "I2C1 (PB8/PB9) — scan/read/write crus (HAL_I2C direto)", NULL),
    SHELL_CMD_ARG(dac, NULL,
                  "<0-4095> — DAC1 canal 1 / PA5 (HAL_DAC direto)", cmd_dac, 2, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(siba, &sub_siba, "Comandos de teste do SIBA — Application -> Sys/HAL direto", NULL);
