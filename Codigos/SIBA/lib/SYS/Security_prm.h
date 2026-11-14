#ifndef SECURITY_PRM_H
#define SECURITY_PRM_H

/* Auto-generated — edite SIBA.yaml (seção `security_limits`), não este
 * arquivo. Limiares dos checadores do Security_Manager (desequilíbrio,
 * sobrecorrente, faixa de sensor, saúde do observador). */

#define SM_OVERCURRENT_A   60.0f  /* limite de sobrecorrente de fase [A] */
#define SM_SENSOR_ABS_MAX  500.0f  /* leitura de V/A acima disso = sensor saturado ou desconectado */
#define SM_RPM_MAX         60000.0f  /* rpm mecânico plausível máximo */
#define SM_CAL_SETTLE_MS   2000.0f  /* espera antes de amostrar o offset [ms]: o SS49E deriva no aquecimento inicial */
#define SM_CAL_FRAMES      4096.0f  /* quadros acumulados para a média do offset (com o ESC parado) */
#define SM_CAL_STD_MAX_MV  8.0f  /* desvio-padrão máximo nas amostras de offset [mV no pino]; acima disso a calibração FALHA */

#endif /* SECURITY_PRM_H */
