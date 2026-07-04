/*
 * SPMM — main. Inicializa os perifericos e sobe as threads de bancada.
 *
 * Threads (prioridades importam — ver CLAUDE.md):
 *   Aquisicao   prio 4 (ALTA)   ADC1/ADC2 -> Clarke/Park -> Iq. Ts constante,
 *                               nunca preemptada pelo shell/telemetria.
 *   RPM         prio 6 (MEDIA)  input capture -> omega.
 *   Telemetria  prio 7 (BAIXA)  empacota e envia (n, omega, Te, ...) por UDP.
 *   Shell(Telnet) prio SHELL_*  (BAIXA, preemptiva) — gerida pelo subsistema.
 *
 * O shell (Telnet) e' iniciado automaticamente pelo subsistema SHELL do
 * Zephyr (CONFIG_SHELL_BACKEND_TELNET); os comandos vivem em shell_cmds.c.
 */
#include "spmm.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("=== SPMM — Sistema Provisorio de Medicao Mecanica ===");

    if (esc_init() != 0)         LOG_ERR("falha init ESC");
    if (rpm_init() != 0)         LOG_ERR("falha init RPM");
    if (acquisition_init() != 0) LOG_ERR("falha init aquisicao");
    if (telemetry_init() != 0)   LOG_ERR("falha init telemetria");

    acquisition_start();
    rpm_start();
    /* telemetry_init ja cria a thread (dorme ate 'log start') */

    LOG_INF("SPMM pronto. Shell de bancada via Telnet (porta 23).");
    return 0;
}
