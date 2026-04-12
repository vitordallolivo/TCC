#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <zephyr/kernel.h>

/* Prioridade e Tamanho da Stack para a thread do ADC */
#define HAL_ADC_PRIO       7
#define HAL_ADC_STACK_SIZE 4096

int HAL_ADC_Init(void);
uint16_t HAL_ADC_Get_Last_Value(void);

/* Protótipo da thread para o TCKEC */
void HAL_thread_fn(void *p1, void *p2, void *p3);

#endif