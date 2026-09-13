#ifndef HAL_PRM_H
#define HAL_PRM_H

/* Seleção de módulos do HAL — descomente o HAL_DISABLE_* do suspeito e
 * recompile para bissectar crashes em bancada. Módulo desabilitado vira
 * no-op; as funções de HAL.h viram stubs seguros.
 * ⚠ ADC/I2C são devices Zephyr: desabilitar aqui só impede o USO pelo
 * HAL.c. Para isolar o driver, marcar `status = "disabled"` no .dts. */

// #define HAL_DISABLE_ADC
// #define HAL_DISABLE_HX711
// #define HAL_DISABLE_I2C

#ifndef CONFIG_ARCH_POSIX
// #define HAL_DISABLE_ESC
// #define HAL_DISABLE_RPM
#endif

/* Nível de ruído dos estímulos synthetic do native_sim (0=estável/ideal
 * para calibração, 1=ruído realista). Só vale no native_sim. */
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

/* DAC removido: PA5 agora é SPI1_SCK (W25Q flash). */

#endif /* HAL_PRM_H */
