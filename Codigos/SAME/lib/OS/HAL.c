/*
 * HAL.c — SAME Board (só aquisição elétrica; a SAME não aciona o motor).
 *
 * Dois ADCs convencionais do CH32V307 (driver custom drivers/adc/adc_ch32v307.c):
 *   ADC1 → V_a,V_b,V_c (canais 0,1,2 = PA0,PA1,PA2)
 *   ADC2 → i_a,i_b,i_c (canais 3,4,5 = PA3,PA4,PA5)
 *
 * Uma única thread de aquisição bloqueia na conversão (evento "ADC pronto"),
 * calibra, faz Clarke (αβ), roda UM passo do observador (Observer_Step) e —
 * decimado no tempo — publica os 13 campos do TELEMETRY_LOG via OSSI. Nenhuma
 * outra thread de hardware existe.
 *
 * native_sim (POSIX): o driver custom não compila para PC; em vez de ler os
 * ADCs, a thread SINTETIZA um motor girando a HAL_SIM_DEMO_RPM (3 tensões +
 * 3 correntes senoidais na frequência elétrica), avançando θe pelo MESMO Ts
 * que o observador usa. A cadeia ADC→Clarke→SMO→PLL→telemetria roda inteira
 * no host e rpm_same converge para HAL_SIM_DEMO_RPM. Mesmo espírito dos blocos
 * `#ifdef CONFIG_ARCH_POSIX` da SIBA.
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

/* Passos de observador por iteração do laço. No hardware = 1 (uma conversão =
 * um passo). No native_sim, um burst comprime o tempo-de-motor para a PLL
 * convergir em tempo de parede (ver nota no laço). */
#ifdef CONFIG_ARCH_POSIX
#define HAL_ACQ_BURST   1000
#else
#define HAL_ACQ_BURST   1
#endif

/* ===========================================================
 * Calibração afim por sensor — índice = target_type (0..5). Fonte única de
 * verdade (CMD_Core/shell alteram via HAL_ADC_Set_Calib). Mapa canal→sensor:
 *   i_a←CURRENT_A  i_b←CURRENT_B  i_c←CURRENT_C
 *   V_a←VOLTAGE_A  V_b←VOLTAGE_B  V_c←VOLTAGE_C  (calibração própria cada uma)
 * =========================================================== */
struct hal_calib { float linear; float offset; };
static struct hal_calib adc_calib[CMD_CORE_SENSOR_COUNT];

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

void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset)
{
    if (sensor_id >= CMD_CORE_SENSOR_COUNT) return;
    adc_calib[sensor_id].linear = linear;
    adc_calib[sensor_id].offset = offset;
}

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

static inline float apply_calib(int sensor_id, float mv)
{
    return mv * adc_calib[sensor_id].linear + adc_calib[sensor_id].offset;
}

/* ===========================================================
 * Aquisição — hardware (2 ADCs) vs synthetic (POSIX)
 * =========================================================== */
#if HAL_ENABLE_ADC && !defined(CONFIG_ARCH_POSIX)

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

static inline float raw_to_mv(uint16_t raw)
{
    return (float)raw * (float)HAL_ADC_VREF_MV / (float)HAL_ADC_FULL_SCALE;
}

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

/* Lê os 2 ADCs e devolve grandezas físicas calibradas + telemetria bruta. */
static int hal_acquire(float *Va, float *Vb, float *Vc,
                       float *ia, float *ib, float *ic)
{
    if (adc_read(adc1_dev, &adc1_seq) != 0) return -EIO;
    if (adc_read(adc2_dev, &adc2_seq) != 0) return -EIO;

    *Va = apply_calib(TARGET_VOLTAGE_A, raw_to_mv(adc1_buf[0]));
    *Vb = apply_calib(TARGET_VOLTAGE_B, raw_to_mv(adc1_buf[1]));
    *Vc = apply_calib(TARGET_VOLTAGE_C, raw_to_mv(adc1_buf[2]));
    *ia = apply_calib(TARGET_CURRENT_A,  raw_to_mv(adc2_buf[0]));
    *ib = apply_calib(TARGET_CURRENT_B,  raw_to_mv(adc2_buf[1]));
    *ic = apply_calib(TARGET_CURRENT_C,  raw_to_mv(adc2_buf[2]));
    return 0;
}

#elif HAL_ENABLE_ADC && defined(CONFIG_ARCH_POSIX)

/* ---- Synthetic (native_sim): motor virtual a HAL_SIM_DEMO_RPM ---- */
#define SIM_P        7.0f
#define SIM_KV_Y     (310.0f / 1.7320508075688772f)
#define SIM_TS       10.0e-6f                 /* == DEF_TS do observador */
#define SIM_I_AMP    8.0f                     /* A (pico) */
#define SIM_LOAD_ANG 0.30f                    /* rad — atraso corrente x BEMF */

