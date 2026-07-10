#ifndef NCI_H
#define NCI_H

#include <stdbool.h>
#include <stddef.h>

/* Cria o socket TX e arranca a thread RX (recvfrom bloqueante). */
void NCI_Init(void);

/* Permite/bloqueia o tráfego de dados. false = offline (descarta TX/RX). */
void NCI_Set_Connection_Status(bool status);

/* Envia `len` bytes de `buf` via UDP ao peer (PC) configurado.
 * Thread-safe; chamado pelo Log_Manager (same_wq) a cada Ts_udp. */
void NCI_Transmit(const void *buf, size_t len);

#endif /* NCI_H */
