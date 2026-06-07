/*
 * HAL_mock.c — SIBA Board (modo mock para testes em native_sim)
 *
 * Implementação alternativa de todo o HAL.h que NÃO toca nenhum periférico
 * real — gera dados sintéticos plausíveis e os publica pelo MESMO caminho
 * OSSI→SYS (TARGET_SYS / REQ_TYPE_SYS_UPDATE_SENSOR, sensor_id conforme
 * Log_Manager.h) que HAL_thread_ADC/HX711/RPM_fn usam na implementação real
 * (HAL.c). Assim, toda a cadeia de cima — System_Manager, Battery/Propeller/
 * Security/Log_Manager, CMD_Core, Test_Shell — roda exatamente igual,
 * permitindo achar bugs de lógica/threading sem o W-Link/placa física.
 *
 * Selecionado em tempo de build pelo CMakeLists.txt quando CONFIG_ARCH_POSIX=y
 * (ex.: `west build -b native_sim`) — substitui HAL.c e o driver
 * out-of-tree adc_ch32v307 (que dependem de DTS/MMIO específicos do
 * CH32V307 e não existem nesse alvo). Ver CLAUDE.md "Build mock (native_sim)"
 * e o item "Mock mode" do TODO.
 *
 * Modelo de simulação — propositalmente simples, só para exercitar lógica
 * (não é um modelo físico de verdade):
 *   - ESC armado + throttle  → "alvo" de RPM; RPM persegue o alvo (1ª ordem)
 *   - i_batt  = corrente de repouso + corrente ∝ throttle² (carga do motor)
 *   - v_batt  drena lentamente conforme a energia consumida (Coulomb count
 *     simplificado, acelerado por HAL_MOCK_TIME_SCALE para não levar horas)
 *   - temperatura sobe lentamente em função da corrente (1ª ordem)
 *   - air_speed acompanha o RPM (esteira/fluxo induzido pela hélice)
 *   - load cells = empuxo ∝ RPM² (modelo de hélice simplificado) + variação
 *     por célula + ruído
 * Tudo com pequeno ruído pseudo-aleatório (sys_rand32_get) para parecer real
 * e exercitar os filtros/limites do Security_Manager.
 */

#include "HAL.h"
#include "OSSI.h"
#include "Log_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <math.h>

LOG_MODULE_REGISTER(hal_mock, LOG_LEVEL_INF);

/* ===========================================================
 * Parâmetros do modelo sintético (sem pretensão de precisão física)
 * =========================================================== */
#define HAL_MOCK_RPM_MAX           9000.0f  /* RPM a 100% throttle           */
#define HAL_MOCK_RPM_LAG              0.15f /* fração do erro corrigida/tick */
#define HAL_MOCK_RPM_NOISE           20.0f

#define HAL_MOCK_I_IDLE               0.4f  /* A — consumo em repouso        */
#define HAL_MOCK_I_MAX               45.0f  /* A — consumo extra @ 100% th.  */
#define HAL_MOCK_I_LAG                0.2f
#define HAL_MOCK_I_NOISE              0.15f

#define HAL_MOCK_VBATT_FULL          25.2f  /* V — 6S a 4.2V/célula          */
#define HAL_MOCK_VBATT_EMPTY         18.0f  /* V — 6S a 3.0V/célula          */
#define HAL_MOCK_VBATT_CAPACITY_AH    5.0f  /* Ah — capacidade do pack       */
#define HAL_MOCK_VBATT_NOISE          0.03f
/* Acelera o "relógio" da simulação de energia — sem isso, ver o pack
 * descarregar levaria horas reais de teste. 120x = 2 min reais ≈ 4 h sim. */
#define HAL_MOCK_TIME_SCALE         120.0f

#define HAL_MOCK_TEMP_BASE_C         25.0f
#define HAL_MOCK_TEMP_GAIN_C_PER_A    0.6f
#define HAL_MOCK_TEMP_LAG             0.05f
#define HAL_MOCK_TEMP_NOISE           0.1f

#define HAL_MOCK_AIRSPEED_K           0.0035f /* (m/s) por RPM — esteira     */
#define HAL_MOCK_AIRSPEED_NOISE       0.4f

