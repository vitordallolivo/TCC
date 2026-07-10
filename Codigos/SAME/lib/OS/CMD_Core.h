#ifndef CMD_CORE_H
#define CMD_CORE_H

#include "Log_Manager.h"    /* SENSORS */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Inicializa a tabela de calibração/validade dos sensores (SAME_SENSOR_TABLE). */
void CMD_Core_Init(void);

/* Despacha um pacote CMD serializado (buf[len]). Chamado pelo cmd_dispatch_fn
 * do OSSI (same_wq) — serializado com o TX do log, sem concorrer com o passo
 * do observador (que roda na thread do ADC). */
void CMD_Core_Dispatch(const uint8_t *buf, size_t len);

/* Config/validade do sensor `id` (0..CMD_CORE_SENSOR_COUNT-1). NULL se fora. */
const SENSORS *CMD_Core_Get_Sensor_Cfg(uint32_t id);

/* true se `value` do sensor `id` está dentro da faixa [min,max] da tabela. */
bool CMD_Core_Validate_Sensor(uint32_t id, float value);

#endif /* CMD_CORE_H */
