/*
 * SPMM — Sistema Provisorio de Medicao Mecanica
 * Config compartilhada, parametros do motor e protocolo de log UDP.
 *
 * O firmware apenas COMANDA (throttle do ESC), MEDE (Iq das fases, rotacao)
 * e TRANSMITE. Todo o pos-processamento (regressao B/tc, derivada dw/dt, J)
 * e' feito offline no PC (scripts/spmm_bench.py). Ver CLAUDE.md.
 */
#ifndef SPMM_H
#define SPMM_H

#include <stdint.h>
#include <math.h>

/* M_PI nao e' garantido pela ISO C (-std=c17); math.h so' o expoe com
 * _GNU_SOURCE/_USE_MATH_DEFINES. Definir fallback para o native_sim. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ====================================================================
 * Motor sob ensaio: Scorpion A-5025-310Kv, 14 polos (Delta)
 * ==================================================================== */
#define SPMM_POLE_PAIRS        7U          /* 14 polos / 2                     */
#define SPMM_KV                310.0f       /* rpm/V                           */
/* Constante de torque KT [N.m/A] = 9.5493 / Kv. Escala Te = KT * Iq.
 * Como o PC faz a regressao (B = inclinacao, tc = intercepto), um erro de
 * escala em KT vira erro de escala em B/tc — calibravel na bancada. */
#define SPMM_KT                (9.5493f / SPMM_KV)

/* ====================================================================
 * Rotacao (RPM) — TIM3 CH3 (PB0), input capture
 * ==================================================================== */
#define SPMM_RPM_PULSES_PER_REV  7U        /* sensor: 7 pulsos/volta (14 polos)*/
#define SPMM_RPM_SAMPLE_MS       100U      /* janela de contagem de pulsos     */

/* ====================================================================
 * ESC — PWM de servo, TIM4 CH1 (PB6)
 * ==================================================================== */
#define SPMM_ESC_PERIOD_US     20000U      /* 50 Hz                            */
#define SPMM_ESC_MIN_US         1000U      /* parado / disarm                  */
#define SPMM_ESC_MAX_US         2000U      /* maximo                           */
#define SPMM_ESC_ARM_US         1000U

/* ADC — dq real: ADC1=Ia/Ib/Ic (PA0-2), ADC2=Va/Vb/Vc (PA3-5) -> theta_e. */
#define SPMM_ADC_NUM_CH        3           /* canais por instancia (ADC1/ADC2) */
#define SPMM_ADC_IDX_A         0           /* Ia / Va                          */
#define SPMM_ADC_IDX_B         1           /* Ib / Vb                          */
#define SPMM_ADC_IDX_C         2           /* Ic / Vc                          */
#define SPMM_ADC_RESOLUTION    12
#define SPMM_ADC_VREF_MV       3300U
#define SPMM_ADC_FULL_SCALE    4096U

/* Canais fisicos ADC (CH0-CH5) usados em cada instancia */
#define SPMM_ADC1_CH_IA        0           /* PA0                              */
#define SPMM_ADC1_CH_IB        1           /* PA1                              */
#define SPMM_ADC1_CH_IC        2           /* PA2                              */
#define SPMM_ADC2_CH_VA        3           /* PA3                              */
#define SPMM_ADC2_CH_VB        4           /* PA4                              */
#define SPMM_ADC2_CH_VC        5           /* PA5                              */

/* Conversao tensao-no-pino -> corrente [A]. Sensor de corrente (shunt+amp ou
 * hall) bidirecional centrado em Vref/2. PROVISORIO — calibrar na bancada.
 *   I[A] = (v_mv - SPMM_I_OFFSET_MV) * SPMM_I_AMPS_PER_MV                    */
#define SPMM_I_OFFSET_MV       1650.0f     /* Vref/2 (sensor centrado)         */
#define SPMM_I_AMPS_PER_MV     0.05f       /* ganho do sensor (placeholder)    */

/* Conversao tensao-no-pino -> tensao de fase [V] (divisor resistivo na
 * entrada). PROVISORIO — calibrar na bancada.
 *   V[V] = v_mv * SPMM_V_VOLTS_PER_MV                                        */
#define SPMM_V_VOLTS_PER_MV    0.02f       /* divisor ~1:20 (placeholder)      */

