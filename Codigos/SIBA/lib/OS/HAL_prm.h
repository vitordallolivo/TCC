#ifndef HAL_PRM_H
#define HAL_PRM_H

/* Seleção de módulos do HAL — descomente o HAL_DISABLE_* do suspeito e
 * recompile para bissectar crashes em bancada. Módulo desabilitado vira
 * no-op; as funções de HAL.h viram stubs seguros.
 * ⚠ ADC/I2C são devices Zephyr: desabilitar aqui só impede o USO pelo
 * HAL.c. Para isolar o driver, marcar `status = "disabled"` no .dts. */

/* ADC ligado: na placa ele mede as FASES do motor pela AQ1 (i_a/i_b/i_c em
 * PA0/PA1/PA3, V_a/V_b/V_c em PC2/PC3/PC4); Pitot, termistores e bateria
 * deixaram de ser lidos na placa. No native_sim segue o estímulo sintético
 * antigo. Descomentar para desligar o ADC. */
// #define HAL_DISABLE_ADC

/* Corrente da fase C: 1 = derivada, i_c = -(i_a + i_b); 0 = medida no PA3. */
#ifndef HAL_PH_I_C_DERIVED
#define HAL_PH_I_C_DERIVED    1
#endif

/* Casamento correntes × tensões, aplicado depois de derivar i_c e antes do
 * observador e da telemetria. Levantado nos ensaios de 15/11/2026: com a
 * ligação direta a sequência das correntes girava ao contrário da das
 * tensões — potência elétrica Σv·i ≈ 0 W e torque estimado ~25 % do
 * esperado. Com troca B↔C e sinal invertido, Σv·i = 39 ± 3 W a 2930 rpm
 * (hélice 15x6E + perdas ≈ 37 W) e a corrente atrasa a tensão 15–27°.
 * Mexeu nos sensores Hall ou na AQ1: refazer essa checagem nos CSVs. */
#ifndef HAL_PH_I_SWAP_BC
#define HAL_PH_I_SWAP_BC      1   /* sensor "b" na fase C, "c" na fase B */
#endif
#ifndef HAL_PH_I_INVERT
#define HAL_PH_I_INVERT       1   /* Hall no sentido oposto ao do firmware */
#endif
// #define HAL_DISABLE_HX711

/* HX711 lido por polling, sem espera bloqueante: a cada volta lê só as
 * células com DOUT=0. 10 SPS (RATE=0) → 50 ms = 2x a taxa de conversão. */
#ifndef HAL_HX711_POLL_MS
#define HAL_HX711_POLL_MS     50
#endif
// #define HAL_DISABLE_I2C

#ifndef CONFIG_ARCH_POSIX
// #define HAL_DISABLE_ESC
/* RPM por captura no TIM3 desligado enquanto se mede o tempo do ADC: evita
 * que a entrada solta (PB0, sem pull) gere interrupções de captura. */
#define HAL_DISABLE_RPM
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
