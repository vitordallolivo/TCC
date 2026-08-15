/**
 * @file HAL.c
 * @brief HAL da SIBA: ADC (driver adc_ch32v307), HX711 x6 (bit-bang GPIO),
 * ESC PWM (TIM4 CH1/PB6, 50 Hz), RPM (TIM3 CH3/PB0, input capture).
 *
 * ADC/HX711 são threads bloqueantes; RPM e o estímulo ADC do native_sim
 * usam k_timer+k_work. HX711 na Porta E (PE2-7=SCK, PE8-13=DOUT), pois
 * PA0-PA7 são do ADC.
 */

#include "HAL.h"
#include "OSSI.h"
#include "Log_Manager.h"
#include "CMD_Core_prm.h"   /* defaults de calibração/faixa por sensor (tabela) */
#include <math.h>                 /* sqrtf — lei de raiz do Pitot */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/irq.h>
#ifdef CONFIG_ARCH_POSIX
#include <zephyr/random/random.h>
#endif

LOG_MODULE_REGISTER(hal, LOG_LEVEL_DBG);

#ifndef CONFIG_ARCH_POSIX
/* RCC de clock em APB1, compartilhado por RPM (TIM3) e DAC (DAC1). */
#define RCC_BASE         0x40021000UL
#define RCC_APB1PCENR    (*(volatile uint32_t *)(RCC_BASE + 0x1C))
#endif /* !CONFIG_ARCH_POSIX */

#ifdef CONFIG_ARCH_POSIX
/* native_sim: throttle synthetic do ESC (percent*100), lido por RPM e
 * pelo estímulo do ADC — declarado aqui por ser usado antes da seção ESC. */
static atomic_t hal_sim_throttle_x100;
#endif

/* ADC — scan DMA de 4 canais (HAL_ADC_IDX_* em HAL.h). ADC1 só enxerga
 * CH0-7/PA0-PA7 (driver custom, não o driver ADC genérico do Zephyr):
 * idx0=CH0(PA0)v_batt, idx1=CH1(PA1)i_batt, idx2=CH2(PA2)air_speed,
 * idx3=CH6(PA6)temperature_battery. PA3/PA4/PA7 são da bancada de ensaio. */
#define ADC_NUM_CHANNELS  4

/* Software puro — compilado mesmo com HAL_ENABLE_ADC=0 pra Test_Shell linkar. */
static atomic_t adc_raw[ADC_NUM_CHANNELS];

#if HAL_ENABLE_ADC

#define ADC_NODE DT_NODELABEL(adc1)
static const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);

#define ADC_RESOLUTION    12
#define ADC_OVERSAMPLE    8

#define ADC_CH_V_BATT      0
#define ADC_CH_I_BATT      1
#define ADC_CH_AIR_SPEED   2
#define ADC_CH_TEMP_BATT   6

/* PA5 é, ao mesmo tempo, ADC1 canal 5 e DAC1_OUT1 (HAL_DAC) — ver
 * HAL_prm.h. Se algum dia o canal 5 entrar nesse scan com o DAC
 * habilitado, é um conflito de pino real: aborta o build. */
#if HAL_ENABLE_DAC
#if (ADC_CH_V_BATT == 5) || (ADC_CH_I_BATT == 5) || \
    (ADC_CH_AIR_SPEED == 5) || (ADC_CH_TEMP_BATT == 5)
#error "PA5 (ADC1 CH5) conflita com DAC1_OUT1 (HAL_DAC) -- escolha um uso para PA5 (ver HAL_prm.h)"
#endif
#endif

static uint16_t adc_seq_buf[ADC_NUM_CHANNELS];

static const struct adc_channel_cfg adc_ch_cfg[ADC_NUM_CHANNELS] = {
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = ADC_CH_V_BATT },     /* PA0 */
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = ADC_CH_I_BATT },     /* PA1 */
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = ADC_CH_AIR_SPEED },  /* PA2 */
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = ADC_CH_TEMP_BATT },  /* PA6 */
};

static struct adc_sequence adc_seq = {
    .channels    = BIT(ADC_CH_V_BATT) | BIT(ADC_CH_I_BATT) |
                   BIT(ADC_CH_AIR_SPEED) | BIT(ADC_CH_TEMP_BATT),
    .buffer      = adc_seq_buf,
    .buffer_size = sizeof(adc_seq_buf),
    .resolution  = ADC_RESOLUTION,
};

#endif /* HAL_ENABLE_ADC */

/**
 * @brief Converte contagem bruta do ADC (12 bits) em tensão no pino (mV).
 * @param raw contagem bruta (0..HAL_ADC_FULL_SCALE-1)
 * @return tensão em mV
 */
static inline float adc_raw_to_mv(uint16_t raw)
{
    return (float)raw * (float)HAL_ADC_VREF_MV / (float)HAL_ADC_FULL_SCALE;
}

/* Calibração genérica dos sensores ADC, ajustada via CMD/shell. Indexado por
 * SENSOR_ID_*; só 6,7,9,10 são usados (8=SOC vem do Battery_Manager, não passa
 * por aqui). Duas leis, escolhidas por sensor na tabela central:
 *
 *   SENSOR_LAW_AFFINE : y = linear*(x - zero) + offset   (x = mV no pino)
 *   SENSOR_LAW_SQRT   : y = linear*sqrt(x - zero) + offset
 *
 * Com zero = 0 a afim é o y = linear*x + offset de sempre — nada do que já
 * estava calibrado muda. A raiz é do tubo de Pitot (Bernoulli). */
struct hal_adc_calib {
    float linear;
    float offset;
    float zero;
    uint8_t law;
};

static struct hal_adc_calib adc_calib[SENSOR_ID_AIR_SPEED + 1];

/** @brief Preenche adc_calib[] com os defaults de SIBA_SENSOR_TABLE. */
static void hal_adc_calib_init(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(adc_calib); i++) {
        adc_calib[i].linear = 1.0f;
        adc_calib[i].offset = 0.0f;
        adc_calib[i].zero   = 0.0f;
        adc_calib[i].law    = SENSOR_LAW_AFFINE;
    }
    /* Semeia defaults da tabela central — precisa rodar aqui (não só no
     * cache do CMD_Core) senão v_batt/i_batt saem em mV e a segurança
     * dispara emergência falsa. */
#define X(ID, DEF_LIN, DEF_OFF, MINV, MAXV, DEF_ZERO, LAW, ERRBIT)   \
    if ((size_t)(ID) < ARRAY_SIZE(adc_calib)) {                      \
        adc_calib[(ID)].linear = (DEF_LIN);                          \
        adc_calib[(ID)].offset = (DEF_OFF);                          \
        adc_calib[(ID)].zero   = (DEF_ZERO);                         \
        adc_calib[(ID)].law    = (uint8_t)(LAW);                     \
    }
    SIBA_SENSOR_TABLE(X)
