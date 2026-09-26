/**
 * @file HAL.c
 * @brief HAL da SIBA: ADC (driver adc_ch32v307), HX711 x6 (bit-bang GPIO),
 * ESC PWM (TIM4 CH1/PB6, 50 Hz), RPM (TIM3 CH3/PB0, input capture).
 *
 * ADC dual simultâneo (ADC1 master PA0/PA1/PA3 + ADC2 slave PC0/PC1/PC2),
 * ISR callback → k_work a ~10 Hz. HX711 x6 na Porta E (PE0-5=SCK, PE6-11=DOUT).
 * Relay de potência com sequência de pré-carga (PB13 → 3 s → PB12).
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

/* ADC dual simultâneo: ADC1 master (PA0/PA1/PA3) + ADC2 slave (PC3/PC4/PC2).
 * ISR callback do driver adc_ch32v307 armazena o último frame atomicamente;
 * um k_work publicado a ~10 Hz converte e envia à OSSI. Sem thread de ADC. */

/* Raws de cada ADC — escritos pela ISR, lidos pelos getters (HAL_ADC1_Get/ADC2_Get). */
static atomic_t adc1_raw[HAL_ADC_CHANNELS_PER_ADC];
static atomic_t adc2_raw[HAL_ADC_CHANNELS_PER_ADC];

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

static struct hal_adc_calib adc_calib[SENSOR_ID_TEMPERATURE_MOTOR + 1];

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
/* native_sim: gera tensões "no pino" (mV) plausíveis e escreve diretamente
 * nos atomics adc1_raw[]/adc2_raw[] (sem adc_emul — sem thread de ADC). */
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

/* Converte mV para contagem de 12 bits, saturando em [0, 4095]. */
static inline uint32_t hal_sim_mv_to_raw(float mv)
{
    if (mv < 0.0f)    mv = 0.0f;
    if (mv > 3300.0f) mv = 3300.0f;
    return (uint32_t)(mv * (float)HAL_ADC_FULL_SCALE / (float)HAL_ADC_VREF_MV);
}

static struct k_work adc_stimulus_work;