/* Offset fixo entre o angulo derivado do vetor de tensao e o eixo de fluxo
 * (o vetor de tensao lidera o fluxo). Calibravel na bancada; so gira a
 * separacao Id/Iq por uma constante. */
#define SPMM_THETA_OFFSET_RAD  0.0f

/* Filtros (media movel exponencial). O ESC comuta em 6-step -> Iq/tensao tem
 * ripple harmonico; usamos a componente fundamental (torque medio). */
#define SPMM_IQ_FILTER_ALPHA   0.02f

/* ====================================================================
 * Snapshot da medicao completa (acquisition.c publica; telemetry/shell leem)
 * ==================================================================== */
typedef struct {
    float ia, ib, ic;   /* correntes de fase [A]                              */
    float va, vb, vc;   /* tensoes de fase   [V]                              */
    float id, iq;       /* eixo dq [A] (Park com theta_e da tensao)           */
    float theta_e;      /* angulo eletrico estimado [rad]                     */
} spmm_meas_t;

void spmm_get_meas(spmm_meas_t *out);   /* copia coerente da ultima medida    */

/* ====================================================================
 * Getters compartilhados (fonte unica: cada modulo publica seu valor)
 * ==================================================================== */
float    spmm_get_omega(void);   /* rad/s mecanico (rpm.c)                    */
float    spmm_get_rpm(void);     /* rpm mecanico   (rpm.c)                    */
float    spmm_get_iq(void);      /* Iq medio filtrado [A] (acquisition.c)    */
float    spmm_get_te(void);      /* Te = KT * Iq [N.m]                        */

static inline float spmm_rpm_to_rads(float rpm) { return rpm * 2.0f * (float)M_PI / 60.0f; }

/* ====================================================================
 * ESC (esc.c)
 * ==================================================================== */
int      esc_init(void);
void     esc_arm(void);
void     esc_disarm(void);
int      esc_set_throttle_us(uint32_t us);   /* 1000..2000; exige armado      */
uint32_t esc_get_throttle_us(void);
int      esc_ramp_us(uint32_t target_us, uint32_t secs);  /* rampa suave      */

/* ====================================================================
 * RPM (rpm.c) / Acquisition (acquisition.c)
 * ==================================================================== */
int      rpm_init(void);
void     rpm_start(void);         /* cria a thread de medicao                 */
int      acquisition_init(void);
void     acquisition_start(void); /* cria a thread de aquisicao (prio ALTA)   */

/* ====================================================================
 * Telemetria / log UDP (telemetry.c)
 * ==================================================================== */
int      telemetry_init(void);
void     telemetry_start_log(uint32_t hz);   /* comeca streaming (n, w, Te)   */
void     telemetry_stop_log(void);
uint32_t telemetry_get_ts_us(void);          /* Ts atual em us (0 = parado)   */

/* Protocolo UDP de log, binario/little-endian/sem padding. PC reconstroi
 * t = n*Ts (contador de amostra, nao o horario de chegada do pacote). */
#pragma pack(push, 1)
typedef struct {
    uint32_t n;        /* indice da amostra; tempo = n * Ts (reconstruido PC) */
    float    omega;    /* rad/s (mecanico)                                    */
    float    te;       /* N.m  (= KT * iq)                                    */
    float    iq;       /* A (eixo q filtrado — corrente de torque)            */
    float    id;       /* A (eixo d)                                          */
    float    theta_e;  /* rad (angulo eletrico estimado da tensao)            */
    float    ia, ib, ic;  /* A — correntes de fase instantaneas              */
    float    va, vb, vc;  /* V — tensoes de fase instantaneas                */
} spmm_sample_t;       /* 4 + 11*4 = 48 bytes (medicao completa)              */

typedef struct {
    uint16_t      count;   /* numero de amostras neste pacote                 */
    uint16_t      ts_us;   /* Ts em microssegundos (PC reconstroi t = n*Ts)   */
    spmm_sample_t s[];     /* count amostras                                  */
} spmm_pkt_hdr_t;
#pragma pack(pop)

#define SPMM_UDP_LOG_PORT      5000U        /* PC faz bind aqui                */

#endif /* SPMM_H */
