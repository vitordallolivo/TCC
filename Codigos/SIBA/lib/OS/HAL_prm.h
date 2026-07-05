#ifndef HAL_PRM_H
#define HAL_PRM_H

/* ===========================================================
 * HAL_prm.h — Seleção de módulos do HAL (bissecção de crash em bancada)
 *
 * Contexto: HAL_Init() (chamado em main.c) inicializa ADC, HX711, ESC,
 * RPM, I2C e DAC e registra as threads correspondentes no TCKEC. Se a
 * placa crasha/reseta durante esse boot, este header permite desligar
 * cada módulo individualmente — recompilar/flashear com só UM módulo
 * habilitado por vez até achar o culpado (suspeita atual: driver ADC,
 * `drivers/adc/adc_ch32v307.c`).
 *
 * Como usar: descomente o HAL_DISABLE_* do módulo que quer tirar do
 * build (ou passe -DHAL_DISABLE_XXX via CMakeLists/extra_conf) e
 * recompile. Um módulo desabilitado:
 *   - NÃO toca em registrador/GPIO/IRQ/pinctrl (HAL_xxx_Init vira no-op)
 *   - NÃO registra sua thread no TCKEC
 *   - continua com as funções públicas de HAL.h definidas como stubs
 *     seguros (retornam 0/valor neutro), então CMD_Core/Test_Shell/Sys/
 *     Propeller_Manager continuam compilando e rodando sem o módulo
 *
 * ⚠ ADC e I2C também são *devices* Zephyr (`adc1`/`i2c1`, status="okay"
 * no .dts, init POST_KERNEL). HAL_DISABLE_ADC/HAL_DISABLE_I2C aqui
 * impedem que HAL.c USE esses devices (channel setup, thread, leitura),
 * mas NÃO impedem o driver (`adc_ch32v307_init`/`i2c_wch`) de rodar no
 * boot — isso é decidido pelo subsistema de device do Zephyr antes do
 * main(). Se a suspeita for o PRÓPRIO driver/device, é preciso TAMBÉM
 * marcar o nó correspondente como `status = "disabled";` no .dts — com
 * HAL_DISABLE_ADC/HAL_DISABLE_I2C definidos aqui, HAL.c não referencia
 * mais `adc_dev`/`i2c_dev` (ver #if HAL_ENABLE_ADC/HAL_ENABLE_I2C em
 * HAL.c), então o build continua OK mesmo com o nó desabilitado no .dts.
 * =========================================================== */

// #define HAL_DISABLE_ADC

/* HX711 (células de carga) roda AGORA também no HARDWARE real — a calibração
 * canônica (grams=(raw-tare)/scale) precisa do driver ativo na placa, não só
 * no native_sim. ESC/RPM seguem desabilitados só no HARDWARE (periféricos
 * ainda não validados em bancada); no native_sim (POSIX) todos rodam em modo
 * SYNTHETIC. Se a bancada crashar no boot por causa do HX711 (GPIO port E
 * PE2–PE13), reabilite este `#define HAL_DISABLE_HX711` para isolar. */
#ifndef CONFIG_ARCH_POSIX
#define HAL_DISABLE_ESC
#define HAL_DISABLE_RPM
#endif

#define HAL_DISABLE_I2C
#define HAL_DISABLE_DAC

/* ===========================================================
 * HAL_SIM_NOISE_SCALE — nível de ruído aleatório do modo synthetic (native_sim)
 *
 * Fator global (0.0–1.0) que multiplica TODO o ruído pseudo-aleatório dos
 * estímulos synthetic (ADC v_batt/i_batt/temp/air_speed, HX711 e RPM). Não
 * afeta as tendências determinísticas (empuxo ∝ rpm², dreno da bateria, etc.),
 * só a parte "chacoalhando".
 *
 *   0.0f  -> valores ESTÁVEIS (determinísticos) — ideal para testar calibração
 *            (offset/linear) sem os números oscilando (padrão atual).
 *   1.0f  -> ruído "realista" (amplitudes originais) — para exercitar filtros/
 *            média e ver a telemetria "viva".
 *
 * Só vale no build native_sim (POSIX); no hardware o ruído é o do sensor real.
 * =========================================================== */
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

/* ===========================================================
 * PA5 — coincidência física DAC1_OUT1 / ADC1 canal 5
 *
 * No CH32V307, PA5 é ao mesmo tempo ADC1 canal 5 (ADC1_IN5) e DAC1_OUT1.
 * O scan atual do ADC1 usa apenas CH0/CH1/CH2/CH6 = PA0/PA1/PA2/PA6 (ver
 * ADC_CH_* em HAL.c), então hoje não há conflito de configuração. Se
 * alguém adicionar o canal 5 ao scan do ADC1 (adc_ch_cfg[]/adc1_default)
 * com HAL_ENABLE_DAC=1, HAL.c aborta a build (ver checagem ao lado de
 * ADC_CH_TEMP_BATT em HAL.c) — é preciso escolher um único uso para PA5.
 * =========================================================== */

#endif /* HAL_PRM_H */
