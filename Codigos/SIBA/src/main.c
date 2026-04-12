#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include "OSSI.h"
#include "NCI.h"

LOG_MODULE_REGISTER(siba_main, LOG_LEVEL_DBG);

/* (Mantenha sua tckec_thread como estava) */
#define TCKEC_STACK_SIZE 1024
#define TCKEC_PRIORITY   -1 
K_THREAD_STACK_DEFINE(tckec_stack, TCKEC_STACK_SIZE);
struct k_thread tckec_thread_data;

void tckec_thread(void *p1, void *p2, void *p3) {
    ossi_msg_t msg;
    while (1) {
        if (k_msgq_get(&fila_vcore, &msg, K_FOREVER) == 0) {
            // Processamento TCKEC omitido por brevidade
        }
    }
}

int main(void) {
    LOG_INF("--- Iniciando SIBA Board Network Test ---");

    /* Inicializa TCKEC e NCI */
    k_thread_create(&tckec_thread_data, tckec_stack, K_THREAD_STACK_SIZEOF(tckec_stack),
                    tckec_thread, NULL, NULL, NULL, TCKEC_PRIORITY, 0, K_NO_WAIT);
    NCI_Init(); 

    /* Define a rede como conectada */
    NCI_Set_Connection_Status(true);
    
    // Dá um tempo para as threads inicializarem e o socket fazer o bind
    k_sleep(K_MSEC(1000)); 

    uint32_t contador_telemetria = 0;

    /* Loop Infinito de Teste - Enviando a cada 1 segundo */
    while (1) {
        LOG_INF("Gerando novo pacote de telemetria...");

        ossi_msg_t telemetria_msg = {
            .target = TARGET_NETWORK,
            .req_type = REQ_TYPE_NET_SEND_TELEMETRY
        };

        /* Injetamos um texto legível no payload apenas para testes no PC */
        snprintf((char *)telemetria_msg.payload.raw, sizeof(telemetria_msg.payload.raw), 
                 "SIBA_TELEMETRIA_PKT:%d", contador_telemetria);

        /* Envia para o OSSI Router */
        OSSI_Send(&telemetria_msg);

        contador_telemetria++;

        /* Aguarda 1 segundo antes do próximo envio */
        k_msleep(1);
    }
    
    return 0;
}