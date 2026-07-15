/**
 * @file OSSI.c
 * @brief Fila de trabalho única da SAME (same_wq): despacho de telemetria e CMD.
 */
#include "OSSI.h"
#include "Log_Manager.h"
#include "CMD_Core.h"
#include "CMD.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(ossi, LOG_LEVEL_WRN);

/* same_wq: thread única de eventos (telemetria + CMDs), prioridade menor
 * que a thread do ADC (7) — o passo do SMO nunca espera a fila. */
#define SAME_WQ_STACK_SIZE  2048
#define SAME_WQ_PRIORITY       5

K_THREAD_STACK_DEFINE(same_wq_stack, SAME_WQ_STACK_SIZE);
struct k_work_q same_wq;

/* Fila de telemetria: 32 slots absorvem uma rajada mesmo com a same_wq
 * ocupada num TX UDP. */
typedef struct { uint32_t sensor_id; float value; } sensor_item_t;

#define SENSOR_QUEUE_LEN 32
K_MSGQ_DEFINE(sensor_queue, sizeof(sensor_item_t), SENSOR_QUEUE_LEN, 4);

static struct k_work sensor_dispatch_work;

/**
 * @brief Drena sensor_queue e grava cada item no TELEMETRY_LOG. Roda na
 * same_wq, serializado com o CMD dispatch e o TX do log.
 * @param w item de trabalho (não usado)
 */
static void sensor_dispatch_fn(struct k_work *w)
{
    ARG_UNUSED(w);
    sensor_item_t item;
    while (k_msgq_get(&sensor_queue, &item, K_NO_WAIT) == 0) {
        Log_Manager_Update(item.sensor_id, item.value);
    }
}

/**
 * @brief Enfileira um valor de sensor pra despacho na same_wq. Thread-safe
 * e ISR-safe; descarta e loga se a fila estiver cheia.
 * @param sensor_id ver SENSOR_ID_* em Log_Manager.h
 * @param value valor já calculado
 */
void OSSI_Submit_Sensor(uint32_t sensor_id, float value)
{
    sensor_item_t item = { sensor_id, value };
    if (k_msgq_put(&sensor_queue, &item, K_NO_WAIT) != 0) {
        LOG_WRN("sensor_queue cheia — sensor_id %u descartado", sensor_id);
    }
    k_work_submit_to_queue(&same_wq, &sensor_dispatch_work);
}

/* ── Fila de CMDs ────────────────────────────────────────────────────────
 * CMDs chegam a <1 Hz (config manual do usuário na GUI). 4 slots bastam. */
typedef struct {
    uint8_t buf[sizeof(CMD)];
    size_t  len;
} cmd_item_t;

#define CMD_QUEUE_LEN 4
K_MSGQ_DEFINE(cmd_queue, sizeof(cmd_item_t), CMD_QUEUE_LEN, 4);

static struct k_work cmd_dispatch_work;

/**
 * @brief Drena cmd_queue e despacha cada CMD pendente no CMD_Core.
 * @param w item de trabalho (não usado)
 */
static void cmd_dispatch_fn(struct k_work *w)
{
    ARG_UNUSED(w);
    cmd_item_t item;
    while (k_msgq_get(&cmd_queue, &item, K_NO_WAIT) == 0) {
        CMD_Core_Dispatch(item.buf, item.len);
    }
}

/**
 * @brief Enfileira um pacote CMD recebido pra despacho na same_wq.
 * @param buf bytes do pacote (copiados internamente, truncado a sizeof(CMD))
 * @param len tamanho de buf
 */
void OSSI_Submit_CMD(const uint8_t *buf, size_t len)
{
    cmd_item_t item;
    size_t copy = MIN(len, sizeof(item.buf));
    memcpy(item.buf, buf, copy);
    item.len = copy;
    if (k_msgq_put(&cmd_queue, &item, K_NO_WAIT) != 0) {
        LOG_WRN("cmd_queue cheia — CMD descartado");
    }
    k_work_submit_to_queue(&same_wq, &cmd_dispatch_work);
}

/** @brief Inicializa os work items e sobe a same_wq. Chamar no início de main(). */
void OSSI_Init(void)
{
    k_work_init(&sensor_dispatch_work, sensor_dispatch_fn);
    k_work_init(&cmd_dispatch_work,    cmd_dispatch_fn);

    static const struct k_work_queue_config cfg = { .name = "same_wq" };
    k_work_queue_start(&same_wq, same_wq_stack,
                       K_THREAD_STACK_SIZEOF(same_wq_stack),
                       SAME_WQ_PRIORITY, &cfg);
}