#undef X
}

/**
 * @brief Ajusta a calibração afim (y = linear*x + offset) de um sensor ADC.
 * @param sensor_id ver SENSOR_ID_V_BATT/I_BATT/TEMPERATURE_BATTERY/AIR_SPEED
 * @param linear ganho
 * @param offset deslocamento
 */
void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset)
{
    if (sensor_id >= ARRAY_SIZE(adc_calib)) {
        return;
    }
    adc_calib[sensor_id].linear = linear;
    adc_calib[sensor_id].offset = offset;
}

/**
 * @brief Lê a calibração atual de um sensor ADC; fora do range devolve (1.0, 0.0).
 * @param sensor_id índice do sensor
 * @param linear [out] ganho atual
 * @param offset [out] deslocamento atual
 */
void HAL_ADC_Get_Calib(uint8_t sensor_id, float *linear, float *offset)
{
    float l = 1.0f, o = 0.0f;
    if (sensor_id < ARRAY_SIZE(adc_calib)) {
        l = adc_calib[sensor_id].linear;
        o = adc_calib[sensor_id].offset;
    }
    if (linear) *linear = l;
    if (offset) *offset = o;
}

/**
 * @brief Aplica a calibração afim de um sensor a um valor físico (mV).
 * @param sensor_id índice do sensor
 * @param phys_value tensão bruta no pino (mV)
 * @return valor calibrado (unidade física do sensor)
 */
static inline float hal_adc_apply_calib(uint8_t sensor_id, float phys_value)
{
    const struct hal_adc_calib *c = &adc_calib[sensor_id];
    float x = phys_value - c->zero;

    if (c->law == SENSOR_LAW_SQRT) {
        /* Bernoulli: v = sqrt(2*dP/rho). Abaixo do zero satura em 0 — ali a
         * pressão diferencial é negativa, o que significa escoamento
         * invertido ou tomada entupida, não velocidade. Sem esta guarda,
         * sqrtf() de negativo devolveria NaN e contaminaria a telemetria. */
        if (x <= 0.0f) {
            return c->offset;
        }
        return c->linear * sqrtf(x) + c->offset;
    }
    return c->linear * x + c->offset;
}

#if HAL_ENABLE_ADC && defined(CONFIG_ARCH_POSIX)
/* native_sim: gera tensões "no pino" (mV) plausíveis e alimenta o adc_emul
 * via adc_emul_const_value_set(). Modelo simples: i_batt sobe com
 * throttle², v_batt drena com a corrente, temperatura sobe com a corrente,
 * air_speed acompanha o RPM synthetic — tudo com ruído pseudo-aleatório. */
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/random/random.h>

#define HAL_SIM_ADC_PERIOD_MS         50U

#define HAL_SIM_VBATT_MV_FULL       3000.0f  /* mV "no pino" — pack cheio (~24V c/ DEF_LINEAR 0.008) */
#define HAL_SIM_VBATT_MV_EMPTY      2400.0f  /* mV "no pino" — pack "vazio" (~19.2V, ainda na faixa segura) */
#define HAL_SIM_VBATT_DRAIN_PER_TICK   0.05f /* mV por amostra por (mV de corrente) */
#define HAL_SIM_VBATT_NOISE           20.0f

#define HAL_SIM_I_MV_IDLE            200.0f  /* mV — repouso        */
#define HAL_SIM_I_MV_MAX            2800.0f  /* mV — @ 100% throttle */
#define HAL_SIM_I_NOISE               40.0f

#define HAL_SIM_TEMP_MV_BASE         500.0f
#define HAL_SIM_TEMP_GAIN_MV_PER_MVI   0.3f  /* ganho de "temperatura" sobre "corrente" (mV/mV) */
#define HAL_SIM_TEMP_NOISE            15.0f

/* Pitot: o sintético gera a TENSÃO NO PINO invertendo a lei de raiz, em vez
 * de uma rampa de mV solta. Sem isso o native_sim leria 0 m/s sempre — o
 * sensor repousa em ~2664 mV e a rampa antiga (0,25 mV/rpm a partir de zero)
 * nunca alcançava o `zero` da raiz.
 *     mV = zero + (v/linear)^2 ,  com v = rpm * HAL_SIM_AIRSPEED_MS_PER_RPM */
#define HAL_SIM_AIRSPEED_MS_PER_RPM   0.0015f   /* 10000 rpm -> 15 m/s */
#define HAL_SIM_AIRSPEED_NOISE         1.5f     /* mV (~0,1 m/s a 10 m/s) */

static float hal_sim_vbatt_mv = HAL_SIM_VBATT_MV_FULL;

/**
 * @brief Ruído pseudo-aleatório simétrico, atenuado por HAL_SIM_NOISE_SCALE
 * (0 = estável, pra testar calibração; 1 = amplitude nominal).
 * @param amplitude amplitude nominal do ruído
 * @return ruído em [-amplitude, +amplitude] * HAL_SIM_NOISE_SCALE
 */
static inline float hal_sim_noise(float amplitude)
{
    int32_t r = (int32_t)(sys_rand32_get() % 2001) - 1000; /* -1000..+1000 */
    return amplitude * HAL_SIM_NOISE_SCALE * ((float)r / 1000.0f);
}

/**
 * @brief Satura um valor em mV na faixa físicamente válida do ADC.
 * @param mv valor em mV (pode vir negativo ou acima da escala)
 * @return valor saturado em [0, 3300]
 */
static inline int32_t hal_sim_clamp_mv(float mv)
{
    if (mv < 0.0f)    mv = 0.0f;
    if (mv > 3300.0f) mv = 3300.0f;
    return (int32_t)mv;
}

static struct k_work adc_stimulus_work;

/**
 * @brief native_sim: gera tensões sintéticas (v_batt/i_batt/temp/air_speed)
 * e alimenta o adc_emul. Roda na siba_wq, disparado por adc_stimulus_timer.
 * @param w item de trabalho (não usado)
 */
