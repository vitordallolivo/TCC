#include "OSSI.h"
#include "Log_Manager.h"
#include "CMD_Core.h"
#include "CMD.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(ossi, LOG_LEVEL_WRN);

/* ── same_wq ─────────────────────────────────────────────────────────────
 * Thread única de processamento de eventos: telemetria + CMDs.
 * Prioridade 5 (menor prioridade que a thread do ADC, que é 7 e roda o
 * observador — o passo do SMO nunca deve esperar a same_wq). Stack 2048B. */
#define SAME_WQ_STACK_SIZE  2048
#define SAME_WQ_PRIORITY       5

K_THREAD_STACK_DEFINE(same_wq_stack, SAME_WQ_STACK_SIZE);
struct k_work_q same_wq;

/* ── Fila de telemetria ──────────────────────────────────────────────────
 * A thread do ADC publica os 13 campos do TELEMETRY_LOG (6 medidas + 4 αβ +
 * rpm/Te/torque) a cada decimação. 32 slots absorvem uma rajada inteira sem
 * perda mesmo com a same_wq ocupada por um TX UDP. */
typedef struct { uint32_t sensor_id; float value; } sensor_item_t;

#define SENSOR_QUEUE_LEN 32
K_MSGQ_DEFINE(sensor_queue, sizeof(sensor_item_t), SENSOR_QUEUE_LEN, 4);

static struct k_work sensor_dispatch_work;

/* Escreve no log TODOS os itens pendentes. Roda na same_wq — serializado com
 * o CMD dispatch e com o TX do log. O observador já rodou (na thread do ADC);
 * aqui só se copia o resultado para o snapshot do log. */
static void sensor_dispatch_fn(struct k_work *w)
{
    ARG_UNUSED(w);
    sensor_item_t item;
    while (k_msgq_get(&sensor_queue, &item, K_NO_WAIT) == 0) {
        Log_Manager_Update(item.sensor_id, item.value);
    }
}

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

static void cmd_dispatch_fn(struct k_work *w)
{
    ARG_UNUSED(w);
    cmd_item_t item;
    while (k_msgq_get(&cmd_queue, &item, K_NO_WAIT) == 0) {
        CMD_Core_Dispatch(item.buf, item.len);
    }
}

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

/* ── Inicialização ───────────────────────────────────────────────────── */
void OSSI_Init(void)
{
    k_work_init(&sensor_dispatch_work, sensor_dispatch_fn);
    k_work_init(&cmd_dispatch_work,    cmd_dispatch_fn);

    static const struct k_work_queue_config cfg = { .name = "same_wq" };
    k_work_queue_start(&same_wq, same_wq_stack,
                       K_THREAD_STACK_SIZEOF(same_wq_stack),
                       SAME_WQ_PRIORITY, &cfg);
}
