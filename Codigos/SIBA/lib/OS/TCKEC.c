#include <zephyr/logging/log.h>
#include "TCKEC.h"

LOG_MODULE_REGISTER(tckec_core, LOG_LEVEL_DBG);

/* -------------------------------------------------------------
 * CONFIGURAÇÕES DO THREAD POOL
 * Define quantas threads dinâmicas o TCKEC pode gerenciar simultaneamente.
 * ------------------------------------------------------------- */
#define MAX_MANAGED_THREADS 5
#define MANAGED_THREAD_STACK_SIZE 1024

/* Array de Stacks alocadas estaticamente na RAM (Seguro para RTOS) */
K_THREAD_STACK_ARRAY_DEFINE(managed_stacks, MAX_MANAGED_THREADS, MANAGED_THREAD_STACK_SIZE);

/* Estrutura de Controle do TCKEC para rastrear quem está vivo */
typedef struct {
    struct k_thread thread_data;
    bool is_active;
    uint32_t thread_id; // Identificador lógico (ex: ID do i2c, ID do motor)
} tckec_slot_t;

static tckec_slot_t thread_pool[MAX_MANAGED_THREADS];

/* -------------------------------------------------------------
 * TCKEC MAIN THREAD
 * ------------------------------------------------------------- */
#define TCKEC_STACK_SIZE 1024
#define TCKEC_PRIORITY   -1 // Prioridade altíssima cooperativa

K_THREAD_STACK_DEFINE(tckec_stack, TCKEC_STACK_SIZE);
struct k_thread tckec_thread_data;

void tckec_main_thread(void *p1, void *p2, void *p3) {
    ossi_msg_t msg;
    
    LOG_INF("TCKEC Iniciado. Monitorando fila_tckec para Create/Kill/Error Check...");

    while (1) {
        
        if (k_msgq_get(&fila_tckec, &msg, K_FOREVER) == 0) {
            
            if (msg.target != TARGET_TCKEC) continue;

            switch (msg.req_type) {
                
                case REQ_TYPE_TCKEC_CREATE_THREAD: {
                    uint32_t target_id = msg.payload.tckec_req.thread_id;
                    int free_slot = -1;
                    bool already_exists = false;

                    // IF THREAD exist then dont make another and check if the slot is free
                    for (int i = 0; i < MAX_MANAGED_THREADS; i++) {
                        if (thread_pool[i].is_active && thread_pool[i].thread_id == target_id) {
                            already_exists = true;
                            break;
                        }
                        if (!thread_pool[i].is_active && free_slot == -1) {
                            free_slot = i;
                        }
                    }

                    if (already_exists) {
                        LOG_WRN("TCKEC Check: Requisicao negada. Thread ID %d ja esta rodando!", target_id);
                        break; // Aborta criação
                    }

                    if (free_slot == -1) {
                        LOG_ERR("TCKEC Check: Sem memoria/slots disponiveis no Pool para criar Thread ID %d", target_id);
                        break; // Aborta criação
                    }

                    /* Passou nos checks de erro e memória. Criando a thread! */
                    LOG_INF("TCKEC: Criando Thread ID %d no slot %d...", target_id, free_slot);
                    
                    thread_pool[free_slot].thread_id = target_id;
                    thread_pool[free_slot].is_active = true;

                    k_thread_create(
                        &thread_pool[free_slot].thread_data,
                        managed_stacks[free_slot],
                        MANAGED_THREAD_STACK_SIZE,
                        msg.payload.tckec_req.entry_point,
                        msg.payload.tckec_req.p1,
                        msg.payload.tckec_req.p2,
                        msg.payload.tckec_req.p3,
                        msg.payload.tckec_req.priority,
                        0, K_NO_WAIT
                    );
                    break;
                }

                case REQ_TYPE_TCKEC_KILL_THREAD: {
                    uint32_t target_id = msg.payload.tckec_req.thread_id;
                    bool found = false;

                    /* Procura a thread pelo ID para matar */
                    for (int i = 0; i < MAX_MANAGED_THREADS; i++) {
                        if (thread_pool[i].is_active && thread_pool[i].thread_id == target_id) {
                            LOG_INF("TCKEC: Matando (Abort) Thread ID %d do slot %d", target_id, i);
                            k_thread_abort(&thread_pool[i].thread_data);
                            thread_pool[i].is_active = false;
                            thread_pool[i].thread_id = 0;
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        LOG_WRN("TCKEC Check: Tentativa de matar Thread ID %d, mas ela nao existe.", target_id);
                    }
                    break;
                }

                default:
                    LOG_ERR("TCKEC Check: Operacao desconhecida no Vcore (%d)", msg.req_type);
                    break;
            }
        }
    }
}

/* -------------------------------------------------------------
 * INICIALIZAÇÃO
 * ------------------------------------------------------------- */
void TCKEC_Init(void) {
    LOG_INF("Booting TCKEC System...");
    
    // Zera o pool de controle
    for (int i = 0; i < MAX_MANAGED_THREADS; i++) {
        thread_pool[i].is_active = false;
        thread_pool[i].thread_id = 0;
    }

    // Sobe a thread mestre
    k_thread_create(&tckec_thread_data, tckec_stack, K_THREAD_STACK_SIZEOF(tckec_stack),
                    tckec_main_thread, NULL, NULL, NULL, TCKEC_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&tckec_thread_data, "TCKEC_MAIN");
}