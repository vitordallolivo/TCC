#ifndef TCKEC_H
#define TCKEC_H
/* TCKEC eliminado (igual à SIBA) — a única thread de I/O bloqueante é criada
 * estaticamente no módulo dono (ADC em HAL.c, RX em NCI.c). Todo o resto é
 * despacho por eventos via same_wq (ver OSSI.h). */
#endif /* TCKEC_H */
