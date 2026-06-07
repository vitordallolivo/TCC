#include "System_Manager.h"
#include "Log_Manager.h"
#include "Battery_Manager.h"
#include "Security_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sys_manager, LOG_LEVEL_DBG);

void System_Manager_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    ossi_msg_t msg;
    LOG_INF("System_Manager thread iniciada");

    while (1) {
        if (k_msgq_get(&fila_sys, &msg, K_FOREVER) != 0) continue;

        if (msg.req_type != REQ_TYPE_SYS_UPDATE_SENSOR) continue;

        uint32_t id    = msg.payload.sys_req.sensor_id;
        float    value = msg.payload.sys_req.value;

        /* Atualiza o log central */
        Log_Manager_Update(id, value);

        /* Despacha para Battery_Manager se for dado de bateria */
        if (id == SENSOR_ID_V_BATT || id == SENSOR_ID_I_BATT ||
            id == SENSOR_ID_TEMPERATURE_BATTERY) {
            Battery_Manager_Update(id, value);
        }

        /* Security verifica limites em qualquer atualização */
        Security_Manager_Check(id, value);
    }
}

void System_Manager_Init(void)
{
    ossi_msg_t req = {
        .target   = TARGET_TCKEC,
        .req_type = REQ_TYPE_TCKEC_CREATE_THREAD,
    };
    req.payload.tckec_req.thread_id   = SYS_MANAGER_THREAD_ID;
    req.payload.tckec_req.entry_point = System_Manager_thread_fn;
    req.payload.tckec_req.priority    = SYS_MANAGER_PRIO;
    req.payload.tckec_req.p1 = NULL;
    req.payload.tckec_req.p2 = NULL;
    req.payload.tckec_req.p3 = NULL;
    OSSI_Send(&req);
    LOG_INF("System_Manager inicializado");
}