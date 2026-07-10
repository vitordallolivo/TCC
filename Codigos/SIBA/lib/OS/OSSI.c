#include "OSSI.h"
#include "Log_Manager.h"
#include "Battery_Manager.h"
#include "Propeller_Manager.h"
#include "Security_Manager.h"
#include "CMD_Core.h"
#include "CMD.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(ossi, LOG_LEVEL_WRN);

/* ── siba_wq ─────────────────────────────────────────────────────────────
 * Thread única de processamento: sensores, CMDs, log TX.
 * Prioridade 5 (mesmo que a antiga Sys_thread_fn e NCI).
 * Stack 2048B — suficiente para Battery_Manager_Update (eval_soc + poly). */
#define SIBA_WQ_STACK_SIZE  2048
#define SIBA_WQ_PRIORITY       5

K_THREAD_STACK_DEFINE(siba_wq_stack, SIBA_WQ_STACK_SIZE);
struct k_work_q siba_wq;

/* ── Fila de sensores ────────────────────────────────────────────────────
 * Item compacto (8 bytes). 40 slots comportam rajadas de 6 HX711 + 4 ADC +
 * 1 RPM sem perda, mesmo com o siba_wq ocupado por ~10ms de processamento. */
typedef struct { uint32_t sensor_id; float value; } sensor_item_t;

#define SENSOR_QUEUE_LEN 40
K_MSGQ_DEFINE(sensor_queue, sizeof(sensor_item_t), SENSOR_QUEUE_LEN, 4);

static struct k_work sensor_dispatch_work;

/* Despacha TODOS os itens pendentes da sensor_queue para os managers.
 * Roda no siba_wq — serializado com cmd_dispatch_fn e log_tx_fn. */
static void sensor_dispatch_fn(struct k_work *w)
{
    sensor_item_t item;
    while (k_msgq_get(&sensor_queue, &item, K_NO_WAIT) == 0) {
        uint32_t id  = item.sensor_id;
        float    val = item.value;

        Log_Manager_Update(id, val);

        if (id == SENSOR_ID_V_BATT || id == SENSOR_ID_I_BATT ||
            id == SENSOR_ID_TEMPERATURE_BATTERY) {
            Battery_Manager_Update(id, val);
        }

        if (id == SENSOR_ID_I_BATT || id <= SENSOR_ID_LOAD_CELL_5) {
            Propeller_Manager_Update(id, val);
        }

        Security_Manager_Check(id, val);
    }
}

void OSSI_Submit_Sensor(uint32_t sensor_id, float value)
{
    sensor_item_t item = { sensor_id, value };
    if (k_msgq_put(&sensor_queue, &item, K_NO_WAIT) != 0) {
        LOG_WRN("sensor_queue cheia — sensor_id %u descartado", sensor_id);
    }
    k_work_submit_to_queue(&siba_wq, &sensor_dispatch_work);
}

/* ── Fila de CMDs ────────────────────────────────────────────────────────
 * 4 slots: CMDs chegam à <1 Hz pela rede (controle manual do usuário). */
typedef struct {
    uint8_t buf[sizeof(CMD)];
    size_t  len;
} cmd_item_t;

#define CMD_QUEUE_LEN 4
K_MSGQ_DEFINE(cmd_queue, sizeof(cmd_item_t), CMD_QUEUE_LEN, 4);

static struct k_work cmd_dispatch_work;

static void cmd_dispatch_fn(struct k_work *w)
{
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
    k_work_submit_to_queue(&siba_wq, &cmd_dispatch_work);
}

/* ── Inicialização ───────────────────────────────────────────────────── */
void OSSI_Init(void)
{
    k_work_init(&sensor_dispatch_work, sensor_dispatch_fn);
    k_work_init(&cmd_dispatch_work,    cmd_dispatch_fn);

    static const struct k_work_queue_config cfg = { .name = "siba_wq" };
    k_work_queue_start(&siba_wq, siba_wq_stack,
                       K_THREAD_STACK_SIZEOF(siba_wq_stack),
                       SIBA_WQ_PRIORITY, &cfg);
}
