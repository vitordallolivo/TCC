/*
 * Aquisicao — MEDICAO COMPLETA das fases do BLDC.
 *   ADC1 (DMA): Ia, Ib, Ic  (correntes)
 *   ADC2 (DMA): Va, Vb, Vc  (tensoes)
 *
 * Pipeline (mesmo codigo na placa e em native_sim):
 *   1. Clarke nas tensoes  -> (Valpha, Vbeta) -> theta_e = atan2(Vbeta,Valpha)
 *      (o vetor de tensao da o angulo eletrico; resolve a falta de sensor de
 *       posicao absoluta — so ha sensor de rotacao de 7 pulsos/volta).
 *   2. Clarke nas correntes -> (Ialpha, Ibeta)
 *   3. Park com theta_e     -> Id, Iq
 *   4. Iq filtrado (EMA, remove ripple do 6-step) -> Te = KT * Iq
 *
 * Prioridade ALTA (ver main.c): nunca pode ser preemptada pelo shell/telemetria.
 *
 * native_sim (CONFIG_ARCH_POSIX): sem ADC de hardware -> sintetiza as 6 fases
 * a partir de omega e passa pelo MESMO pipeline (testa Clarke/Park no host).
 */
#include "spmm.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(acq, LOG_LEVEL_INF);

#define ACQ_STACK_SIZE 2048
#define ACQ_PRIORITY   4           /* ALTA — acima de rpm(6)/telemetria(7)     */
#define ACQ_PERIOD_MS  1           /* ~1 kHz de atualizacao                    */

static K_THREAD_STACK_DEFINE(acq_stack, ACQ_STACK_SIZE);
static struct k_thread acq_thread;

/* Ultima medida completa + Iq filtrado, protegidos por spinlock */
static struct k_spinlock  meas_lock;
static spmm_meas_t        g_meas;
static float              iq_filt;

void spmm_get_meas(spmm_meas_t *out)
{
    k_spinlock_key_t key = k_spin_lock(&meas_lock);
    *out = g_meas;
    k_spin_unlock(&meas_lock, key);
}

float spmm_get_iq(void)
{
    k_spinlock_key_t key = k_spin_lock(&meas_lock);
    float v = iq_filt;
    k_spin_unlock(&meas_lock, key);
    return v;
}

float spmm_get_te(void) { return SPMM_KT * spmm_get_iq(); }

/* Clarke invariante em amplitude (sem neutro: Xc medido, nao assumido) */
static inline void clarke(float xa, float xb, float xc, float *xal, float *xbe)
{
    *xal = (2.0f * xa - xb - xc) * (1.0f / 3.0f);
    *xbe = (xb - xc) * (1.0f / 1.7320508f);   /* 1/sqrt(3) */
}

/* Nucleo do processamento — comum a placa real e ao native_sim */
static void process_measurement(float ia, float ib, float ic,
                                float va, float vb, float vc)
{
    float val, vbe, ial, ibe;
    clarke(va, vb, vc, &val, &vbe);
    clarke(ia, ib, ic, &ial, &ibe);

    float theta_e = atan2f(vbe, val) + SPMM_THETA_OFFSET_RAD;
    float ct = cosf(theta_e), st = sinf(theta_e);

    float id =  ial * ct + ibe * st;
    float iq = -ial * st + ibe * ct;

    k_spinlock_key_t key = k_spin_lock(&meas_lock);
    iq_filt += SPMM_IQ_FILTER_ALPHA * (iq - iq_filt);
    g_meas.ia = ia; g_meas.ib = ib; g_meas.ic = ic;
    g_meas.va = va; g_meas.vb = vb; g_meas.vc = vc;
    g_meas.id = id; g_meas.iq = iq_filt;
    g_meas.theta_e = theta_e;
    k_spin_unlock(&meas_lock, key);
}

static inline float raw_to_mv(uint16_t raw)
{
    return (float)raw * (float)SPMM_ADC_VREF_MV / (float)SPMM_ADC_FULL_SCALE;
}
static inline float mv_to_amp(float mv)  { return (mv - SPMM_I_OFFSET_MV) * SPMM_I_AMPS_PER_MV; }
static inline float mv_to_volt(float mv) { return mv * SPMM_V_VOLTS_PER_MV; }

#ifndef CONFIG_ARCH_POSIX
/* -------------------- Placa real: ADC1 (I) + ADC2 (V) via DMA ----------- */
#include <zephyr/drivers/adc.h>

static const struct device *adc1 = DEVICE_DT_GET(DT_NODELABEL(adc1));
static const struct device *adc2 = DEVICE_DT_GET(DT_NODELABEL(adc2));

static uint16_t buf_i[SPMM_ADC_NUM_CH];   /* Ia,Ib,Ic (ordem de canal)        */
static uint16_t buf_v[SPMM_ADC_NUM_CH];   /* Va,Vb,Vc                         */

static const struct adc_channel_cfg cfg_i[SPMM_ADC_NUM_CH] = {
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = SPMM_ADC1_CH_IA },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = SPMM_ADC1_CH_IB },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = SPMM_ADC1_CH_IC },
};
static const struct adc_channel_cfg cfg_v[SPMM_ADC_NUM_CH] = {
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = SPMM_ADC2_CH_VA },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = SPMM_ADC2_CH_VB },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = SPMM_ADC2_CH_VC },
};

