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
#include "Security_Manager.h"      /* diagnóstico: error_code agregado */
#include "Security_prm.h"          /* SM_CAL_* — limiares da calibração */
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

#define INV_SQRT3   0.5773502691896258f    /* 1/√3 */
#define TWO_THIRDS  0.6666666666666666f     /* 2/3  */

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
/* ---- Synthetic (native_sim): motor virtual a HAL_SIM_DEMO_RPM ----
 *
 * O gerador sintetiza a tensão NO PINO DO ADC, em mV — a mesma unidade que
 * hal_read_raw() devolve no hardware —, aplicando o inverso do
 * condicionamento real. Assim o native_sim exercita a MESMA cadeia de
 * calibração do hardware (incluindo o levantamento de offset no boot), em vez
 * de injetar V/A já prontos e pular tudo isso. */
#define SIM_P        7.0f
#define SIM_KV_Y     (310.0f / 1.7320508075688772f)
#define SIM_TS       10.0e-6f                 /* == DEF_TS do observador */
#define SIM_I_AMP    8.0f                     /* A (pico) */
#define SIM_LOAD_ANG 0.30f                    /* rad — atraso corrente x BEMF */

/* Erro deliberado no repouso do Hall simulado, dentro do espalhamento medido
 * em bancada (5,8 mV entre corridas — Apêndice A da monografia). Serve para o
 * native_sim DEMONSTRAR a calibração de boot removendo o offset: sem ela,
 * estes mV viram ~0,25 A de corrente fantasma nas três fases. */
#define SIM_HALL_ZERO_ERR_MV   4.2f

static float sim_theta_e;

/* O motor virtual parte PARADO e só arranca quando a calibração conclui —
 * é o que a bancada exige de verdade (offset levantado com o ESC parado) e é
 * o que faz o native_sim exercitar a sequência inteira: repouso -> calibra ->
 * gira. Com o motor girando desde o boot, a calibração corretamente REPROVA
 * (desvio ~95 mV, que é o valor RMS dos 8 A de pico na cadeia). */
static bool sim_motor_on;

/** @brief native_sim: stub (não há ADC real). @return sempre 0. */
static int hal_adc_hw_init(void) { LOG_INF("HAL: ADC synthetic (native_sim)"); return 0; }

/**
 * @brief native_sim: motor virtual a HAL_SIM_DEMO_RPM, devolvido como tensão
 * no pino do ADC (mV) — igual ao hardware.
 * @param Va, Vb, Vc [out] tensão no pino dos canais de tensão (mV)
 * @param ia, ib, ic [out] tensão no pino dos canais de corrente (mV)
 * @return sempre 0
 */
static int hal_read_raw(float *Va, float *Vb, float *Vc,
                        float *ia, float *ib, float *ic)
{
    const float TWO_PI_3 = 2.0943951023931953f;
    const float FOUR_PI_3 = 4.1887902047863905f;

    if (!sim_motor_on) {
        /* Motor parado: fases em 0 V e o Hall no seu repouso (com o erro
         * deliberado). É exatamente o que a calibração precisa medir. */
        *Va = *Vb = *Vc = 0.0f;
        *ia = *ib = *ic = HAL_HALL_ZERO_MV + SIM_HALL_ZERO_ERR_MV;
        return 0;
    }

    float wm = HAL_SIM_DEMO_RPM * (2.0f * (float)M_PI / 60.0f);  /* rad/s mec */
    float we = wm * SIM_P;                                        /* rad/s elétrico */
    float ke_mec = 60.0f / (2.0f * (float)M_PI * SIM_KV_Y);
    float E = ke_mec * wm;                                        /* amplitude BEMF */

    sim_theta_e += we * SIM_TS;
    if (sim_theta_e >= 2.0f * (float)M_PI) sim_theta_e -= 2.0f * (float)M_PI;

    float th = sim_theta_e;

    /* Grandezas físicas do motor virtual */
    float v_a = E * sinf(th);
    float v_b = E * sinf(th - TWO_PI_3);
    float v_c = E * sinf(th - FOUR_PI_3);
    float i_a = SIM_I_AMP * sinf(th - SIM_LOAD_ANG);
    float i_b = SIM_I_AMP * sinf(th - SIM_LOAD_ANG - TWO_PI_3);
    float i_c = SIM_I_AMP * sinf(th - SIM_LOAD_ANG - FOUR_PI_3);

    /* Inverso do condicionamento -> mV no pino.
     * HAL_VOLT_MV_PER_V já é mV_pino por volt de fase (= k x 1000), então
     * MULTIPLICA: 14 V de fase -> 762 mV no pino. */
    *Va = v_a * HAL_VOLT_MV_PER_V;
    *Vb = v_b * HAL_VOLT_MV_PER_V;
    *Vc = v_c * HAL_VOLT_MV_PER_V;
    *ia = HAL_HALL_ZERO_MV + SIM_HALL_ZERO_ERR_MV + i_a * HAL_CURR_MV_PER_A;
    *ib = HAL_HALL_ZERO_MV + SIM_HALL_ZERO_ERR_MV + i_b * HAL_CURR_MV_PER_A;
    *ic = HAL_HALL_ZERO_MV + SIM_HALL_ZERO_ERR_MV + i_c * HAL_CURR_MV_PER_A;
    return 0;
}
#endif /* CONFIG_ARCH_POSIX */

