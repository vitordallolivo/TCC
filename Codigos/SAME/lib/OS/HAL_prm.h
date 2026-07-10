#ifndef HAL_PRM_H
#define HAL_PRM_H

/* ===========================================================
 * HAL_prm.h — Seleção de módulos do HAL (bissecção de crash em bancada),
 * mesmo padrão da SIBA. A SAME só tem UM subsistema de hardware: os dois
 * ADCs. Se a placa crasha no boot suspeitando do driver de ADC, defina
 * HAL_DISABLE_ADC (ou -DHAL_DISABLE_ADC) — HAL_ADC_Init vira no-op, a thread
 * do ADC não arranca, e as funções públicas viram stubs neutros para o resto
 * (CMD_Core/Log/Observer) continuar linkando.
 * =========================================================== */

// #define HAL_DISABLE_ADC

/* ===========================================================
 * HAL_SIM_DEMO_RPM — rotação (rpm) que o estímulo synthetic do native_sim
 * "gira" o motor virtual. Gera 3 tensões + 3 correntes senoidais na
 * frequência elétrica correspondente; a PLL do observador trava e reporta
 * rpm_same ≈ este valor (demonstra a cadeia ADC→Clarke→SMO→PLL→telemetria
 * sem hardware). Só vale no build native_sim (POSIX).
 * =========================================================== */
#ifndef HAL_SIM_DEMO_RPM
#define HAL_SIM_DEMO_RPM   2500.0f
#endif

#ifndef HAL_DISABLE_ADC
#define HAL_ENABLE_ADC   1
#else
#define HAL_ENABLE_ADC   0
#endif

#endif /* HAL_PRM_H */
