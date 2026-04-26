#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include "NCI.h"

LOG_MODULE_REGISTER(nci_core, LOG_LEVEL_DBG);

/* Porta UDP que o PC estará escutando para telemetria */
#define PEER_PORT 5555 
/* Porta UDP que a PLACA vai escutar para receber comandos do PC */
#define LOCAL_PORT 5000 

/* IDs lógicos arbitrários para o TCKEC gerenciar (ajuste conforme sua arquitetura) */
#define NCI_TX_THREAD_ID 10
#define NCI_RX_THREAD_ID 11

static bool is_network_connected = false;

/* -------------------------------------------------------------
 * NCI TX THREAD (Sleeper na Fila OSSI)
 * ------------------------------------------------------------- */
void nci_tx_thread(void *p1, void *p2, void *p3) {

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    ossi_msg_t msg;
    int sock;
    struct sockaddr_in peer_addr;
    
    LOG_INF("NCI TX Iniciada. Configurando Socket UDP de Saída...");

    sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Falha ao criar socket UDP TX: %d", errno);
        return;
    }

    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(PEER_PORT)5555
    zsock_inet_pton(AF_INET, CONFIG_NET_CONFIG_PEER_IPV4_ADDR, &peer_addr.sin_addr);

    while (1) {
        if (k_msgq_get(&fila_nci, &msg, K_FOREVER) == 0) {
            
            if (!is_network_connected) continue;

            if (msg.target == TARGET_NETWORK) {
                ssize_t sent = zsock_sendto(sock, msg.payload.raw, 32, 0, 
                                            (struct sockaddr *)&peer_addr, sizeof(peer_addr));
                if (sent < 0) {
                    LOG_ERR("Erro ao enviar UDP: %d", errno);
                } else {
                    LOG_INF("NCI TX: -> [%d bytes enviados via UDP]", (int)sent);
                }
            }
        }
    }
}

/* -------------------------------------------------------------
 * NCI RX THREAD (Sleeper no Socket)
 * ------------------------------------------------------------- */
void nci_rx_thread(void *p1, void *p2, void *p3) {

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int sock;
    struct sockaddr_in bind_addr;
    uint8_t rx_buffer[64]; 

    LOG_INF("NCI RX Iniciada. Configurando Socket UDP de Escuta...");

    sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Falha ao criar socket UDP RX: %d", errno);
        return;
    }

    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    bind_addr.sin_port = htons(LOCAL_PORT);

    if (zsock_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("Falha no bind do socket RX: %d", errno);
        return;
    }

    LOG_INF("NCI RX escutando na porta %d. Entrando em Sleep (aguardando rede)...", LOCAL_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        ssize_t received = zsock_recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0,
                                          (struct sockaddr *)&client_addr, &client_addr_len);

        if (received < 0) {
            LOG_ERR("Erro no zsock_recvfrom: %d", errno);
            continue;
        }

        if (!is_network_connected) {
            LOG_WRN("NCI RX: Pacote ignorado pois o sistema está marcado como offline.");
            continue;
        }

        LOG_INF("NCI RX: Recebeu Pacote UDP [%d bytes]. Injetando na OSSI -> CMD Core...", (int)received);

        ossi_msg_t rx_msg = {
            .target = TARGET_CMD,
            .req_type = REQ_TYPE_CMD_PARSE
        };

        size_t copy_len = (received < sizeof(rx_msg.payload.cmd_req.buffer)) ? received : sizeof(rx_msg.payload.cmd_req.buffer);
        memcpy(rx_msg.payload.cmd_req.buffer, rx_buffer, copy_len);
        rx_msg.payload.cmd_req.len = copy_len;

        if (OSSI_Send(&rx_msg) != 0) {
            LOG_ERR("NCI RX: Falha ao enviar para o OSSI Router.");
        }
    }
}

/* -------------------------------------------------------------
 * FUNÇÕES DE CONTROLE E INICIALIZAÇÃO
 * ------------------------------------------------------------- */
void NCI_Set_Connection_Status(bool status) {
    is_network_connected = status;
}

void NCI_Init(void) {
    LOG_INF("Solicitando ao TCKEC a criacao das threads da NCI...");

    ossi_msg_t req_tx = {
        .target = TARGET_TCKEC,
        .req_type = REQ_TYPE_TCKEC_CREATE_THREAD
    };
    req_tx.payload.tckec_req.thread_id = NCI_TX_THREAD_ID;
    req_tx.payload.tckec_req.entry_point = nci_tx_thread;
    req_tx.payload.tckec_req.priority = NCI_TX_PRIORITY; // Assumindo que isso está no nci.h
    req_tx.payload.tckec_req.p1 = NULL;
    req_tx.payload.tckec_req.p2 = NULL;
    req_tx.payload.tckec_req.p3 = NULL;

    if (OSSI_Send(&req_tx) != 0) {
        LOG_ERR("Falha ao enviar request de criacao para NCI TX.");
    }

    ossi_msg_t req_rx = {
        .target = TARGET_TCKEC,
        .req_type = REQ_TYPE_TCKEC_CREATE_THREAD
    };
    req_rx.payload.tckec_req.thread_id = NCI_RX_THREAD_ID;
    req_rx.payload.tckec_req.entry_point = nci_rx_thread;
    req_rx.payload.tckec_req.priority = NCI_RX_PRIORITY; // Assumindo que isso está no nci.h
    req_rx.payload.tckec_req.p1 = NULL;
    req_rx.payload.tckec_req.p2 = NULL;
    req_rx.payload.tckec_req.p3 = NULL;

    if (OSSI_Send(&req_rx) != 0) {
        LOG_ERR("Falha ao enviar request de criacao para NCI RX.");
    }
}