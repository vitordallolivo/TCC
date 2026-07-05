#ifndef CMD_CORE_H
#define CMD_CORE_H

#include "OSSI.h"
#include "Log_Manager.h"        /* SENSORS */
#include "Security_Manager.h"   /* siba_error_bit_t */
#include <stdint.h>

void CMD_Core_Init(void);
void CMD_Core_thread_fn(void *p1, void *p2, void *p3);

/* Configuração/limites do sensor `id` (fonte: CMD_Core_prm.h). Devolve NULL
 * para id fora da faixa. Usado pelos managers p/ ler a MESMA faixa [min,max]
 * sem hardcode espalhado. */
const SENSORS *CMD_Core_Get_Sensor_Cfg(uint32_t id);

/* Valida `value` do sensor `id` contra a faixa [min,max] da tabela.
 * Retorna o bit de erro (siba_error_bit_t) configurado se estiver FORA da
 * faixa, ou 0 se dentro (ou sensor sem verificação). Não altera estado —
 * quem seta/limpa o bit e loga na borda é o manager (Security). */
uint32_t CMD_Core_Validate_Sensor(uint32_t id, float value);

#define CMD_CORE_THREAD_ID  60
#define CMD_CORE_PRIO        4

#endif /* CMD_CORE_H */
