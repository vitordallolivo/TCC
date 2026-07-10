/*
 * HAL.c — SIBA Board
 *
 * Hardware Abstraction Layer para:
 *   - ADC (via driver adc_ch32v307, DMA circular)
 *   - HX711 x6 (bit-bang GPIO, protocolo serial síncrono)
 *   - ESC PWM (TIM4 CH1, PB6, 50 Hz)
 *   - RPM counter (TIM3 CH3, PB0, input capture)
 *
 * As threads bloqueantes (ADC, HX711) são criadas com k_thread_create.
 * RPM e estímulo ADC (native_sim) usam k_timer + k_work no siba_wq.
 *
 * PINOS HX711 — AJUSTE CONFORME SEU ESQUEMÁTICO (remapeados para a Porta E
 * de propósito: o ADC1 do CH32V307 só alcança os canais 0-7 / pinos PA0-PA7,
 * então PA1-PA7 ficam reservados para os canais analógicos — ver bloco ADC):
 *   Célula 0: SCK=PE2   DOUT=PE8
 *   Célula 1: SCK=PE3   DOUT=PE9
 *   Célula 2: SCK=PE4   DOUT=PE10
 *   Célula 3: SCK=PE5   DOUT=PE11
 *   Célula 4: SCK=PE6   DOUT=PE12
 *   Célula 5: SCK=PE7   DOUT=PE13   (PB0 reservado para TIM3/RPM)
 */

#include "HAL.h"
#include "OSSI.h"
#include "Log_Manager.h"
#include "CMD_Core_prm.h"   /* defaults de calibração/faixa por sensor (tabela) */
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
/* RCC — registrador de habilitação de clock dos periféricos em APB1.
 * Compartilhado entre RPM (TIM3, RCC_APB1_TIM3EN) e DAC (DAC1,
 * RCC_APB1PCENR_DAC_EN) — fica aqui no topo para não criar dependência
 * entre as seções RPM/DAC quando uma delas é desabilitada via HAL_prm.h. */
#define RCC_BASE         0x40021000UL
#define RCC_APB1PCENR    (*(volatile uint32_t *)(RCC_BASE + 0x1C))
#endif /* !CONFIG_ARCH_POSIX */

#ifdef CONFIG_ARCH_POSIX
/* native_sim: throttle "comandado" do ESC synthetic (percent * 100),
 * compartilhado entre HAL_ESC_Set_Throttle (escritor — ver seção ESC),
 * HAL_thread_RPM_fn (RPM persegue o throttle) e o stimulus do ADC
 * (corrente/temperatura/air_speed dependem do throttle/RPM) — declarado
 * aqui no topo porque é referenciado antes da seção ESC. */
static atomic_t hal_sim_throttle_x100;
#endif

/* ===========================================================
 * ADC — scan circular único (DMA) com 4 canais (ver HAL_ADC_IDX_* em HAL.h).
 *
 * IMPORTANTE: o periférico ADC1 do CH32V307 (único suportado pelo driver
 * custom adc_ch32v307 — daí essa pasta existir em vez de usar o driver ADC
 * genérico do Zephyr) só enxerga os canais 0-7, ligados fisicamente aos
 * pinos PA0-PA7. Canais 8+ (PB0/PB1/PC0-PC5) pertencem ao mux do ADC2 e NÃO
 * são alcançáveis por este driver. Pinagem REAL aplicada (ver HW.md):
 *   idx 0 = CH0 (PA0)  v_batt
 *   idx 1 = CH1 (PA1)  i_batt
 *   idx 2 = CH2 (PA2)  air_speed (Pitot)
 *   idx 3 = CH6 (PA6)  temperature_battery (uso duplo c/ Ensaio_Termistor_1)
 * PA3/PA4/PA7 (Ensaio_Bateria_*) não entram nesse scan — ver siba_board.dts.
 * =========================================================== */
#define ADC_NUM_CHANNELS  4

/* adc_raw[]/HAL_ADC_Get() são software puro (nenhum acesso a hardware) —
 * ficam sempre compilados mesmo com HAL_ENABLE_ADC=0 (ver HAL_prm.h),
 * para que HAL_ADC_Get_*()/Test_Shell continuem linkando e devolvendo 0. */
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

