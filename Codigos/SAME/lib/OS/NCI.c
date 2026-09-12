/**
 * @file NCI.c
 * @brief Camada de rede da SAME: TX de telemetria e RX de CMD por UDP.
 */
#include "NCI.h"
#include "OSSI.h"
#include "CMD.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <string.h>

#include "Security_Manager.h"
#if defined(CONFIG_SAME_NET_STATUS_UART)
#include "Log_Manager.h"
#endif

LOG_MODULE_REGISTER(nci_core, LOG_LEVEL_DBG);

/* Portas do protocolo SAME (fonte: Aplicacao_Pi/Config/SAME.yaml, bloco
 * `link`): a placa ESCUTA CMDs em board_rx=4444; o PC ESCUTA a telemetria em
 * pc_rx=4445 (para onde a placa TRANSMITE). */
#define LOCAL_PORT  4444   /* board_rx — escuta CMD */
#define PEER_PORT   4445   /* pc_rx    — envia telemetria */

#define NCI_RX_STACK_SIZE  2048
#define NCI_RX_PRIORITY       5

static bool is_network_connected;
static int  tx_sock = -1;
static struct sockaddr_in peer_addr;

K_THREAD_STACK_DEFINE(nci_rx_stack, NCI_RX_STACK_SIZE);
static struct k_thread nci_rx_thread_data;

/* Callback net_mgmt: atualiza is_network_connected automaticamente quando
 * o PHY sobe (NET_EVENT_IF_UP) ou cai (NET_EVENT_IF_DOWN). */
static struct net_mgmt_event_callback nci_mgmt_cb;

static void nci_net_event_cb(struct net_mgmt_event_callback *cb,
                              uint64_t mgmt_event, struct net_if *iface)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(iface);
    if (mgmt_event == NET_EVENT_IF_UP) {
        is_network_connected = true;
        Security_Manager_Error_Set(ERROR_SYS_NET_DISCONNECTED, false);
        LOG_INF("NCI: link UP → TX habilitado");
    } else if (mgmt_event == NET_EVENT_IF_DOWN) {
        is_network_connected = false;
        Security_Manager_Error_Set(ERROR_SYS_NET_DISCONNECTED, true);
        LOG_WRN("NCI: link DOWN → TX pausado");
    }
}

/**
 * @brief Override manual do estado de conexão (útil em testes ou native_sim).
 * No hardware, o estado é gerido automaticamente pelo callback net_mgmt.
 */
void NCI_Set_Connection_Status(bool status)
{
    is_network_connected = status;
    Security_Manager_Error_Set(ERROR_SYS_NET_DISCONNECTED, !status);
    LOG_INF("NCI: conexão (override) %s", status ? "ONLINE" : "OFFLINE");
}

/**
 * @brief Envia um buffer por UDP pro peer configurado (pc_rx), se online.
 * @param buf dados a enviar (tipicamente TELEMETRY_LOG)
 * @param len tamanho de buf em bytes
 */
void NCI_Transmit(const void *buf, size_t len)
{
    if (!is_network_connected || tx_sock < 0) return;

    ssize_t sent = zsock_sendto(tx_sock, buf, len, 0,
                                (struct sockaddr *)&peer_addr,
                                sizeof(peer_addr));
    if (sent < 0) {
        LOG_DBG("NCI TX: sendto errno=%d", errno);
    }
}

/**
 * @brief Thread RX: bloqueia em recvfrom (evento "CMD recebido") e submete
 * o pacote ao same_wq via OSSI_Submit_CMD. Nunca retorna.
 * @param p1, p2, p3 não usados (assinatura exigida por k_thread_create)
 */
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

#if defined(CONFIG_SAME_NET_STATUS_UART)
static void net_status_work_fn(struct k_work *work)
{
    ARG_UNUSED(work);
    TELEMETRY_LOG snap = Log_Manager_Get_Snapshot();
    uint32_t err = Security_Manager_Get_Error_Code();

    bool ready = Security_Manager_Is_MCU_Ready();
    printk("=== SAME status [t=%u] mcu_ready=%s ===\n",
           (unsigned)snap.timestamp, ready ? "SIM" : "NAO");
    printk("  rede:    %s  (sock=%d)\n",
           is_network_connected ? "ONLINE" : "OFFLINE", tx_sock);
    printk("  IP:      %s\n", CONFIG_NET_CONFIG_MY_IPV4_ADDR);
    printk("  V_abc:   %.3f  %.3f  %.3f  V\n",
           (double)snap.V_a, (double)snap.V_b, (double)snap.V_c);
    printk("  i_abc:   %.3f  %.3f  %.3f  A\n",
           (double)snap.i_a, (double)snap.i_b, (double)snap.i_c);
    printk("  V_ab:    %.3f  %.3f  V\n",
           (double)snap.V_alpha, (double)snap.V_beta);
    printk("  i_ab:    %.3f  %.3f  A\n",
           (double)snap.i_alpha, (double)snap.i_beta);
    printk("  rpm:     %.1f\n", (double)snap.rpm_same);
    printk("  Te:      %.4f N.m\n", (double)snap.torque_eletrico);
    printk("  Th:      %.4f N.m\n", (double)snap.torque_helice);
    printk("  status:  0x%02X   erros: 0x%08X\n",
           (unsigned)snap.status, err);
    printk("=========================\n");
}

static K_WORK_DEFINE(net_status_work, net_status_work_fn);

static void net_status_timer_fn(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_work_submit_to_queue(&same_wq, &net_status_work);
}

static K_TIMER_DEFINE(net_status_timer, net_status_timer_fn, NULL);
#endif /* CONFIG_SAME_NET_STATUS_UART */

/** @brief Cria o socket TX e sobe a thread RX. Chamar em main() antes do HAL. */
void NCI_Init(void)
{
    /* Callback net_mgmt: detecta PHY UP/DOWN automaticamente.
     * NET_EVENT_IF_UP: interface com IP configurado e pronta para TX.
     * NET_EVENT_IF_DOWN: link caiu, para de tentar enviar.
     * Deve ser registrado ANTES de criar o socket — evita a janela onde
     * o link sobe entre o socket pronto e o callback registrado. */
    net_mgmt_init_event_callback(&nci_mgmt_cb, nci_net_event_cb,
                                 NET_EVENT_IF_UP | NET_EVENT_IF_DOWN);
    net_mgmt_add_event_callback(&nci_mgmt_cb);

    /* Rede inicia como desconectada até o callback confirmar. */
    Security_Manager_Error_Set(ERROR_SYS_NET_DISCONNECTED, true);

    /* Verifica se a interface já está up (pode acontecer no native_sim). */
    struct net_if *iface = net_if_get_default();
    if (iface && net_if_is_up(iface)) {
        is_network_connected = true;
        Security_Manager_Error_Set(ERROR_SYS_NET_DISCONNECTED, false);
        LOG_INF("NCI: interface já UP ao iniciar");
    }

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

    LOG_INF("NCI_Init: socket TX criado, thread RX arrancada (aguardando link)");

#if defined(CONFIG_SAME_NET_STATUS_UART)
    k_timer_start(&net_status_timer, K_MSEC(5000), K_MSEC(5000));
    LOG_INF("NCI: status UART habilitado (5000 ms)");
#endif
}