static void adc_stimulus_work_fn(struct k_work *w)
{
    ARG_UNUSED(w);
    float throttle_pct = (float)atomic_get(&hal_sim_throttle_x100) / 100.0f;
    float rpm          = HAL_RPM_Get();
    float th_frac      = throttle_pct / 100.0f;

    /* Com os dois contatos abertos não passa corrente pela bateria. Sem esta
     * porta o estímulo injetava corrente desde o boot e o Battery_Manager
     * acusava contato colado (ERROR_BATT_RELAY_MISMATCH) em toda execução do
     * simulado — falso positivo que não existe no hardware. */
    float i_mv = 0.0f;
    if (HAL_Relay_Any_Closed()) {
        i_mv = HAL_SIM_I_MV_IDLE
             + (HAL_SIM_I_MV_MAX - HAL_SIM_I_MV_IDLE) * th_frac * th_frac
             + hal_sim_noise(HAL_SIM_I_NOISE);
        if (i_mv < 0.0f) i_mv = 0.0f;
    }

    hal_sim_vbatt_mv -= i_mv * HAL_SIM_VBATT_DRAIN_PER_TICK * 0.01f;
    if (hal_sim_vbatt_mv < HAL_SIM_VBATT_MV_EMPTY) hal_sim_vbatt_mv = HAL_SIM_VBATT_MV_EMPTY;
    float v_mv = hal_sim_vbatt_mv + hal_sim_noise(HAL_SIM_VBATT_NOISE);

    float temp_mv = HAL_SIM_TEMP_MV_BASE
                  + i_mv * HAL_SIM_TEMP_GAIN_MV_PER_MVI
                  + hal_sim_noise(HAL_SIM_TEMP_NOISE);

    const struct hal_adc_calib *pit = &adc_calib[SENSOR_ID_AIR_SPEED];
    float air_ms = rpm * HAL_SIM_AIRSPEED_MS_PER_RPM;
    float air_mv = pit->zero + hal_sim_noise(HAL_SIM_AIRSPEED_NOISE);
    if (pit->law == SENSOR_LAW_SQRT && pit->linear > 0.0f) {
        float u = (air_ms - pit->offset) / pit->linear;
        air_mv += u * u;
    } else {
        air_mv += air_ms;
    }
    if (air_mv < 0.0f) air_mv = 0.0f;

    /* ADC2 slave: v_batt (PC3), i_batt (PC4), spare=0 (PC2) */
    atomic_set(&adc2_raw[HAL_ADC2_IDX_V_BATT],  (atomic_val_t)hal_sim_mv_to_raw(v_mv));
    atomic_set(&adc2_raw[HAL_ADC2_IDX_I_BATT],  (atomic_val_t)hal_sim_mv_to_raw(i_mv));
    atomic_set(&adc2_raw[HAL_ADC2_IDX_SPARE],   0U);
    /* ADC1 master: air_speed (PA0), temp_batt (PA1), temp_motor (PA3) */
    atomic_set(&adc1_raw[HAL_ADC1_IDX_AIR_SPEED],  (atomic_val_t)hal_sim_mv_to_raw(air_mv));
    atomic_set(&adc1_raw[HAL_ADC1_IDX_TEMP_BATT],  (atomic_val_t)hal_sim_mv_to_raw(temp_mv));
    atomic_set(&adc1_raw[HAL_ADC1_IDX_TEMP_MOTOR], (atomic_val_t)hal_sim_mv_to_raw(temp_mv));

    OSSI_Submit_Sensor(SENSOR_ID_V_BATT,              HAL_ADC_Get_VBatt());
    OSSI_Submit_Sensor(SENSOR_ID_I_BATT,              HAL_ADC_Get_IBatt());
    OSSI_Submit_Sensor(SENSOR_ID_TEMPERATURE_BATTERY, HAL_ADC_Get_Temperature_Batt());
    OSSI_Submit_Sensor(SENSOR_ID_AIR_SPEED,           HAL_ADC_Get_Air_Speed());
    OSSI_Submit_Sensor(SENSOR_ID_TEMPERATURE_MOTOR,   HAL_ADC_Get_Temperature_Motor());
}

static void adc_stimulus_timer_fn(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_work_submit_to_queue(&siba_wq, &adc_stimulus_work);
}

K_TIMER_DEFINE(adc_stimulus_timer, adc_stimulus_timer_fn, NULL);
#endif /* HAL_ENABLE_ADC && CONFIG_ARCH_POSIX */

#if HAL_ENABLE_ADC && !defined(CONFIG_ARCH_POSIX)
/* ── Hardware real: ADC dual simultâneo via ISR callback ─────────────────── *
 *
 * adc_ch32v307_set_frame_callback() registra hal_adc_isr, que roda
 * diretamente na ISR do DMA sem context-switch. Cada frame entrega 3 words
 * de 32 bits: bits[15:0]=ADC1 (ar/temp), bits[31:16]=ADC2 (bat/corrente).
 * A cada HAL_ADC_PUBLISH_FRAMES (~10 Hz) um k_work publica na siba_wq.     */

#include "../../drivers/adc_ch32v307.h"

#define ADC1_NODE DT_NODELABEL(adc1)
#define ADC2_NODE DT_NODELABEL(adc2)
static const struct device *adc1_dev = DEVICE_DT_GET(ADC1_NODE);
static const struct device *adc2_dev = DEVICE_DT_GET(ADC2_NODE);

/* ADC1: PA0=CH0(air_speed), PA1=CH1(temp_batt), PA3=CH3(temp_motor). */
static const struct adc_channel_cfg adc1_ch_cfg[HAL_ADC_CHANNELS_PER_ADC] = {
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 0  },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 1  },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 3  },
};
/* ADC2: PC3=CH13(v_batt), PC4=CH14(i_batt), PC2=CH12(spare). */
static const struct adc_channel_cfg adc2_ch_cfg[HAL_ADC_CHANNELS_PER_ADC] = {
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 13 },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 14 },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
      .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 12 },
};

