/**
 * @file shell_cmds.c
 * @brief Shell de bancada (Telnet) — comandos customizados do SPMM.
 *
 * Registrados via SHELL_CMD_ARG_REGISTER:
 *   arm | disarm            sequencia de armar/desarmar o ESC
 *   throttle <us>           define o PWM do ESC (1000=parado .. 2000=max)
 *   ramp <us> <s>           rampa suave de throttle (evita degrau no ESC)
 *   rpm                     le rotacao atual
 *   iq                      le Iq medio atual
 *   read                    dump completo: rpm, omega, Id/Iq, Te, fases
 *   patamar <us> <s>        vai ao throttle, espera estabilizar, grava ponto
 *   coast                   corta throttle -> dispara coast-down (ensaio J)
 *   log start <hz> | stop   streaming binario (n, omega, Te, ...) por UDP
 */
#include "spmm.h"
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

/** @brief "arm" — arma o ESC (bloqueia ~2s). @param sh shell
 * @param argc, argv não usados @return sempre 0 */
static int cmd_arm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    shell_print(sh, "arm: armando ESC (aguarde ~2s)...");
    esc_arm();
    shell_print(sh, "arm: ok");
    return 0;
}

/** @brief "disarm" — desarma o ESC. @param sh shell
 * @param argc, argv não usados @return sempre 0 */
static int cmd_disarm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    esc_disarm();
    shell_print(sh, "disarm: ok");
    return 0;
}

/** @brief "throttle <us>" — define o PWM do ESC. @param sh shell
 * @param argc não usado @param argv argv[1]=us @return 0 ou -EPERM se
 * não armado */
static int cmd_throttle(const struct shell *sh, size_t argc, char **argv)
{
    int us = atoi(argv[1]);
    int r = esc_set_throttle_us((uint32_t)us);
    if (r == -EPERM) {
        shell_error(sh, "throttle: ESC nao armado (use 'arm')");
        return -EPERM;
    }
    shell_print(sh, "throttle = %d us", esc_get_throttle_us());
    return 0;
}

/** @brief "ramp <us> <s>" — rampa suave de throttle. @param sh shell
 * @param argc não usado @param argv argv[1]=us, argv[2]=segundos
 * @return 0 ou -EPERM se não armado */
static int cmd_ramp(const struct shell *sh, size_t argc, char **argv)
{
    int us   = atoi(argv[1]);
    int secs = atoi(argv[2]);
    shell_print(sh, "ramp: %d us em %d s ...", us, secs);
    int r = esc_ramp_us((uint32_t)us, (uint32_t)secs);
    if (r == -EPERM) {
        shell_error(sh, "ramp: ESC nao armado (use 'arm')");
        return -EPERM;
    }
    shell_print(sh, "ramp: ok, throttle = %d us", esc_get_throttle_us());
    return 0;
}

/** @brief "rpm" — imprime rotação e velocidade angular atuais.
 * @param sh shell @param argc, argv não usados @return sempre 0 */
static int cmd_rpm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    shell_print(sh, "rpm=%.1f  omega=%.3f rad/s", (double)spmm_get_rpm(),
                (double)spmm_get_omega());
    return 0;
}

/** @brief "iq" — imprime a corrente de torque média filtrada.
 * @param sh shell @param argc, argv não usados @return sempre 0 */
static int cmd_iq(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    shell_print(sh, "iq=%.4f A", (double)spmm_get_iq());
    return 0;
}

/** @brief "read" — dump completo: rpm, omega, Te, Iq, Id, theta_e e as 6
 * fases. @param sh shell @param argc, argv não usados @return sempre 0 */
static int cmd_read(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    spmm_meas_t m;
    spmm_get_meas(&m);
    /* Linha chave/valor (parseavel pelo script Python: parse_kv) */
    shell_print(sh, "rpm=%.1f omega=%.3f te=%.5f iq=%.4f id=%.4f theta_e=%.3f",
                (double)spmm_get_rpm(), (double)spmm_get_omega(),
                (double)spmm_get_te(), (double)m.iq, (double)m.id,
                (double)m.theta_e);
    shell_print(sh, "ia=%.4f ib=%.4f ic=%.4f va=%.3f vb=%.3f vc=%.3f",
                (double)m.ia, (double)m.ib, (double)m.ic,
                (double)m.va, (double)m.vb, (double)m.vc);
    return 0;
}

