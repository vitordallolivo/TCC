#ifndef NCI_H
#define NCI_H

#include <zephyr/kernel.h>
#include "OSSI.h"

/* Tamanho e prioridade da thread NCI (TX) */
#define NCI_TX_STACK_SIZE 1024
#define NCI_TX_PRIORITY   5 // Prioridade menor que o TCKEC/Vcore

extern struct k_thread nci_tx_thread_data;
extern k_thread_stack_t nci_tx_stack[];

/* Inicializa a NCI (Cria threads e gerencia estado) */
void NCI_Init(void);

/* Simula a recepção de dados pela rede (NCI RX) */
void NCI_RX_Simulate_Receive(uint8_t *dummy_data, size_t len);

/* Simula a mudança de status da conexão WiFi/Ethernet */
void NCI_Set_Connection_Status(bool status);

#endif // NCI_H