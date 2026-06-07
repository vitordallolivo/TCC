/*
 * HAL.c — SIBA Board
 *
 * Hardware Abstraction Layer para:
 *   - ADC (via driver adc_ch32v307, DMA circular)
 *   - HX711 x6 (bit-bang GPIO, protocolo serial síncrono)
 *   - ESC PWM (TIM4 CH1, PB6, 50 Hz)
 *   - RPM counter (TIM3 CH3, PB0, input capture)
 *
 * Todas as threads são criadas pelo TCKEC via OSSI.
 * A comunicação para cima usa OSSI → fila_sys com REQ_TYPE_SYS_UPDATE_SENSOR.
 *
 * PINOS HX711 — AJUSTE CONFORME SEU ESQUEMÁTICO:
 *   Célula 0: SCK=PA1  DOUT=PA2
 *   Célula 1: SCK=PA3  DOUT=PA4
 *   Célula 2: SCK=PA5  DOUT=PA6
 *   Célula 3: SCK=PB1  DOUT=PB2   (PB0 reservado para TIM3/RPM)
 *   Célula 4: SCK=PB3  DOUT=PB4
 *   Célula 5: SCK=PB5  DOUT=PC0
 */

#include "HAL.h"
#include "OSSI.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/irq.h>

LOG_MODULE_REGISTER(hal, LOG_LEVEL_DBG);

/* ===========================================================
 * ADC
 * =========================================================== */
#define ADC_NODE DT_NODELABEL(adc1)
static const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);

#define ADC_NUM_CHANNELS  1
#define ADC_RESOLUTION    12
#define ADC_OVERSAMPLE    8

static uint16_t adc_seq_buf[ADC_NUM_CHANNELS];
static atomic_t adc_raw[ADC_NUM_CHANNELS];

static const struct adc_channel_cfg adc_ch_cfg[] = {
    {
        .gain             = ADC_GAIN_1,
        .reference        = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id       = 0,   /* PA0 */
    },
};

static struct adc_sequence adc_seq = {
    .channels    = BIT(0),
    .buffer      = adc_seq_buf,
    .buffer_size = sizeof(adc_seq_buf),
    .resolution  = ADC_RESOLUTION,
};

int HAL_ADC_Init(void)
{
    if (!device_is_ready(adc_dev)) {
        LOG_ERR("ADC não pronto");
        return -ENODEV;
    }
    for (int i = 0; i < ADC_NUM_CHANNELS; i++) {
        int r = adc_channel_setup(adc_dev, &adc_ch_cfg[i]);
        if (r) {
            LOG_ERR("ADC setup ch%d falhou: %d", i, r);
            return r;
        }
    }
    LOG_INF("HAL ADC: %d canal(is) configurado(s)", ADC_NUM_CHANNELS);
    return 0;
}

uint16_t HAL_ADC_Get(uint8_t channel)
{
    if (channel >= ADC_NUM_CHANNELS) return 0;
    return (uint16_t)atomic_get(&adc_raw[channel]);
}

void HAL_thread_ADC_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    struct k_poll_signal sig;
    struct k_poll_event  evt;
    k_poll_signal_init(&sig);
    k_poll_event_init(&evt, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig);

    uint32_t acc[ADC_NUM_CHANNELS] = {0};
    int      cnt = 0;

    LOG_INF("HAL ADC thread iniciada");

    while (1) {
        k_poll_signal_reset(&sig);

        if (adc_read_async(adc_dev, &adc_seq, &sig) != 0) {
            k_sleep(K_MSEC(10));
            continue;
        }

        if (k_poll(&evt, 1, K_MSEC(200)) == -EAGAIN) {
            LOG_WRN("HAL ADC timeout");
            continue;
        }

        for (int i = 0; i < ADC_NUM_CHANNELS; i++) {
            acc[i] += adc_seq_buf[i];
        }
        cnt++;

        if (cnt >= ADC_OVERSAMPLE) {
            for (int i = 0; i < ADC_NUM_CHANNELS; i++) {
                atomic_set(&adc_raw[i], acc[i] / ADC_OVERSAMPLE);
                acc[i] = 0;
            }
            cnt = 0;
        }
    }
}

