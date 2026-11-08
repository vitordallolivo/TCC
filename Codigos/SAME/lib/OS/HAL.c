/**
 * @file HAL.c
 * @brief HAL da SAME: só aquisição elétrica, não aciona motor.
 *
 * ADC1 = i_a,i_b,i_c | ADC2 = V_a,V_b,V_c. Thread única bloqueia no ADC,
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
#include <string.h>
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
/* A SAME não usa `zero`/`law` (as 6 entradas são afins com zero=0): o offset
 * do Hall é levantado no boot e gravado em `offset`. Os dois campos entram na
 * assinatura só porque a tabela é compartilhada com a SIBA, onde o Pitot usa
 * a lei de raiz. */
#define X(TID, DEF_LIN, DEF_OFF, MINV, MAXV, DEF_ZERO, LAW)   \
    adc_calib[(TID)].linear = (DEF_LIN);                      \
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
/* Flag atômico: quando != 0 o ISR de ADC retorna sem processar.
 * Permite suspender o pipeline ADC→Clarke→Observer durante atualizações
 * de calibração vindas do NCI, sem tocar no DMA. */
static atomic_t hal_adc_paused = ATOMIC_INIT(0);

void HAL_ADC_Pause(void)  { atomic_set(&hal_adc_paused, 1); }
void HAL_ADC_Resume(void) { atomic_set(&hal_adc_paused, 0); }

void HAL_ADC_Set_Calib(uint8_t sensor_id, float linear, float offset)
{
    if (sensor_id >= CMD_CORE_SENSOR_COUNT) return;
    HAL_ADC_Pause();
    adc_calib[sensor_id].linear = linear;
    adc_calib[sensor_id].offset = offset;
    HAL_ADC_Resume();
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
static float hal_cal_mean[3];
static float hal_cal_m2[3];

/* Funções definidas na seção comum abaixo (hal_calibration_step,
 * hal_publish_telemetry) — protótipos visíveis a HW e POSIX. */
static void hal_calibration_step(const float raw_i[3]);
static void hal_publish_telemetry(float Va, float Vb, float Vc,
                                  float ia, float ib, float ic,
                                  float Val, float Vbe, float ial, float ibe);

#ifndef CONFIG_ARCH_POSIX
/* ---- Hardware real: 2 ADCs do CH32V307 — caminho direto por ISR ---- *
 *
 * adc_ch32v307_set_frame_callback() registra hal_adc_isr_frame(), que roda
 * diretamente na ISR de DMA (sem semáforo, sem context-switch). A sequência
 * ADC → Clarke → Observer_Step ocorre inteira no nível de interrupção,
 * maximizando o tempo disponível para computação entre frames (≈6,58 µs).
 *
 * Observer_Apply_Pending() usa k_mutex (não é ISR-safe): delegado ao
 * hal_obs_work que roda no work-queue do sistema. */

#include "../../drivers/adc_ch32v307.h"

#define ADC1_NODE DT_NODELABEL(adc1)
#define ADC2_NODE DT_NODELABEL(adc2)
static const struct device *adc1_dev = DEVICE_DT_GET(ADC1_NODE);
static const struct device *adc2_dev = DEVICE_DT_GET(ADC2_NODE);

/* A ordem de setup = ordem de varredura (RSQR). No modo dual o slot k do
 * ADC1 é convertido junto com o slot k do ADC2, então as duas tabelas são
 * ordenadas por fase (a, b, c) para manter i_x e V_x simultâneos. */
/* ADC1 (mestre): PA3=CH3(i_a), PA1=CH1(i_b), PA0=CH0(i_c). PA2 = W25Q. */
static const struct adc_channel_cfg adc1_ch_cfg[HAL_ADC1_NUM_CH] = {
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL, .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 3  },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL, .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 1  },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL, .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 0  },
};
/* ADC2 (escravo): PC2=CH12(V_a), PC1=CH11(V_b), PC0=CH10(V_c — conexão solta). */
static const struct adc_channel_cfg adc2_ch_cfg[HAL_ADC2_NUM_CH] = {
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL, .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 12 },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL, .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 11 },
    { .gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL, .acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 10 },
};

static inline float raw_to_mv(uint16_t raw)
{
    return (float)raw * (float)HAL_ADC_VREF_MV / (float)HAL_ADC_FULL_SCALE;
}

/* Replica local do adc_apply_cal do driver (que é static) — aplica o offset
 * de calibração de HW ao código bruto de 12 bits, saturando em [0,4095]. */
static inline uint16_t hal_adc_raw_cal(uint32_t raw12, int32_t off)
{
    if (raw12 == 0U || raw12 == 4095U) return (uint16_t)raw12;
    int32_t v = (int32_t)raw12 + off;
    if (v < 0)    return 0U;
    if (v > 4095) return 4095U;
    return (uint16_t)v;
}

/* Snapshot do último frame processado — escrito pelo batch_work, lido na
 * publicação de telemetria (mesmo contexto: same_wq). */
