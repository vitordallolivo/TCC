#include "NCI.h"
#include "OSSI.h"
#include "CMD.h"
#include "Log_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <string.h>

LOG_MODULE_REGISTER(nci_core, LOG_LEVEL_DBG);

#define PEER_PORT   5555
#define LOCAL_PORT  5000
#define NCI_RX_STACK_SIZE  2048
#define NCI_RX_PRIORITY       5

static bool is_network_connected;
static int  tx_sock = -1;
static struct sockaddr_in peer_addr;

K_THREAD_STACK_DEFINE(nci_rx_stack, NCI_RX_STACK_SIZE);
static struct k_thread nci_rx_thread_data;

void NCI_Set_Connection_Status(bool status)
{
    is_network_connected = status;
    LOG_INF("NCI: conexão %s", status ? "ONLINE" : "OFFLINE");
}

void NCI_Transmit(const void *buf, size_t len)
{
    if (!is_network_connected || tx_sock < 0) return;

    ssize_t sent = zsock_sendto(tx_sock, buf, len, 0,
                                (struct sockaddr *)&peer_addr,
                                sizeof(peer_addr));
    if (sent < 0) {
        /* sendto falhou — silencioso em produção, só log debug */
        LOG_DBG("NCI TX: sendto errno=%d", errno);
    }
}

/* NCI RX — bloqueia em recvfrom e submete CMDs recebidos ao siba_wq */
static void nci_rx_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    int sock;
    struct sockaddr_in bind_addr, client;
    socklen_t client_len = sizeof(client);
    uint8_t rx_buf[sizeof(CMD) + 4];

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
        OSSI_Submit_CMD(rx_buf, (size_t)received);
    }
}

void NCI_Init(void)
{
    /* Socket TX — criado aqui, reutilizado por NCI_Transmit() */
    tx_sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (tx_sock < 0) {
        LOG_ERR("NCI TX: socket falhou %d", errno);
    } else {
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port   = htons(PEER_PORT);
        zsock_inet_pton(AF_INET, CONFIG_NET_CONFIG_PEER_IPV4_ADDR,
                        &peer_addr.sin_addr);
    }

    /* Thread RX — única thread de I/O bloqueante do NCI */
    k_thread_create(&nci_rx_thread_data, nci_rx_stack,
                    K_THREAD_STACK_SIZEOF(nci_rx_stack),
                    nci_rx_thread_fn, NULL, NULL, NULL,
                    NCI_RX_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&nci_rx_thread_data, "nci_rx");

    LOG_INF("NCI_Init: socket TX criado, thread RX arrancada");
}