/* ===========================================================
 * HX711 — Bit-bang GPIO
 *
 * Protocolo:
 *   1. Aguarda DOUT=LOW (conversão pronta, ~100ms @ 10Hz)
 *   2. Pulsa SCK 24 vezes lendo 1 bit por pulso (MSB first)
 *   3. Pulsa SCK (HX711_GAIN_PULSES) vezes extras → seleciona ganho
 *   4. Resultado é complemento de dois de 24 bits com sinal
 * =========================================================== */

/* Mapeamento de pinos — ajuste conforme esquemático */
#define HX711_SCK_NODE  DT_NODELABEL(hx711_sck_pins)
#define HX711_DOUT_NODE DT_NODELABEL(hx711_dout_pins)

static const struct gpio_dt_spec hx711_sck[HX711_COUNT] = {
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 0),  /* PA1 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 1),  /* PA3 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 2),  /* PA5 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 3),  /* PB1 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 4),  /* PB3 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 5),  /* PB5 */
};

static const struct gpio_dt_spec hx711_dout[HX711_COUNT] = {
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 0),  /* PA2 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 1),  /* PA4 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 2),  /* PA6 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 3),  /* PB2 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 4),  /* PB4 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 5),  /* PC0 */
};

/* Estado interno de cada célula */
static struct {
    int32_t raw;        /* último valor bruto 24 bits com sinal */
    int32_t tare;       /* zero de referência */
    float   scale;      /* fator de calibração (raw/grama) */
    float   offset;     /* offset pós-escala */
    bool    ready;
} hx711_state[HX711_COUNT];

/* Mutex para acesso thread-safe ao estado */
static struct k_mutex hx711_mutex;

/* Pulso de clock em torno de 1µs @ 144MHz */
static inline void hx711_clock_pulse(const struct gpio_dt_spec *sck)
{
    gpio_pin_set_dt(sck, 1);
    k_busy_wait(1);
    gpio_pin_set_dt(sck, 0);
    k_busy_wait(1);
}

/* Leitura de 1 sensor — bloqueia até DOUT=LOW ou timeout */
static bool hx711_read_one(uint8_t idx, int32_t *out)
{
    const struct gpio_dt_spec *sck  = &hx711_sck[idx];
    const struct gpio_dt_spec *dout = &hx711_dout[idx];

    /* Aguarda DOUT=0 (dado pronto), timeout 200ms */
    int64_t deadline = k_uptime_get() + 200;
    while (gpio_pin_get_dt(dout) != 0) {
        if (k_uptime_get() > deadline) {
            LOG_WRN("HX711[%d]: timeout aguardando DOUT", idx);
            return false;
        }
        k_sleep(K_USEC(100));
    }

    /* Lê 24 bits MSB first */
    uint32_t raw = 0;
    for (int bit = 23; bit >= 0; bit--) {
        gpio_pin_set_dt(sck, 1);
        k_busy_wait(1);
        int val = gpio_pin_get_dt(dout);
        gpio_pin_set_dt(sck, 0);
        k_busy_wait(1);
        if (val) raw |= (1UL << bit);
    }

    /* Pulsos extras para configurar ganho na próxima conversão */
    for (int i = 0; i < HX711_GAIN_PULSES; i++) {
        hx711_clock_pulse(sck);
    }

    /* Converte complemento de dois de 24 bits para int32_t com sinal */
    if (raw & 0x800000UL) {
        *out = (int32_t)(raw | 0xFF000000UL);
    } else {
        *out = (int32_t)raw;
    }

    return true;
}

int HAL_HX711_Init(void)
{
    k_mutex_init(&hx711_mutex);

    for (int i = 0; i < HX711_COUNT; i++) {
        if (!gpio_is_ready_dt(&hx711_sck[i]) ||
            !gpio_is_ready_dt(&hx711_dout[i])) {
            LOG_ERR("HX711[%d]: GPIO não pronto", i);
            return -ENODEV;
        }

        gpio_pin_configure_dt(&hx711_sck[i],  GPIO_OUTPUT_LOW);
        gpio_pin_configure_dt(&hx711_dout[i], GPIO_INPUT);

        hx711_state[i].raw    = 0;
        hx711_state[i].tare   = 0;
        hx711_state[i].scale  = 1.0f;
        hx711_state[i].offset = 0.0f;
        hx711_state[i].ready  = false;
    }

    LOG_INF("HAL HX711: %d células configuradas", HX711_COUNT);
    return 0;
}

