#ifndef SYS_H
#define SYS_H

#include "OSSI.h"

void Sys_Init(void);
void Sys_thread_fn(void *p1, void *p2, void *p3);

#define SYS_MANAGER_THREAD_ID  50
#define SYS_MANAGER_PRIO        5

#endif /* SYS_H */
