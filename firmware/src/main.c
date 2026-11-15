/**
 * @file main.c
 * @brief Ponto de entrada do firmware da SIBA: ordem de inicialização dos
 * subsistemas.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>

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
#include "NVM.h"
#include "Observer_Manager.h"
/* Test_Shell.c registra comandos no Zephyr Shell estaticamente — basta estar
 * linkado (ver lib/SYS/Test_Shell.c), nenhum include/init aqui é necessário */

LOG_MODULE_REGISTER(siba_main, LOG_LEVEL_INF);

/* I2C2 (PB10=SCL, PB11=SDA) — EEPROM FT24C32A em 0x50. */
#define SIBA_I2C_NODE   DT_NODELABEL(i2c2)
#define SIBA_EEPROM_ADDR  0x50U

/**
 * @brief Inicializa todos os subsistemas em ordem e habilita o tráfego de
 * rede. HAL_Init falhando não impede o boot. NVM_Init falhando (EEPROM
 * ausente) é tolerado — defaults de fábrica permanecem ativos.
 * @return 0 sempre (Zephyr chama main() uma vez e segue no scheduler)
 */
int main(void)
{
    LOG_INF("=== SIBA Board — Iniciando ===");

    /* 1. OSSI primeiro — arranca siba_wq (toda a lógica de despacho depende dele) */
    OSSI_Init();

    /* 2. SYS — inicializa subsistemas antes de receber dados.
     * Security_Manager_Init seta ERROR_MCU_POWER_READY (relay aberto ao boot). */
    Security_Manager_Init();
    Battery_Manager_Init();
    Propeller_Manager_Init();
    Observer_Init();   /* SMO+PLL (vindo da SAME): defaults antes do NVM e do HAL */

    /* 3. Log_Manager — inicializa struct e arranca timer 10 Hz */
    Log_Manager_Init();

    /* 4. CMD_Core — inicializa tabela de calibração */
    CMD_Core_Init();

    /* 5. NCI — abre socket TX e arranca thread RX */
    NCI_Init();

    /* 6. HAL — ADC dual, HX711, ESC, RPM, I2C2, relay (ambos abertos ao boot).
     * (HAL_prm.h controla quais módulos realmente inicializam hardware) */
    if (HAL_Init() != 0) {
        LOG_ERR("HAL_Init falhou — sistema degradado");
    }

    /* 7. NVM — restaura calibração persistida do EEPROM I2C2 (0x50).
     * Deve vir após HAL_Init (I2C2 e HAL_ADC/HX711 já prontos).
     * Falha tolerada: defaults de CMD_Core_prm.h já estão ativos. */
/* NVM_Init comentado temporariamente — wch_i2c_finish_transfer trava em
 * busy-wait se EEPROM não responde, impedindo main() de retornar. */
#if 1
#ifndef CONFIG_ARCH_POSIX
    {
        const struct device *i2c2 = DEVICE_DT_GET(SIBA_I2C_NODE);
        if (NVM_Init(i2c2, SIBA_EEPROM_ADDR) != 0) {
            LOG_WRN("NVM: EEPROM inacessível — operando com defaults de fábrica");
        }
    }
#endif
#endif

    /* NCI_Set_Connection_Status não é chamado aqui: NCI.c registra
     * um callback net_mgmt que detecta o UP/DOWN do PHY automaticamente. */

    LOG_INF("=== SIBA pronto (relay aguardando comando GUI) ===");
    return 0;
}