static struct {
    float Va, Vb, Vc, ia, ib, ic;
} hal_snap;

/* Frames entre disparos de telemetria (~100 Hz com frame de 6,58 µs e
 * batch de 256 → ~1,7 ms por batch → 1520 / 256 ≈ 6 batches). */
#define HAL_TELEM_FRAMES  1520U

static uint32_t hal_frame_cnt;

/* Número de frames por batch — deve bater com CH32_ADC_BATCH_FRAMES no driver.
 * 256 frames × 6,58 µs = ~1,7 ms por batch → ISR a ~590 Hz durante boot ETH.
 * Canais por frame no ADC dual = HAL_ADC1_NUM_CH (3). */
#define HAL_BATCH_FRAMES  256U
#define HAL_BATCH_WORDS   (HAL_BATCH_FRAMES * HAL_ADC1_NUM_CH)

/* Double buffer: ISR escreve sempre no buffer "inativo" (1 - widx) e
 * publica o novo índice; o work_fn lê sempre do índice publicado.
 * Com um único core não há aliasing: ISR sempre escreve no oposto do que
 * o work lê, mesmo que os dois se sobreponham no tempo. */
static uint32_t    hal_batch_bufs[2][HAL_BATCH_WORDS];
static atomic_t    hal_batch_widx = ATOMIC_INIT(0);
static int32_t     hal_batch_cal_master;
static int32_t     hal_batch_cal_slave;

static struct k_work hal_batch_work;

/**
 * @brief Work que processa um batch de HAL_BATCH_FRAMES frames ADC.
 *
 * Roda na same_wq, não na ISR. Aplica calibração afim, executa o observador
 * para cada frame do batch, depois Observer_Apply_Pending (que usa k_mutex)
 * e publica telemetria a ~100 Hz.
 */
static void hal_batch_work_fn(struct k_work *w)
{
    ARG_UNUSED(w);

    int ridx = atomic_get(&hal_batch_widx);
    const uint32_t *buf = hal_batch_bufs[ridx];
    int32_t cal_m = hal_batch_cal_master;
    int32_t cal_s = hal_batch_cal_slave;

    for (uint32_t f = 0; f < HAL_BATCH_FRAMES; f++) {
        const uint32_t *fr = &buf[f * HAL_ADC1_NUM_CH];

        /* [15:0] = ADC1 (correntes), [31:16] = ADC2 (tensões). */
        float ria  = raw_to_mv(hal_adc_raw_cal(fr[0] & 0xFFFU,         cal_m));
        float rib  = raw_to_mv(hal_adc_raw_cal(fr[1] & 0xFFFU,         cal_m));
        float ric  = raw_to_mv(hal_adc_raw_cal(fr[2] & 0xFFFU,         cal_m));
        float rVa = raw_to_mv(hal_adc_raw_cal((fr[0] >> 16) & 0xFFFU, cal_s));
        float rVb = raw_to_mv(hal_adc_raw_cal((fr[1] >> 16) & 0xFFFU, cal_s));
        float rVc = raw_to_mv(hal_adc_raw_cal((fr[2] >> 16) & 0xFFFU, cal_s));

        if (hal_cal_phase != HAL_CAL_READY) {
            if (hal_cal_phase != HAL_CAL_FAILED) {
                float raw_i[3] = {ria, rib, ric};
                hal_calibration_step(raw_i);
            }
            continue;
        }

        float Va = apply_calib(TARGET_VOLTAGE_A, rVa);
        float Vb = apply_calib(TARGET_VOLTAGE_B, rVb);
        float Vc = apply_calib(TARGET_VOLTAGE_C, rVc);
        float ia = apply_calib(TARGET_CURRENT_A, ria);
        float ib = apply_calib(TARGET_CURRENT_B, rib);
        float ic = apply_calib(TARGET_CURRENT_C, ric);

        /* Clarke + SMO ficam dentro do Observer_Manager. */
        Observer_Step_ABC(Va, Vb, Vc, ia, ib, ic);

        hal_snap.Va = Va;  hal_snap.Vb = Vb;  hal_snap.Vc = Vc;
        hal_snap.ia = ia;  hal_snap.ib = ib;  hal_snap.ic = ic;
    }

    /* Observer_Apply_Pending uma vez por batch — usa k_mutex, seguro aqui. */
    if (hal_cal_phase == HAL_CAL_READY) {
        Observer_Apply_Pending();
    }

    /* Telemetria a ~100 Hz. */
    hal_frame_cnt += HAL_BATCH_FRAMES;
    if (hal_frame_cnt >= HAL_TELEM_FRAMES) {
        hal_frame_cnt = 0;
        float Va = hal_snap.Va, Vb = hal_snap.Vb, Vc = hal_snap.Vc;
        float ia = hal_snap.ia, ib = hal_snap.ib, ic = hal_snap.ic;
        Security_Manager_Check_Electrical(Va, Vb, Vc, ia, ib, ic);
        Security_Manager_Check_Observer();
        hal_publish_telemetry(Va, Vb, Vc, ia, ib, ic,
                              Observer_Get_V_Alpha(), Observer_Get_V_Beta(),
                              Observer_Get_I_Alpha(), Observer_Get_I_Beta());
    }

    HAL_ADC_Resume();
}

