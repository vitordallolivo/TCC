#include <zephyr/logging/log.h>
#include "OSSI.h"

LOG_MODULE_REGISTER(ossi_router, LOG_LEVEL_DBG);

/* * Definição das filas do OSSI. 
 * O tamanho da fila (ex: 20 itens) pode ser ajustado conforme o uso de RAM.
 */
K_MSGQ_DEFINE(fila_cmd, sizeof(ossi_msg_t), 20, 4);    // 
K_MSGQ_DEFINE(fila_tckec, sizeof(ossi_msg_t), 20, 4);  // 
K_MSGQ_DEFINE(fila_sys, sizeof(ossi_msg_t), 20, 4);    // 
K_MSGQ_DEFINE(fila_nci, sizeof(ossi_msg_t), 20, 4);    // 

/*
 * Implementação do OSSI Router
 * Recebe a mensagem e a coloca na fila correta baseada no TARGET.
 */
int OSSI_Send(ossi_msg_t *msg) {
    int ret = 0;

    if (msg == NULL) {
        LOG_ERR("OSSI_Send falhou: Mensagem nula.");
        return -EINVAL;
    }

    /* Roteador OSSI  */
    switch (msg->target) {
        case TARGET_CMD:
            ret = k_msgq_put(&fila_cmd, msg, K_NO_WAIT);
            if (ret != 0) LOG_WRN("Fila CMD cheia! Msg descartada.");
            break;

        case TARGET_TCKEC:
            ret = k_msgq_put(&fila_tckec, msg, K_NO_WAIT);
            if (ret != 0) LOG_WRN("Fila VCORE cheia! Msg descartada.");
            break;

        case TARGET_SYS:
            ret = k_msgq_put(&fila_sys, msg, K_NO_WAIT);
            if (ret != 0) LOG_WRN("Fila SYS cheia! Msg descartada.");
            break;

        case TARGET_NETWORK:
            ret = k_msgq_put(&fila_nci, msg, K_NO_WAIT);
            if (ret != 0) LOG_WRN("Fila NCI cheia! Msg descartada.");
            break;

        default:
            LOG_ERR("OSSI Router: Target desconhecido (%d).", msg->target);
            ret = -ENOTSUP;
            break;
    }

    if (ret == 0) {
        // LOG_DBG("Msg roteada com sucesso para target %d", msg->target);
    }

    return ret;
}