static void adc_stimulus_work_fn(struct k_work *w)
{
    ARG_UNUSED(w);
    float throttle_pct = (float)atomic_get(&hal_sim_throttle_x100) / 100.0f;
    float rpm          = HAL_RPM_Get();
    float th_frac      = throttle_pct / 100.0f;

    float i_mv = HAL_SIM_I_MV_IDLE
               + (HAL_SIM_I_MV_MAX - HAL_SIM_I_MV_IDLE) * th_frac * th_frac
               + hal_sim_noise(HAL_SIM_I_NOISE);
    if (i_mv < 0.0f) i_mv = 0.0f;

    hal_sim_vbatt_mv -= i_mv * HAL_SIM_VBATT_DRAIN_PER_TICK * 0.01f;
    if (hal_sim_vbatt_mv < HAL_SIM_VBATT_MV_EMPTY) hal_sim_vbatt_mv = HAL_SIM_VBATT_MV_EMPTY;
    float v_mv = hal_sim_vbatt_mv + hal_sim_noise(HAL_SIM_VBATT_NOISE);

    float temp_mv = HAL_SIM_TEMP_MV_BASE
                  + i_mv * HAL_SIM_TEMP_GAIN_MV_PER_MVI
                  + hal_sim_noise(HAL_SIM_TEMP_NOISE);

    /* Inverte a lei do sensor com a MESMA calibração que o firmware usa para
     * ler — assim o native_sim exercita a raiz de verdade, e um erro nas
     * constantes aparece como velocidade errada em vez de passar batido. */
    const struct hal_adc_calib *pit = &adc_calib[SENSOR_ID_AIR_SPEED];
    float air_ms = rpm * HAL_SIM_AIRSPEED_MS_PER_RPM;
    float air_mv = pit->zero + hal_sim_noise(HAL_SIM_AIRSPEED_NOISE);
    if (pit->law == SENSOR_LAW_SQRT && pit->linear > 0.0f) {
        float u = (air_ms - pit->offset) / pit->linear;
        air_mv += u * u;
    } else {
        air_mv += air_ms;   /* lei afim: mV cru mesmo */
    }
    if (air_mv < 0.0f) air_mv = 0.0f;

    adc_emul_const_value_set(adc_dev, ADC_CH_V_BATT,    (uint32_t)hal_sim_clamp_mv(v_mv));
    adc_emul_const_value_set(adc_dev, ADC_CH_I_BATT,    (uint32_t)hal_sim_clamp_mv(i_mv));
    adc_emul_const_value_set(adc_dev, ADC_CH_TEMP_BATT, (uint32_t)hal_sim_clamp_mv(temp_mv));
    adc_emul_const_value_set(adc_dev, ADC_CH_AIR_SPEED, (uint32_t)hal_sim_clamp_mv(air_mv));
}

/**
 * @brief Agenda adc_stimulus_work_fn na siba_wq. Callback do k_timer.
 * @param t timer que disparou (não usado)
 */
static void adc_stimulus_timer_fn(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_work_submit_to_queue(&siba_wq, &adc_stimulus_work);
}

K_TIMER_DEFINE(adc_stimulus_timer, adc_stimulus_timer_fn, NULL);
#endif /* HAL_ENABLE_ADC && CONFIG_ARCH_POSIX */

/**
 * @brief Inicializa calibração e ADC1 (ou stub, se HAL_ENABLE_ADC=0); em
 * native_sim também arranca o timer de estímulo synthetic.
 * @return 0 em sucesso, -ENODEV ou erro do adc_channel_setup
 */
int HAL_ADC_Init(void)
{
    /* Software puro, roda mesmo desabilitado pra manter os getters coerentes. */
    hal_adc_calib_init();

#if HAL_ENABLE_ADC
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
    LOG_INF("HAL ADC: %d canal(is) configurado(s) — scan circular via DMA", ADC_NUM_CHANNELS);
#ifdef CONFIG_ARCH_POSIX
    /* native_sim: arranca o timer de estímulo do adc_emul */
    k_work_init(&adc_stimulus_work, adc_stimulus_work_fn);
    k_timer_start(&adc_stimulus_timer,
                  K_MSEC(HAL_SIM_ADC_PERIOD_MS),
                  K_MSEC(HAL_SIM_ADC_PERIOD_MS));
    LOG_INF("HAL ADC: estímulo synthetic (%dms via timer)", HAL_SIM_ADC_PERIOD_MS);
#endif
#else
    LOG_INF("HAL ADC: desabilitado via HAL_prm.h (HAL_DISABLE_ADC)");
#endif
    return 0;
}

/**
 * @brief Leitura bruta (oversampled) de um canal do scan.
 * @param channel índice na sequência (ver HAL_ADC_IDX_*)
 * @return contagem bruta (0 se channel fora do range)
 */
uint16_t HAL_ADC_Get(uint8_t channel)
{
    if (channel >= ADC_NUM_CHANNELS) return 0;
    return (uint16_t)atomic_get(&adc_raw[channel]);
}

/** @brief Tensão da bateria calibrada. @return V. */
float HAL_ADC_Get_VBatt(void)
{
    return hal_adc_apply_calib(SENSOR_ID_V_BATT,
                               adc_raw_to_mv(HAL_ADC_Get(HAL_ADC_IDX_V_BATT)));
}

/** @brief Corrente da bateria calibrada. @return A. */
float HAL_ADC_Get_IBatt(void)
{
    return hal_adc_apply_calib(SENSOR_ID_I_BATT,
                               adc_raw_to_mv(HAL_ADC_Get(HAL_ADC_IDX_I_BATT)));
}

/** @brief Temperatura da bateria calibrada. @return °C. */
float HAL_ADC_Get_Temperature_Batt(void)
{
    return hal_adc_apply_calib(SENSOR_ID_TEMPERATURE_BATTERY,
                               adc_raw_to_mv(HAL_ADC_Get(HAL_ADC_IDX_TEMP_BATT)));
}

/** @brief Velocidade do ar (Pitot) calibrada. @return m/s. */
float HAL_ADC_Get_Air_Speed(void)
{
    return hal_adc_apply_calib(SENSOR_ID_AIR_SPEED,
                               adc_raw_to_mv(HAL_ADC_Get(HAL_ADC_IDX_AIR_SPEED)));
}

#if HAL_ENABLE_ADC
/**
 * @brief Thread de aquisição do ADC: dispara conversões assíncronas,
 * acumula oversample e publica as grandezas físicas via OSSI. Nunca retorna.
 * @param p1, p2, p3 não usados (assinatura exigida por k_thread_create)
 */
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

#ifdef CONFIG_ARCH_POSIX
        /* adc_emul converte na hora; sem pausa o loop trava o clock do
         * native_sim. */
        k_sleep(K_MSEC(2));
#endif

        if (cnt >= ADC_OVERSAMPLE) {
            for (int i = 0; i < ADC_NUM_CHANNELS; i++) {
                atomic_set(&adc_raw[i], acc[i] / ADC_OVERSAMPLE);
                acc[i] = 0;
            }
            cnt = 0;

            /* Publica grandezas físicas convertidas via siba_wq */
            OSSI_Submit_Sensor(SENSOR_ID_V_BATT,             HAL_ADC_Get_VBatt());
            OSSI_Submit_Sensor(SENSOR_ID_I_BATT,             HAL_ADC_Get_IBatt());
            OSSI_Submit_Sensor(SENSOR_ID_TEMPERATURE_BATTERY,HAL_ADC_Get_Temperature_Batt());
            OSSI_Submit_Sensor(SENSOR_ID_AIR_SPEED,          HAL_ADC_Get_Air_Speed());
        }
    }
}
#endif /* HAL_ENABLE_ADC */

