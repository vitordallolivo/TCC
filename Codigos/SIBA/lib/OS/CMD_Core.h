#ifndef CMD_CORE_H
#define CMD_CORE_H

#include "OSSI.h"

void CMD_Core_Init(void);
void CMD_Core_thread_fn(void *p1, void *p2, void *p3);

#define CMD_CORE_THREAD_ID  60
#define CMD_CORE_PRIO        4

#endif /* CMD_CORE_H */