static float sim_theta_e;

static int hal_adc_hw_init(void) { LOG_INF("HAL: ADC synthetic (native_sim)"); return 0; }

static int hal_acquire(float *Va, float *Vb, float *Vc,
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

    /* Calibração aplicada TAMBÉM no synthetic (antes só a leitura de HW real
     * chamava apply_calib) — senão um CMD de calibração enviado durante o
     * native_sim não tem efeito NENHUM observável na telemetria, o que
     * inviabiliza testar o fluxo de calibração sem bancada física. Defaults
     * (linear=1, offset=0) deixam o sinal sintético idêntico a antes. */
    float th = sim_theta_e;
    *Va = apply_calib(TARGET_VOLTAGE_A, E * sinf(th));
    *Vb = apply_calib(TARGET_VOLTAGE_B, E * sinf(th - TWO_PI_3));
    *Vc = apply_calib(TARGET_VOLTAGE_C, E * sinf(th - FOUR_PI_3));
    *ia = apply_calib(TARGET_CURRENT_A, SIM_I_AMP * sinf(th - SIM_LOAD_ANG));
    *ib = apply_calib(TARGET_CURRENT_B, SIM_I_AMP * sinf(th - SIM_LOAD_ANG - TWO_PI_3));
    *ic = apply_calib(TARGET_CURRENT_C, SIM_I_AMP * sinf(th - SIM_LOAD_ANG - FOUR_PI_3));
    return 0;
}

#else  /* HAL_ENABLE_ADC == 0 */

static int hal_adc_hw_init(void) { LOG_INF("HAL: ADC desabilitado (HAL_prm.h)"); return 0; }
static int hal_acquire(float *Va, float *Vb, float *Vc,
                       float *ia, float *ib, float *ic)
{
    *Va = *Vb = *Vc = *ia = *ib = *ic = 0.0f;
    return 0;
}
#endif

int HAL_ADC_Init(void)
{
    hal_adc_calib_init();   /* software puro — sempre roda */
    return hal_adc_hw_init();
}

/* ===========================================================
 * Thread de aquisição — o laço de eventos do observador
 * =========================================================== */
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

static void HAL_thread_ADC_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    LOG_INF("HAL: thread de aquisição iniciada");
    uint32_t last_pub = k_uptime_get_32();

    float Va = 0, Vb = 0, Vc = 0, ia = 0, ib = 0, ic = 0;
    float V_alpha = 0, V_beta = 0, i_alpha = 0, i_beta = 0;

    while (1) {
        /* Aplica reconfiguração pendente (reinicia o observador se um
         * parâmetro/ganho mudou por CMD) — topo do laço, fora do lock. */
        Observer_Apply_Pending();

        /* No hardware, cada conversão = 1 passo. No native_sim, um "burst" de
         * passos por iteração comprime o tempo-de-motor (o ADC real roda a
         * ~333 kSps; sem o burst, os 2 ms de k_sleep dariam poucos passos e a
         * PLL nunca convergiria em tempo de parede). Ver HAL_ACQ_BURST. */
        for (int b = 0; b < HAL_ACQ_BURST; b++) {
            if (hal_acquire(&Va, &Vb, &Vc, &ia, &ib, &ic) != 0) {
                k_sleep(K_MSEC(1));
                break;
            }
            /* Clarke (amplitude-invariante, i_a+i_b+i_c=0):
             *   α = fase_a ; β = (fase_a + 2·fase_b)/√3 (ver test_coeff_auto.py) */
            i_alpha = ia;
            i_beta  = (ia + 2.0f * ib) * INV_SQRT3;
            V_alpha = Va;
            V_beta  = (Va + 2.0f * Vb) * INV_SQRT3;

            /* UM passo do observador, colado ao evento de conversão. */
            Observer_Step(V_alpha, V_beta, i_alpha, i_beta);
        }

        /* Telemetria decimada no tempo (não por contagem de amostras). */
        uint32_t now = k_uptime_get_32();
        if ((now - last_pub) >= HAL_TELEM_PUBLISH_MS) {
            last_pub = now;
            hal_publish_telemetry(Va, Vb, Vc, ia, ib, ic,
                                  V_alpha, V_beta, i_alpha, i_beta);
        }

#ifdef CONFIG_ARCH_POSIX
        /* adc_emul/synthetic "converte" instantaneamente — sem uma pausa o
         * laço nunca cede e trava o scheduler/clock do native_sim. ~500 Hz
         * de laço, telemetria a 50 Hz. */
        k_sleep(K_MSEC(2));
#endif
    }
}

/* ===========================================================
 * HAL_Init
 * =========================================================== */
K_THREAD_STACK_DEFINE(hal_adc_stack, HAL_STACK_SIZE);
static struct k_thread hal_adc_thread_data;

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