/* HX711 bit-bang: espera DOUT=LOW, pulsa SCK 24x lendo MSB-first, mais
 * HX711_GAIN_PULSES pulsos pra selecionar ganho; resultado é complemento
 * de dois de 24 bits. */

#if HAL_ENABLE_HX711 && !defined(CONFIG_ARCH_POSIX)
/* Mapeamento de pinos — ajuste conforme esquemático */
#define HX711_SCK_NODE  DT_NODELABEL(hx711_sck_pins)
#define HX711_DOUT_NODE DT_NODELABEL(hx711_dout_pins)

static const struct gpio_dt_spec hx711_sck[HX711_COUNT] = {
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 0),  /* PE2 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 1),  /* PE3 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 2),  /* PE4 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 3),  /* PE5 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 4),  /* PE6 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 5),  /* PE7 */
};

static const struct gpio_dt_spec hx711_dout[HX711_COUNT] = {
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 0),  /* PE8 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 1),  /* PE9 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 2),  /* PE10 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 3),  /* PE11 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 4),  /* PE12 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 5),  /* PE13 */
};
#endif /* HAL_ENABLE_HX711 && !CONFIG_ARCH_POSIX */

/* Estado interno de cada célula — modelo CANÔNICO do HX711 (HW e native_sim
 * usam ESTE mesmo struct/estado): grams = (raw - tare)/scale.
 * UM só offset: o `tare` (em counts). Não há mais o segundo offset aditivo. */
static struct {
    int32_t raw;        /* último valor bruto 24 bits com sinal */
    int32_t tare;       /* zero de referência, em CONTAGENS brutas (o único offset) */
    float   scale;      /* contagens por unidade (counts/grama) */
    bool    ready;
} hx711_state[HX711_COUNT];

/* Mutex para acesso thread-safe ao estado */
static struct k_mutex hx711_mutex;

#if HAL_ENABLE_HX711 && !defined(CONFIG_ARCH_POSIX)
/**
 * @brief Gera um pulso de clock (~1µs @ 144MHz) no pino SCK.
 * @param sck pino SCK da célula
 */
static inline void hx711_clock_pulse(const struct gpio_dt_spec *sck)
{
    gpio_pin_set_dt(sck, 1);
    k_busy_wait(1);
    gpio_pin_set_dt(sck, 0);
    k_busy_wait(1);
}

/**
 * @brief Lê uma célula via bit-bang: bloqueia até DOUT=LOW (ou timeout),
 * lê 24 bits MSB-first e pulsa o ganho pra próxima conversão.
 * @param idx índice da célula (0..HX711_COUNT-1)
 * @param out [out] valor bruto (complemento de dois, 24 bits)
 * @return true se leu com sucesso, false em timeout
 */
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
#endif /* HAL_ENABLE_HX711 && !CONFIG_ARCH_POSIX */

#if HAL_ENABLE_HX711 && defined(CONFIG_ARCH_POSIX)
/* native_sim: sem GPIO real — sintetiza o raw 24 bits (empuxo ∝ RPM²),
 * publicado pelas mesmas HAL_HX711_Get_..., Set_Calib e Tare reais. */
#define HAL_SIM_HX711_BASE_RAW      8000000L
#define HAL_SIM_HX711_GAIN_PER_RPM2     0.06f
#define HAL_SIM_HX711_NOISE_RAW          400L
/* Escala synthetic (counts/grama) — 0,06·rpm² a 3000 RPM = 540k counts. */
#define HAL_SIM_HX711_SCALE            270.0f
/* Leitura de repouso (grams) por célula, distinta por idx, pra ter algo
 * pra tarar/calibrar mesmo com rpm=0. */
#define HAL_SIM_HX711_IDLE_GRAMS      1000.0f
#define HAL_SIM_HX711_IDLE_STEP_G      250.0f

/**
 * @brief native_sim: sintetiza o raw 24 bits (empuxo ∝ RPM², distinto por
 * célula), no lugar do bit-bang real.
 * @param idx índice da célula (0..HX711_COUNT-1)
 * @param out [out] valor bruto sintético
 * @return sempre true
 */
static bool hx711_read_one(uint8_t idx, int32_t *out)
{
    /* Sem sleep aqui, a thread roda em loop apertado e trava o clock do
     * native_sim (a versão real bloqueia esperando DOUT=LOW). */
    k_sleep(K_MSEC(100));

    float rpm = HAL_RPM_Get();
    float idle_g = HAL_SIM_HX711_IDLE_GRAMS + (float)idx * HAL_SIM_HX711_IDLE_STEP_G;
    int32_t noise = (int32_t)(sys_rand32_get() % (2 * HAL_SIM_HX711_NOISE_RAW + 1))
                    - HAL_SIM_HX711_NOISE_RAW;
    *out = HAL_SIM_HX711_BASE_RAW
           + (int32_t)(idle_g * HAL_SIM_HX711_SCALE)
           + (int32_t)(HAL_SIM_HX711_GAIN_PER_RPM2 * rpm * rpm)
           + (int32_t)(noise * HAL_SIM_NOISE_SCALE);   /* HAL_prm.h: 0 = estável */
    return true;
}
#endif /* HAL_ENABLE_HX711 && CONFIG_ARCH_POSIX */

/**
 * @brief Inicializa estado/mutex das 6 células e configura os GPIOs (ou o
 * modo synthetic no native_sim).
 * @return 0 em sucesso, -ENODEV se algum GPIO não estiver pronto
 */
int HAL_HX711_Init(void)
{
    k_mutex_init(&hx711_mutex);

    /* Software puro, sempre inicializado mesmo com HX711 desabilitado. */
    for (int i = 0; i < HX711_COUNT; i++) {
        hx711_state[i].raw    = 0;
        hx711_state[i].tare   = 0;
        hx711_state[i].scale  = 1.0f;
        hx711_state[i].ready  = false;
    }

#if HAL_ENABLE_HX711
#ifndef CONFIG_ARCH_POSIX
    for (int i = 0; i < HX711_COUNT; i++) {
        if (!gpio_is_ready_dt(&hx711_sck[i]) ||
            !gpio_is_ready_dt(&hx711_dout[i])) {
            LOG_ERR("HX711[%d]: GPIO não pronto", i);
            return -ENODEV;
        }

        gpio_pin_configure_dt(&hx711_sck[i],  GPIO_OUTPUT_LOW);
        gpio_pin_configure_dt(&hx711_dout[i], GPIO_INPUT);
    }
#else
    /* Synthetic: tara o baseline e usa escala realista, senão grams sai em
     * ~8 milhões e a validação rejeita as células. */
    for (int i = 0; i < HX711_COUNT; i++) {
        hx711_state[i].tare  = HAL_SIM_HX711_BASE_RAW;
        hx711_state[i].scale = HAL_SIM_HX711_SCALE;
    }
    LOG_INF("HAL HX711: modo synthetic (native_sim) — tare=%ld scale=%.0f",
            (long)HAL_SIM_HX711_BASE_RAW, (double)HAL_SIM_HX711_SCALE);
#endif
    LOG_INF("HAL HX711: %d células configuradas", HX711_COUNT);
#else
    LOG_INF("HAL HX711: desabilitado via HAL_prm.h (HAL_DISABLE_HX711)");
#endif
    return 0;
}