static inline uint16_t hal_adc_raw_cal(uint32_t raw12, int32_t off)
{
    if (raw12 == 0U || raw12 == 4095U) return (uint16_t)raw12;
    int32_t v = (int32_t)raw12 + off;
    if (v < 0)    return 0U;
    if (v > 4095) return 4095U;
    return (uint16_t)v;
}

/* ~16 Hz: acumula 1500 frames (~62,6 ms a 23,96 kSps com SMP=7/BATCH=1).
 * 4095 × 1500 = 6 142 500 — cabe em uint32_t sem overflow. */
#define HAL_ADC_ACCUM_FRAMES  1500U

static uint32_t hal_adc_frame_cnt;
static uint32_t adc1_accum[HAL_ADC_CHANNELS_PER_ADC];
static uint32_t adc2_accum[HAL_ADC_CHANNELS_PER_ADC];
static struct k_work hal_adc_publish_work;

static void hal_adc_publish_work_fn(struct k_work *w)
{
    ARG_UNUSED(w);
    OSSI_Submit_Sensor(SENSOR_ID_V_BATT,              HAL_ADC_Get_VBatt());
    OSSI_Submit_Sensor(SENSOR_ID_I_BATT,              HAL_ADC_Get_IBatt());
    OSSI_Submit_Sensor(SENSOR_ID_TEMPERATURE_BATTERY, HAL_ADC_Get_Temperature_Batt());
    OSSI_Submit_Sensor(SENSOR_ID_AIR_SPEED,           HAL_ADC_Get_Air_Speed());
    OSSI_Submit_Sensor(SENSOR_ID_TEMPERATURE_MOTOR,   HAL_ADC_Get_Temperature_Motor());
}

static void hal_adc_isr(const uint32_t *frame, uint16_t nch,
                         int32_t cal_m, int32_t cal_s, void *user)
{
    ARG_UNUSED(user);
    uint16_t ch = (nch < (uint16_t)HAL_ADC_CHANNELS_PER_ADC)
                ? nch : (uint16_t)HAL_ADC_CHANNELS_PER_ADC;
    for (uint16_t i = 0; i < ch; i++) {
        adc1_accum[i] += hal_adc_raw_cal(frame[i] & 0xFFFU, cal_m);
        adc2_accum[i] += hal_adc_raw_cal((frame[i] >> 16) & 0xFFFU, cal_s);
    }
    if (++hal_adc_frame_cnt >= HAL_ADC_ACCUM_FRAMES) {
        for (uint16_t i = 0; i < ch; i++) {
            atomic_set(&adc1_raw[i],
                       (atomic_val_t)(adc1_accum[i] / HAL_ADC_ACCUM_FRAMES));
            atomic_set(&adc2_raw[i],
                       (atomic_val_t)(adc2_accum[i] / HAL_ADC_ACCUM_FRAMES));
            adc1_accum[i] = 0U;
            adc2_accum[i] = 0U;
        }
        hal_adc_frame_cnt = 0;
        k_work_submit_to_queue(&siba_wq, &hal_adc_publish_work);
    }
}
#endif /* HAL_ENABLE_ADC && !CONFIG_ARCH_POSIX */

