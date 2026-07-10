#ifndef NCI_H
#define NCI_H

#include <stdbool.h>
#include <stddef.h>

/* Cria o socket TX e arranca a thread RX (recvfrom bloqueante). */
void NCI_Init(void);

/* Atualiza a flag que permite/bloqueia o tráfego de dados.
 * false = offline (descarta TX, ignora RX). */
void NCI_Set_Connection_Status(bool status);

/* Envia `len` bytes de `buf` via UDP ao peer configurado.
 * Thread-safe; chamado pelo Log_Manager a cada 10 Hz (siba_wq). */
void NCI_Transmit(const void *buf, size_t len);

#endif /* NCI_H */
