#ifndef OSSI_H
#define OSSI_H

#include <zephyr/kernel.h>

/* Destinos baseados no fluxo do roteador OSSI */
typedef enum {
    TARGET_CMD = 0,     // Para o CMD Core Interpreter
    TARGET_VCORE,       // Para o TCKEC / HAL
    TARGET_SYS,         // Para o System Manager (Propeller, Battery, etc)
    TARGET_NETWORK      // Para o NCI TX (Telemetria/Logs)
} ossi_target_t;

/* Tipos de Request genéricos (podem ser expandidos por módulo) */
typedef enum {
    REQ_TYPE_CMD_PARSE = 0,
    REQ_TYPE_TCKEC_CREATE_THREAD,
    REQ_TYPE_SYS_UPDATE_SENSOR,
    REQ_TYPE_NET_SEND_TELEMETRY
} ossi_req_type_t;

/* Estrutura unificada do OSSI (Tamanho Fixo) */
typedef struct {
    ossi_target_t target;   
    uint32_t req_type;      
    
    union {
        /* Payload para o TCKEC/VCORE */
        struct {
            k_thread_entry_t entry_point;
            int priority;
        } vcore_req;
        
        /* Payload para o CMD Core (ex: pacote binário traduzido) */
        struct {
            uint8_t buffer[64];
            size_t len;
        } cmd_req;

        /* Payload para o System Manager */
        struct {
            uint32_t sensor_id;
            float value;
        } sys_req;

        /* Padding para garantir tamanho fixo */
        uint8_t raw[128]; 
    } payload;
} ossi_msg_t;

/* * Declaração das Filas (Queues) expostas para os consumidores.
 * Cada thread consumidora vai dar um k_msgq_get na sua respectiva fila.
 */
extern struct k_msgq fila_cmd;    //
extern struct k_msgq fila_vcore;  //
extern struct k_msgq fila_sys;    //
extern struct k_msgq fila_nci;    //

/*
 * OSSI_Send: Função central de roteamento do OSSI.
 * Retorna 0 em caso de sucesso ou código de erro negativo.
 */
int OSSI_Send(ossi_msg_t *msg);

#endif // OSSI_H