#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include "OSSI.h"
#include "NCI.h"
#include "TCKEC.h"

LOG_MODULE_REGISTER(siba_main, LOG_LEVEL_DBG);


#define MOCK_CMD_THREAD_ID 20


void mock_cmd_thread(void *p1, void *p2, void *p3) {

    ossi_msg_t msg;
    LOG_INF("MOCK CMD Iniciado. Aguardando comandos na fila_cmd...");

    while (1) {
        if (k_msgq_get(&fila_cmd, &msg, K_FOREVER) == 0) {
            LOG_INF("MOCK CMD: Recebido pacote via OSSI RX: %s [%d bytes].",msg.payload.raw, msg.payload.cmd_req.len);
        }
    }
}

int main(void) {
    LOG_INF("\x1b[92m\n\n\n\n\n\n--- Iniciando SIBA Board Teste NCI/OSSI ---\n\n\n\n\n\n\x1b[0m");

    TCKEC_Init();

    NCI_Init(); 

    /* Solicita ao TCKEC a criação do MOCK CMD */
    ossi_msg_t req_cmd = {
        .target = TARGET_TCKEC,
        .req_type = REQ_TYPE_TCKEC_CREATE_THREAD
    };
    req_cmd.payload.tckec_req.thread_id = MOCK_CMD_THREAD_ID;
    req_cmd.payload.tckec_req.entry_point = mock_cmd_thread;
    req_cmd.payload.tckec_req.priority = 7;
    OSSI_Send(&req_cmd);

    NCI_Set_Connection_Status(true);

    uint32_t counter = 0;

    /* Loop de envio periódico para testar NCI TX */
    while (1) {
        k_sleep(K_SECONDS(2));

        ossi_msg_t tx_msg = {
            .target = TARGET_NETWORK,
            .req_type = REQ_TYPE_NET_SEND_TELEMETRY
        };
        snprintf((char *)tx_msg.payload.raw, 32, "TESTE_UDP_TX:%d", counter++);

        OSSI_Send(&tx_msg);
    }

    return 0;
}

