#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include "OSSI.h"

void System_Manager_Init(void);
void System_Manager_thread_fn(void *p1, void *p2, void *p3);

#define SYS_MANAGER_THREAD_ID  50
#define SYS_MANAGER_PRIO        5

#endif /* SYSTEM_MANAGER_H */