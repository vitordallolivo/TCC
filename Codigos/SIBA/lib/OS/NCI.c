#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include "NCI.h"

LOG_MODULE_REGISTER(nci_core, LOG_LEVEL_DBG);

/* Porta UDP que o PC estará escutando */
#define PEER_PORT 8080 

static bool is_network_connected = false;

K_THREAD_STACK_DEFINE(nci_tx_stack, NCI_TX_STACK_SIZE);
struct k_thread nci_tx_thread_data;

/* -------------------------------------------------------------
 * NCI TX THREAD (Sleeper) - AGORA COM SOCKETS REAIS
 * ------------------------------------------------------------- */
void nci_tx_thread(void *p1, void *p2, void *p3) {
    ossi_msg_t msg;
    int sock;
    struct sockaddr_in peer_addr;
    
    LOG_INF("NCI TX Thread Iniciada. Configurando Socket UDP...");

    /* Cria o socket UDP */
    sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Falha ao criar socket UDP: %d", errno);
        return;
    }

    /* Configura o endereço e IP de destino baseados no seu prj.conf */
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(PEER_PORT);
    zsock_inet_pton(AF_INET, CONFIG_NET_CONFIG_PEER_IPV4_ADDR, &peer_addr.sin_addr);

    LOG_INF("Socket configurado. IP Destino: %s | Porta: %d", CONFIG_NET_CONFIG_PEER_IPV4_ADDR, PEER_PORT);
    LOG_INF("Entrando em Sleep nativo (aguardando OSSI)...");

    while (1) {
        /* Bloqueia até chegar algo no OSSI Router para a NCI */
        if (k_msgq_get(&fila_nci, &msg, K_FOREVER) == 0) {
            
            if (!is_network_connected) {
                LOG_WRN("Rede Desconectada. Pacote descartado.");
                continue;
            }

            if (msg.target == TARGET_NETWORK) {
                /* Simulando que queremos enviar os primeiros bytes do payload */
                // Nota: Idealmente sua struct cmd_req/sys_req teria o dado formatado
                ssize_t sent = zsock_sendto(sock, msg.payload.raw, 32, 0, 
                                            (struct sockaddr *)&peer_addr, sizeof(peer_addr));
                
                if (sent < 0) {
                    LOG_ERR("Erro ao enviar pacote UDP: %d", errno);
                } else {
                    LOG_INF("NCI TX: -> [%d bytes enviados via UDP]", (int)sent);
                }
            }
        }
    }
}

/* -------------------------------------------------------------
 * SIMULAÇÃO DE NCI RX (Mantida para o Teste)
 * ------------------------------------------------------------- */
void NCI_RX_Simulate_Receive(uint8_t *dummy_data, size_t len) {
    if (!is_network_connected) return;
    
    ossi_msg_t rx_msg = {
        .target = TARGET_CMD,
        .req_type = REQ_TYPE_CMD_PARSE
    };
    
    size_t copy_len = (len < sizeof(rx_msg.payload.cmd_req.buffer)) ? len : sizeof(rx_msg.payload.cmd_req.buffer);
    memcpy(rx_msg.payload.cmd_req.buffer, dummy_data, copy_len);
    rx_msg.payload.cmd_req.len = copy_len;

    OSSI_Send(&rx_msg);
}

void NCI_Set_Connection_Status(bool status) {
    is_network_connected = status;
}

void NCI_Init(void) {
    k_thread_create(&nci_tx_thread_data, nci_tx_stack, K_THREAD_STACK_SIZEOF(nci_tx_stack),
                    nci_tx_thread, NULL, NULL, NULL, NCI_TX_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&nci_tx_thread_data, "NCI_TX");
}