/**
 * @file HAL.c
 * @brief HAL da SAME: só aquisição elétrica, não aciona motor.
 *
 * ADC1 = V_a,V_b,V_c | ADC2 = i_a,i_b,i_c. Thread única bloqueia no ADC,
 * faz Clarke + Observer_Step, publica telemetria. native_sim: sintetiza
 * tensões/correntes senoidais em vez de ler ADC real.
 */

#include "HAL.h"
#include "OSSI.h"
#include "Log_Manager.h"           /* SENSOR_ID_* */
#include "CMD_Core_prm.h"          /* defaults de calibração (tabela) */
#include "Observer_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <math.h>
#ifndef CONFIG_ARCH_POSIX
#include <zephyr/drivers/adc.h>
#endif

LOG_MODULE_REGISTER(hal, LOG_LEVEL_INF);

/* A libc mínima do Zephyr não define M_PI (não é padrão C). */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define INV_SQRT3   0.5773502691896258f

/* Telemetria publicada ~50 Hz (independe da taxa do laço de aquisição). */
#define HAL_TELEM_PUBLISH_MS   20U

/* Passos do observador por iteração: 1 no hardware, burst no native_sim
 * pra PLL convergir em tempo de parede. */
#ifdef CONFIG_ARCH_POSIX
#define HAL_ACQ_BURST   1000
#else
#define HAL_ACQ_BURST   1
#endif

/* Calibração afim por sensor, indexada por target_type (CMD_Core altera
 * via HAL_ADC_Set_Calib). Cada fase de V e de i tem sua própria calibração. */
struct hal_calib { float linear; float offset; };
static struct hal_calib adc_calib[CMD_CORE_SENSOR_COUNT];

/** @brief Preenche adc_calib[] com os defaults de SAME_SENSOR_TABLE. */
static void hal_adc_calib_init(void)
{
    for (int i = 0; i < CMD_CORE_SENSOR_COUNT; i++) {
        adc_calib[i].linear = 1.0f;
        adc_calib[i].offset = 0.0f;
    }
#define X(TID, DEF_LIN, DEF_OFF, MINV, MAXV)   \
    adc_calib[(TID)].linear = (DEF_LIN);       \
    adc_calib[(TID)].offset = (DEF_OFF);
    SAME_SENSOR_TABLE(X)
#undef X
}

/**
 * @brief Ajusta a calibração afim (y = linear*x + offset) de um sensor.
 * @param sensor_id índice do sensor (target_type), ver CMD_Core_prm.h
 * @param linear ganho
 * @param offset deslocamento
 */
void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset)
{
    if (sensor_id >= CMD_CORE_SENSOR_COUNT) return;
    adc_calib[sensor_id].linear = linear;
    adc_calib[sensor_id].offset = offset;
}

/**
 * @brief Lê a calibração atual de um sensor.
 * @param sensor_id índice do sensor (target_type)
 * @param linear [out] ganho atual
 * @param offset [out] deslocamento atual
 */
void HAL_ADC_Get_Calib(uint8_t sensor_id, float *linear, float *offset)
{
    float l = 1.0f, o = 0.0f;
    if (sensor_id < CMD_CORE_SENSOR_COUNT) {
        l = adc_calib[sensor_id].linear;
        o = adc_calib[sensor_id].offset;
    }
    if (linear) *linear = l;
    if (offset) *offset = o;
}

/**
 * @brief Aplica a calibração afim de um sensor a uma leitura em mV.
 * @param sensor_id índice do sensor (target_type)
 * @param mv tensão bruta no pino (mV)
 * @return valor calibrado (unidade física do sensor)
 */
static inline float apply_calib(int sensor_id, float mv)
{
    return mv * adc_calib[sensor_id].linear + adc_calib[sensor_id].offset;
}

/* Só a fonte da amostra bruta muda por plataforma (ADC real vs seno
 * sintético), isolada em hal_adc_hw_init()/hal_read_raw(). A calibração
 * (hal_acquire, abaixo) é uma única implementação para as duas. */
#if HAL_ENABLE_ADC

#ifndef CONFIG_ARCH_POSIX
/* ---- Hardware real: 2 ADCs do CH32V307 ---- */

#define ADC1_NODE DT_NODELABEL(adc1)
#define ADC2_NODE DT_NODELABEL(adc2)
static const struct device *adc1_dev = DEVICE_DT_GET(ADC1_NODE);
static const struct device *adc2_dev = DEVICE_DT_GET(ADC2_NODE);

#define ADC_RESOLUTION  12

