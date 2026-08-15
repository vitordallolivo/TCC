#ifndef SECURITY_PRM_H
#define SECURITY_PRM_H

/* Auto-generated — edite SAME.yaml (seção `security_limits`), não este
 * arquivo. Limiares dos checadores do Security_Manager (desequilíbrio,
 * sobrecorrente, faixa de sensor, saúde do observador). */

#define SM_IMBAL_FRAC      0.15f  /* fração tolerável do desequilíbrio (15% da soma dos módulos das 3 fases) */
#define SM_IMBAL_ABS_I     0.5f  /* desequilíbrio absoluto de corrente tolerável [A] (cobre o skew do sampling sequencial) */
#define SM_IMBAL_ABS_V     1.0f  /* desequilíbrio absoluto de tensão tolerável [V] */
#define SM_OVERCURRENT_A   60.0f  /* limite de sobrecorrente de fase [A] */
#define SM_SENSOR_ABS_MAX  500.0f  /* leitura de V/A acima disso = sensor saturado ou desconectado */
#define SM_BEMF_MIN        1.0f  /* |ê| mínimo p/ PLL travar (deve casar com o limiar em Observer_Step) */
#define SM_PLL_ERR_MAX     0.35f  /* erro de fase da PLL tolerável [rad] */
#define SM_RPM_MAX         60000.0f  /* rpm mecânico plausível máximo */
#define SM_CAL_SETTLE_MS   2000.0f  /* espera antes de amostrar o offset [ms]: o SS49E parte em Vcc/2 e deriva no aquecimento inicial do ASIC; medir cedo trava um offset errado */
#define SM_CAL_FRAMES      4096.0f  /* frames acumulados para a média do offset (com o ESC parado) */
#define SM_CAL_STD_MAX_MV  8.0f  /* desvio-padrão máximo tolerado nas amostras de offset [mV no pino]; acima disso a calibração FALHA em vez de gravar lixo (8 mV ~ 0,47 A na cadeia) */

#endif /* SECURITY_PRM_H */
