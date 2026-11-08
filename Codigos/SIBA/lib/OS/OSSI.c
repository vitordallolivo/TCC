/**
 * @file OSSI.c
 * @brief Fila de trabalho única da SIBA (siba_wq): despacho de sensores e CMD.
 */
#include "OSSI.h"
#include "Log_Manager.h"
#include "Battery_Manager.h"
#include "Propeller_Manager.h"
#include "Security_Manager.h"
#include "CMD_Core.h"
#include "CMD.h"
#include "HAL_prm.h"         /* HAL_ENABLE_ADC */
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(ossi, LOG_LEVEL_WRN);

/* ── siba_wq ─────────────────────────────────────────────────────────────
 * Thread única de processamento: sensores, CMDs, log TX.
 * Prioridade 5 (mesmo que a antiga Sys_thread_fn e NCI).
 * Stack 8192B: FPU_SHARING salva 128B de contexto FPU na stack do siba_wq
 * a cada preempção da ISR do ADC (~24 kHz). Com 2048B o stack transbordava
 * durante Battery/Propeller/Security_Manager + LOG_ERR, corrompendo o
 * estado do ETH stack — NET_EVENT_IF_UP nunca disparava. */
#define SIBA_WQ_STACK_SIZE  8192
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

/**
 * @brief Drena sensor_queue e despacha cada item pros managers (Log,
 * Battery, Propeller, Security conforme o sensor_id). Roda na siba_wq.
 * @param w item de trabalho (não usado)
 */
static void sensor_dispatch_fn(struct k_work *w)
{
    sensor_item_t item;
    while (k_msgq_get(&sensor_queue, &item, K_NO_WAIT) == 0) {
        uint32_t id  = item.sensor_id;
        float    val = item.value;

        Log_Manager_Update(id, val);

#if HAL_ENABLE_ADC
        /* Alarmes da bateria (SOC baixo, temperatura) dependem do ADC —
         * desligados junto com ele (HAL_DISABLE_ADC). */
        if (id == SENSOR_ID_V_BATT || id == SENSOR_ID_I_BATT ||
            id == SENSOR_ID_TEMPERATURE_BATTERY) {
            Battery_Manager_Update(id, val);
        }
#endif

        if (id == SENSOR_ID_I_BATT || id <= SENSOR_ID_LOAD_CELL_5) {
            Propeller_Manager_Update(id, val);
        }

        Security_Manager_Check(id, val);
    }
}

/**
 * @brief Enfileira um valor de sensor pra despacho na siba_wq. Thread-safe
 * e ISR-safe; descarta e loga se a fila estiver cheia.
 * @param sensor_id ver SENSOR_ID_* em Log_Manager.h
 * @param value valor já convertido
 */
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

/**
 * @brief Drena cmd_queue e despacha cada CMD pendente no CMD_Core.
 * @param w item de trabalho (não usado)
 */
static void cmd_dispatch_fn(struct k_work *w)
{
    cmd_item_t item;
    while (k_msgq_get(&cmd_queue, &item, K_NO_WAIT) == 0) {
        CMD_Core_Dispatch(item.buf, item.len);
    }
}

/**
 * @brief Enfileira um pacote CMD recebido pra despacho na siba_wq.
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
    k_work_submit_to_queue(&siba_wq, &cmd_dispatch_work);
}

/** @brief Inicializa os work items e sobe a siba_wq. Chamar no início de main(). */
void OSSI_Init(void)
{
    k_work_init(&sensor_dispatch_work, sensor_dispatch_fn);
    k_work_init(&cmd_dispatch_work,    cmd_dispatch_fn);

    static const struct k_work_queue_config cfg = { .name = "siba_wq" };
    k_work_queue_start(&siba_wq, siba_wq_stack,
                       K_THREAD_STACK_SIZEOF(siba_wq_stack),
                       SIBA_WQ_PRIORITY, &cfg);
}