/* ADC1 → tensões (canais 0,1,2) ; ADC2 → correntes (canais 3,4,5). */
static uint16_t adc1_buf[HAL_ADC1_NUM_CH];
static uint16_t adc2_buf[HAL_ADC2_NUM_CH];

static const struct adc_channel_cfg adc1_ch_cfg[HAL_ADC1_NUM_CH] = {
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL, .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 0 },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL, .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 1 },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL, .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 2 },
};
static const struct adc_channel_cfg adc2_ch_cfg[HAL_ADC2_NUM_CH] = {
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL, .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 3 },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL, .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 4 },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL, .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 5 },
};

static struct adc_sequence adc1_seq = {
    .channels = BIT(0) | BIT(1) | BIT(2),
    .buffer = adc1_buf, .buffer_size = sizeof(adc1_buf), .resolution = ADC_RESOLUTION,
};
static struct adc_sequence adc2_seq = {
    .channels = BIT(3) | BIT(4) | BIT(5),
    .buffer = adc2_buf, .buffer_size = sizeof(adc2_buf), .resolution = ADC_RESOLUTION,
};

/**
 * @brief Converte contagem bruta do ADC (12 bits) para tensão no pino (mV).
 * @param raw contagem bruta (0..HAL_ADC_FULL_SCALE-1)
 * @return tensão em mV
 */
static inline float raw_to_mv(uint16_t raw)
{
    return (float)raw * (float)HAL_ADC_VREF_MV / (float)HAL_ADC_FULL_SCALE;
}

/**
 * @brief Inicializa e configura os canais de ADC1/ADC2 (hardware real).
 * @return 0 em sucesso, ou -ENODEV/erro do adc_channel_setup
 */
static int hal_adc_hw_init(void)
{
    if (!device_is_ready(adc1_dev) || !device_is_ready(adc2_dev)) {
        LOG_ERR("ADC1/ADC2 não prontos");
        return -ENODEV;
    }
    for (int i = 0; i < HAL_ADC1_NUM_CH; i++) {
        int r = adc_channel_setup(adc1_dev, &adc1_ch_cfg[i]);
        if (r) { LOG_ERR("ADC1 setup ch%d: %d", i, r); return r; }
    }
    for (int i = 0; i < HAL_ADC2_NUM_CH; i++) {
        int r = adc_channel_setup(adc2_dev, &adc2_ch_cfg[i]);
        if (r) { LOG_ERR("ADC2 setup ch%d: %d", i, r); return r; }
    }
    LOG_INF("HAL: ADC1(3 tensões)+ADC2(3 correntes) configurados");
    return 0;
}

/**
 * @brief Lê os 2 ADCs -> valores brutos em mV (calibração é em hal_acquire).
 *
 * As duas chamadas parecem sequenciais mas não são: o driver dispara ADC1+ADC2
 * em modo dual simultâneo (DUALMOD), então adc_read(adc2) só espera o
 * semáforo já sinalizado pela ISR do ADC1 (ADC2 não tem DMA próprio no
 * CH32V307 — ver driver).
 *
 * @param Va, Vb, Vc [out] tensões de fase brutas (mV)
 * @param ia, ib, ic [out] correntes de fase brutas (mV no shunt/sensor)
 * @return 0 em sucesso, -EIO se a leitura falhar
 */
static int hal_read_raw(float *Va, float *Vb, float *Vc,
                        float *ia, float *ib, float *ic)
{
    if (adc_read(adc1_dev, &adc1_seq) != 0) return -EIO;
    if (adc_read(adc2_dev, &adc2_seq) != 0) return -EIO;
    *Va = raw_to_mv(adc1_buf[0]);
    *Vb = raw_to_mv(adc1_buf[1]);
    *Vc = raw_to_mv(adc1_buf[2]);
    *ia = raw_to_mv(adc2_buf[0]);
    *ib = raw_to_mv(adc2_buf[1]);
    *ic = raw_to_mv(adc2_buf[2]);
    return 0;
}

#else /* CONFIG_ARCH_POSIX */
/* ---- Synthetic (native_sim): motor virtual a HAL_SIM_DEMO_RPM ---- */
#define SIM_P        7.0f
#define SIM_KV_Y     (310.0f / 1.7320508075688772f)
#define SIM_TS       10.0e-6f                 /* == DEF_TS do observador */
#define SIM_I_AMP    8.0f                     /* A (pico) */
#define SIM_LOAD_ANG 0.30f                    /* rad — atraso corrente x BEMF */