/**
 * @brief Último valor bruto lido de uma célula.
 * @param idx índice da célula
 * @return valor bruto (24 bits com sinal), ou 0 se idx fora do range
 */
int32_t HAL_HX711_Get_Raw(uint8_t idx)
{
    if (idx >= HX711_COUNT) return 0;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    int32_t v = hx711_state[idx].raw;
    k_mutex_unlock(&hx711_mutex);
    return v;
}

/**
 * @brief Peso calibrado: grams = (raw - tare) / scale.
 * @param idx índice da célula
 * @return peso na unidade calibrada, ou 0.0f se idx fora do range
 */
float HAL_HX711_Get_Grams(uint8_t idx)
{
    if (idx >= HX711_COUNT) return 0.0f;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    float v = (float)(hx711_state[idx].raw - hx711_state[idx].tare)
              / hx711_state[idx].scale;
    k_mutex_unlock(&hx711_mutex);
    return v;
}

/**
 * @brief Ajuste direto do par canônico (scale, tare).
 * @param idx índice da célula
 * @param scale contagens por unidade; 0 é tratado como 1.0
 * @param tare_raw zero de referência, em contagens brutas
 */
void HAL_HX711_Set_Calib(uint8_t idx, float scale, float tare_raw)
{
    if (idx >= HX711_COUNT) return;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    hx711_state[idx].scale = (scale == 0.0f) ? 1.0f : scale;
    hx711_state[idx].tare  = (int32_t)tare_raw;
    k_mutex_unlock(&hx711_mutex);
    LOG_INF("HX711[%d]: scale=%.4f tare=%d (counts)", idx, (double)scale, (int)tare_raw);
}

/**
 * @brief Lê o par canônico (scale, tare) atualmente aplicado.
 * @param idx índice da célula; fora do range devolve (1.0, 0.0)
 * @param scale [out] contagens por unidade
 * @param tare_raw [out] zero de referência (contagens)
 */
void HAL_HX711_Get_Calib(uint8_t idx, float *scale, float *tare_raw)
{
    float s = 1.0f, t = 0.0f;
    if (idx < HX711_COUNT) {
        k_mutex_lock(&hx711_mutex, K_FOREVER);
        s = hx711_state[idx].scale;
        t = (float)hx711_state[idx].tare;
        k_mutex_unlock(&hx711_mutex);
    }
    if (scale)    *scale    = s;
    if (tare_raw) *tare_raw = t;
}

/**
 * @brief Tara canônica: tare = raw atual, zerando a leitura. Tarar de novo
 * só re-zera, não empilha.
 * @param idx índice da célula
 */
void HAL_HX711_Tare(uint8_t idx)
{
    if (idx >= HX711_COUNT) return;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    hx711_state[idx].tare = hx711_state[idx].raw;
    k_mutex_unlock(&hx711_mutex);
    LOG_INF("HX711[%d]: tare=%d (counts)", idx, hx711_state[idx].tare);
}

/**
 * @brief Calibra a escala por peso conhecido: com a célula já tarada,
 * resolve scale = (raw - tare) / known_weight.
 * @param idx índice da célula
 * @param known_weight peso aplicado, na unidade desejada; 0 é ignorado (log)
 */
void HAL_HX711_Calibrate_Weight(uint8_t idx, float known_weight)
{
    if (idx >= HX711_COUNT) return;
    if (known_weight == 0.0f) {
        LOG_WRN("HX711[%d]: calib por peso ignorada (peso=0)", idx);
        return;
    }
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    float counts = (float)(hx711_state[idx].raw - hx711_state[idx].tare);
    float scale  = counts / known_weight;
    hx711_state[idx].scale = (scale == 0.0f) ? 1.0f : scale;
    k_mutex_unlock(&hx711_mutex);
    LOG_INF("HX711[%d]: calib peso=%.3f -> scale=%.4f counts/un",
            idx, (double)known_weight, (double)hx711_state[idx].scale);
}

#if HAL_ENABLE_HX711
/**
 * @brief Thread: lê as 6 células em sequência e publica no OSSI (sensor id
 * 0-5 = load_cell_N). Nunca retorna.
 * @param p1, p2, p3 não usados (assinatura exigida por k_thread_create)
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
            /* Canônico: grams = (raw - tare)/scale (ver HAL_HX711_Get_Grams). */
            float grams = (float)(raw - hx711_state[i].tare)
                          / hx711_state[i].scale;
            k_mutex_unlock(&hx711_mutex);

            OSSI_Submit_Sensor((uint32_t)i, grams); /* load_cell_N */
        }
    }
}
#endif /* HAL_ENABLE_HX711 */

/* ESC PWM — TIM4 CH1 (PB6), 50 Hz. native_sim não tem TIM4 nem PWM fake
 * utilizável fora de builds ztest, então só a saída é `#ifdef
 * CONFIG_ARCH_POSIX` (hal_esc_hw_*); a lógica (clamp, armado, percent→pulso)
 * é uma implementação só em HAL_ESC_Set_Throttle/Arm/Disarm. */
#if HAL_ENABLE_ESC
#ifdef CONFIG_ARCH_POSIX

/* hal_sim_throttle_x100 (throttle * 100) declarado no topo do arquivo —
 * compartilhado com o stimulus do ADC e HAL_thread_RPM_fn. */

/** @brief native_sim: stub (não há PWM/TIM4 real). @return sempre 0. */
static int hal_esc_hw_init(void)
{
    atomic_set(&hal_sim_throttle_x100, 0);
    LOG_INF("HAL ESC: modo synthetic (native_sim) — sem PWM/TIM4");
    return 0;
}

/**
 * @brief Sem PWM real: guarda o throttle comandado (0-100%) em "por cento
 * x100" pro RPM synthetic perseguir.
 * @param percent throttle em 0.0-100.0
 */
static void hal_esc_hw_apply(float percent)
{
    atomic_set(&hal_sim_throttle_x100, (atomic_val_t)(percent * 100.0f));
}

#else /* !CONFIG_ARCH_POSIX — siba_board real: TIM4/PWM via API genérica */

#define ESC_PWM_NODE DT_NODELABEL(esc_motor)
static const struct pwm_dt_spec esc_pwm = PWM_DT_SPEC_GET(ESC_PWM_NODE);

/**
 * @brief Inicializa o PWM do ESC em posição de disarm (1000µs).
 * @return 0 em sucesso, -ENODEV/erro do pwm_set_dt
 */