int32_t HAL_HX711_Get_Raw(uint8_t idx)
{
    if (idx >= HX711_COUNT) return 0;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    int32_t v = hx711_state[idx].raw;
    k_mutex_unlock(&hx711_mutex);
    return v;
}

float HAL_HX711_Get_Grams(uint8_t idx)
{
    if (idx >= HX711_COUNT) return 0.0f;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    float v = ((float)(hx711_state[idx].raw - hx711_state[idx].tare)
               / hx711_state[idx].scale)
              + hx711_state[idx].offset;
    k_mutex_unlock(&hx711_mutex);
    return v;
}

void HAL_HX711_Set_Calib(uint8_t idx, float scale, float offset)
{
    if (idx >= HX711_COUNT) return;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    hx711_state[idx].scale  = (scale == 0.0f) ? 1.0f : scale;
    hx711_state[idx].offset = offset;
    k_mutex_unlock(&hx711_mutex);
    LOG_INF("HX711[%d]: scale=%.4f offset=%.4f", idx, (double)scale, (double)offset);
}

void HAL_HX711_Tare(uint8_t idx)
{
    if (idx >= HX711_COUNT) return;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    hx711_state[idx].tare = hx711_state[idx].raw;
    k_mutex_unlock(&hx711_mutex);
    LOG_INF("HX711[%d]: tare=%d", idx, hx711_state[idx].tare);
}

/*
 * HAL_thread_HX711_fn
 * Lê as 6 células em sequência (~10Hz cada) e publica no OSSI→SYS.
 * Sensor IDs: 0-5 mapeiam para load_cell_0 ... load_cell_5 no TELEMETRY_LOG.
 */
void HAL_thread_HX711_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    LOG_INF("HAL HX711 thread iniciada");

    /* Descarta primeiras leituras para estabilizar */
    k_sleep(K_MSEC(500));
    for (int i = 0; i < HX711_COUNT; i++) {
        int32_t dummy;
        hx711_read_one(i, &dummy);
    }

    while (1) {
        for (int i = 0; i < HX711_COUNT; i++) {
            int32_t raw;
            if (!hx711_read_one(i, &raw)) continue;

            k_mutex_lock(&hx711_mutex, K_FOREVER);
            hx711_state[i].raw   = raw;
            hx711_state[i].ready = true;
            float grams = ((float)(raw - hx711_state[i].tare)
                           / hx711_state[i].scale)
                          + hx711_state[i].offset;
            k_mutex_unlock(&hx711_mutex);

            /* Publica no SYS via OSSI */
            ossi_msg_t msg = {
                .target   = TARGET_SYS,
                .req_type = REQ_TYPE_SYS_UPDATE_SENSOR,
            };
            msg.payload.sys_req.sensor_id = (uint32_t)i; /* load_cell_N */
            msg.payload.sys_req.value     = grams;
            OSSI_Send(&msg);
        }
    }
}

/* ===========================================================
 * ESC PWM — TIM4 CH1 (PB6), 50 Hz
 * =========================================================== */
#define ESC_PWM_NODE DT_NODELABEL(esc_motor)
static const struct pwm_dt_spec esc_pwm = PWM_DT_SPEC_GET(ESC_PWM_NODE);

static bool esc_armed = false;

int HAL_ESC_Init(void)
{
    if (!pwm_is_ready_dt(&esc_pwm)) {
        LOG_ERR("ESC PWM não pronto");
        return -ENODEV;
    }

    /* Coloca em posição de disarm (1000µs) */
    int r = pwm_set_dt(&esc_pwm,
                       PWM_USEC(HAL_ESC_PWM_PERIOD_US),
                       PWM_USEC(HAL_ESC_PWM_ARM_US));
    if (r) {
        LOG_ERR("ESC PWM set falhou: %d", r);
        return r;
    }

    esc_armed = false;
    LOG_INF("HAL ESC: PWM inicializado em %dµs", HAL_ESC_PWM_ARM_US);
    return 0;
}

void HAL_ESC_Arm(void)
{
    /* Sequência de arm: mínimo por 2s */
    pwm_set_dt(&esc_pwm,
               PWM_USEC(HAL_ESC_PWM_PERIOD_US),
               PWM_USEC(HAL_ESC_PWM_MIN_US));
    k_sleep(K_MSEC(2000));
    esc_armed = true;
    LOG_INF("HAL ESC: armado");
}

