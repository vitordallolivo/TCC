/**
 * @file main.c
 * @brief Ponto de entrada do firmware da SAME: ordem de inicialização dos
 * subsistemas.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* OS */
#include "OSSI.h"
#include "NCI.h"
#include "HAL.h"
#include "CMD_Core.h"

/* SYS */
#include "Log_Manager.h"
#include "Observer_Manager.h"
#include "Security_Manager.h"

LOG_MODULE_REGISTER(same_main, LOG_LEVEL_INF);

/**
 * @brief Inicializa OSSI/Observer/Log/CMD_Core/NCI/HAL nessa ordem e habilita
 * o tráfego de rede. HAL_Init falhando não impede o boot (log de erro só).
 * @return 0 sempre (Zephyr chama main() uma vez e segue no scheduler)
 */
int main(void)
{
    LOG_INF("=== SAME Board — nó de aquisição elétrica do motor ===");

    /* 1. OSSI — arranca a same_wq (todo o despacho por eventos depende dela) */
    OSSI_Init();

    /* 2. Observer — defaults do SMO/PLL antes de qualquer amostra */
    Observer_Init();

    /* 2b. Security_Manager — zera o código de erro agregado (diagnóstico) */
    Security_Manager_Init();

    /* 3. Log_Manager — zera o log e arranca o timer de TX (10 Hz) */
    Log_Manager_Init();

    /* 4. CMD_Core — tabela de calibração dos sensores (currents/voltages) */
    CMD_Core_Init();

    /* 5. NCI — socket TX + thread RX (escuta CMD na porta 4444) */
    NCI_Init();

    /* 6. HAL — configura os 2 ADCs e arranca a thread de aquisição, que roda
     *    o observador colado ao evento de conversão e publica a telemetria */
    if (HAL_Init() != 0) {
        LOG_ERR("HAL_Init falhou — sistema degradado");
    }

    /* Habilita o tráfego de rede (TX telemetria / RX CMD) */
    NCI_Set_Connection_Status(true);

    LOG_INF("=== SAME pronto ===");
    return 0;
}
