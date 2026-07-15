/**
 * @file main.c
 * @brief SPMM — ponto de entrada: inicializa os periféricos e sobe as
 * threads de bancada.
 *
 * Threads: Aquisição (prio 4, alta), RPM (prio 6, média), Telemetria
 * (prio 7, baixa), Shell/Telnet (gerida pelo subsistema SHELL do Zephyr,
 * comandos em shell_cmds.c).
 */
#include "spmm.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/**
 * @brief Inicializa ESC/RPM/Aquisição/Telemetria e sobe as threads de
 * aquisição e RPM (telemetria já cria a sua própria em telemetry_init).
 * @return 0 sempre (Zephyr chama main() uma vez e segue no scheduler)
 */
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