static int hal_esc_hw_init(void)
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

    LOG_INF("HAL ESC: PWM inicializado em %dµs", HAL_ESC_PWM_ARM_US);
    return 0;
}

/**
 * @brief Converte percent (0-100) em pulso PWM e aplica no TIM4.
 * @param percent throttle em 0.0-100.0
 */
static void hal_esc_hw_apply(float percent)
{
    uint32_t pulse_us = HAL_ESC_PWM_MIN_US +
        (uint32_t)(percent / 100.0f *
                   (HAL_ESC_PWM_MAX_US - HAL_ESC_PWM_MIN_US));
    pwm_set_dt(&esc_pwm,
              PWM_USEC(HAL_ESC_PWM_PERIOD_US),
              PWM_USEC(pulse_us));
}

#endif /* CONFIG_ARCH_POSIX */

static bool esc_armed;

/** @brief Inicializa o ESC desarmado. @return 0 em sucesso, erro de hal_esc_hw_init. */
int HAL_ESC_Init(void)
{
    esc_armed = false;
    return hal_esc_hw_init();
}

/** @brief Sequência de arm: aplica 0% por 2s e marca armado. */
void HAL_ESC_Arm(void)
{
    hal_esc_hw_apply(0.0f);
    k_sleep(K_MSEC(2000));
    esc_armed = true;
    LOG_INF("HAL ESC: armado");
}

/** @brief Aplica 0% (piso seguro) e marca desarmado. */
void HAL_ESC_Disarm(void)
{
    hal_esc_hw_apply(0.0f);
    esc_armed = false;
    LOG_INF("HAL ESC: desarmado");
}

/**
 * @brief Aplica um throttle, se o ESC estiver armado.
 * @param percent throttle desejado; clampado em [0, 100]
 * @return 0 em sucesso, -EPERM se não armado
 */
int HAL_ESC_Set_Throttle(float percent)
{
    if (!esc_armed) {
        LOG_WRN("HAL ESC: throttle ignorado — ESC não armado");
        return -EPERM;
    }

    if (percent < 0.0f)   percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    hal_esc_hw_apply(percent);
    return 0;
}

#else /* !HAL_ENABLE_ESC */

/** @brief Stub (HAL_ENABLE_ESC=0). @return sempre 0. */
int HAL_ESC_Init(void)
{
    LOG_INF("HAL ESC: desabilitado via HAL_prm.h (HAL_DISABLE_ESC)");
    return 0;
}

/** @brief Stub (HAL_ENABLE_ESC=0): apenas loga. */
void HAL_ESC_Arm(void)
{
    LOG_INF("HAL ESC: arm ignorado — módulo desabilitado (HAL_DISABLE_ESC)");
}

/** @brief Stub (HAL_ENABLE_ESC=0): no-op. */
void HAL_ESC_Disarm(void)
{
    /* no-op — módulo desabilitado */
}

/** @brief Stub (HAL_ENABLE_ESC=0). @param percent não usado. @return -ENODEV. */
int HAL_ESC_Set_Throttle(float percent)
{
    ARG_UNUSED(percent);
    return -ENODEV;
}

#endif /* HAL_ENABLE_ESC */

/* RPM — TIM3 CH3 (PB0), input capture. native_sim não tem TIM3 (MMIO em
 * endereço fixo daria SIGSEGV) nem driver Counter equivalente, então
 * `#ifdef CONFIG_ARCH_POSIX` sintetiza o RPM perseguindo o throttle do ESC,
 * publicando pelo mesmo caminho OSSI. */

/* Sempre compilado (usado pelas threads synthetic do ADC/HX711 mesmo com
 * RPM desabilitado — fica em 0 nesse caso). */
static atomic_t rpm_value;

/** @brief Última rotação medida. @return rpm mecânico. */
float HAL_RPM_Get(void)
{
    return (float)atomic_get(&rpm_value) / 10.0f;
}

#if HAL_ENABLE_RPM
#ifdef CONFIG_ARCH_POSIX

#define HAL_RPM_SIM_MAX_RPM   9000.0f
#define HAL_RPM_SIM_LAG          0.15f
#define HAL_RPM_SIM_NOISE_RPM   20.0f

static float rpm_sim_current;

static struct k_work  rpm_sim_work;

/**
 * @brief native_sim: persegue o throttle do ESC com atraso de 1ª ordem +
 * ruído, atualiza rpm_value e publica no OSSI. Roda na siba_wq.
 * @param w item de trabalho (não usado)
 */
static void rpm_sim_work_fn(struct k_work *w)
{
    ARG_UNUSED(w);
    float throttle_pct = (float)atomic_get(&hal_sim_throttle_x100) / 100.0f;
    float target_rpm   = (throttle_pct / 100.0f) * HAL_RPM_SIM_MAX_RPM;

    rpm_sim_current += (target_rpm - rpm_sim_current) * HAL_RPM_SIM_LAG;

    int32_t noise = (int32_t)(sys_rand32_get() % 2001) - 1000;
    float   rpm   = rpm_sim_current
                  + HAL_RPM_SIM_NOISE_RPM * HAL_SIM_NOISE_SCALE * ((float)noise / 1000.0f);
    if (rpm < 0.0f) rpm = 0.0f;

    atomic_set(&rpm_value, (uint32_t)(rpm * 10.0f));
    OSSI_Submit_Sensor(SENSOR_ID_RPM, rpm);
}

/**
 * @brief Agenda rpm_sim_work_fn na siba_wq. Callback do k_timer.
 * @param t timer que disparou (não usado)
 */
static void rpm_sim_timer_fn(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_work_submit_to_queue(&siba_wq, &rpm_sim_work);
}

K_TIMER_DEFINE(rpm_sim_timer, rpm_sim_timer_fn, NULL);

/**
 * @brief native_sim: zera o estado e arranca o timer de RPM sintético.
 * @return sempre 0
 */
int HAL_RPM_Init(void)
{
    rpm_sim_current = 0.0f;
    atomic_set(&rpm_value, 0);
    k_work_init(&rpm_sim_work, rpm_sim_work_fn);
    k_timer_start(&rpm_sim_timer,
                  K_MSEC(HAL_RPM_SAMPLE_MS),
                  K_MSEC(HAL_RPM_SAMPLE_MS));
    LOG_INF("HAL RPM: modo synthetic (native_sim) — timer %dms", HAL_RPM_SAMPLE_MS);
    return 0;
}

#else /* !CONFIG_ARCH_POSIX — siba_board real: TIM3 via MMIO direto */