static float sim_theta_e;

/** @brief native_sim: stub (não há ADC real). @return sempre 0. */
static int hal_adc_hw_init(void) { LOG_INF("HAL: ADC synthetic (native_sim)"); return 0; }

/**
 * @brief native_sim: sintetiza um motor virtual girando a HAL_SIM_DEMO_RPM
 * (grandezas já em V/A; calibração aplicada em hal_acquire).
 * @param Va, Vb, Vc [out] tensões de fase sintéticas (V)
 * @param ia, ib, ic [out] correntes de fase sintéticas (A)
 * @return sempre 0
 */
static int hal_read_raw(float *Va, float *Vb, float *Vc,
                        float *ia, float *ib, float *ic)
{
    const float TWO_PI_3 = 2.0943951023931953f;
    const float FOUR_PI_3 = 4.1887902047863905f;
    float wm = HAL_SIM_DEMO_RPM * (2.0f * (float)M_PI / 60.0f);  /* rad/s mec */
    float we = wm * SIM_P;                                        /* rad/s elétrico */
    float ke_mec = 60.0f / (2.0f * (float)M_PI * SIM_KV_Y);
    float E = ke_mec * wm;                                        /* amplitude BEMF */

    sim_theta_e += we * SIM_TS;
    if (sim_theta_e >= 2.0f * (float)M_PI) sim_theta_e -= 2.0f * (float)M_PI;

    float th = sim_theta_e;
    *Va = E * sinf(th);
    *Vb = E * sinf(th - TWO_PI_3);
    *Vc = E * sinf(th - FOUR_PI_3);
    *ia = SIM_I_AMP * sinf(th - SIM_LOAD_ANG);
    *ib = SIM_I_AMP * sinf(th - SIM_LOAD_ANG - TWO_PI_3);
    *ic = SIM_I_AMP * sinf(th - SIM_LOAD_ANG - FOUR_PI_3);
    return 0;
}
#endif /* CONFIG_ARCH_POSIX */

/**
 * @brief Lê a fonte bruta (real ou sintética) e aplica a calibração por
 * sensor — implementação única, compartilhada pelas duas plataformas.
 * @param Va, Vb, Vc [out] tensões de fase calibradas (V)
 * @param ia, ib, ic [out] correntes de fase calibradas (A)
 * @return 0 em sucesso, -EIO se hal_read_raw falhar
 */
static int hal_acquire(float *Va, float *Vb, float *Vc,
                       float *ia, float *ib, float *ic)
{
    float rVa, rVb, rVc, ria, rib, ric;
    if (hal_read_raw(&rVa, &rVb, &rVc, &ria, &rib, &ric) != 0) return -EIO;
    *Va = apply_calib(TARGET_VOLTAGE_A, rVa);
    *Vb = apply_calib(TARGET_VOLTAGE_B, rVb);
    *Vc = apply_calib(TARGET_VOLTAGE_C, rVc);
    *ia = apply_calib(TARGET_CURRENT_A, ria);
    *ib = apply_calib(TARGET_CURRENT_B, rib);
    *ic = apply_calib(TARGET_CURRENT_C, ric);
    return 0;
}

#else  /* HAL_ENABLE_ADC == 0 */

/** @brief Stub (HAL_ENABLE_ADC=0). @return sempre 0. */
static int hal_adc_hw_init(void) { LOG_INF("HAL: ADC desabilitado (HAL_prm.h)"); return 0; }
/**
 * @brief Stub (HAL_ENABLE_ADC=0): zera todas as saídas.
 * @param Va, Vb, Vc [out] sempre 0.0f
 * @param ia, ib, ic [out] sempre 0.0f
 * @return sempre 0
 */
static int hal_acquire(float *Va, float *Vb, float *Vc,
                       float *ia, float *ib, float *ic)
{
    *Va = *Vb = *Vc = *ia = *ib = *ic = 0.0f;
    return 0;
}
#endif

/**
 * @brief Inicializa calibração e ADCs (ou stub, se HAL_ENABLE_ADC=0).
 * @return 0 em sucesso, ou erro de hal_adc_hw_init
 */
int HAL_ADC_Init(void)
{
    hal_adc_calib_init();   /* sempre roda, independe do hardware */
    return hal_adc_hw_init();
}

/**
 * @brief Publica os campos elétricos/estimados no TELEMETRY_LOG via OSSI.
 * @param Va, Vb, Vc tensões de fase (V)
 * @param ia, ib, ic correntes de fase (A)
 * @param Val, Vbe tensão em αβ (V)
 * @param ial, ibe corrente em αβ (A)
 */
