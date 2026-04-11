#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "main.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

int main(void)
{
    printk("SIBA iniciando...\n");
    siba_init();

    k_msleep(5000);

    uint32_t counter = 0;
    char payload[32];

    while (1) {
        snprintf(payload, sizeof(payload), "SIBA_%u", counter++);
        nci_err_t err = nci_send((const uint8_t *)payload, strlen(payload), 0x01);
        if (err == NCI_OK) {
            LOG_INF("TX: %s", payload);
        } else {
            LOG_WRN("Falha TX: %d", err);
        }
        k_msleep(1000);
    }

    return 0;
}