/**
 * @brief Lê a fonte bruta (real ou sintética) e aplica a calibração por
 * sensor — implementação única, compartilhada pelas duas plataformas.
 * @param Va, Vb, Vc [out] tensões de fase calibradas (V)
 * @param ia, ib, ic [out] correntes de fase calibradas (A)
 * @param raw_i [out] tensão bruta no pino dos 3 canais de corrente (mV),
 *        NULL se não interessa — usada pela calibração de offset do boot,
 *        que precisa do valor antes da reta de calibração.
 * @return 0 em sucesso, -EIO se hal_read_raw falhar
 */
static int hal_acquire(float *Va, float *Vb, float *Vc,
                       float *ia, float *ib, float *ic,
                       float raw_i[3])
{
    float rVa, rVb, rVc, ria, rib, ric;
    if (hal_read_raw(&rVa, &rVb, &rVc, &ria, &rib, &ric) != 0) return -EIO;
    if (raw_i) { raw_i[0] = ria; raw_i[1] = rib; raw_i[2] = ric; }
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

/* ======================================================================== *
 * Calibração de offset do sensor Hall de corrente, no boot                 *
 *
 * Por que existe: o SS49E é ratiométrico e repousa em Vcc/2, mas o ponto
 * exato deriva — nos três ensaios de bancada (Apêndice A da monografia) o
 * repouso a 0 A caiu em 2,48570 / 2,48640 / 2,49150 V. São 5,8 mV de
 * espalhamento que, na cadeia de 16,875 mV/A, valem 0,31 A de corrente
 * FANTASMA. E como o erro é comum às três fases, ele não se cancela no
 * Clarke de 3 termos: entra como componente de sequência zero e vira BEMF
 * espúria no SMO. Gravar um offset de catálogo não resolve; tem de ser
 * medido, com o ESC parado, a cada boot.
 *
 * O observador fica BLOQUEADO até isto concluir — senão a PLL trava num
 * ângulo errado e nenhum ajuste de Γ conserta, porque o erro está na
 * entrada, não nos ganhos.
 * ======================================================================== */
typedef enum {
    HAL_CAL_SETTLING,   /* esperando o Hall estabilizar (SM_CAL_SETTLE_MS) */
    HAL_CAL_SAMPLING,   /* acumulando SM_CAL_FRAMES com o ESC parado */
    HAL_CAL_READY,      /* offset gravado — observador liberado */
    HAL_CAL_FAILED,     /* ruído alto demais: não gravou */
} hal_cal_phase_t;

#if HAL_CALIBRATE_ON_BOOT
static hal_cal_phase_t hal_cal_phase = HAL_CAL_SETTLING;
#else
static hal_cal_phase_t hal_cal_phase = HAL_CAL_READY;
#endif

static uint32_t hal_cal_t0;
static uint32_t hal_cal_n;

/* Média e variância por Welford, em float puro.
 *
 * A forma ingênua (somatório + somatório dos quadrados) NÃO serve aqui: com
 * 4096 amostras de ~2238 mV, Σx² chega a 2,05e10, onde o ulp do float é 2048.
 * A variância sairia da diferença E[x²] − E[x]² entre dois números de ~5e6
 * cujo resultado esperado é da ordem de dezenas: cancelamento catastrófico,
 * desvio-padrão puro ruído de arredondamento. Usar double resolveria a
 * numérica e custaria ~10 kB de flash em soft-float, já que o CH32V307 só
 * tem FPU de precisão simples. Welford é estável e cabe em float. */
static float    hal_cal_mean[3];
static float    hal_cal_m2[3];

static const uint8_t hal_cal_target[3] = {
    TARGET_CURRENT_A, TARGET_CURRENT_B, TARGET_CURRENT_C
};

/** @brief true se o offset já foi levantado e o observador está liberado. */
bool HAL_Calibration_Done(void) { return hal_cal_phase == HAL_CAL_READY; }

/**
 * @brief Reinicia a calibração de offset do zero (espera + acumulação).
 *
 * Sem isto, uma calibração reprovada (ESC ligado na hora errada, por exemplo)
 * só se recupera reiniciando a placa — e no meio de um ensaio isso é caro.
 * Chamada pelo FUNC_START/TARGET_DEFAULT do CMD.
 */
void HAL_Calibration_Restart(void)
{
#if HAL_CALIBRATE_ON_BOOT
    hal_cal_phase = HAL_CAL_SETTLING;
    hal_cal_t0    = k_uptime_get_32();
    hal_cal_n     = 0;
    Security_Manager_Error_Set(ERROR_CAL_FAILED, false);
    Security_Manager_Error_Set(ERROR_CAL_IN_PROGRESS, true);
#ifdef CONFIG_ARCH_POSIX
    sim_motor_on = false;      /* o motor virtual volta a parar */
#endif
    LOG_INF("Calibracao de offset REINICIADA: aguardando %u ms "
            "(MANTENHA O ESC PARADO)", (unsigned)SM_CAL_SETTLE_MS);
#endif
}

/**
 * @brief Fecha a calibração: média e desvio-padrão por canal, valida e grava.
 *
 * O desvio-padrão é o critério de aceite: com o ESC parado as três correntes
 * devem estar praticamente em repouso. Ruído alto ali significa ESC
 * energizado durante a calibração, sensor mal alimentado ou terra aberto —
 * casos em que gravar a média seria travar um offset errado para o ensaio
 * inteiro. Melhor FALHAR e sinalizar.
 */
static void hal_calibration_finish(void)
{
    bool ok = true;

    for (int k = 0; k < 3; k++) {
        float mean = hal_cal_mean[k];
        float var  = hal_cal_m2[k] / (float)hal_cal_n;
        float sd   = (var > 0.0f) ? sqrtf(var) : 0.0f;

        if (sd > SM_CAL_STD_MAX_MV) {
            LOG_ERR("Calibracao fase %d REPROVADA: desvio %.2f mV > %.2f mV "
                    "(ESC energizado? sensor sem alimentacao? terra aberto?)",
                    k, (double)sd, (double)SM_CAL_STD_MAX_MV);
            ok = false;
            continue;
        }

        /* Mantém o ganho da tabela e substitui só o offset: para I = 0 no
         * repouso medido, offset = -mean * linear. */
        float linear, old_offset;
        HAL_ADC_Get_Calib(hal_cal_target[k], &linear, &old_offset);
        float new_offset = -mean * linear;
        HAL_ADC_Set_Calib(hal_cal_target[k], linear, new_offset);

        LOG_INF("Calibracao fase %d: repouso %.2f mV (sd %.2f) -> offset "
                "%.3f A (era %.3f A; deriva %.3f A)",
                k, (double)mean, (double)sd, (double)new_offset,
                (double)old_offset, (double)(new_offset - old_offset));
    }

    hal_cal_phase = ok ? HAL_CAL_READY : HAL_CAL_FAILED;
    Security_Manager_Error_Set(ERROR_CAL_IN_PROGRESS, false);
    Security_Manager_Error_Set(ERROR_CAL_FAILED, !ok);

    if (ok) {
        Observer_Reset();   /* zera o transitório acumulado durante a espera */
#ifdef CONFIG_ARCH_POSIX
        sim_motor_on = true;   /* native_sim: só agora o motor virtual arranca */
#endif
        LOG_INF("Calibracao concluida — observador liberado");
    } else {
        LOG_ERR("Calibracao FALHOU — observador segue bloqueado");
    }
}

/**
 * @brief Um passo da calibração. Chamada por amostra enquanto não concluir.
 * @param raw_i tensão bruta no pino dos 3 canais de corrente (mV)
 */
static void hal_calibration_step(const float raw_i[3])
{
    uint32_t now = k_uptime_get_32();

    if (hal_cal_phase == HAL_CAL_SETTLING) {
        if ((now - hal_cal_t0) < (uint32_t)SM_CAL_SETTLE_MS) {
            return;     /* ainda aquecendo: descarta a amostra de propósito */
        }
        hal_cal_phase = HAL_CAL_SAMPLING;
        hal_cal_n = 0;
        for (int k = 0; k < 3; k++) { hal_cal_mean[k] = 0.0f; hal_cal_m2[k] = 0.0f; }
        LOG_INF("Hall estabilizado (%u ms) — acumulando %u frames p/ offset",
                (unsigned)SM_CAL_SETTLE_MS, (unsigned)SM_CAL_FRAMES);
    }

    /* Welford: mean += (x-mean)/n ; M2 += (x-mean_antigo)*(x-mean_novo) */
    hal_cal_n++;
    for (int k = 0; k < 3; k++) {
        float d = raw_i[k] - hal_cal_mean[k];
        hal_cal_mean[k] += d / (float)hal_cal_n;
        hal_cal_m2[k]   += d * (raw_i[k] - hal_cal_mean[k]);
    }

    if (hal_cal_n >= (uint32_t)SM_CAL_FRAMES) {
        hal_calibration_finish();
    }
}

/* ======================================================================== *
 * Profiler de ciclo real do Observer_Step (HAL_PROFILE_OBSERVER)           *
 * ======================================================================== */
#if HAL_PROFILE_OBSERVER

static uint32_t prof_n, prof_min, prof_max, prof_overhead;
static uint64_t prof_sum;

/** @brief Mede o custo de duas leituras consecutivas do contador, para
 * descontar a própria instrumentação. Chamar antes do laço. */
static void hal_profile_init(void)
{
    uint32_t best = UINT32_MAX;
    for (int i = 0; i < 32; i++) {
        uint32_t a = k_cycle_get_32();
        uint32_t b = k_cycle_get_32();
        uint32_t d = b - a;
        if (d < best) best = d;      /* mínimo: amostra menos perturbada */
    }
    prof_overhead = best;
    prof_min = UINT32_MAX;
    LOG_INF("Profiler ON: %u Hz de contador, overhead=%u ciclos",
            (unsigned)sys_clock_hw_cycles_per_sec(), (unsigned)prof_overhead);
}

/**
 * @brief Acumula uma amostra e reporta periodicamente.
 * @param dt ciclos brutos medidos (instrumentação ainda incluída)
 */
static void hal_profile_add(uint32_t dt)
{
    dt = (dt > prof_overhead) ? (dt - prof_overhead) : 0U;

    if (dt < prof_min) prof_min = dt;
    if (dt > prof_max) prof_max = dt;
    prof_sum += dt;

    if (++prof_n >= HAL_PROFILE_REPORT_EVERY) {
        uint32_t avg = (uint32_t)(prof_sum / prof_n);
        uint32_t hz  = sys_clock_hw_cycles_per_sec();
        /* ns = ciclos * 1e9 / f — em inteiro, sem estourar 32 bits. */
        LOG_INF("Observer_Step: min=%u med=%u max=%u ciclos "
                "(med=%u ns @ %u MHz) -> teto de amostragem = %u kSps",
                (unsigned)prof_min, (unsigned)avg, (unsigned)prof_max,
                (unsigned)((uint64_t)avg * 1000000000ULL / hz),
                (unsigned)(hz / 1000000U),
                (unsigned)(prof_max ? (hz / prof_max / 1000U) : 0U));
        prof_n = 0; prof_sum = 0; prof_min = UINT32_MAX; prof_max = 0;
    }
}
#endif /* HAL_PROFILE_OBSERVER */

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
#if HAL_PROFILE_OBSERVER
    hal_profile_init();
#endif
    hal_cal_t0 = k_uptime_get_32();
#if HAL_CALIBRATE_ON_BOOT
    Security_Manager_Error_Set(ERROR_CAL_IN_PROGRESS, true);
    LOG_INF("Calibracao de offset: aguardando %u ms de estabilizacao do Hall "
            "(MANTENHA O ESC PARADO)", (unsigned)SM_CAL_SETTLE_MS);
#endif
    uint32_t last_pub = k_uptime_get_32();

    float Va = 0, Vb = 0, Vc = 0, ia = 0, ib = 0, ic = 0;
    float V_alpha = 0, V_beta = 0, i_alpha = 0, i_beta = 0;

    while (1) {
        /* Reinicia o observador se um parâmetro/ganho mudou por CMD. */
        Observer_Apply_Pending();

        /* HAL_ACQ_BURST: 1 conversão = 1 passo no hardware; no native_sim
         * comprime várias iterações pra PLL convergir em tempo de parede. */
        bool acq_ok = true;
        for (int b = 0; b < HAL_ACQ_BURST; b++) {
            float raw_i[3];
            if (hal_acquire(&Va, &Vb, &Vc, &ia, &ib, &ic, raw_i) != 0) {
                acq_ok = false;
                k_sleep(K_MSEC(1));
                break;
            }

            /* Enquanto o offset do Hall não estiver levantado, o observador
             * NÃO roda: a corrente fantasma entraria no Clarke e viraria BEMF
             * espúria, travando a PLL num ângulo errado — e isso nenhum ajuste
             * de Γ conserta, porque o erro está na entrada. */
            if (hal_cal_phase != HAL_CAL_READY) {
                if (hal_cal_phase != HAL_CAL_FAILED) {
                    hal_calibration_step(raw_i);
                }
                continue;
            }
            /* Clarke amplitude-invariante, forma completa de 3 termos: usa as
             * três fases medidas (não assume i_a+i_b+i_c=0). O termo −½b−½c
             * subtrai a componente de modo comum/sequência-zero, rejeitando
             * offset/desbalanço que a forma reduzida (α=a) deixaria vazar. */
            i_alpha = TWO_THIRDS * (ia - 0.5f * ib - 0.5f * ic);
            i_beta  = INV_SQRT3  * (ib - ic);
            V_alpha = TWO_THIRDS * (Va - 0.5f * Vb - 0.5f * Vc);
            V_beta  = INV_SQRT3  * (Vb - Vc);

#if HAL_PROFILE_OBSERVER
            uint32_t t_ini = k_cycle_get_32();
            Observer_Step(V_alpha, V_beta, i_alpha, i_beta);
            hal_profile_add(k_cycle_get_32() - t_ini);
#else
            Observer_Step(V_alpha, V_beta, i_alpha, i_beta);
#endif
        }
        /* Falha de leitura do ADC → bit de diagnóstico (limpa quando volta). */
        Security_Manager_Error_Set(ERROR_SYS_ADC_READ_FAILED, !acq_ok);

        /* Telemetria decimada no tempo, não por amostra. */
        uint32_t now = k_uptime_get_32();
        if ((now - last_pub) >= HAL_TELEM_PUBLISH_MS) {
            last_pub = now;
            /* Diagnóstico no ritmo da telemetria (não por amostra): sanidade
             * elétrica das fases + saúde do observador → error_code. */
            Security_Manager_Check_Electrical(Va, Vb, Vc, ia, ib, ic);
            Security_Manager_Check_Observer();
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