int HAL_ADC_Init(void)
{
    hal_adc_calib_init();

#if HAL_ENABLE_ADC
#ifndef CONFIG_ARCH_POSIX
    if (!device_is_ready(adc1_dev)) {
        LOG_ERR("ADC1 não pronto");
        return -ENODEV;
    }
    if (!device_is_ready(adc2_dev)) {
        LOG_ERR("ADC2 não pronto");
        return -ENODEV;
    }
    for (int i = 0; i < (int)HAL_ADC_CHANNELS_PER_ADC; i++) {
        int r = adc_channel_setup(adc1_dev, &adc1_ch_cfg[i]);
        if (r) { LOG_ERR("ADC1 setup ch%d falhou: %d", i, r); return r; }
        r = adc_channel_setup(adc2_dev, &adc2_ch_cfg[i]);
        if (r) { LOG_ERR("ADC2 setup ch%d falhou: %d", i, r); return r; }
    }
    k_work_init(&hal_adc_publish_work, hal_adc_publish_work_fn);
    adc_ch32v307_set_frame_callback(adc1_dev, hal_adc_isr, NULL);
    LOG_INF("HAL ADC: dual simultâneo, ISR callback, ~100 Hz publish (média 1500 frames)");
#else
    k_work_init(&adc_stimulus_work, adc_stimulus_work_fn);
    k_timer_start(&adc_stimulus_timer,
                  K_MSEC(HAL_SIM_ADC_PERIOD_MS),
                  K_MSEC(HAL_SIM_ADC_PERIOD_MS));
    LOG_INF("HAL ADC: estímulo synthetic (%dms via timer)", HAL_SIM_ADC_PERIOD_MS);
#endif /* CONFIG_ARCH_POSIX */
#else
    LOG_INF("HAL ADC: desabilitado via HAL_prm.h (HAL_DISABLE_ADC)");
#endif /* HAL_ENABLE_ADC */
    return 0;
}

/** @brief Leitura bruta de um canal do ADC1 (HAL_ADC1_IDX_*). */
uint16_t HAL_ADC1_Get(uint8_t idx)
{
    if (idx >= HAL_ADC_CHANNELS_PER_ADC) return 0;
    return (uint16_t)atomic_get(&adc1_raw[idx]);
}

/** @brief Leitura bruta de um canal do ADC2 (HAL_ADC2_IDX_*). */
uint16_t HAL_ADC2_Get(uint8_t idx)
{
    if (idx >= HAL_ADC_CHANNELS_PER_ADC) return 0;
    return (uint16_t)atomic_get(&adc2_raw[idx]);
}

/** @brief Tensão da bateria calibrada. @return V. */
float HAL_ADC_Get_VBatt(void)
{
    return hal_adc_apply_calib(SENSOR_ID_V_BATT,
                               adc_raw_to_mv(HAL_ADC2_Get(HAL_ADC2_IDX_V_BATT)));
}

/** @brief Corrente da bateria calibrada. @return A. */
float HAL_ADC_Get_IBatt(void)
{
    return hal_adc_apply_calib(SENSOR_ID_I_BATT,
                               adc_raw_to_mv(HAL_ADC2_Get(HAL_ADC2_IDX_I_BATT)));
}

/** @brief Temperatura da bateria calibrada. @return °C. */
float HAL_ADC_Get_Temperature_Batt(void)
{
    return hal_adc_apply_calib(SENSOR_ID_TEMPERATURE_BATTERY,
                               adc_raw_to_mv(HAL_ADC1_Get(HAL_ADC1_IDX_TEMP_BATT)));
}

/** @brief Temperatura do motor calibrada. @return °C. */
float HAL_ADC_Get_Temperature_Motor(void)
{
    return hal_adc_apply_calib(SENSOR_ID_TEMPERATURE_MOTOR,
                               adc_raw_to_mv(HAL_ADC1_Get(HAL_ADC1_IDX_TEMP_MOTOR)));
}

/** @brief Velocidade do ar (Pitot) calibrada. @return m/s. */
float HAL_ADC_Get_Air_Speed(void)
{
    return hal_adc_apply_calib(SENSOR_ID_AIR_SPEED,
                               adc_raw_to_mv(HAL_ADC1_Get(HAL_ADC1_IDX_AIR_SPEED)));
}

/* HX711 bit-bang: espera DOUT=LOW, pulsa SCK 24x lendo MSB-first, mais
 * HX711_GAIN_PULSES pulsos pra selecionar ganho; resultado é complemento
 * de dois de 24 bits. */

#if HAL_ENABLE_HX711 && !defined(CONFIG_ARCH_POSIX)
/* Mapeamento de pinos — ajuste conforme esquemático */
#define HX711_SCK_NODE  DT_NODELABEL(hx711_sck_pins)
#define HX711_DOUT_NODE DT_NODELABEL(hx711_dout_pins)