#define HAL_MOCK_THRUST_K             3.6e-5f /* gramas-força por RPM²       */
#define HAL_MOCK_THRUST_NOISE_G       8.0f

#define HAL_MOCK_HX711_BASE_COUNTS  500000
#define HAL_MOCK_HX711_COUNTS_PER_GRAM 20.0f

#define HAL_MOCK_ADC_PERIOD_MS      100U

/* ---- gerador de ruído pequeno: devolve float uniforme em [-amp, +amp] ---- */
static float sim_noise(float amplitude)
{
    int32_t r = (int32_t)(sys_rand32_get() % 2001) - 1000; /* -1000..+1000 */
    return amplitude * ((float)r / 1000.0f);
}

/* ===========================================================
 * Estado simulado — protegido por sim_mutex (acessado por 3 threads)
 * =========================================================== */
struct hal_mock_state {
    bool  esc_armed;
    float esc_throttle_pct;

    float v_batt;                 /* V   — valor físico simulado (sem calib) */
    float i_batt;                 /* A                                       */
    float temperature_battery;    /* °C                                      */
    float air_speed;              /* m/s                                     */
    float rpm;                    /* RPM                                     */
    float consumed_ah;            /* Ah — Coulomb count simplificado         */
};

static struct hal_mock_state sim = {
    .esc_armed           = false,
    .esc_throttle_pct    = 0.0f,
    .v_batt              = HAL_MOCK_VBATT_FULL - 0.6f, /* "quase cheio"      */
    .i_batt              = HAL_MOCK_I_IDLE,
    .temperature_battery = HAL_MOCK_TEMP_BASE_C,
    .air_speed           = 0.0f,
    .rpm                 = 0.0f,
    .consumed_ah         = 0.0f,
};

static struct k_mutex sim_mutex;

/* ===========================================================
 * Calibração ADC — mesmo padrão/API da implementação real (ver HAL.c
 * HAL_ADC_Set_Calib / hal_adc_apply_calib): y = linear * x + offset,
 * indexado por SENSOR_ID_*. Reaproveitar aqui garante que os comandos
 * "sensor <id> linear|offset" testem exatamente o mesmo código de CMD_Core.
 * =========================================================== */
struct hal_adc_calib { float linear; float offset; };
static struct hal_adc_calib adc_calib[SENSOR_ID_AIR_SPEED + 1];

static void hal_adc_calib_init(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(adc_calib); i++) {
        adc_calib[i].linear = 1.0f;
        adc_calib[i].offset = 0.0f;
    }
}

void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset)
{
    if (sensor_id >= ARRAY_SIZE(adc_calib)) {
        return;
    }
    adc_calib[sensor_id].linear = linear;
    adc_calib[sensor_id].offset = offset;
}

static inline float hal_adc_apply_calib(uint8_t sensor_id, float phys_value)
{
    return phys_value * adc_calib[sensor_id].linear + adc_calib[sensor_id].offset;
}

int HAL_ADC_Init(void)
{
    hal_adc_calib_init();
    LOG_INF("HAL ADC (mock): gerando v_batt/i_batt/temperatura/air_speed sintéticos");
    return 0;
}

uint16_t HAL_ADC_Get(uint8_t channel)
{
    ARG_UNUSED(channel);
    return 0; /* não existe canal físico no mock — use HAL_ADC_Get_<grandeza> */
}

float HAL_ADC_Get_VBatt(void)
{
    k_mutex_lock(&sim_mutex, K_FOREVER);
    float v = sim.v_batt;
    k_mutex_unlock(&sim_mutex);
    return hal_adc_apply_calib(SENSOR_ID_V_BATT, v);
}

float HAL_ADC_Get_IBatt(void)
{
    k_mutex_lock(&sim_mutex, K_FOREVER);
    float i = sim.i_batt;
    k_mutex_unlock(&sim_mutex);
    return hal_adc_apply_calib(SENSOR_ID_I_BATT, i);
}

float HAL_ADC_Get_Temperature_Batt(void)
{
    k_mutex_lock(&sim_mutex, K_FOREVER);
    float t = sim.temperature_battery;
    k_mutex_unlock(&sim_mutex);
    return hal_adc_apply_calib(SENSOR_ID_TEMPERATURE_BATTERY, t);
}

