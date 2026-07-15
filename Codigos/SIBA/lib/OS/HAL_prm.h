#ifndef HAL_PRM_H
#define HAL_PRM_H

/* Seleção de módulos do HAL, pra bissecção de crash em bancada: descomente
 * o HAL_DISABLE_* do suspeito e recompile. Módulo desabilitado vira no-op
 * (sem tocar registrador/GPIO/IRQ) e as funções de HAL.h viram stubs
 * seguros. ⚠ ADC/I2C também são devices Zephyr — desabilitar aqui só
 * impede o USO pelo HAL.c, não o driver de inicializar; pra isolar o
 * driver em si marque `status = "disabled"` no .dts também. */

// #define HAL_DISABLE_ADC

/* HX711 roda no hardware real também. ESC/RPM ficam desabilitados só fora
 * do native_sim (periféricos ainda não validados em bancada). */
#ifndef CONFIG_ARCH_POSIX
// #define HAL_DISABLE_ESC
// #define HAL_DISABLE_RPM
#endif

// #define HAL_DISABLE_I2C
// #define HAL_DISABLE_DAC

/* Nível de ruído dos estímulos synthetic do native_sim (0=estável/ideal
 * pra calibração, 1=ruído realista). Só vale no native_sim. */
#ifndef HAL_SIM_NOISE_SCALE
#define HAL_SIM_NOISE_SCALE   0.0f
#endif

#ifndef HAL_DISABLE_ADC
#define HAL_ENABLE_ADC   1
#else
#define HAL_ENABLE_ADC   0
#endif

#ifndef HAL_DISABLE_HX711
#define HAL_ENABLE_HX711 1
#else
#define HAL_ENABLE_HX711 0
#endif

#ifndef HAL_DISABLE_ESC
#define HAL_ENABLE_ESC   1
#else
#define HAL_ENABLE_ESC   0
#endif

#ifndef HAL_DISABLE_RPM
#define HAL_ENABLE_RPM   1
#else
#define HAL_ENABLE_RPM   0
#endif

#ifndef HAL_DISABLE_I2C
#define HAL_ENABLE_I2C   1
#else
#define HAL_ENABLE_I2C   0
#endif

#ifndef HAL_DISABLE_DAC
#define HAL_ENABLE_DAC   1
#else
#define HAL_ENABLE_DAC   0
#endif

/* PA5 é ao mesmo tempo ADC1 canal 5 e DAC1_OUT1. Hoje sem conflito (ADC1
 * não usa CH5) — se alguém adicionar CH5 ao scan com HAL_ENABLE_DAC=1,
 * HAL.c aborta a build (ver checagem perto de ADC_CH_TEMP_BATT). */

#endif /* HAL_PRM_H */
