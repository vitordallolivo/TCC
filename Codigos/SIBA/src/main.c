#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* OS */
#include "TCKEC.h"
#include "OSSI.h"
#include "NCI.h"
#include "HAL.h"
#include "CMD_Core.h"

/* SYS */
#include "Sys.h"
#include "Battery_Manager.h"
#include "Propeller_Manager.h"
#include "Security_Manager.h"
#include "Log_Manager.h"
/* Test_Shell.c registra comandos no Zephyr Shell estaticamente — basta estar
 * linkado (ver lib/SYS/Test_Shell.c), nenhum include/init aqui é necessário */

LOG_MODULE_REGISTER(siba_main, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("=== SIBA Board — Iniciando ===");

    /* 1. TCKEC primeiro — gerencia todos os outros */
    TCKEC_Init();

    /* 2. SYS — inicializa subsistemas antes de receber dados */
    Security_Manager_Init();
    Battery_Manager_Init();
    Propeller_Manager_Init();

    /* 3. Log_Manager — registra thread via TCKEC */
    Log_Manager_Init();

    /* 4. System_Manager — consome fila_sys e despacha */
    Sys_Init();

    /* 5. CMD_Core — interpreta comandos da rede */
    CMD_Core_Init();

    /* 6. NCI — TX/RX UDP */
    NCI_Init();

    /* 7. HAL — ADC, HX711, ESC, RPM — registra threads via TCKEC */
    if (HAL_Init() != 0) {
        LOG_ERR("HAL_Init falhou — sistema degradado");
    }

    /* Habilita tráfego de rede */
    NCI_Set_Connection_Status(true);

    LOG_INF("=== SIBA pronto. Threads ativas. ===");

    /* main retorna — Zephyr scheduler assume o controle; as threads
     * registradas no TCKEC seguem rodando normalmente. */
    return 0;
}