float HAL_ADC_Get_Air_Speed(void)
{
    k_mutex_lock(&sim_mutex, K_FOREVER);
    float v = sim.air_speed;
    k_mutex_unlock(&sim_mutex);
    return hal_adc_apply_calib(SENSOR_ID_AIR_SPEED, v);
}

/* ===========================================================
 * HAL_thread_ADC_fn (mock) — avança o modelo de bateria/ar e publica
 * v_batt/i_batt/temperature_battery/air_speed via OSSI→SYS, no mesmo
 * formato/cadência (10 Hz) da implementação real.
 * =========================================================== */
void HAL_thread_ADC_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    LOG_INF("HAL ADC thread (mock) iniciada");

    const float dt_s = HAL_MOCK_ADC_PERIOD_MS / 1000.0f;
    const float dt_h = dt_s * HAL_MOCK_TIME_SCALE / 3600.0f;

    while (1) {
        k_sleep(K_MSEC(HAL_MOCK_ADC_PERIOD_MS));

        k_mutex_lock(&sim_mutex, K_FOREVER);

        float throttle = sim.esc_armed ? sim.esc_throttle_pct : 0.0f;
        float frac     = throttle / 100.0f;

        /* Corrente: persegue um alvo proporcional a throttle² (carga do motor) */
        float i_target = HAL_MOCK_I_IDLE + HAL_MOCK_I_MAX * frac * frac;
        sim.i_batt += (i_target - sim.i_batt) * HAL_MOCK_I_LAG + sim_noise(HAL_MOCK_I_NOISE);
        if (sim.i_batt < 0.0f) sim.i_batt = 0.0f;

        /* Tensão: drena conforme energia consumida (linear simplificado) */
        sim.consumed_ah += sim.i_batt * dt_h;
        float drained = (HAL_MOCK_VBATT_FULL - HAL_MOCK_VBATT_EMPTY)
                        * (sim.consumed_ah / HAL_MOCK_VBATT_CAPACITY_AH);
        float v = HAL_MOCK_VBATT_FULL - drained + sim_noise(HAL_MOCK_VBATT_NOISE);
        if (v < HAL_MOCK_VBATT_EMPTY) v = HAL_MOCK_VBATT_EMPTY;
        sim.v_batt = v;

        /* Temperatura: sobe com a corrente (1ª ordem) */
        float t_target = HAL_MOCK_TEMP_BASE_C + HAL_MOCK_TEMP_GAIN_C_PER_A * sim.i_batt;
        sim.temperature_battery += (t_target - sim.temperature_battery) * HAL_MOCK_TEMP_LAG
                                   + sim_noise(HAL_MOCK_TEMP_NOISE);

        /* Velocidade do ar: acompanha o RPM (esteira da hélice) */
        float speed = HAL_MOCK_AIRSPEED_K * sim.rpm + fabsf(sim_noise(HAL_MOCK_AIRSPEED_NOISE));
        sim.air_speed = (speed < 0.0f) ? 0.0f : speed;

        k_mutex_unlock(&sim_mutex);

        /* Publica grandezas físicas já calibradas — mesmo formato da HAL.c real */
        struct { uint32_t id; float value; } updates[] = {
            { SENSOR_ID_V_BATT,              HAL_ADC_Get_VBatt() },
            { SENSOR_ID_I_BATT,              HAL_ADC_Get_IBatt() },
            { SENSOR_ID_TEMPERATURE_BATTERY, HAL_ADC_Get_Temperature_Batt() },
            { SENSOR_ID_AIR_SPEED,           HAL_ADC_Get_Air_Speed() },
        };

        for (size_t i = 0; i < ARRAY_SIZE(updates); i++) {
            ossi_msg_t msg = {
                .target   = TARGET_SYS,
                .req_type = REQ_TYPE_SYS_UPDATE_SENSOR,
            };
            msg.payload.sys_req.sensor_id = updates[i].id;
            msg.payload.sys_req.value     = updates[i].value;
            OSSI_Send(&msg);
        }
    }
}

