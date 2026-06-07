#include "NCI.h"
#include "CMD.h"
#include "Log_Manager.h"
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <string.h>

LOG_MODULE_REGISTER(nci_core, LOG_LEVEL_DBG);

#define PEER_PORT   5555
#define LOCAL_PORT  5000

static bool is_network_connected = false;

/* ---------------------------------------------------------------
 * NCI TX — consome fila_nci e envia TELEMETRY_LOG binário via UDP
 * --------------------------------------------------------------- */
void nci_tx_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    ossi_msg_t msg;
    int sock;
    struct sockaddr_in peer;

    LOG_INF("NCI TX iniciada");

    sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { LOG_ERR("NCI TX: socket falhou %d", errno); return; }

    peer.sin_family = AF_INET;
    peer.sin_port   = htons(PEER_PORT);
    zsock_inet_pton(AF_INET, CONFIG_NET_CONFIG_PEER_IPV4_ADDR, &peer.sin_addr);

    while (1) {
        if (k_msgq_get(&fila_nci, &msg, K_FOREVER) != 0) continue;
        if (!is_network_connected) continue;
        if (msg.target != TARGET_NETWORK) continue;

        /* payload.raw contém TELEMETRY_LOG serializado pelo Log_Manager */
        ssize_t sent = zsock_sendto(sock,
                                    msg.payload.raw,
                                    sizeof(TELEMETRY_LOG),
                                    0,
                                    (struct sockaddr *)&peer,
                                    sizeof(peer));
        if (sent < 0) {
            LOG_ERR("NCI TX: sendto falhou %d", errno);
        } else {
            LOG_DBG("NCI TX: %d bytes enviados", (int)sent);
        }
    }
}

/* ---------------------------------------------------------------
 * NCI RX — escuta UDP, desserializa CMD e injeta no OSSI→CMD
 * --------------------------------------------------------------- */
void nci_rx_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    int sock;
    struct sockaddr_in bind_addr, client;
    socklen_t client_len = sizeof(client);
    uint8_t rx_buf[sizeof(CMD) + 4];  /* CMD + margem */

    LOG_INF("NCI RX iniciada, escutando porta %d", LOCAL_PORT);

    sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { LOG_ERR("NCI RX: socket falhou %d", errno); return; }

    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port        = htons(LOCAL_PORT);

    if (zsock_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("NCI RX: bind falhou %d", errno);
        return;
    }

    while (1) {
        ssize_t received = zsock_recvfrom(sock, rx_buf, sizeof(rx_buf),
                                          0, (struct sockaddr *)&client,
                                          &client_len);
        if (received < 0) { LOG_ERR("NCI RX: recvfrom %d", errno); continue; }
        if (!is_network_connected) continue;
        if (received < (ssize_t)sizeof(CMD)) {
            LOG_WRN("NCI RX: pacote curto %d bytes", (int)received);
            continue;
        }

        LOG_DBG("NCI RX: %d bytes recebidos", (int)received);

        ossi_msg_t rx_msg = {
            .target   = TARGET_CMD,
            .req_type = REQ_TYPE_CMD_PARSE,
        };
        size_t copy = MIN((size_t)received, sizeof(rx_msg.payload.cmd_req.buffer));
        memcpy(rx_msg.payload.cmd_req.buffer, rx_buf, copy);
        rx_msg.payload.cmd_req.len = copy;

        if (OSSI_Send(&rx_msg) != 0) {
            LOG_ERR("NCI RX: OSSI cheio, CMD descartado");
        }
    }
}

/* ---------------------------------------------------------------
 * Controle e inicialização
 * --------------------------------------------------------------- */
void NCI_Set_Connection_Status(bool status)
{
    is_network_connected = status;
    LOG_INF("NCI: conexão %s", status ? "ONLINE" : "OFFLINE");
}

void NCI_Init(void)
{
    LOG_INF("NCI_Init: criando threads TX e RX via TCKEC");

    ossi_msg_t req = {
        .target   = TARGET_TCKEC,
        .req_type = REQ_TYPE_TCKEC_CREATE_THREAD,
    };

    req.payload.tckec_req.thread_id   = NCI_TX_THREAD_ID;
    req.payload.tckec_req.entry_point = nci_tx_thread;
    req.payload.tckec_req.priority    = NCI_TX_PRIORITY;
    req.payload.tckec_req.p1 = NULL;
    req.payload.tckec_req.p2 = NULL;
    req.payload.tckec_req.p3 = NULL;
    OSSI_Send(&req);

    req.payload.tckec_req.thread_id   = NCI_RX_THREAD_ID;
    req.payload.tckec_req.entry_point = nci_rx_thread;
    req.payload.tckec_req.priority    = NCI_RX_PRIORITY;
    OSSI_Send(&req);
}
