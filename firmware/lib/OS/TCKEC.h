#ifndef TCKEC_H
#define TCKEC_H
/* TCKEC eliminado — threads definidas estaticamente nos próprios módulos
 * (HAL.c, NCI.c) via K_THREAD_STACK_DEFINE + k_thread_create.
 * Despacho de eventos via siba_wq (ver OSSI.h). */
#endif /* TCKEC_H */