#define TIM3_BASE        0x40000400UL
#define TIM3_CTLR1       (*(volatile uint32_t *)(TIM3_BASE + 0x00))
#define TIM3_DMAINTENR   (*(volatile uint32_t *)(TIM3_BASE + 0x0C))
#define TIM3_INTFR       (*(volatile uint32_t *)(TIM3_BASE + 0x10))
#define TIM3_CHCTLR2     (*(volatile uint32_t *)(TIM3_BASE + 0x1C))
#define TIM3_CCER        (*(volatile uint32_t *)(TIM3_BASE + 0x20))
#define TIM3_PSC         (*(volatile uint32_t *)(TIM3_BASE + 0x28))
#define TIM3_ATRLR       (*(volatile uint32_t *)(TIM3_BASE + 0x2C))
#define TIM3_CH3CVR      (*(volatile uint32_t *)(TIM3_BASE + 0x3C))

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

static atomic_t rpm_pulse_count; /* incrementado na ISR a cada captura */

static struct k_work rpm_work;

/**
 * @brief ISR de input capture do TIM3 CH3: limpa a flag e conta 1 pulso.
 * @param arg não usado
 */
static void tim3_capture_isr(const void *arg)
{
    ARG_UNUSED(arg);
    if (TIM3_INTFR & TIM_SR_CC3IF) {
        (void)TIM3_CH3CVR;
        TIM3_INTFR &= ~TIM_SR_CC3IF;
        atomic_inc(&rpm_pulse_count);
    }
}

/**
 * @brief A cada 100ms, agenda rpm_work_fn na siba_wq. Callback do k_timer.
 * @param t timer que disparou (não usado)
 */
static void rpm_timer_fn(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_work_submit_to_queue(&siba_wq, &rpm_work);
}

K_TIMER_DEFINE(rpm_timer, rpm_timer_fn, NULL);

/**
 * @brief Lê e zera o contador de pulsos da ISR, converte pra RPM e publica
 * no OSSI. Roda na siba_wq.
 * @param w item de trabalho (não usado)
 */
static void rpm_work_fn(struct k_work *w)
{
    ARG_UNUSED(w);
    /* Lê e zera janela atomicamente (timer já garantiu 100ms) */
    uint32_t pulses = (uint32_t)atomic_get(&rpm_pulse_count);
    atomic_clear(&rpm_pulse_count);

    uint32_t rpm10 = (uint32_t)(
        ((float)pulses / (float)HAL_RPM_PULSES_PER_REV)
        * (60000.0f / (float)HAL_RPM_SAMPLE_MS)
        * 10.0f
    );
    atomic_set(&rpm_value, rpm10);
    OSSI_Submit_Sensor(SENSOR_ID_RPM, (float)rpm10 / 10.0f);
}

/**
 * @brief Configura o GPIO PB0 e o TIM3 CH3 em input capture, e arranca o
 * timer de conversão a RPM.
 * @return 0 em sucesso, erro do gpio_pin_configure_dt
 */
int HAL_RPM_Init(void)
{
    int err = gpio_pin_configure_dt(&rpm_capture_pin, GPIO_INPUT);
    if (err) {
        LOG_ERR("HAL RPM: GPIO PB0 falhou (%d)", err);
        return err;
    }

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

    k_work_init(&rpm_work, rpm_work_fn);
    k_timer_start(&rpm_timer,
                  K_MSEC(HAL_RPM_SAMPLE_MS),
                  K_MSEC(HAL_RPM_SAMPLE_MS));

    LOG_INF("HAL RPM: TIM3 CH3 input capture + timer %dms", HAL_RPM_SAMPLE_MS);
    return 0;
}

#endif /* CONFIG_ARCH_POSIX */

#else /* !HAL_ENABLE_RPM */

/** @brief Stub (HAL_ENABLE_RPM=0). @return sempre 0. */
int HAL_RPM_Init(void)
{
    LOG_INF("HAL RPM: desabilitado via HAL_prm.h (HAL_DISABLE_RPM)");
    return 0;
}

#endif /* HAL_ENABLE_RPM */

/* I2C1 (PB8/PB9, remap=1) — infra crua sobre a API genérica i2c_* do
 * Zephyr, sem decisão de protocolo. Roda igual em siba_board e native_sim
 * (driver real ou i2c_emul). */
#if HAL_ENABLE_I2C

#define I2C_NODE DT_NODELABEL(i2c1)
static const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

/** @brief Verifica se o device I2C1 está pronto. @return 0 ou -ENODEV. */
int HAL_I2C_Init(void)
{
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C1 não pronto");
        return -ENODEV;
    }
    LOG_INF("HAL I2C: I2C1 inicializado (PB8/PB9, remap=1)");
    return 0;
}

/**
 * @brief Lê len bytes do endereço addr.
 * @param addr endereço I2C (7 bits)
 * @param buf [out] destino dos bytes lidos
 * @param len quantidade de bytes
 * @return 0 em sucesso, ou errno negativo do i2c_read
 */
int HAL_I2C_Read(uint16_t addr, uint8_t *buf, size_t len)
{
    return i2c_read(i2c_dev, buf, len, (uint16_t)addr);
}

/**
 * @brief Escreve len bytes no endereço addr.
 * @param addr endereço I2C (7 bits)
 * @param buf bytes a escrever
 * @param len quantidade de bytes
 * @return 0 em sucesso, ou errno negativo do i2c_write
 */
int HAL_I2C_Write(uint16_t addr, const uint8_t *buf, size_t len)
{
    return i2c_write(i2c_dev, buf, len, (uint16_t)addr);
}

/**
 * @brief Varre o barramento (HAL_I2C_SCAN_ADDR_MIN..MAX) procurando ACK.
 * @param found_addrs [out] endereços encontrados (até max)
 * @param max capacidade de found_addrs
 * @param out_count [out] número de endereços encontrados (pode exceder max)
 * @return sempre 0
 */
int HAL_I2C_Scan(uint8_t *found_addrs, size_t max, size_t *out_count)
{
    size_t  count = 0;
    uint8_t dummy;

    for (uint16_t addr = HAL_I2C_SCAN_ADDR_MIN; addr <= HAL_I2C_SCAN_ADDR_MAX; addr++) {
        /* Leitura de 0 bytes — só verifica se o dispositivo faz ACK do endereço */
        if (i2c_read(i2c_dev, &dummy, 0, (uint16_t)addr) == 0) {
            if (count < max) {
                found_addrs[count] = (uint8_t)addr;
            }
            count++;
        }
    }

    if (out_count) {
        *out_count = count;
    }
    return 0;
}

#else /* !HAL_ENABLE_I2C */

/** @brief Stub (HAL_ENABLE_I2C=0). @return sempre 0. */
int HAL_I2C_Init(void)
{
    LOG_INF("HAL I2C: desabilitado via HAL_prm.h (HAL_DISABLE_I2C)");
    return 0;
}

/** @brief Stub (HAL_ENABLE_I2C=0). @return -ENODEV. */
int HAL_I2C_Read(uint16_t addr, uint8_t *buf, size_t len)
{
    ARG_UNUSED(addr); ARG_UNUSED(buf); ARG_UNUSED(len);
    return -ENODEV;
}