/* Tensão lida no pino do ADC, em mV, a partir da contagem bruta */
static inline float adc_raw_to_mv(uint16_t raw)
{
    return (float)raw * (float)HAL_ADC_VREF_MV / (float)HAL_ADC_FULL_SCALE;
}

/* ---- Calibração genérica dos sensores baseados em ADC (v_batt/i_batt/
 * temperatura/air_speed): y = linear * (tensão_no_pino_mV) + offset — o
 * MESMO padrão e a MESMA estrutura do HX711 (HAL_HX711_Set_Calib), sem
 * nenhuma fórmula física de hardware no meio (divisor resistivo, ACS758,
 * NTC, MPXV7002DP — removidas; ver TODO "calibrar constantes..." em
 * CLAUDE.md: ninguém tem os componentes reais em mãos para confiar nelas).
 * O usuário ajusta linear/offset via CMD/shell com os valores medidos na
 * bancada, exatamente como já faz para as células de carga. Indexado por
 * SENSOR_ID_* (Log_Manager.h); só as posições 6,7,9,10 são usadas (8 = SOC,
 * calculado pelo Battery_Manager via polinômio — não passa por aqui). */
struct hal_adc_calib { float linear; float offset; };

static struct hal_adc_calib adc_calib[SENSOR_ID_AIR_SPEED + 1];

static void hal_adc_calib_init(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(adc_calib); i++) {
        adc_calib[i].linear = 1.0f;
        adc_calib[i].offset = 0.0f;
    }
    /* Semeia os defaults de engenharia (mV->V/A/°C) da tabela central
     * (CMD_Core_prm.h). Aplicado AQUI (e não só no cache do CMD_Core) porque
     * HAL_Init roda DEPOIS de CMD_Core_Init — senão v_batt/i_batt sairiam em mV
     * e a segurança dispararia emergência falsa, congelando o RPM. */
#define X(ID, DEF_LIN, DEF_OFF, MINV, MAXV, ERRBIT)          \
    if ((size_t)(ID) < ARRAY_SIZE(adc_calib)) {              \
        adc_calib[(ID)].linear = (DEF_LIN);                  \
        adc_calib[(ID)].offset = (DEF_OFF);                  \
    }
    SIBA_SENSOR_TABLE(X)
#undef X
}

void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset)
{
    if (sensor_id >= ARRAY_SIZE(adc_calib)) {
        return;
    }
    adc_calib[sensor_id].linear = linear;
    adc_calib[sensor_id].offset = offset;
}

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

static inline float hal_adc_apply_calib(uint8_t sensor_id, float phys_value)
{
    return phys_value * adc_calib[sensor_id].linear + adc_calib[sensor_id].offset;
}

#if HAL_ENABLE_ADC && defined(CONFIG_ARCH_POSIX)
/* ---- native_sim: "estímulo" do ADC emulado (zephyr,adc-emul, ver
 * native_sim.overlay). Como HAL_ADC_Get_* agora é calibração linear pura
 * (sem fórmula física — ver bloco de calibração acima), não há mais nada
 * para "inverter": geramos diretamente tendências plausíveis de tensão
 * "no pino" em mV (0-3300) e alimentamos o adc_emul via
 * adc_emul_const_value_set(). Com a calibração default (linear=1, offset=0)
 * o valor relatado é numericamente igual ao mV gerado aqui — comportamento
 * esperado, igual às células de carga não calibradas.
 *
 * Modelo — propositalmente simples (só para gerar sinais plausíveis e
 * variáveis, não um modelo físico de verdade): "i_batt" (mV) sobe com
 * throttle² do ESC; "v_batt" (mV) drena devagar com a corrente; "temperatura"
 * (mV) sobe com a corrente; "air_speed" (mV) acompanha o RPM (já synthetic —
 * ver HAL_thread_RPM_fn). Tudo com pequeno ruído pseudo-aleatório. */
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

#define HAL_SIM_AIRSPEED_MV_PER_RPM   0.25f
#define HAL_SIM_AIRSPEED_NOISE        25.0f

static float hal_sim_vbatt_mv = HAL_SIM_VBATT_MV_FULL;