static const struct gpio_dt_spec hx711_sck[HX711_COUNT] = {
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 0),  /* PE0 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 1),  /* PE1 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 2),  /* PE2 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 3),  /* PE3 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 4),  /* PE4 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_SCK_NODE, gpios, 5),  /* PE5 */
};

static const struct gpio_dt_spec hx711_dout[HX711_COUNT] = {
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 0),  /* PE6  */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 1),  /* PE7  */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 2),  /* PE8  */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 3),  /* PE9  */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 4),  /* PE10 */
    GPIO_DT_SPEC_GET_BY_IDX(HX711_DOUT_NODE, gpios, 5),  /* PE11 */
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
        /* Limita a ~100 Hz e garante fatia de CPU para threads de menor prioridade. */
        k_sleep(K_MSEC(10));
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

static bool  esc_armed;
static float esc_pct;

/** @brief Inicializa o ESC desarmado. @return 0 em sucesso, erro de hal_esc_hw_init. */
int HAL_ESC_Init(void)
{
    esc_armed = false;
    esc_pct   = 0.0f;
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
    esc_pct   = 0.0f;
    LOG_INF("HAL ESC: desarmado");
}

/** @brief Retorna true se o ESC está armado. */
bool HAL_ESC_Is_Armed(void)
{
    return esc_armed;
}

