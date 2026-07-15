/**
 * @file main.c
 * @brief Ponto de entrada do firmware da SIBA: ordem de inicialização dos
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
#include "Battery_Manager.h"
#include "Propeller_Manager.h"
#include "Security_Manager.h"
#include "Log_Manager.h"
/* Test_Shell.c registra comandos no Zephyr Shell estaticamente — basta estar
 * linkado (ver lib/SYS/Test_Shell.c), nenhum include/init aqui é necessário */

LOG_MODULE_REGISTER(siba_main, LOG_LEVEL_INF);

/**
 * @brief Inicializa OSSI/managers/Log/CMD_Core/NCI/HAL nessa ordem e
 * habilita o tráfego de rede. HAL_Init falhando não impede o boot.
 * @return 0 sempre (Zephyr chama main() uma vez e segue no scheduler)
 */
int main(void)
{
    LOG_INF("=== SIBA Board — Iniciando ===");

    /* 1. OSSI primeiro — arranca siba_wq (toda a lógica de despacho depende dele) */
    OSSI_Init();

    /* 2. SYS — inicializa subsistemas antes de receber dados */
    Security_Manager_Init();
    Battery_Manager_Init();
    Propeller_Manager_Init();

    /* 3. Log_Manager — inicializa struct e arranca timer 10 Hz */
    Log_Manager_Init();

    /* 4. CMD_Core — inicializa tabela de calibração */
    CMD_Core_Init();

    /* 5. NCI — abre socket TX e arranca thread RX */
    NCI_Init();

    /* 6. HAL — ADC, HX711, ESC, RPM, I2C, DAC
     * (HAL_prm.h controla quais módulos realmente inicializam hardware) */
    if (HAL_Init() != 0) {
        LOG_ERR("HAL_Init falhou — sistema degradado");
    }

    /* Habilita tráfego de rede */
    NCI_Set_Connection_Status(true);

    LOG_INF("=== SIBA pronto ===");
    return 0;
}