static inline float hal_sim_noise(float amplitude)
{
    /* HAL_SIM_NOISE_SCALE (HAL_prm.h) zera/atenua o ruído p/ testar calibração. */
    int32_t r = (int32_t)(sys_rand32_get() % 2001) - 1000; /* -1000..+1000 */
    return amplitude * HAL_SIM_NOISE_SCALE * ((float)r / 1000.0f);
}

static inline int32_t hal_sim_clamp_mv(float mv)
{
    if (mv < 0.0f)    mv = 0.0f;
    if (mv > 3300.0f) mv = 3300.0f;
    return (int32_t)mv;
}

static struct k_work adc_stimulus_work;

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

    float air_mv = rpm * HAL_SIM_AIRSPEED_MV_PER_RPM + hal_sim_noise(HAL_SIM_AIRSPEED_NOISE);
    if (air_mv < 0.0f) air_mv = 0.0f;

    adc_emul_const_value_set(adc_dev, ADC_CH_V_BATT,    (uint32_t)hal_sim_clamp_mv(v_mv));
    adc_emul_const_value_set(adc_dev, ADC_CH_I_BATT,    (uint32_t)hal_sim_clamp_mv(i_mv));
    adc_emul_const_value_set(adc_dev, ADC_CH_TEMP_BATT, (uint32_t)hal_sim_clamp_mv(temp_mv));
    adc_emul_const_value_set(adc_dev, ADC_CH_AIR_SPEED, (uint32_t)hal_sim_clamp_mv(air_mv));
}

static void adc_stimulus_timer_fn(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_work_submit_to_queue(&siba_wq, &adc_stimulus_work);
}

K_TIMER_DEFINE(adc_stimulus_timer, adc_stimulus_timer_fn, NULL);
#endif /* HAL_ENABLE_ADC && CONFIG_ARCH_POSIX */

