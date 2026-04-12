#ifndef NCI_H
#define NCI_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include "OSSI.h" 

/* Prioridades das threads da rede */
#define NCI_TX_PRIORITY 5
#define NCI_RX_PRIORITY 5

/* IDs lógicos para o TCKEC identificar essas threads internamente */
#define NCI_TX_THREAD_ID 10
#define NCI_RX_THREAD_ID 11

/**
 * @brief Solicita ao TCKEC (via OSSI) a criação das threads de rede (TX e RX).
 */
void NCI_Init(void);

/**
 * @brief Atualiza a flag interna que permite/bloqueia o tráfego de dados.
 * * @param status true = online (envia/recebe), false = offline (descarta).
 */
void NCI_Set_Connection_Status(bool status);

#endif // NCI_H