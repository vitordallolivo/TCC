#include "System_Manager.h"
#include "Log_Manager.h"
#include "Battery_Manager.h"
#include "Propeller_Manager.h"
#include "Security_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sys_manager, LOG_LEVEL_DBG);

/*
 * System_Manager_thread_fn
 *
 * Única thread da camada SYS: além de consumir fila_sys e rotear para
 * Battery/Propeller/Security_Manager (todos chamados diretamente, sem
 * threads próprias — mantém o pool TCKEC enxuto e reduz uso de stack),
 * também dispara a telemetria periódica (Log_Manager_Transmit, 10 Hz)
 * que antes rodava em uma thread dedicada do Log_Manager.
 *
 * Para isso, k_msgq_get espera com timeout até o próximo instante de TX —
 * processa mensagens de sensor enquanto aguarda e, ao expirar (ou logo após
 * processar uma mensagem, se já passou da hora), transmite o snapshot atual.
 */
void System_Manager_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    ossi_msg_t msg;
    int64_t next_tx = k_uptime_get() + LOG_MANAGER_PERIOD_MS;

    LOG_INF("System_Manager thread iniciada (telemetria a cada %d ms)",
            LOG_MANAGER_PERIOD_MS);

    while (1) {
        int64_t now = k_uptime_get();
        k_timeout_t wait = (next_tx > now) ? K_MSEC(next_tx - now) : K_NO_WAIT;

        if (k_msgq_get(&fila_sys, &msg, wait) == 0 &&
            msg.req_type == REQ_TYPE_SYS_UPDATE_SENSOR) {

            uint32_t id    = msg.payload.sys_req.sensor_id;
            float    value = msg.payload.sys_req.value;

            /* Atualiza o log central */
            Log_Manager_Update(id, value);

            /* Despacha para Battery_Manager se for dado de bateria */
            if (id == SENSOR_ID_V_BATT || id == SENSOR_ID_I_BATT ||
                id == SENSOR_ID_TEMPERATURE_BATTERY) {
                Battery_Manager_Update(id, value);
            }

            /* Propeller_Manager monitora i_batt para nunca deixar a corrente
             * do motor ultrapassar o limite (sobrescreve o PWM do ESC) */
            if (id == SENSOR_ID_I_BATT) {
                Propeller_Manager_Update(id, value);
            }

            /* Security verifica limites em qualquer atualização */
            Security_Manager_Check(id, value);
        }

        now = k_uptime_get();
        if (now >= next_tx) {
            Log_Manager_Transmit();
            next_tx = now + LOG_MANAGER_PERIOD_MS;
        }
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