/**
 * @brief Callback de frame ADC — chamado diretamente pela ISR de DMA.
 *
 * nch = HAL_ADC1_NUM_CH × HAL_BATCH_FRAMES (ex: 3 × 256 = 768 words).
 * ISR copia o batch para o double buffer e submete o work na same_wq.
 * O processamento pesado (Clarke × N + Observer_Step × N) fica fora da ISR.
 */
static void hal_adc_isr_frame(const uint32_t *frame, uint16_t nch,
                               int32_t cal_master, int32_t cal_slave,
                               void *unused)
{
    ARG_UNUSED(unused);

    if (atomic_get(&hal_adc_paused)) return;

    /* Escreve sempre no buffer oposto ao que o work está lendo. */
    int widx = 1 - atomic_get(&hal_batch_widx);
    memcpy(hal_batch_bufs[widx], frame, (size_t)nch * sizeof(uint32_t));
    hal_batch_cal_master = cal_master;
    hal_batch_cal_slave  = cal_slave;
    atomic_set(&hal_batch_widx, widx);

    HAL_ADC_Pause();
    
    /* Se o work já está enfileirado, este submit é no-op — o próximo batch
     * será lido pelo índice publicado acima, então não perdemos dados críticos. */
    k_work_submit_to_queue(&same_wq, &hal_batch_work);
}

/**
 * @brief Inicializa ADC1/ADC2 e registra o callback de frame direto na ISR.
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

    k_work_init(&hal_batch_work, hal_batch_work_fn);
    adc_ch32v307_set_frame_callback(adc1_dev, hal_adc_isr_frame, NULL);

    LOG_INF("HAL: ADC ISR copia batch de %u frames → same_wq, "
            "telemetria a cada %u frames (~%u ms)",
            (unsigned)HAL_BATCH_FRAMES, (unsigned)HAL_TELEM_FRAMES,
            (unsigned)(HAL_TELEM_FRAMES * 7U / 1000U));
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

/* hal_acquire: wrapeia hal_read_raw + calibração afim — só usado no
 * native_sim (no hardware o caminho é hal_adc_isr_frame via ISR). */
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
#endif /* CONFIG_ARCH_POSIX */

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

/** @brief Expõe os defaults de calibração para main() chamar antes do NVM. */
void HAL_Calib_Init_Defaults(void)
{
    hal_adc_calib_init();
}

/**
 * @brief Inicializa ADCs (ou stub, se HAL_ENABLE_ADC=0).
 * NÃO reseta adc_calib[] — main() deve chamar HAL_Calib_Init_Defaults()
 * antes de NVM_Init() para que os valores do EEPROM sobrevivam ao HAL_ADC_Init.
 * @return 0 em sucesso, ou erro de hal_adc_hw_init
 */
int HAL_ADC_Init(void)
{
    return hal_adc_hw_init();
}

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

/* A thread só existe no native_sim: no hardware o caminho crítico corre
 * inteiro em ISR (hal_adc_isr_frame) sem nunca bloquear num semáforo. */
#ifdef CONFIG_ARCH_POSIX
/**
 * @brief Thread de aquisição (native_sim): chama hal_acquire em loop,
 * roda Clarke + Observer_Step e publica telemetria decimada. Nunca retorna.
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
#endif /* CONFIG_ARCH_POSIX */

/**
 * @brief Inicializa os ADCs e (só no native_sim) sobe a thread de aquisição.
 *
 * No hardware real o caminho ADC → Clarke → Observer roda direto na ISR de
 * DMA via hal_adc_isr_frame — nenhuma thread é criada aqui.
 * @return 0 em sucesso, ou erro de HAL_ADC_Init
 */
int HAL_Init(void)
{
#if HAL_CALIBRATE_ON_BOOT && !defined(CONFIG_ARCH_POSIX)
    hal_cal_t0 = k_uptime_get_32();
    Security_Manager_Error_Set(ERROR_CAL_IN_PROGRESS, true);
    LOG_INF("Calibracao de offset: aguardando %u ms de estabilizacao do Hall "
            "(MANTENHA O ESC PARADO)", (unsigned)SM_CAL_SETTLE_MS);
#endif

    int r = HAL_ADC_Init();
    if (r) return r;

#ifdef CONFIG_ARCH_POSIX
    k_thread_create(&hal_adc_thread_data, hal_adc_stack,
                    K_THREAD_STACK_SIZEOF(hal_adc_stack),
                    HAL_thread_ADC_fn, NULL, NULL, NULL,
                    HAL_ADC_PRIO, 0, K_NO_WAIT);
    k_thread_name_set(&hal_adc_thread_data, "hal_adc");
#endif

    LOG_INF("HAL_Init concluído");
    return 0;
}