/* ===========================================================
 * HX711 (mock) — simula 6 células de carga lendo "empuxo" ∝ RPM², com
 * variação por célula e ruído. Mantém raw/tare/scale/offset com a MESMA
 * fórmula da implementação real (HAL_HX711_Get_Grams), então os comandos
 * "tare"/"sensor <id> linear|offset" exercitam o código de verdade.
 * =========================================================== */
struct hx711_sim_state {
    int32_t raw;
    int32_t tare;
    float   scale;
    float   offset;
};

static struct hx711_sim_state hx711_state[HX711_COUNT];
static struct k_mutex          hx711_mutex;

int HAL_HX711_Init(void)
{
    k_mutex_init(&hx711_mutex);

    for (int i = 0; i < HX711_COUNT; i++) {
        hx711_state[i].raw    = HAL_MOCK_HX711_BASE_COUNTS;
        hx711_state[i].tare   = 0;
        hx711_state[i].scale  = 1.0f;
        hx711_state[i].offset = 0.0f;
    }

    LOG_INF("HAL HX711 (mock): %d células sintéticas (empuxo ~ RPM²)", HX711_COUNT);
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
    LOG_INF("HX711[%d] (mock): scale=%.4f offset=%.4f", idx, (double)scale, (double)offset);
}

void HAL_HX711_Tare(uint8_t idx)
{
    if (idx >= HX711_COUNT) return;
    k_mutex_lock(&hx711_mutex, K_FOREVER);
    hx711_state[idx].tare = hx711_state[idx].raw;
    k_mutex_unlock(&hx711_mutex);
    LOG_INF("HX711[%d] (mock): tare=%d", idx, hx711_state[idx].tare);
}

void HAL_thread_HX711_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    LOG_INF("HAL HX711 thread (mock) iniciada");
    k_sleep(K_MSEC(500));

    while (1) {
        for (int i = 0; i < HX711_COUNT; i++) {
            k_mutex_lock(&sim_mutex, K_FOREVER);
            float rpm = sim.rpm;
            k_mutex_unlock(&sim_mutex);

            /* Empuxo simplificado ~ RPM², com pequena variação por célula
             * (simula desbalanceamento/posição na bancada) + ruído */
            float per_cell_gain = 1.0f + 0.04f * (float)(i - HX711_COUNT / 2);
            float force_g = HAL_MOCK_THRUST_K * rpm * rpm * per_cell_gain
                            + sim_noise(HAL_MOCK_THRUST_NOISE_G);

            int32_t raw = HAL_MOCK_HX711_BASE_COUNTS
                        + (int32_t)(force_g * HAL_MOCK_HX711_COUNTS_PER_GRAM);

            k_mutex_lock(&hx711_mutex, K_FOREVER);
            hx711_state[i].raw = raw;
            k_mutex_unlock(&hx711_mutex);

            ossi_msg_t msg = {
                .target   = TARGET_SYS,
                .req_type = REQ_TYPE_SYS_UPDATE_SENSOR,
            };
            msg.payload.sys_req.sensor_id = (uint32_t)i; /* load_cell_N */
            msg.payload.sys_req.value     = HAL_HX711_Get_Grams((uint8_t)i);
            OSSI_Send(&msg);

            k_sleep(K_MSEC(15)); /* espaça as 6 leituras, como o bit-bang real */
        }
    }
}

/* ===========================================================
 * ESC (mock) — só guarda estado (armado/throttle); o RPM "responde" a ele
 * =========================================================== */
int HAL_ESC_Init(void)
{
    k_mutex_lock(&sim_mutex, K_FOREVER);
    sim.esc_armed        = false;
    sim.esc_throttle_pct = 0.0f;
    k_mutex_unlock(&sim_mutex);

    LOG_INF("HAL ESC (mock): inicializado (desarmado)");
    return 0;
}

void HAL_ESC_Arm(void)
{
    k_mutex_lock(&sim_mutex, K_FOREVER);
    sim.esc_armed = true;
    k_mutex_unlock(&sim_mutex);
    LOG_INF("HAL ESC (mock): armado");
}

