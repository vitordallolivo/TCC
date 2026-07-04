/*
 * Telemetria / log UDP — streaming binario das amostras (n, omega, Te, dq,
 * fases) para o PC. Pacote = cabecalho (count, ts_us) + lote de amostras,
 * empacotado ate ~MTU. Little-endian, sem padding (ver spmm_pkt_hdr_t).
 *
 * BASE DE TEMPO: cada amostra carrega o contador `n`; o PC reconstroi
 * t = n * Ts (Ts vem daqui, nominal do rate configurado), NUNCA o horario
 * de chegada do pacote UDP. Saltos em `n` -> perda/reordenacao detectavel.
 *
 * Prioridade BAIXA/MEDIA — nao pode preemptar a aquisicao. Em native_sim
 * os sockets sao "offloaded" (NSOS) direto pro host: o Python recebe UDP de
 * verdade do binario rodando no PC.
 */
#include "spmm.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <string.h>

LOG_MODULE_REGISTER(telemetry, LOG_LEVEL_INF);

#define TELE_STACK_SIZE 2048
#define TELE_PRIORITY   7           /* BAIXA — abaixo de acq(4)/rpm(6)         */
#define TELE_BATCH_MAX  20          /* 4 + 20*48 = 964 bytes < MTU             */

static K_THREAD_STACK_DEFINE(tele_stack, TELE_STACK_SIZE);
static struct k_thread tele_thread;
static struct k_sem    tele_wake;   /* acorda a thread quando o log inicia     */

static volatile bool     log_active;
static volatile uint32_t log_ts_us;    /* periodo de amostragem (0 = parado)   */
static uint32_t          sample_n;

/* Buffer do pacote: cabecalho + lote de amostras */
static uint8_t pkt_buf[sizeof(spmm_pkt_hdr_t) + TELE_BATCH_MAX * sizeof(spmm_sample_t)];

uint32_t telemetry_get_ts_us(void) { return log_ts_us; }

void telemetry_start_log(uint32_t hz)
{
    if (hz == 0) hz = 1;
    log_ts_us = 1000000U / hz;
    sample_n  = 0;
    log_active = true;
    k_sem_give(&tele_wake);
    LOG_INF("LOG: iniciado @ %u Hz (Ts=%u us)", hz, log_ts_us);
}

void telemetry_stop_log(void)
{
    log_active = false;
    log_ts_us  = 0;
    LOG_INF("LOG: parado (%u amostras)", sample_n);
}

static void fill_sample(spmm_sample_t *s)
{
    spmm_meas_t m;
    spmm_get_meas(&m);
    s->n       = sample_n++;
    s->omega   = spmm_get_omega();
    s->iq      = m.iq;
    s->id      = m.id;
    s->te      = SPMM_KT * m.iq;
    s->theta_e = m.theta_e;
    s->ia = m.ia; s->ib = m.ib; s->ic = m.ic;
    s->va = m.va; s->vb = m.vb; s->vc = m.vc;
}

static void tele_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { LOG_ERR("LOG: socket falhou %d", errno); return; }

    struct sockaddr_in peer = {
        .sin_family = AF_INET,
        .sin_port   = htons(SPMM_UDP_LOG_PORT),
    };
    zsock_inet_pton(AF_INET, CONFIG_NET_CONFIG_PEER_IPV4_ADDR, &peer.sin_addr);

    LOG_INF("LOG: thread pronta (peer %s:%u)",
            CONFIG_NET_CONFIG_PEER_IPV4_ADDR, SPMM_UDP_LOG_PORT);

    while (1) {
        if (!log_active) {
            k_sem_take(&tele_wake, K_FOREVER);   /* dorme ate log start        */
            continue;
        }

        spmm_pkt_hdr_t *hdr = (spmm_pkt_hdr_t *)pkt_buf;
        uint16_t count = 0;

        /* Junta um lote de amostras respeitando o periodo Ts entre elas */
        while (log_active && count < TELE_BATCH_MAX) {
            fill_sample(&hdr->s[count]);
            count++;
            if (log_ts_us) {
                k_sleep(K_USEC(log_ts_us));
            }
        }
        if (count == 0) continue;

        hdr->count = count;
        hdr->ts_us = (uint16_t)log_ts_us;
        size_t len = sizeof(spmm_pkt_hdr_t) + count * sizeof(spmm_sample_t);

        ssize_t sent = zsock_sendto(sock, pkt_buf, len, 0,
                                    (struct sockaddr *)&peer, sizeof(peer));
        if (sent < 0) {
            LOG_WRN("LOG: sendto falhou %d", errno);
        }
    }
}

int telemetry_init(void)
{
    k_sem_init(&tele_wake, 0, 1);
    k_thread_create(&tele_thread, tele_stack, TELE_STACK_SIZE,
                    tele_thread_fn, NULL, NULL, NULL,
                    TELE_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&tele_thread, "telemetry");
    return 0;
}