/** @brief "patamar <us> <s>" — aplica throttle, espera estabilizar e
 * imprime o ponto de regime (ω, Te) pro ensaio B/τc.
 * @param sh shell @param argc não usado @param argv argv[1]=us, argv[2]=segundos
 * @return 0 ou -EPERM se não armado */
static int cmd_patamar(const struct shell *sh, size_t argc, char **argv)
{
    int us   = atoi(argv[1]);
    int secs = atoi(argv[2]);
    int r = esc_set_throttle_us((uint32_t)us);
    if (r == -EPERM) {
        shell_error(sh, "patamar: ESC nao armado (use 'arm')");
        return -EPERM;
    }
    shell_print(sh, "patamar: throttle=%d us, estabilizando %d s...", us, secs);
    k_sleep(K_SECONDS(secs));
    /* Ponto de regime permanente (dw/dt=0): (omega, Te) -> regressao no PC */
    shell_print(sh, "patamar rpm=%.1f omega=%.3f te=%.5f iq=%.4f",
                (double)spmm_get_rpm(), (double)spmm_get_omega(),
                (double)spmm_get_te(), (double)spmm_get_iq());
    return 0;
}

/** @brief "coast" — corta o throttle (mínimo) pra disparar o coast-down
 * (ensaio J); o log ativo captura omega(t) do decaimento.
 * @param sh shell @param argc, argv não usados @return sempre 0 */
static int cmd_coast(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    esc_set_throttle_us(SPMM_ESC_MIN_US);
    shell_print(sh, "coast: throttle cortado (%u us) — capturando omega(t)",
                SPMM_ESC_MIN_US);
    return 0;
}

/** @brief "log start <hz> | log stop" — controla o streaming UDP.
 * @param sh shell @param argc número de argumentos
 * @param argv argv[1]="start"/"stop", argv[2]=hz (se start)
 * @return 0 em sucesso, -EINVAL em uso inválido */
static int cmd_log(const struct shell *sh, size_t argc, char **argv)
{
    if (strcmp(argv[1], "start") == 0) {
        if (argc < 3) { shell_error(sh, "uso: log start <hz>"); return -EINVAL; }
        uint32_t hz = (uint32_t)atoi(argv[2]);
        telemetry_start_log(hz);
        shell_print(sh, "log: start @ %u Hz", hz);
    } else if (strcmp(argv[1], "stop") == 0) {
        telemetry_stop_log();
        shell_print(sh, "log: stop");
    } else {
        shell_error(sh, "uso: log start <hz> | log stop");
        return -EINVAL;
    }
    return 0;
}

/* argc minimo inclui o proprio comando: throttle exige 2, ramp/patamar 3 */
SHELL_CMD_ARG_REGISTER(arm,      NULL, "Arma o ESC",                       cmd_arm,      1, 0);
SHELL_CMD_ARG_REGISTER(disarm,   NULL, "Desarma o ESC",                    cmd_disarm,   1, 0);
SHELL_CMD_ARG_REGISTER(throttle, NULL, "throttle <us> (1000-2000)",        cmd_throttle, 2, 0);
SHELL_CMD_ARG_REGISTER(ramp,     NULL, "ramp <us> <s>: rampa suave",       cmd_ramp,     3, 0);
SHELL_CMD_ARG_REGISTER(rpm,      NULL, "Le rotacao atual",                 cmd_rpm,      1, 0);
SHELL_CMD_ARG_REGISTER(iq,       NULL, "Le Iq medio atual",                cmd_iq,       1, 0);
SHELL_CMD_ARG_REGISTER(read,     NULL, "Dump: rpm, omega, Te, Iq, fases",  cmd_read,     1, 0);
SHELL_CMD_ARG_REGISTER(patamar,  NULL, "patamar <us> <s>: ponto B/tc",     cmd_patamar,  3, 0);
SHELL_CMD_ARG_REGISTER(coast,    NULL, "Corta throttle -> coast-down (J)", cmd_coast,    1, 0);
SHELL_CMD_ARG_REGISTER(log,      NULL, "log start <hz> | log stop",        cmd_log,      2, 1);