int HAL_ADC_Init(void)
{
    /* hal_adc_calib_init() é software puro — roda mesmo desabilitado,
     * para que HAL_ADC_Get_*()/HAL_ADC_Set_Calib() continuem coerentes. */
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

uint16_t HAL_ADC_Get(uint8_t channel)
{
    if (channel >= ADC_NUM_CHANNELS) return 0;
    return (uint16_t)atomic_get(&adc_raw[channel]);
}

float HAL_ADC_Get_VBatt(void)
{
    return hal_adc_apply_calib(SENSOR_ID_V_BATT,
                               adc_raw_to_mv(HAL_ADC_Get(HAL_ADC_IDX_V_BATT)));
}

float HAL_ADC_Get_IBatt(void)
{
    return hal_adc_apply_calib(SENSOR_ID_I_BATT,
                               adc_raw_to_mv(HAL_ADC_Get(HAL_ADC_IDX_I_BATT)));
}

float HAL_ADC_Get_Temperature_Batt(void)
{
    return hal_adc_apply_calib(SENSOR_ID_TEMPERATURE_BATTERY,
                               adc_raw_to_mv(HAL_ADC_Get(HAL_ADC_IDX_TEMP_BATT)));
}

float HAL_ADC_Get_Air_Speed(void)
{
    return hal_adc_apply_calib(SENSOR_ID_AIR_SPEED,
                               adc_raw_to_mv(HAL_ADC_Get(HAL_ADC_IDX_AIR_SPEED)));
}

#if HAL_ENABLE_ADC
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
        /* adc_emul completa a "conversão" instantaneamente — sem isso o
         * loop nunca bloqueia e trava o scheduler/clock do native_sim
         * (ver CLAUDE.md "Modo Mock"). Pausa equivalente ao tempo real
         * de uma conversão+oversample na placa. */
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

/* ===========================================================
 * HX711 — Bit-bang GPIO
 *
 * Protocolo:
 *   1. Aguarda DOUT=LOW (conversão pronta, ~100ms @ 10Hz)
 *   2. Pulsa SCK 24 vezes lendo 1 bit por pulso (MSB first)
 *   3. Pulsa SCK (HX711_GAIN_PULSES) vezes extras → seleciona ganho
 *   4. Resultado é complemento de dois de 24 bits com sinal
 * =========================================================== */

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
#endif /* HAL_ENABLE_HX711 && !CONFIG_ARCH_POSIX */

#if HAL_ENABLE_HX711 && defined(CONFIG_ARCH_POSIX)
/* native_sim: sem GPIO bit-bang (DT_NODELABEL(hx711_sck/dout_pins) não
 * existem no devicetree do native_sim) — sintetiza o valor "raw" 24 bits
 * direto, modelo de empuxo proporcional ao quadrado do RPM, mesmo padrão
 * synthetic do antigo HAL_mock.c, mas publicado pelo MESMO
 * HAL_thread_HX711_fn / HAL_HX711_Get_.. / Set_Calib / Tare reais — só a
 * leitura do transdutor muda.
 */
#define HAL_SIM_HX711_BASE_RAW      8000000L
#define HAL_SIM_HX711_GAIN_PER_RPM2     0.06f
#define HAL_SIM_HX711_NOISE_RAW          400L
/* Escala synthetic (counts/grama): sem isso, com scale=1 os grams saem em
 * centenas de milhar. 0,06·rpm² a 3000 RPM = 540k counts; /270 ~= 2000 g. */
#define HAL_SIM_HX711_SCALE            270.0f
/* Leitura de REPOUSO (grams) por célula, mesmo com rpm=0 e ruído=0. Sem isto
 * grams=(BASE-tare)/scale=0 e não há NADA para tarar/calibrar no native_sim.
 * Distinto por célula (idle + idx·step) p/ dar pra distinguir uma da outra. */
#define HAL_SIM_HX711_IDLE_GRAMS      1000.0f
#define HAL_SIM_HX711_IDLE_STEP_G      250.0f

static bool hx711_read_one(uint8_t idx, int32_t *out)
{
    /* A versão real bloqueia aguardando DOUT=LOW (~10Hz por célula — ver
     * hx711_read_one acima). Sem esse sleep aqui, HAL_thread_HX711_fn roda
     * em loop apertado sem nunca ceder a CPU: o relógio simulado do
     * native_sim trava em t=0 e nenhuma outra thread (shell incluído)
     * consegue progredir. */
    k_sleep(K_MSEC(100));

    float rpm = HAL_RPM_Get();
    /* Baseline de repouso não-nulo (e distinto por célula): dá um "peso" fixo
     * em cima do tare para você conseguir tarar (leva a 0) e calibrar o ganho. */
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

int HAL_HX711_Init(void)
{
    k_mutex_init(&hx711_mutex);

    /* Estado/calibração são software puro — sempre inicializados, mesmo
     * com HAL_ENABLE_HX711=0 (ver HAL_prm.h), para que HAL_HX711_Get_*()/
     * Set_Calib/Tare/Get_Calib continuem coerentes para CMD_Core/Test_Shell. */
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
    /* Synthetic: "tara" o baseline (BASE_RAW) e usa uma escala realista, senão
     * grams = raw/scale ~= 8.000.000 (rejeitado pela validação -> células em 0
     * e imunes a offset/linear). Com isto: repouso ~= 0, sobe com rpm² (empuxo)
     * e a calibração (offset/linear via CMD) passa a refletir na telemetria. */
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

int32_t HAL_HX711_Get_Raw(uint8_t idx)
{
    if (idx >= HX711_COUNT) return 0;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    int32_t v = hx711_state[idx].raw;
    k_mutex_unlock(&hx711_mutex);
    return v;
}

/* Modelo canônico do HX711 (igual às libs de referência, ex.: Bogde/AVR):
 *
 *      grams = (raw - tare) / scale
 *
 * UM só offset — o `tare`, em CONTAGENS BRUTAS (raw), subtraído ANTES de
 * dividir. `scale` = contagens por unidade (counts/grama). NÃO existe um
 * segundo offset aditivo (o antigo `+ offset` era redundante e invertia a
 * intuição da calibração). Ver `HAL_HX711_Tare` (tara = raw) e
 * `HAL_HX711_Calibrate_Weight` (deriva scale de um peso conhecido). */
float HAL_HX711_Get_Grams(uint8_t idx)
{
    if (idx >= HX711_COUNT) return 0.0f;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    float v = (float)(hx711_state[idx].raw - hx711_state[idx].tare)
              / hx711_state[idx].scale;
    k_mutex_unlock(&hx711_mutex);
    return v;
}

/* Ajuste direto do par canônico: `scale` (counts/unidade) e `tare` (raw). */
void HAL_HX711_Set_Calib(uint8_t idx, float scale, float tare_raw)
{
    if (idx >= HX711_COUNT) return;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    hx711_state[idx].scale = (scale == 0.0f) ? 1.0f : scale;
    hx711_state[idx].tare  = (int32_t)tare_raw;
    k_mutex_unlock(&hx711_mutex);
    LOG_INF("HX711[%d]: scale=%.4f tare=%d (counts)", idx, (double)scale, (int)tare_raw);
}

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

/* Tara canônica: offset = leitura bruta atual (em counts). Zera a leitura
 * (grams = (raw-raw)/scale = 0). Tarar de novo só re-zera — não empilha. */
void HAL_HX711_Tare(uint8_t idx)
{
    if (idx >= HX711_COUNT) return;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    hx711_state[idx].tare = hx711_state[idx].raw;
    k_mutex_unlock(&hx711_mutex);
    LOG_INF("HX711[%d]: tare=%d (counts)", idx, hx711_state[idx].tare);
}

/* Calibra a escala por PESO CONHECIDO: com a célula já tarada, coloca-se um
 * peso `known_weight` (na unidade desejada) e resolve-se scale de modo que
 * grams == known_weight:  scale = (raw - tare) / known_weight  (counts/unidade). */
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
            /* Canônico: grams = (raw - tare)/scale (ver HAL_HX711_Get_Grams). */
            float grams = (float)(raw - hx711_state[i].tare)
                          / hx711_state[i].scale;
            k_mutex_unlock(&hx711_mutex);

            OSSI_Submit_Sensor((uint32_t)i, grams); /* load_cell_N */
        }
    }
}
#endif /* HAL_ENABLE_HX711 */

