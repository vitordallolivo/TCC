#ifndef CMD_CORE_H
#define CMD_CORE_H

#include "Log_Manager.h"
#include "Security_Manager.h"
#include <stdint.h>
#include <stddef.h>

/* Inicializa a tabela de calibração/limites dos sensores (SIBA_SENSOR_TABLE). */
void CMD_Core_Init(void);

/* Despacha um pacote CMD serializado (buf[len]) para o handler correspondente.
 * Chamado pelo cmd_dispatch_fn do OSSI (siba_wq) — serializado com o
 * despacho de sensores, sem concorrência com Battery/Propeller/Security. */
void CMD_Core_Dispatch(const uint8_t *buf, size_t len);

/* Configuração/limites do sensor `id` (fonte: CMD_Core_prm.h). Devolve NULL
 * para id fora da faixa. Usado pelos managers p/ ler a faixa [min,max]
 * sem hardcode espalhado. */
const SENSORS *CMD_Core_Get_Sensor_Cfg(uint32_t id);

/* Valida `value` do sensor `id` contra a faixa [min,max] da tabela.
 * Retorna o bit de erro se estiver FORA da faixa, ou 0 se dentro. */
uint32_t CMD_Core_Validate_Sensor(uint32_t id, float value);

#endif /* CMD_CORE_H */