void HAL_ESC_Disarm(void)
{
    pwm_set_dt(&esc_pwm,
               PWM_USEC(HAL_ESC_PWM_PERIOD_US),
               PWM_USEC(HAL_ESC_PWM_ARM_US));
    esc_armed = false;
    LOG_INF("HAL ESC: desarmado");
}

int HAL_ESC_Set_Throttle(float percent)
{
    if (!esc_armed) {
        LOG_WRN("HAL ESC: throttle ignorado — ESC não armado");
        return -EPERM;
    }

    if (percent < 0.0f)   percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    uint32_t pulse_us = HAL_ESC_PWM_MIN_US +
        (uint32_t)(percent / 100.0f *
                   (HAL_ESC_PWM_MAX_US - HAL_ESC_PWM_MIN_US));

    return pwm_set_dt(&esc_pwm,
                      PWM_USEC(HAL_ESC_PWM_PERIOD_US),
                      PWM_USEC(pulse_us));
}

/* ===========================================================
 * RPM — TIM3 CH3 (PB0), modo input capture
 *
 * Não existe driver Zephyr para o compatible "wch,gptm-counter" usado
 * no nó counter3 do DTS (apenas "wch,gptm-pwm" está implementado nesta
 * versão do SDK), então acessamos os registradores do TIM3 diretamente
 * via MMIO — mesmo padrão usado em drivers/adc/adc_ch32v307.c.
 *
 * TIM3 CH3 é configurado em modo input capture (IC3 ← TI3, borda de
 * subida); cada captura gera uma interrupção que incrementa um contador
 * atômico de pulsos. A thread mede a janela HAL_RPM_SAMPLE_MS e converte
 * a contagem em RPM.
 * =========================================================== */
#define TIM3_BASE        0x40000400UL
#define TIM3_CTLR1       (*(volatile uint32_t *)(TIM3_BASE + 0x00))
#define TIM3_DMAINTENR   (*(volatile uint32_t *)(TIM3_BASE + 0x0C))
#define TIM3_INTFR       (*(volatile uint32_t *)(TIM3_BASE + 0x10))
#define TIM3_CHCTLR2     (*(volatile uint32_t *)(TIM3_BASE + 0x1C))
#define TIM3_CCER        (*(volatile uint32_t *)(TIM3_BASE + 0x20))
#define TIM3_PSC         (*(volatile uint32_t *)(TIM3_BASE + 0x28))
#define TIM3_ATRLR       (*(volatile uint32_t *)(TIM3_BASE + 0x2C))
#define TIM3_CH3CVR      (*(volatile uint32_t *)(TIM3_BASE + 0x3C))

#define RCC_BASE         0x40021000UL
#define RCC_APB1PCENR    (*(volatile uint32_t *)(RCC_BASE + 0x1C))
#define RCC_APB1_TIM3EN  (1UL << 1)

#define TIM_CC3S_IC3_TI3 (1UL << 0)   /* CHCTLR2.CC3S = 01: IC3 mapeado em TI3 */
#define TIM_CCER_CC3E    (1UL << 8)
#define TIM_CCER_CC3P    (1UL << 9)   /* 0 = captura na borda de subida */
#define TIM_DIER_CC3IE   (1UL << 3)
#define TIM_SR_CC3IF     (1UL << 3)
#define TIM_CTLR1_CEN    (1UL << 0)
#define TIM_CTLR1_ARPE   (1UL << 7)

/* PB0 / TIM3_CH3 (sem remap). Em MCUs estilo STM32F1/CH32V30x o periférico
 * de input capture lê o pino diretamente — basta configurá-lo como entrada
 * (não é necessária função alternada de GPIO, só usada para saídas como PWM). */
#define RPM_GPIO_NODE DT_NODELABEL(rpm_capture_pin)
static const struct gpio_dt_spec rpm_capture_pin =
    GPIO_DT_SPEC_GET_BY_IDX(RPM_GPIO_NODE, gpios, 0);

#define TIM3_IRQN        45

static atomic_t rpm_value;       /* RPM * 10 para preservar 1 casa decimal */
static atomic_t rpm_pulse_count; /* incrementado na ISR a cada captura     */

static void tim3_capture_isr(const void *arg)
{
    ARG_UNUSED(arg);

    if (TIM3_INTFR & TIM_SR_CC3IF) {
        (void)TIM3_CH3CVR;              /* leitura limpa overcapture (CC3OF) */
        TIM3_INTFR &= ~TIM_SR_CC3IF;    /* limpa flag de captura             */
        atomic_inc(&rpm_pulse_count);
    }
}