void HAL_ESC_Disarm(void)
{
    k_mutex_lock(&sim_mutex, K_FOREVER);
    sim.esc_armed        = false;
    sim.esc_throttle_pct = 0.0f;
    k_mutex_unlock(&sim_mutex);
    LOG_INF("HAL ESC (mock): desarmado");
}

int HAL_ESC_Set_Throttle(float percent)
{
    k_mutex_lock(&sim_mutex, K_FOREVER);

    if (!sim.esc_armed) {
        k_mutex_unlock(&sim_mutex);
        LOG_WRN("HAL ESC (mock): throttle ignorado — ESC não armado");
        return -EPERM;
    }

    if (percent < 0.0f)   percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;
    sim.esc_throttle_pct = percent;

    k_mutex_unlock(&sim_mutex);
    return 0;
}

/* ===========================================================
 * RPM (mock) — persegue um "alvo" proporcional ao throttle armado,
 * com resposta de 1ª ordem (simula inércia do conjunto motor+hélice)
 * =========================================================== */
int HAL_RPM_Init(void)
{
    LOG_INF("HAL RPM (mock): inicializado — RPM segue o throttle do ESC");
    return 0;
}

float HAL_RPM_Get(void)
{
    k_mutex_lock(&sim_mutex, K_FOREVER);
    float r = sim.rpm;
    k_mutex_unlock(&sim_mutex);
    return r;
}

void HAL_thread_RPM_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    LOG_INF("HAL RPM thread (mock) iniciada");

    while (1) {
        k_sleep(K_MSEC(HAL_RPM_SAMPLE_MS));

        k_mutex_lock(&sim_mutex, K_FOREVER);

        float target = sim.esc_armed
                     ? (sim.esc_throttle_pct / 100.0f) * HAL_MOCK_RPM_MAX
                     : 0.0f;
        sim.rpm += (target - sim.rpm) * HAL_MOCK_RPM_LAG + sim_noise(HAL_MOCK_RPM_NOISE);
        if (sim.rpm < 0.0f) sim.rpm = 0.0f;

        float rpm = sim.rpm;
        k_mutex_unlock(&sim_mutex);

        ossi_msg_t msg = {
            .target   = TARGET_SYS,
            .req_type = REQ_TYPE_SYS_UPDATE_SENSOR,
        };
        msg.payload.sys_req.sensor_id = SENSOR_ID_RPM;
        msg.payload.sys_req.value     = rpm;
        OSSI_Send(&msg);
    }
}

/* ===========================================================
 * HAL_Init (mock) — inicializa o estado sintético e registra as MESMAS
 * threads/IDs/prioridades do HAL real no TCKEC (ver HAL.h)
 * =========================================================== */
int HAL_Init(void)
{
    int r;

    k_mutex_init(&sim_mutex);

    r = HAL_ADC_Init();
    if (r) return r;

    r = HAL_HX711_Init();
    if (r) return r;

    r = HAL_ESC_Init();
    if (r) return r;

    r = HAL_RPM_Init();
    if (r) return r;

    ossi_msg_t req = { .target = TARGET_TCKEC,
                       .req_type = REQ_TYPE_TCKEC_CREATE_THREAD };

    req.payload.tckec_req.thread_id   = HAL_THREAD_ADC_ID;
    req.payload.tckec_req.entry_point = HAL_thread_ADC_fn;
    req.payload.tckec_req.priority    = HAL_ADC_PRIO;
    OSSI_Send(&req);

    req.payload.tckec_req.thread_id   = HAL_THREAD_HX711_ID;
    req.payload.tckec_req.entry_point = HAL_thread_HX711_fn;
    req.payload.tckec_req.priority    = HAL_HX711_PRIO;
    OSSI_Send(&req);

    req.payload.tckec_req.thread_id   = HAL_THREAD_RPM_ID;
    req.payload.tckec_req.entry_point = HAL_thread_RPM_fn;
    req.payload.tckec_req.priority    = HAL_RPM_PRIO;
    OSSI_Send(&req);

    LOG_INF("HAL_mock inicializado — gerando dados sintéticos "
            "(ver CLAUDE.md/HW.md \"Mock mode\"/native_sim)");
    return 0;
}