/** @brief Retorna o throttle atualmente aplicado (%). 0.0 se desarmado. */
float HAL_ESC_Get_Throttle_Pct(void)
{
    return esc_pct;
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
    esc_pct = percent;
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

/** @brief Stub (HAL_ENABLE_ESC=0). @return sempre false. */
bool HAL_ESC_Is_Armed(void)
{
    return false;
}

/** @brief Stub (HAL_ENABLE_ESC=0). @return sempre 0.0f. */
float HAL_ESC_Get_Throttle_Pct(void)
{
    return 0.0f;
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

/* I2C2 (PB10=SCL, PB11=SDA) — infra crua sobre a API genérica i2c_* do
 * Zephyr, sem decisão de protocolo. Roda igual em siba_board e native_sim
 * (driver real ou i2c_emul). */
#if HAL_ENABLE_I2C

#define I2C_NODE DT_NODELABEL(i2c2)
static const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

/** @brief Verifica se o device I2C2 está pronto. @return 0 ou -ENODEV. */
int HAL_I2C_Init(void)
{
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C2 não pronto");
        return -ENODEV;
    }
    LOG_INF("HAL I2C: I2C2 inicializado (PB10/PB11)");
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

/* ── Relés de potência (PB12=power, PB13=pré-carga) ───────────────────────
 * Sequência obrigatória:
 *   LIGAR  : precharge ON → sleep 3 s → power ON
 *   DESLIGAR: power OFF → precharge OFF (sem delay)
 * native_sim: stubs no-op (sem GPIO real). */

static bool hal_relay_energized;

/* Estado comandado de cada contato, individualmente. `hal_relay_energized`
 * só fica verdadeiro com a sequência INTEIRA concluída; estes dois seguem
 * cada linha, inclusive durante os segundos em que apenas a pré-carga está
 * fechada — janela em que já circula corrente e isso é esperado. */
static bool hal_relay_precharge_closed;
static bool hal_relay_power_closed;

#ifndef CONFIG_ARCH_POSIX

#define RELAY_POWER_NODE      DT_NODELABEL(relay_power)
#define RELAY_PRECHARGE_NODE  DT_NODELABEL(relay_precharge)

static const struct gpio_dt_spec relay_power_pin     =
    GPIO_DT_SPEC_GET(RELAY_POWER_NODE, gpios);
static const struct gpio_dt_spec relay_precharge_pin =
    GPIO_DT_SPEC_GET(RELAY_PRECHARGE_NODE, gpios);

int HAL_Relay_Init(void)
{
    if (!device_is_ready(relay_power_pin.port)) {
        LOG_ERR("relay_power GPIO não pronto");
        return -ENODEV;
    }
    if (!device_is_ready(relay_precharge_pin.port)) {
        LOG_ERR("relay_precharge GPIO não pronto");
        return -ENODEV;
    }
    gpio_pin_configure_dt(&relay_power_pin,     GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&relay_precharge_pin, GPIO_OUTPUT_INACTIVE);
    hal_relay_energized       = false;
    hal_relay_precharge_closed = false;
    hal_relay_power_closed     = false;
    LOG_INF("HAL Relay: PB12(power) + PB13(precharge) init OK — ambos abertos");
    return 0;
}

void HAL_Relay_Energize(uint32_t precharge_ms)
{
    if (hal_relay_energized) return;
    LOG_INF("HAL Relay: fechando pre-carga (PB13) — aguardando %u ms",
            (unsigned)precharge_ms);
    gpio_pin_set_dt(&relay_precharge_pin, 1);
    hal_relay_precharge_closed = true;
    k_sleep(K_MSEC(precharge_ms));
    LOG_INF("HAL Relay: fechando potencia (PB12)");
    gpio_pin_set_dt(&relay_power_pin, 1);
    hal_relay_power_closed = true;
    hal_relay_energized    = true;
    LOG_INF("HAL Relay: energizado");
}

void HAL_Relay_Deenergize(void)
{
    if (!hal_relay_energized) return;
    LOG_INF("HAL Relay: abrindo potencia (PB12)");
    gpio_pin_set_dt(&relay_power_pin, 0);
    hal_relay_power_closed = false;
    LOG_INF("HAL Relay: abrindo pre-carga (PB13)");
    gpio_pin_set_dt(&relay_precharge_pin, 0);
    hal_relay_precharge_closed = false;
    hal_relay_energized        = false;
    LOG_INF("HAL Relay: desenergizado");
}

#else /* CONFIG_ARCH_POSIX — stubs sem GPIO real */

int HAL_Relay_Init(void)
{
    hal_relay_energized        = false;
    hal_relay_precharge_closed = false;
    hal_relay_power_closed     = false;
    LOG_INF("HAL Relay: native_sim — stubs sem GPIO real");
    return 0;
}

void HAL_Relay_Energize(uint32_t precharge_ms)
{
    if (hal_relay_energized) return;
    hal_relay_precharge_closed = true;
    hal_relay_power_closed     = true;
    hal_relay_energized        = true;
    LOG_INF("HAL Relay: [sim] energizado (pre-carga de %u ms nao simulada)",
            (unsigned)precharge_ms);
}

void HAL_Relay_Deenergize(void)
{
    if (!hal_relay_energized) return;
    hal_relay_power_closed     = false;
    hal_relay_precharge_closed = false;
    hal_relay_energized        = false;
    LOG_INF("HAL Relay: [sim] desenergizado");
}

#endif /* CONFIG_ARCH_POSIX */

bool HAL_Relay_Is_Energized(void)
{
    return hal_relay_energized;
}

bool HAL_Relay_Any_Closed(void)
{
    return hal_relay_precharge_closed || hal_relay_power_closed;
}

bool HAL_Relay_Precharge_Closed(void)
{
    return hal_relay_precharge_closed;
}

bool HAL_Relay_Power_Closed(void)
{
    return hal_relay_power_closed;
}

/* ── Botão de emergência (PD2) + LED externo de pronto (PE15) ───────────
 * PD2 é entrada com pull-up interno: 0 no pino = apertado, 1 = solto. A
 * inversão está no DTS (GPIO_ACTIVE_LOW), então gpio_pin_get_dt devolve 1
 * para "apertado" e o código aqui não inverte nada de novo.
 * PE15 é saída e acende o LED externo de "sistema pronto".
 * Quem consome os dois é o Security_Manager, a cada despacho de sensor.
 * native_sim: stubs — botão sempre solto, LED só loga a transição. */

#ifndef CONFIG_ARCH_POSIX

static const struct gpio_dt_spec emergency_btn =
    GPIO_DT_SPEC_GET(DT_NODELABEL(emergency_button), gpios);
static const struct gpio_dt_spec ready_led =
    GPIO_DT_SPEC_GET(DT_NODELABEL(ready_pin), gpios);

int HAL_Emergency_Init(void)
{
    int r;

    if (!device_is_ready(emergency_btn.port)) {
        LOG_ERR("botao de emergencia (PD2): GPIO nao pronto");
        return -ENODEV;
    }
    if (!device_is_ready(ready_led.port)) {
        LOG_ERR("LED externo de pronto (PE15): GPIO nao pronto");
        return -ENODEV;
    }

    r = gpio_pin_configure_dt(&emergency_btn, GPIO_INPUT | GPIO_PULL_UP);
    if (r) {
        LOG_ERR("PD2 configure falhou: %d", r);
        return r;
    }
    r = gpio_pin_configure_dt(&ready_led, GPIO_OUTPUT_INACTIVE);
    if (r) {
        LOG_ERR("PE15 configure falhou: %d", r);
        return r;
    }

    LOG_INF("HAL Emergencia: PD2(botao, 0=apertado) + PE15(LED pronto) init OK");
    return 0;
}

bool HAL_Emergency_Is_Pressed(void)
{
    int v = gpio_pin_get_dt(&emergency_btn);
    if (v < 0) {
        LOG_ERR("leitura de PD2 falhou (%d) — tratando como apertado", v);
        return true;
    }
    return v == 1;   /* lógico: 1 = ativo = apertado (DTS marca ACTIVE_LOW) */
}

void HAL_Ready_Led_Set(bool on)
{
    gpio_pin_set_dt(&ready_led, on ? 1 : 0);
}

#else /* CONFIG_ARCH_POSIX — stubs sem GPIO real */

static bool hal_sim_ready_led;
static bool hal_sim_btn_pressed;

int HAL_Emergency_Init(void)
{
    hal_sim_ready_led  = false;
    hal_sim_btn_pressed = false;
    LOG_INF("HAL Emergencia: native_sim — botao simulado (solto)");
    return 0;
}

bool HAL_Emergency_Is_Pressed(void)
{
    return hal_sim_btn_pressed;
}

void HAL_Emergency_Sim_Set_Pressed(bool pressed)
{
    hal_sim_btn_pressed = pressed;
    LOG_WRN("HAL Emergencia: [sim] botao %s", pressed ? "APERTADO" : "solto");
}

void HAL_Ready_Led_Set(bool on)
{
    if (on == hal_sim_ready_led) return;
    hal_sim_ready_led = on;
    LOG_INF("HAL Emergencia: [sim] LED de pronto %s", on ? "aceso" : "apagado");
}

#endif /* CONFIG_ARCH_POSIX */

/* HAL_Init — inicializa periféricos e cria a thread bloqueante do HX711.
 * O ADC usa ISR callback (sem thread própria). O relay deve ser energizado
 * explicitamente por HAL_Relay_Energize() após a GUI sinalizar pronto. */

#if HAL_ENABLE_HX711
K_THREAD_STACK_DEFINE(hal_hx711_stack, HAL_STACK_SIZE);
static struct k_thread hal_hx711_thread_data;
#endif

int HAL_Init(void)
{
    int r;

    r = HAL_Relay_Init();
    if (r) return r;

    r = HAL_Emergency_Init();
    if (r) return r;

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

#if HAL_ENABLE_HX711
    k_thread_create(&hal_hx711_thread_data, hal_hx711_stack,
                    K_THREAD_STACK_SIZEOF(hal_hx711_stack),
                    HAL_thread_HX711_fn, NULL, NULL, NULL,
                    HAL_HX711_PRIO, 0, K_NO_WAIT);
    k_thread_name_set(&hal_hx711_thread_data, "hal_hx711");
#endif

    LOG_INF("HAL_Init concluído");
    return 0;
}