/* ===========================================================
 * ESC PWM — TIM4 CH1 (PB6), 50 Hz
 *
 * native_sim/POSIX não tem TIM4 nem um substituto in-tree utilizável: o
 * driver genérico zephyr,fake-pwm é FFF-based e exige
 * subsys/testsuite/include/zephyr/fff.h, que só existe em builds
 * Twister/ztest (não em firmware de aplicação normal — ver
 * native_sim.conf). Então, só para este subsistema, HAL.c ganha um
 * pequeno bloco `#ifdef CONFIG_ARCH_POSIX` com um substituto synthetic
 * — mesma API pública (HAL_ESC_Init/Arm/Disarm/Set_Throttle), mesmo
 * comportamento de alto nível (precisa armar antes de aceitar throttle),
 * só que sem tocar PWM de verdade. `hal_sim_throttle_pct` é consultado
 * por HAL_thread_RPM_fn (também synthetic em POSIX, ver abaixo) para que
 * o RPM "siga" o throttle, como no antigo HAL_mock.c.
 * =========================================================== */
#if HAL_ENABLE_ESC
#ifdef CONFIG_ARCH_POSIX

static bool esc_armed;
/* hal_sim_throttle_x100 (throttle * 100) declarado no topo do arquivo —
 * compartilhado com o stimulus do ADC e HAL_thread_RPM_fn. */

int HAL_ESC_Init(void)
{
    esc_armed = false;
    atomic_set(&hal_sim_throttle_x100, 0);
    LOG_INF("HAL ESC: modo synthetic (native_sim) — sem PWM/TIM4");
    return 0;
}

void HAL_ESC_Arm(void)
{
    k_sleep(K_MSEC(2000));
    esc_armed = true;
    LOG_INF("HAL ESC: armado (synthetic)");
}

void HAL_ESC_Disarm(void)
{
    esc_armed = false;
    atomic_set(&hal_sim_throttle_x100, 0);
    LOG_INF("HAL ESC: desarmado (synthetic)");
}

int HAL_ESC_Set_Throttle(float percent)
{
    if (!esc_armed) {
        LOG_WRN("HAL ESC: throttle ignorado — ESC não armado");
        return -EPERM;
    }

    if (percent < 0.0f)   percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    atomic_set(&hal_sim_throttle_x100, (atomic_val_t)(percent * 100.0f));
    return 0;
}

#else /* !CONFIG_ARCH_POSIX — siba_board real: TIM4/PWM via API genérica */

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

#endif /* CONFIG_ARCH_POSIX */

#else /* !HAL_ENABLE_ESC */