static void hal_publish_telemetry(float Va, float Vb, float Vc,
                                  float ia, float ib, float ic,
                                  float Val, float Vbe, float ial, float ibe)
{
    OSSI_Submit_Sensor(SENSOR_ID_V_A, Va);
    OSSI_Submit_Sensor(SENSOR_ID_V_B, Vb);
    OSSI_Submit_Sensor(SENSOR_ID_V_C, Vc);
    OSSI_Submit_Sensor(SENSOR_ID_I_A, ia);
    OSSI_Submit_Sensor(SENSOR_ID_I_B, ib);
    OSSI_Submit_Sensor(SENSOR_ID_I_C, ic);
    OSSI_Submit_Sensor(SENSOR_ID_V_ALPHA, Val);
    OSSI_Submit_Sensor(SENSOR_ID_V_BETA,  Vbe);
    OSSI_Submit_Sensor(SENSOR_ID_I_ALPHA, ial);
    OSSI_Submit_Sensor(SENSOR_ID_I_BETA,  ibe);
    OSSI_Submit_Sensor(SENSOR_ID_RPM_SAME,        Observer_Get_Rpm());
    OSSI_Submit_Sensor(SENSOR_ID_TORQUE_ELETRICO, Observer_Get_Te());
    OSSI_Submit_Sensor(SENSOR_ID_TORQUE_HELICE,   Observer_Get_Torque_Helice());
}

/**
 * @brief Thread de aquisição: bloqueia na conversão do ADC, roda Clarke +
 * Observer_Step, e publica telemetria decimada no tempo. Nunca retorna.
 * @param p1, p2, p3 não usados (assinatura exigida por k_thread_create)
 */
static void HAL_thread_ADC_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    LOG_INF("HAL: thread de aquisição iniciada");
    uint32_t last_pub = k_uptime_get_32();

    float Va = 0, Vb = 0, Vc = 0, ia = 0, ib = 0, ic = 0;
    float V_alpha = 0, V_beta = 0, i_alpha = 0, i_beta = 0;

    while (1) {
        /* Reinicia o observador se um parâmetro/ganho mudou por CMD. */
        Observer_Apply_Pending();

        /* HAL_ACQ_BURST: 1 conversão = 1 passo no hardware; no native_sim
         * comprime várias iterações pra PLL convergir em tempo de parede. */
        for (int b = 0; b < HAL_ACQ_BURST; b++) {
            if (hal_acquire(&Va, &Vb, &Vc, &ia, &ib, &ic) != 0) {
                k_sleep(K_MSEC(1));
                break;
            }
            /* Clarke amplitude-invariante (i_a+i_b+i_c=0). */
            i_alpha = ia;
            i_beta  = (ia + 2.0f * ib) * INV_SQRT3;
            V_alpha = Va;
            V_beta  = (Va + 2.0f * Vb) * INV_SQRT3;

            Observer_Step(V_alpha, V_beta, i_alpha, i_beta);
        }

        /* Telemetria decimada no tempo, não por amostra. */
        uint32_t now = k_uptime_get_32();
        if ((now - last_pub) >= HAL_TELEM_PUBLISH_MS) {
            last_pub = now;
            hal_publish_telemetry(Va, Vb, Vc, ia, ib, ic,
                                  V_alpha, V_beta, i_alpha, i_beta);
        }

#ifdef CONFIG_ARCH_POSIX
        /* Synthetic "converte" na hora; sem pausa o laço trava o clock do
         * native_sim. ~500 Hz de laço, telemetria a 50 Hz. */
        k_sleep(K_MSEC(2));
#endif
    }
}

K_THREAD_STACK_DEFINE(hal_adc_stack, HAL_STACK_SIZE);
static struct k_thread hal_adc_thread_data;

/**
 * @brief Inicializa os ADCs e sobe a thread de aquisição/observador.
 * @return 0 em sucesso, ou erro de HAL_ADC_Init
 */
int HAL_Init(void)
{
    int r = HAL_ADC_Init();
    if (r) return r;

    k_thread_create(&hal_adc_thread_data, hal_adc_stack,
                    K_THREAD_STACK_SIZEOF(hal_adc_stack),
                    HAL_thread_ADC_fn, NULL, NULL, NULL,
                    HAL_ADC_PRIO, 0, K_NO_WAIT);
    k_thread_name_set(&hal_adc_thread_data, "hal_adc");

    LOG_INF("HAL_Init concluído");
    return 0;
}
