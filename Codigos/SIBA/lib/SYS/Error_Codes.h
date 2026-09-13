#ifndef ERROR_CODES_H
#define ERROR_CODES_H

/* Auto-generated — edite SIBA.yaml (seção `error_bits`), não este arquivo.
 *
 * Código de erro agregado: 1 bit por condição de erro/alarme. Cada manager
 * reporta via Security_Manager_Error_Detect/_Erase (só loga na transição). O
 * agregado (uint32_t) vai pro TELEMETRY_LOG.error_code e a GUI decodifica em
 * pt-BR (Communication_Data/errors.py, que lê a MESMA seção `error_bits`). */

#include <stdint.h>

typedef enum {
    ERROR_BATT_LOW_SOC = (1U << 0),  /* Bateria: Bateria com carga baixa (nível abaixo do limite de alerta) */
    ERROR_BATT_POLY_MISCONFIGURED = (1U << 1),  /* Bateria: Cálculo de carga da bateria mal configurado (revise o polinômio de SOC) */
    ERROR_BATT_LOW_VOLTAGE = (1U << 2),  /* Bateria: Tensão da bateria baixa — alguma célula perto do limite mínimo */
    ERROR_BATT_RELAY_MISMATCH = (1U << 3),  /* Bateria: Passando corrente com o relé desligado — possível falha no relé */
    ERROR_BATT_OVER_TEMPERATURE = (1U << 4),  /* Bateria: Bateria superaquecendo (temperatura acima do limite de alarme) */
    ERROR_PROP_LOAD_CELL_NOT_CONFIGURED = (1U << 8),  /* Motor/Hélice: Célula de carga sem calibração — tare e calibre antes de medir */
    ERROR_PROP_LOAD_CELL_NOT_CONNECTED = (1U << 9),  /* Motor/Hélice: Célula de carga desconectada (HX711 não responde) */
    ERROR_PROP_OVERCURRENT = (1U << 10),  /* Motor/Hélice: Corrente do motor acima do limite permitido */
    ERROR_SEC_VOLTAGE_OUT_OF_RANGE = (1U << 16),  /* Segurança: Tensão da bateria fora da faixa segura de operação */
    ERROR_SEC_CURRENT_EXCESSIVE = (1U << 17),  /* Segurança: Corrente da bateria excessiva (acima do limite de segurança) */
    ERROR_SEC_RPM_EXCESSIVE = (1U << 18),  /* Segurança: Rotação do motor (RPM) acima do limite de segurança */
    ERROR_SEC_EMERGENCY_ACTIVE = (1U << 19),  /* Segurança: PARADA DE EMERGÊNCIA ativa — motor desarmado */
    ERROR_OS_GPIO_INIT_FAILED = (1U << 24),  /* Sistema: Falha ao inicializar um módulo de hardware (GPIO) */
    ERROR_MCU_POWER_READY = (1U << 25),  /* Sistema: Bancada não inicializada — aguardando sequência de energização (relé) */
} siba_error_bit_t;

#endif /* ERROR_CODES_H */
