#ifndef HAL_PRM_H
#define HAL_PRM_H

/* Defina HAL_DISABLE_ADC pra isolar crash de boot: HAL_ADC_Init vira no-op
 * e as funções públicas viram stubs neutros. */
// #define HAL_DISABLE_ADC

/* RPM que o motor virtual do native_sim simula (só build POSIX). */
#ifndef HAL_SIM_DEMO_RPM
#define HAL_SIM_DEMO_RPM   2500.0f
#endif

/* ---- Cadeia de condicionamento (espelha os defaults do SAME.yaml) --------
 *
 * Estes valores NÃO são a calibração: a calibração vive em adc_calib[] e vem
 * da tabela gerada (SAME_SENSOR_TABLE), ajustável por CMD. Aqui ficam só as
 * constantes de projeto do hardware, usadas pelo gerador sintético do
 * native_sim e pela calibração de offset do boot.
 *
 * CORRENTE — SS49E (N=1) + divisor 2k0/18k:
 *     18,750 mV/A x 0,900 = 16,875 mV/A, repouso em 2,4866 V x 0,900
 * TENSÃO — divisor 130k/7k5: k = 7,5/137,5 = 0,0545455 */
#define HAL_CURR_MV_PER_A    16.875f     /* ganho da cadeia de corrente */
#define HAL_HALL_ZERO_MV   2238.0f       /* repouso NOMINAL do Hall no pino */
#define HAL_VOLT_MV_PER_V    54.5455f    /* mV no pino por volt de fase */

/* Calibração de offset do Hall no boot: a thread de aquisição espera o sensor
 * estabilizar, acumula N frames com o ESC PARADO e grava o offset medido.
 * Enquanto isso o observador fica bloqueado — ver HAL.c. Limiares em
 * Security_prm.h (SM_CAL_*, gerados do SAME.yaml). Desligar só para depurar. */
#ifndef HAL_CALIBRATE_ON_BOOT
#define HAL_CALIBRATE_ON_BOOT   1
#endif

#ifndef HAL_DISABLE_ADC
#define HAL_ENABLE_ADC   1
#else
#define HAL_ENABLE_ADC   0
#endif

/* Profiler de CICLO REAL do Observer_Step.
 *
 * É a única medida de ciclo que vale: nem o native_sim (x86_64) nem o qemu
 * (não modela o pipeline de 3 estágios do QingKe V4F, nem BHT/BTB/RAS, nem
 * wait state de flash) dizem quantos ciclos o passo custa na placa. E o
 * CH32V307 não implementa mcycle/minstret — a lista de CSR do QingKe V4
 * (manual V1.5, tabela 8-1) não tem contador de performance nenhum.
 *
 * Quem conta é o SysTick, e o Zephyr já o configura em HCLK:
 * CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC = 144000000 no same_board, ou seja
 * k_cycle_get_32() devolve ciclo de núcleo 1:1, sem mexer em registrador.
 *
 * Ligar com -DHAL_PROFILE_OBSERVER=1 (ou aqui). O overhead das duas leituras
 * é medido no boot e descontado. Custa ~2 leituras por passo — desligado por
 * padrão para não entrar no caminho quente em produção. */
#ifndef HAL_PROFILE_OBSERVER
#define HAL_PROFILE_OBSERVER   0
#endif

/* A cada quantos passos o profiler despeja min/média/máx no log. */
#ifndef HAL_PROFILE_REPORT_EVERY
#define HAL_PROFILE_REPORT_EVERY   20000U
#endif

#endif /* HAL_PRM_H */
