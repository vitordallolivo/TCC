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
#include "NVM.h"

#include <zephyr/drivers/i2c.h>

#define NVM_I2C_ADDR  0x50U

LOG_MODULE_REGISTER(same_main, LOG_LEVEL_INF);

/**
 * @brief Inicializa OSSI/Observer/Log/CMD_Core/NCI/HAL nessa ordem e habilita
 * o tráfego de rede. HAL_Init falhando não impede o boot (log de erro só).
 * @return 0 sempre (Zephyr chama main() uma vez e segue no scheduler)
 */
int main(void)
{
    printk("[SAME] main() atingido\n");

    printk("[SAME] === SAME Board — no de aquisicao eletrica do motor ===\n");

    printk("[SAME] Observer_Init...\n");
    Observer_Init();

    printk("[SAME] Security_Manager_Init...\n");
    Security_Manager_Init();

    printk("[SAME] Log_Manager_Init...\n");
    Log_Manager_Init();

    printk("[SAME] CMD_Core_Init...\n");
    CMD_Core_Init();

    /* 1. Defaults de calib no array interno do HAL (sem ligar ADC nem WQ). */
    HAL_Calib_Init_Defaults();

    /* 2. EEPROM primeiro, sem nenhuma thread/worker concorrente ainda.
     *    Aplica calibracao e params do observador salvos antes de ligar
     *    o ADC ou subir a work queue. */
    printk("[SAME] NVM_Init...\n");
    {
        const struct device *nvm_i2c = DEVICE_DT_GET(DT_NODELABEL(i2c2));
        if (NVM_Init(nvm_i2c, NVM_I2C_ADDR) != 0) {
            LOG_WRN("NVM_Init falhou — calibracao/observador com defaults");
        }
    }

    /* 3. Work queue sobe so depois do NVM: garante que o I2C do boot
     *    nao compete com nenhum worker. */
    printk("[SAME] OSSI_Init...\n");
    OSSI_Init();

    /* 4. Ethernet apos a WQ estar pronta (NCI thread chama OSSI_Submit_CMD). */
    printk("[SAME] NCI_Init...\n");
    NCI_Init();

    /* 5. ADC por ultimo: so comeca a gerar dados quando tudo esta pronto. */
    printk("[SAME] HAL_Init...\n");
    if (HAL_Init() != 0) {
        LOG_ERR("HAL_Init falhou — sistema degradado");
    }

    /* NCI_Set_Connection_Status não é mais chamado aqui: NCI.c registra
     * um callback net_mgmt que detecta o UP/DOWN do PHY automaticamente. */

    LOG_INF("=== SAME pronto ===");
    printk("[SAME] boot completo\n");
    return 0;
}