static struct adc_sequence seq_i = {
    .channels    = BIT(SPMM_ADC1_CH_IA) | BIT(SPMM_ADC1_CH_IB) | BIT(SPMM_ADC1_CH_IC),
    .buffer      = buf_i, .buffer_size = sizeof(buf_i), .resolution = SPMM_ADC_RESOLUTION,
};
static struct adc_sequence seq_v = {
    .channels    = BIT(SPMM_ADC2_CH_VA) | BIT(SPMM_ADC2_CH_VB) | BIT(SPMM_ADC2_CH_VC),
    .buffer      = buf_v, .buffer_size = sizeof(buf_v), .resolution = SPMM_ADC_RESOLUTION,
};

int acquisition_init(void)
{
    if (!device_is_ready(adc1)) { LOG_ERR("ADC1 nao pronto"); return -ENODEV; }
    if (!device_is_ready(adc2)) {
        /* ADC2 pode falhar se o silicio nao rotear DMA — currents seguem OK */
        LOG_WRN("ADC2 nao pronto — tensoes indisponiveis (ver risco no DTS)");
    }
    for (int i = 0; i < SPMM_ADC_NUM_CH; i++) {
        int r = adc_channel_setup(adc1, &cfg_i[i]);
        if (r) { LOG_ERR("ADC1 ch%d setup: %d", i, r); return r; }
    }
    if (device_is_ready(adc2)) {
        for (int i = 0; i < SPMM_ADC_NUM_CH; i++) {
            int r = adc_channel_setup(adc2, &cfg_v[i]);
            if (r) { LOG_ERR("ADC2 ch%d setup: %d", i, r); return r; }
        }
    }
    LOG_INF("Aquisicao: ADC1(3 correntes) + ADC2(3 tensoes) configurados");
    return 0;
}

static int adc_read_signal(const struct device *dev, struct adc_sequence *seq)
{
    struct k_poll_signal sig;
    struct k_poll_event  evt;
    k_poll_signal_init(&sig);
    k_poll_event_init(&evt, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig);
    if (adc_read_async(dev, seq, &sig) != 0) return -EIO;
    if (k_poll(&evt, 1, K_MSEC(50)) == -EAGAIN) return -ETIMEDOUT;
    return 0;
}

static void acq_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    bool have_v = device_is_ready(adc2);
    LOG_INF("Aquisicao: thread iniciada");
    while (1) {
        if (adc_read_signal(adc1, &seq_i) != 0) { k_sleep(K_MSEC(5)); continue; }
        if (have_v) { (void)adc_read_signal(adc2, &seq_v); }

        float ia = mv_to_amp(raw_to_mv(buf_i[SPMM_ADC_IDX_A]));
        float ib = mv_to_amp(raw_to_mv(buf_i[SPMM_ADC_IDX_B]));
        float ic = mv_to_amp(raw_to_mv(buf_i[SPMM_ADC_IDX_C]));
        float va = have_v ? mv_to_volt(raw_to_mv(buf_v[SPMM_ADC_IDX_A])) : 0.0f;
        float vb = have_v ? mv_to_volt(raw_to_mv(buf_v[SPMM_ADC_IDX_B])) : 0.0f;
        float vc = have_v ? mv_to_volt(raw_to_mv(buf_v[SPMM_ADC_IDX_C])) : 0.0f;

        process_measurement(ia, ib, ic, va, vb, vc);
        k_sleep(K_MSEC(ACQ_PERIOD_MS));
    }
}

#else /* CONFIG_ARCH_POSIX — sintetiza as 6 fases e usa o MESMO pipeline */

int acquisition_init(void)
{
    LOG_INF("Aquisicao: modo synthetic (native_sim) — fases geradas de omega");
    return 0;
}

static void acq_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    LOG_INF("Aquisicao: thread synthetic iniciada");
    float theta = 0.0f;
    const float dt = ACQ_PERIOD_MS * 1e-3f;
    const float TWO_PI_3 = 2.0f * (float)M_PI / 3.0f;
    while (1) {
        float omega = spmm_get_omega();
        float we    = omega * (float)SPMM_POLE_PAIRS;   /* rad/s eletrico      */
        theta += we * dt;
        if (theta > 2.0f * (float)M_PI) theta -= 2.0f * (float)M_PI;

        float imag = 0.2f + 0.0004f * omega;            /* carga cresce com w   */
        float vmag = 0.05f * omega + 0.5f;              /* back-EMF ~ w         */
        /* corrente atrasa a tensao de um angulo de carga fixo (~15 graus) */
        float load = 0.26f;
        float ia = imag * cosf(theta - load);
        float ib = imag * cosf(theta - load - TWO_PI_3);
        float ic = imag * cosf(theta - load + TWO_PI_3);
        float va = vmag * cosf(theta);
        float vb = vmag * cosf(theta - TWO_PI_3);
        float vc = vmag * cosf(theta + TWO_PI_3);

        process_measurement(ia, ib, ic, va, vb, vc);
        k_sleep(K_MSEC(ACQ_PERIOD_MS));
    }
}
#endif /* CONFIG_ARCH_POSIX */

void acquisition_start(void)
{
    k_thread_create(&acq_thread, acq_stack, ACQ_STACK_SIZE,
                    acq_thread_fn, NULL, NULL, NULL,
                    ACQ_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&acq_thread, "acq");
}