int HAL_ESC_Init(void)
{
    LOG_INF("HAL ESC: desabilitado via HAL_prm.h (HAL_DISABLE_ESC)");
    return 0;
}

void HAL_ESC_Arm(void)
{
    LOG_INF("HAL ESC: arm ignorado — módulo desabilitado (HAL_DISABLE_ESC)");
}

void HAL_ESC_Disarm(void)
{
    /* no-op — módulo desabilitado */
}

int HAL_ESC_Set_Throttle(float percent)
{
    ARG_UNUSED(percent);
    return -ENODEV;
}

#endif /* HAL_ENABLE_ESC */

/* ===========================================================
 * RPM — TIM3 CH3 (PB0), modo input capture
 *
 * native_sim/POSIX não tem TIM3 — o bloco abaixo acessa registradores via
 * MMIO em endereço físico FIXO (0x40000400): em POSIX esse endereço não
 * mapeia para nada e a primeira escrita derruba o processo (SIGSEGV). Não
 * existe driver Counter genérico equivalente (nem para o "wch,gptm-counter"
 * do .dts real, nem um emulado in-tree minimamente próximo do que
 * precisamos), então — só para esta peça — HAL.c ganha um pequeno
 * substituto `#ifdef CONFIG_ARCH_POSIX`: sintetiza o RPM perseguindo o
 * throttle do ESC (mesma ideia/parâmetros do antigo HAL_mock.c, isolada
 * aqui), publicando pelo MESMO caminho OSSI→SYS.
 * =========================================================== */

/* rpm_value (RPM * 10) é consultado por HAL_RPM_Get() — usado pelas threads
 * synthetic do ADC/HX711 em native_sim independentemente de HAL_ENABLE_RPM
 * (ver HAL_prm.h), por isso fica sempre compilado. Permanece 0 (RPM=0) se o
 * módulo RPM estiver desabilitado. */
static atomic_t rpm_value;

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

static void rpm_sim_timer_fn(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_work_submit_to_queue(&siba_wq, &rpm_sim_work);
}

K_TIMER_DEFINE(rpm_sim_timer, rpm_sim_timer_fn, NULL);

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

static void tim3_capture_isr(const void *arg)
{
    ARG_UNUSED(arg);
    if (TIM3_INTFR & TIM_SR_CC3IF) {
        (void)TIM3_CH3CVR;
        TIM3_INTFR &= ~TIM_SR_CC3IF;
        atomic_inc(&rpm_pulse_count);
    }
}

/* Timer a cada 100ms: lê e zera o contador ISR, submete cálculo ao siba_wq */
static void rpm_timer_fn(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_work_submit_to_queue(&siba_wq, &rpm_work);
}

K_TIMER_DEFINE(rpm_timer, rpm_timer_fn, NULL);

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

int HAL_RPM_Init(void)
{
    LOG_INF("HAL RPM: desabilitado via HAL_prm.h (HAL_DISABLE_RPM)");
    return 0;
}

#endif /* HAL_ENABLE_RPM */

/* ===========================================================
 * I2C — I2C1 remapeado para PB8(SCL)/PB9(SDA)
 *
 * Infra MÍNIMA e crua sobre a API genérica i2c_* do Zephyr (driver wch,i2c
 * já existe in-tree — só precisava ser instanciado no .dts, ver
 * siba_board.dts/&i2c1). Sem decisão de protocolo: ler/escrever N bytes
 * num endereço, e scan do barramento — quem decidir como falar com um
 * dispositivo específico (vai substituir o HX711 no futuro) constrói por
 * cima disso. Roda igual nos dois alvos (siba_board e native_sim — ambos
 * têm o driver i2c, real ou emulado pelo i2c_emul).
 * =========================================================== */
#if HAL_ENABLE_I2C

#define I2C_NODE DT_NODELABEL(i2c1)
static const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

int HAL_I2C_Init(void)
{
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C1 não pronto");
        return -ENODEV;
    }
    LOG_INF("HAL I2C: I2C1 inicializado (PB8/PB9, remap=1)");
    return 0;
}

int HAL_I2C_Read(uint16_t addr, uint8_t *buf, size_t len)
{
    return i2c_read(i2c_dev, buf, len, (uint16_t)addr);
}

int HAL_I2C_Write(uint16_t addr, const uint8_t *buf, size_t len)
{
    return i2c_write(i2c_dev, buf, len, (uint16_t)addr);
}

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