int HAL_RPM_Init(void)
{
    int err = gpio_pin_configure_dt(&rpm_capture_pin, GPIO_INPUT);
    if (err) {
        LOG_ERR("HAL RPM: GPIO PB0 falhou (%d)", err);
        return err;
    }

    /* Habilita clock do TIM3 */
    RCC_APB1PCENR |= RCC_APB1_TIM3EN;

    TIM3_CTLR1     = 0;
    TIM3_PSC       = 0;
    TIM3_ATRLR     = 0xFFFF;
    TIM3_CHCTLR2   = (TIM3_CHCTLR2 & ~0x3u) | TIM_CC3S_IC3_TI3;
    TIM3_CCER      = (TIM3_CCER & ~(TIM_CCER_CC3E | TIM_CCER_CC3P)) | TIM_CCER_CC3E;
    TIM3_DMAINTENR |= TIM_DIER_CC3IE;
    TIM3_INTFR     = 0;
    TIM3_CTLR1     = TIM_CTLR1_ARPE | TIM_CTLR1_CEN;

    IRQ_CONNECT(TIM3_IRQN, 1, tim3_capture_isr, NULL, 0);
    irq_enable(TIM3_IRQN);

    LOG_INF("HAL RPM: TIM3 CH3 input capture inicializado");
    return 0;
}

float HAL_RPM_Get(void)
{
    return (float)atomic_get(&rpm_value) / 10.0f;
}

void HAL_thread_RPM_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    LOG_INF("HAL RPM thread iniciada");

    while (1) {
        atomic_clear(&rpm_pulse_count);
        k_sleep(K_MSEC(HAL_RPM_SAMPLE_MS));
        uint32_t pulses = (uint32_t)atomic_get(&rpm_pulse_count);

        /* RPM = (pulsos / pólos) * (60000 / janela_ms) */
        uint32_t rpm10 = (uint32_t)(
            ((float)pulses / (float)HAL_RPM_PULSES_PER_REV)
            * (60000.0f / (float)HAL_RPM_SAMPLE_MS)
            * 10.0f
        );
        atomic_set(&rpm_value, rpm10);

        /* Publica no SYS — sensor_id=10 mapeado para rpm no Log_Manager */
        ossi_msg_t msg = {
            .target   = TARGET_SYS,
            .req_type = REQ_TYPE_SYS_UPDATE_SENSOR,
        };
        msg.payload.sys_req.sensor_id = 10U;
        msg.payload.sys_req.value     = (float)rpm10 / 10.0f;
        OSSI_Send(&msg);
    }
}

/* ===========================================================
 * HAL_Init — inicializa tudo e registra threads no TCKEC
 * =========================================================== */
int HAL_Init(void)
{
    int r;

    r = HAL_ADC_Init();
    if (r) return r;

    r = HAL_HX711_Init();
    if (r) return r;

    r = HAL_ESC_Init();
    if (r) return r;

    r = HAL_RPM_Init();
    if (r) return r;

    /* Registra thread ADC no TCKEC */
    ossi_msg_t req = { .target = TARGET_TCKEC,
                       .req_type = REQ_TYPE_TCKEC_CREATE_THREAD };
    req.payload.tckec_req.thread_id   = HAL_THREAD_ADC_ID;
    req.payload.tckec_req.entry_point = HAL_thread_ADC_fn;
    req.payload.tckec_req.priority    = HAL_ADC_PRIO;
    OSSI_Send(&req);

    /* Registra thread HX711 no TCKEC */
    req.payload.tckec_req.thread_id   = HAL_THREAD_HX711_ID;
    req.payload.tckec_req.entry_point = HAL_thread_HX711_fn;
    req.payload.tckec_req.priority    = HAL_HX711_PRIO;
    OSSI_Send(&req);

    /* Registra thread RPM no TCKEC */
    req.payload.tckec_req.thread_id   = HAL_THREAD_RPM_ID;
    req.payload.tckec_req.entry_point = HAL_thread_RPM_fn;
    req.payload.tckec_req.priority    = HAL_RPM_PRIO;
    OSSI_Send(&req);

    LOG_INF("HAL_Init concluído");
    return 0;
}