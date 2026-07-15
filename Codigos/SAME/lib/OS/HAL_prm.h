#ifndef HAL_PRM_H
#define HAL_PRM_H

/* Defina HAL_DISABLE_ADC pra isolar crash de boot: HAL_ADC_Init vira no-op
 * e as funções públicas viram stubs neutros. */
// #define HAL_DISABLE_ADC

/* RPM que o motor virtual do native_sim simula (só build POSIX). */
#ifndef HAL_SIM_DEMO_RPM
#define HAL_SIM_DEMO_RPM   2500.0f
#endif

#ifndef HAL_DISABLE_ADC
#define HAL_ENABLE_ADC   1
#else
#define HAL_ENABLE_ADC   0
#endif

#endif /* HAL_PRM_H */