int HAL_I2C_Init(void)
{
    LOG_INF("HAL I2C: desabilitado via HAL_prm.h (HAL_DISABLE_I2C)");
    return 0;
}

int HAL_I2C_Read(uint16_t addr, uint8_t *buf, size_t len)
{
    ARG_UNUSED(addr); ARG_UNUSED(buf); ARG_UNUSED(len);
    return -ENODEV;
}

int HAL_I2C_Write(uint16_t addr, const uint8_t *buf, size_t len)
{
    ARG_UNUSED(addr); ARG_UNUSED(buf); ARG_UNUSED(len);
    return -ENODEV;
}

int HAL_I2C_Scan(uint8_t *found_addrs, size_t max, size_t *out_count)
{
    ARG_UNUSED(found_addrs); ARG_UNUSED(max);
    if (out_count) {
        *out_count = 0;
    }
    return -ENODEV;
}

#endif /* HAL_ENABLE_I2C */

/* ===========================================================
 * DAC — DAC1 canal 1 (PA5 / DAC_OUT1)
 *
 * Não existe driver/binding Zephyr para o DAC do CH32V307 — acesso via
 * MMIO bruto, mesmo padrão usado para RPM/TIM3 acima (registradores fixos,
 * clock habilitado direto via RCC). Em native_sim/POSIX não há periférico
 * nem endereço mapeado — substituto synthetic guarda o valor numa variável
 * (mesma ideia de hal_sim_throttle_x100 para o ESC).
 * =========================================================== */
#if HAL_ENABLE_DAC
#ifdef CONFIG_ARCH_POSIX

static atomic_t hal_sim_dac_value;

int HAL_DAC_Init(void)
{
    atomic_set(&hal_sim_dac_value, 0);
    LOG_INF("HAL DAC: modo synthetic (native_sim) — sem DAC1/PA5");
    return 0;
}

void HAL_DAC_Set_Value(uint16_t value_12bit)
{
    if (value_12bit > HAL_DAC_MAX_VALUE) value_12bit = HAL_DAC_MAX_VALUE;
    atomic_set(&hal_sim_dac_value, value_12bit);
}

#else /* !CONFIG_ARCH_POSIX — siba_board real: DAC1 via MMIO direto */

#define DAC_BASE        0x40007400UL
#define DAC_CTLR        (*(volatile uint32_t *)(DAC_BASE + 0x00))
#define DAC_R12BDHR1    (*(volatile uint32_t *)(DAC_BASE + 0x08))  /* canal 1, 12 bits alinhado à direita */

#define DAC_CTLR_EN1    (1UL << 0)   /* habilita canal 1            */
#define DAC_CTLR_BOFF1  (1UL << 1)   /* 0 = buffer de saída ligado  */

#define RCC_APB1PCENR_DAC_EN  (1UL << 29)  /* CH32V20X_V30X_CLOCK_DAC = APB1,29 */

int HAL_DAC_Init(void)
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

void HAL_DAC_Set_Value(uint16_t value_12bit)
{
    if (value_12bit > HAL_DAC_MAX_VALUE) value_12bit = HAL_DAC_MAX_VALUE;
    DAC_R12BDHR1 = value_12bit;
}

#endif /* CONFIG_ARCH_POSIX */

#else /* !HAL_ENABLE_DAC */

int HAL_DAC_Init(void)
{
    LOG_INF("HAL DAC: desabilitado via HAL_prm.h (HAL_DISABLE_DAC)");
    return 0;
}

void HAL_DAC_Set_Value(uint16_t value_12bit)
{
    ARG_UNUSED(value_12bit);
}

#endif /* HAL_ENABLE_DAC */

/* ===========================================================
 * HAL_Init — inicializa periféricos e cria threads bloqueantes
 * =========================================================== */

#if HAL_ENABLE_ADC
K_THREAD_STACK_DEFINE(hal_adc_stack, HAL_STACK_SIZE);
static struct k_thread hal_adc_thread_data;
#endif

#if HAL_ENABLE_HX711
K_THREAD_STACK_DEFINE(hal_hx711_stack, HAL_STACK_SIZE);
static struct k_thread hal_hx711_thread_data;
#endif

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