/** @brief Stub (HAL_ENABLE_I2C=0). @return -ENODEV. */
int HAL_I2C_Write(uint16_t addr, const uint8_t *buf, size_t len)
{
    ARG_UNUSED(addr); ARG_UNUSED(buf); ARG_UNUSED(len);
    return -ENODEV;
}

/** @brief Stub (HAL_ENABLE_I2C=0): out_count=0. @return -ENODEV. */
int HAL_I2C_Scan(uint8_t *found_addrs, size_t max, size_t *out_count)
{
    ARG_UNUSED(found_addrs); ARG_UNUSED(max);
    if (out_count) {
        *out_count = 0;
    }
    return -ENODEV;
}

#endif /* HAL_ENABLE_I2C */

/* DAC1 canal 1 (PA5/DAC_OUT1) — sem driver Zephyr, MMIO bruto (mesmo
 * padrão do RPM/TIM3). native_sim guarda o valor numa variável, sem periférico. */
#if HAL_ENABLE_DAC
#ifdef CONFIG_ARCH_POSIX

static atomic_t hal_sim_dac_value;

/** @brief native_sim: stub (não há DAC real). @return sempre 0. */
static int hal_dac_hw_init(void)
{
    atomic_set(&hal_sim_dac_value, 0);
    LOG_INF("HAL DAC: modo synthetic (native_sim) — sem DAC1/PA5");
    return 0;
}

/** @brief native_sim: guarda o valor numa variável, sem periférico real.
 * @param value_12bit valor 0-4095 */
static void hal_dac_hw_write(uint16_t value_12bit)
{
    atomic_set(&hal_sim_dac_value, value_12bit);
}

#else /* !CONFIG_ARCH_POSIX — siba_board real: DAC1 via MMIO direto */

#define DAC_BASE        0x40007400UL
#define DAC_CTLR        (*(volatile uint32_t *)(DAC_BASE + 0x00))
#define DAC_R12BDHR1    (*(volatile uint32_t *)(DAC_BASE + 0x08))  /* canal 1, 12 bits alinhado à direita */

#define DAC_CTLR_EN1    (1UL << 0)   /* habilita canal 1            */
#define DAC_CTLR_BOFF1  (1UL << 1)   /* 0 = buffer de saída ligado  */

#define RCC_APB1PCENR_DAC_EN  (1UL << 29)  /* CH32V20X_V30X_CLOCK_DAC = APB1,29 */

/**
 * @brief Habilita o clock do DAC1 e configura o canal 1 (buffer ligado).
 * @return sempre 0
 */
static int hal_dac_hw_init(void)
{
    /* Habilita clock do DAC (APB1 bit 29) */
    RCC_APB1PCENR |= RCC_APB1PCENR_DAC_EN;

    /* Canal 1 habilitado, buffer de saída ligado (BOFF1=0) */
    DAC_CTLR = (DAC_CTLR & ~DAC_CTLR_BOFF1) | DAC_CTLR_EN1;
    DAC_R12BDHR1 = 0;

    /* Nota: o pino PA5/DAC_OUT1 precisa estar em modo "entrada analógica"
     * para o DAC poder dirigi-lo — configuração de GPIO a confirmar/ajustar
     * na bancada (mesma ressalva das constantes de hardware incertas). */
    LOG_INF("HAL DAC: DAC1 canal 1 inicializado (PA5/DAC_OUT1)");
    return 0;
}

/** @brief Escreve o valor bruto no registrador do DAC1 canal 1.
 * @param value_12bit valor 0-4095 */
static void hal_dac_hw_write(uint16_t value_12bit)
{
    DAC_R12BDHR1 = value_12bit;
}

#endif /* CONFIG_ARCH_POSIX */

/** @brief Inicializa o DAC1. @return sempre 0. */
int HAL_DAC_Init(void)
{
    return hal_dac_hw_init();
}

/**
 * @brief Escreve um valor no DAC, saturado em HAL_DAC_MAX_VALUE.
 * @param value_12bit valor 0-4095 (12 bits)
 */
void HAL_DAC_Set_Value(uint16_t value_12bit)
{
    if (value_12bit > HAL_DAC_MAX_VALUE) value_12bit = HAL_DAC_MAX_VALUE;
    hal_dac_hw_write(value_12bit);
}

#else /* !HAL_ENABLE_DAC */

/** @brief Stub (HAL_ENABLE_DAC=0). @return sempre 0. */
int HAL_DAC_Init(void)
{
    LOG_INF("HAL DAC: desabilitado via HAL_prm.h (HAL_DISABLE_DAC)");
    return 0;
}

/** @brief Stub (HAL_ENABLE_DAC=0): no-op. @param value_12bit não usado. */
void HAL_DAC_Set_Value(uint16_t value_12bit)
{
    ARG_UNUSED(value_12bit);
}

#endif /* HAL_ENABLE_DAC */

/* HAL_Init — inicializa periféricos e cria threads bloqueantes. */

#if HAL_ENABLE_ADC
K_THREAD_STACK_DEFINE(hal_adc_stack, HAL_STACK_SIZE);
static struct k_thread hal_adc_thread_data;
#endif

#if HAL_ENABLE_HX711
K_THREAD_STACK_DEFINE(hal_hx711_stack, HAL_STACK_SIZE);
static struct k_thread hal_hx711_thread_data;
#endif

/**
 * @brief Inicializa ADC/HX711/ESC/RPM/I2C/DAC nessa ordem e sobe as threads
 * bloqueantes (ADC, HX711).
 * @return 0 em sucesso, ou o primeiro erro de inicialização encontrado
 */
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

    r = HAL_I2C_Init();
    if (r) return r;

    r = HAL_DAC_Init();
    if (r) return r;

#if HAL_ENABLE_ADC
    k_thread_create(&hal_adc_thread_data, hal_adc_stack,
                    K_THREAD_STACK_SIZEOF(hal_adc_stack),
                    HAL_thread_ADC_fn, NULL, NULL, NULL,
                    HAL_ADC_PRIO, 0, K_NO_WAIT);
    k_thread_name_set(&hal_adc_thread_data, "hal_adc");
#endif

#if HAL_ENABLE_HX711
    k_thread_create(&hal_hx711_thread_data, hal_hx711_stack,
                    K_THREAD_STACK_SIZEOF(hal_hx711_stack),
                    HAL_thread_HX711_fn, NULL, NULL, NULL,
                    HAL_HX711_PRIO, 0, K_NO_WAIT);
    k_thread_name_set(&hal_hx711_thread_data, "hal_hx711");
#endif
    /* RPM: ISR contadora + timer 100ms → siba_wq (ver HAL_RPM_Init) */
    /* ADC stimulus native_sim: timer 50ms → siba_wq (ver HAL_ADC_Init) */

    LOG_INF("HAL_Init concluído");
    return 0;
}