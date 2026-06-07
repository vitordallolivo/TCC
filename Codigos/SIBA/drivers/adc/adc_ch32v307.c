/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Driver ADC para CH32V307 — Zephyr RTOS
 * Implementa a API drivers/adc.h via MMIO direto (sem HAL WCH).
 *
 * Modo de operação:
 *   - Scan contínuo de N canais configurados via adc_channel_cfg
 *   - DMA1 Canal 1 em modo circular → sem intervenção da CPU por amostra
 *   - adc_read() síncrono: dispara software trigger e aguarda semáforo
 *   - adc_read_async() assíncrono: usa k_poll_signal sinalizado na ISR DMA TC
 *
 * Registradores (base ADC1 = 0x40012400):
 *   STATR   +0x00   Status
 *   CTLR1   +0x04   Control 1
 *   CTLR2   +0x08   Control 2  (ADON, CONT, DMA, SWSTART, CAL, RSTCAL)
 *   SAMPTR1 +0x0C   Sample time ch10-17
 *   SAMPTR2 +0x10   Sample time ch0-9
 *   RSQR1   +0x2C   Regular sequence 1  (comprimento L)
 *   RSQR2   +0x30   Regular sequence 2
 *   RSQR3   +0x34   Regular sequence 3  (SQ1..SQ6)
 *   RDATAR  +0x4C   Regular data (lido pelo DMA)
 *
 * DMA1 Canal 1 (base DMA1 = 0x40020000, offset canal = 0x08 + (ch-1)*0x14):
 *   CFGR    +0x08   Config
 *   CNTR    +0x0C   Count
 *   PADDR   +0x10   Peripheral addr
 *   MADDR   +0x14   Memory addr
 *
 * Referências:
 *   openwch/ch32v307 — ch32v30x.h, ch32v30x_adc.c
 *   Seu ADC.c do Projeto_Final_AAM
 */

#define DT_DRV_COMPAT wch_adc

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <stdint.h>

LOG_MODULE_REGISTER(adc_ch32v307, CONFIG_ADC_LOG_LEVEL);

/* =========================================================
 * Mapa de registradores — MMIO direto
 * ========================================================= */

/* ADC1 */
#define ADC1_BASE       0x40012400UL
#define ADC_STATR       (*(volatile uint32_t *)(ADC1_BASE + 0x00))
#define ADC_CTLR1       (*(volatile uint32_t *)(ADC1_BASE + 0x04))
#define ADC_CTLR2       (*(volatile uint32_t *)(ADC1_BASE + 0x08))
#define ADC_SAMPTR1     (*(volatile uint32_t *)(ADC1_BASE + 0x0C))
#define ADC_SAMPTR2     (*(volatile uint32_t *)(ADC1_BASE + 0x10))
#define ADC_RSQR1       (*(volatile uint32_t *)(ADC1_BASE + 0x2C))
#define ADC_RSQR2       (*(volatile uint32_t *)(ADC1_BASE + 0x30))
#define ADC_RSQR3       (*(volatile uint32_t *)(ADC1_BASE + 0x34))
#define ADC_RDATAR_ADDR (ADC1_BASE + 0x4C)

/* CTLR2 bits */
#define ADC_CTLR2_ADON      BIT(0)
#define ADC_CTLR2_CONT      BIT(1)
#define ADC_CTLR2_DMA       BIT(8)
#define ADC_CTLR2_ALIGN     BIT(11)   /* 0=right, 1=left */
#define ADC_CTLR2_SWSTART   BIT(22)
#define ADC_CTLR2_EXTTRIG   BIT(20)
#define ADC_CTLR2_RSTCAL    BIT(3)
#define ADC_CTLR2_CAL       BIT(2)

/* STATR bits */
#define ADC_STATR_EOC       BIT(1)

/* CTLR1 bits */
#define ADC_CTLR1_SCAN      BIT(8)

/* DMA1 Canal 1 */
#define DMA1_BASE       0x40020000UL
#define DMA1_ISR        (*(volatile uint32_t *)(DMA1_BASE + 0x00))
#define DMA1_IFCR       (*(volatile uint32_t *)(DMA1_BASE + 0x04))
/* Canal 1: offset 0x08 */
#define DMA1_CH1_CFGR   (*(volatile uint32_t *)(DMA1_BASE + 0x08))
#define DMA1_CH1_CNTR   (*(volatile uint32_t *)(DMA1_BASE + 0x0C))
#define DMA1_CH1_PADDR  (*(volatile uint32_t *)(DMA1_BASE + 0x10))
#define DMA1_CH1_MADDR  (*(volatile uint32_t *)(DMA1_BASE + 0x14))

/* CFGR bits */
#define DMA_CFGR_EN         BIT(0)
#define DMA_CFGR_TCIE       BIT(1)   /* Transfer Complete IRQ enable */
#define DMA_CFGR_CIRC       BIT(5)   /* Circular mode */
#define DMA_CFGR_MINC       BIT(7)   /* Memory increment */
#define DMA_CFGR_PSIZE_16   BIT(8)   /* Peripheral size 16 bit */
#define DMA_CFGR_MSIZE_16   BIT(10)  /* Memory size 16 bit */
#define DMA_CFGR_PL_HIGH    BIT(12)  /* Priority level high */

/* ISR/IFCR Canal 1: bits 0..3 */
#define DMA1_ISR_TCIF1      BIT(1)
#define DMA1_IFCR_CTCIF1    BIT(1)

/* RCC */
#define RCC_BASE        0x40021000UL
#define RCC_AHBPCENR    (*(volatile uint32_t *)(RCC_BASE + 0x14))
#define RCC_APB2PCENR   (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_CFGR0       (*(volatile uint32_t *)(RCC_BASE + 0x04))

#define RCC_AHB_DMA1    BIT(0)
#define RCC_APB2_ADC1   BIT(9)
/* ADC clock prescaler: bits 14:15 de CFGR0 → /6 = 0b10 */
#define RCC_CFGR0_ADCPRE_DIV6  (0x2UL << 14)
#define RCC_CFGR0_ADCPRE_MASK  (0x3UL << 14)

/* =========================================================
 * Limite de canais suportados simultaneamente
 * ========================================================= */
#define CH32_ADC_MAX_CHANNELS  16

/* =========================================================
 * Estado do driver
 * ========================================================= */
struct adc_ch32v307_data {
    /* Buffer DMA — alinhado em 4 bytes, escrito pelo DMA */
    __attribute__((aligned(4)))
    uint16_t dma_buf[CH32_ADC_MAX_CHANNELS];

    /* Canais configurados (bitmask) */
    uint32_t ch_mask;
    uint8_t  ch_count;
    uint8_t  ch_ids[CH32_ADC_MAX_CHANNELS];  /* ordem na sequência */

    /* Resultado: ponteiro pro buffer do usuário (adc_sequence.buffer) */
    uint16_t *user_buf;
    uint8_t   user_ch_count;

    /* Sincronização */
    struct k_sem            sem_sync;   /* para adc_read() síncrono */
    struct k_poll_signal   *async_sig;  /* para adc_read_async()     */

    bool initialized;
};

/* =========================================================
 * Config (vinda do DTS via DEVICE_DT_INST_DEFINE)
 * ========================================================= */
struct adc_ch32v307_cfg {
    uint32_t base;          /* 0x40012400 — mantido para extensão futura */
};

/* =========================================================
 * ISR do DMA1 Canal 1 — Transfer Complete
 * ========================================================= */
static struct adc_ch32v307_data *g_data;  /* ptr global para ISR */

static void dma1_ch1_isr(const struct device *dev)
{
    ARG_UNUSED(dev);

    if (!(DMA1_ISR & DMA1_ISR_TCIF1)) {
        return;
    }
    /* Limpa flag TC */
    DMA1_IFCR = DMA1_IFCR_CTCIF1;

    if (!g_data) {
        return;
    }

    /* Copia DMA → buffer do usuário na ordem dos canais configurados */
    if (g_data->user_buf) {
        for (int i = 0; i < g_data->ch_count; i++) {
            g_data->user_buf[i] = g_data->dma_buf[i];
        }
    }

    /* Sinaliza quem estiver aguardando */
    if (g_data->async_sig) {
        k_poll_signal_raise(g_data->async_sig, 0);
        g_data->async_sig = NULL;
    } else {
        k_sem_give(&g_data->sem_sync);
    }
}

/* =========================================================
 * Funções internas de HW
 * ========================================================= */

static void adc_rcc_enable(void)
{
    /* Habilita clock DMA1 e ADC1 */
    RCC_AHBPCENR  |= RCC_AHB_DMA1;
    RCC_APB2PCENR |= RCC_APB2_ADC1;

    /* ADC clock = PCLK2 / 6  (144 MHz / 6 = 24 MHz, dentro do limite 14 MHz
     * do datasheet — ajuste para /8 se usar HSE < 112 MHz) */
    RCC_CFGR0 = (RCC_CFGR0 & ~RCC_CFGR0_ADCPRE_MASK) | RCC_CFGR0_ADCPRE_DIV6;
}

static void dma_init(uint16_t *buf, uint16_t count)
{
    /* Desabilita canal antes de reconfigurar */
    DMA1_CH1_CFGR &= ~DMA_CFGR_EN;

    DMA1_CH1_PADDR = ADC_RDATAR_ADDR;
    DMA1_CH1_MADDR = (uint32_t)buf;
    DMA1_CH1_CNTR  = count;

    DMA1_CH1_CFGR = DMA_CFGR_TCIE      |   /* IRQ TC */
                    DMA_CFGR_CIRC       |   /* circular */
                    DMA_CFGR_MINC       |   /* incrementa memória */
                    DMA_CFGR_PSIZE_16   |   /* periférico 16 bits */
                    DMA_CFGR_MSIZE_16   |   /* memória 16 bits */
                    DMA_CFGR_PL_HIGH;       /* prioridade alta */

    /* Habilita canal */
    DMA1_CH1_CFGR |= DMA_CFGR_EN;
}

static void adc_calibrate(void)
{
    /* Reset calibração */
    ADC_CTLR2 |= ADC_CTLR2_RSTCAL;
    while (ADC_CTLR2 & ADC_CTLR2_RSTCAL) {
        /* aguarda */
    }
    /* Inicia calibração */
    ADC_CTLR2 |= ADC_CTLR2_CAL;
    while (ADC_CTLR2 & ADC_CTLR2_CAL) {
        /* aguarda */
    }
}

/*
 * Configura RSQR3/2/1 com a sequência de canais.
 * Cada SQx é 5 bits; RSQR3 contém SQ1..SQ6 (canais 1 a 6 da sequência),
 * RSQR2 contém SQ7..SQ12, RSQR1 contém SQ13..SQ16 + L (comprimento-1).
 */
static void adc_set_sequence(const uint8_t *ids, uint8_t count)
{
    uint32_t rsqr3 = 0, rsqr2 = 0, rsqr1 = 0;

    for (int i = 0; i < count && i < 6; i++) {
        rsqr3 |= ((uint32_t)ids[i] & 0x1F) << (i * 5);
    }
    for (int i = 6; i < count && i < 12; i++) {
        rsqr2 |= ((uint32_t)ids[i] & 0x1F) << ((i - 6) * 5);
    }
    for (int i = 12; i < count && i < 16; i++) {
        rsqr1 |= ((uint32_t)ids[i] & 0x1F) << ((i - 12) * 5);
    }
    /* L = count - 1, bits 23:20 de RSQR1 */
    rsqr1 |= ((uint32_t)(count - 1) & 0xF) << 20;

    ADC_RSQR3 = rsqr3;
    ADC_RSQR2 = rsqr2;
    ADC_RSQR1 = rsqr1;
}

/*
 * Sample time: 239.5 ciclos para todos os canais (máxima precisão).
 * Bits por canal: 3 bits; SAMPTR2 cobre ch0-ch9, SAMPTR1 ch10-ch17.
 * Valor 0b111 = 239.5 ciclos.
 */
static void adc_set_sample_times(const uint8_t *ids, uint8_t count)
{
    uint32_t smp2 = ADC_SAMPTR2;
    uint32_t smp1 = ADC_SAMPTR1;

    for (int i = 0; i < count; i++) {
        uint8_t ch = ids[i];
        if (ch <= 9) {
            smp2 |= (0x7U << (ch * 3));
        } else if (ch <= 17) {
            smp1 |= (0x7U << ((ch - 10) * 3));
        }
    }
    ADC_SAMPTR2 = smp2;
    ADC_SAMPTR1 = smp1;
}

/* =========================================================
 * API Zephyr: channel_setup
 * ========================================================= */
static int adc_ch32v307_channel_setup(const struct device *dev,
                                      const struct adc_channel_cfg *cfg)
{
    struct adc_ch32v307_data *data = dev->data;

    if (cfg->channel_id >= CH32_ADC_MAX_CHANNELS) {
        LOG_ERR("Canal %d inválido (max %d)", cfg->channel_id,
                CH32_ADC_MAX_CHANNELS - 1);
        return -EINVAL;
    }

    /* Apenas ADC_GAIN_1 e ADC_REF_INTERNAL suportados pelo HW */
    if (cfg->gain != ADC_GAIN_1) {
        LOG_ERR("Gain não suportado (só ADC_GAIN_1)");
        return -ENOTSUP;
    }
    if (cfg->reference != ADC_REF_INTERNAL) {
        LOG_ERR("Referência não suportada (só ADC_REF_INTERNAL)");
        return -ENOTSUP;
    }

    /* Adiciona canal à máscara se ainda não estiver */
    if (!(data->ch_mask & BIT(cfg->channel_id))) {
        if (data->ch_count >= CH32_ADC_MAX_CHANNELS) {
            LOG_ERR("Limite de canais atingido");
            return -ENOMEM;
        }
        data->ch_ids[data->ch_count++] = cfg->channel_id;
        data->ch_mask |= BIT(cfg->channel_id);

        LOG_DBG("Canal %d adicionado (total: %d)", cfg->channel_id,
                data->ch_count);
    }

    return 0;
}

/* =========================================================
 * Reconfiguração do ADC quando os canais mudam
 * ========================================================= */
static void adc_hw_reconfigure(struct adc_ch32v307_data *data)
{
    /* Desliga ADC e DMA antes de reconfigurar */
    DMA1_CH1_CFGR &= ~DMA_CFGR_EN;
    ADC_CTLR2 &= ~ADC_CTLR2_ADON;

    adc_set_sequence(data->ch_ids, data->ch_count);
    adc_set_sample_times(data->ch_ids, data->ch_count);

    dma_init(data->dma_buf, data->ch_count);

    /* Liga ADC + scan + DMA + continuous */
    ADC_CTLR1 |= ADC_CTLR1_SCAN;
    ADC_CTLR2  = ADC_CTLR2_ADON | ADC_CTLR2_CONT | ADC_CTLR2_DMA |
                 ADC_CTLR2_EXTTRIG | (0x7UL << 17); /* EXTSEL=111: SWSTART */

    /* Necessário: ligar novamente após setar bits */
    ADC_CTLR2 |= ADC_CTLR2_ADON;

    adc_calibrate();

    /* Dispara primeira conversão */
    ADC_CTLR2 |= ADC_CTLR2_SWSTART;
}

/* =========================================================
 * API Zephyr: read (síncrono)
 * ========================================================= */
static int adc_ch32v307_read(const struct device *dev,
                             const struct adc_sequence *seq)
{
    struct adc_ch32v307_data *data = dev->data;

    if (!data->initialized) {
        LOG_ERR("ADC não inicializado");
        return -ENODEV;
    }
    if (!seq->buffer) {
        return -EINVAL;
    }

    /* Valida que todos os canais pedidos foram configurados */
    if (seq->channels & ~data->ch_mask) {
        LOG_ERR("Canal não configurado (req=0x%08X, cfg=0x%08X)",
                seq->channels, data->ch_mask);
        return -EINVAL;
    }

    data->user_buf  = (uint16_t *)seq->buffer;
    data->async_sig = NULL;

    /* O DMA circular já está rodando; semáforo é dado na próxima TC */
    k_sem_reset(&data->sem_sync);
    int ret = k_sem_take(&data->sem_sync, K_MSEC(100));
    if (ret) {
        LOG_ERR("Timeout aguardando conversão ADC");
        return -ETIMEDOUT;
    }

    return 0;
}

/* =========================================================
 * API Zephyr: read_async
 * ========================================================= */
#ifdef CONFIG_ADC_ASYNC
static int adc_ch32v307_read_async(const struct device *dev,
                                   const struct adc_sequence *seq,
                                   struct k_poll_signal *async)
{
    struct adc_ch32v307_data *data = dev->data;

    if (!data->initialized) {
        return -ENODEV;
    }
    if (!seq->buffer || !async) {
        return -EINVAL;
    }
    if (seq->channels & ~data->ch_mask) {
        return -EINVAL;
    }

    data->user_buf  = (uint16_t *)seq->buffer;
    data->async_sig = async;

    /* DMA circular → ISR sinalizará async quando TC ocorrer */
    return 0;
}
#endif /* CONFIG_ADC_ASYNC */

/* =========================================================
 * API Zephyr: init
 * ========================================================= */
static int adc_ch32v307_init(const struct device *dev)
{
    struct adc_ch32v307_data *data = dev->data;

    g_data = data;

    k_sem_init(&data->sem_sync, 0, 1);
    data->ch_mask   = 0;
    data->ch_count  = 0;
    data->async_sig = NULL;
    data->user_buf  = NULL;

    adc_rcc_enable();

    /* Liga ADC pela primeira vez (necessário antes da calibração) */
    ADC_CTLR2 |= ADC_CTLR2_ADON;

    /* Pequeno delay de estabilização (~1µs @ 144 MHz) */
    for (volatile int i = 0; i < 200; i++) {
        __asm__ volatile("nop");
    }

    /*
     * Registra ISR do DMA1 Canal 1.
     * IRQn do DMA1_CH1 no CH32V307 = 27 (conforme ch32v30x.h openwch)
     */
    IRQ_CONNECT(27, 1, dma1_ch1_isr, NULL, 0);
    irq_enable(27);

    data->initialized = true;
    LOG_INF("ADC CH32V307 inicializado");
    return 0;
}

/* =========================================================
 * Tabela de API do driver
 * ========================================================= */
static const struct adc_driver_api adc_ch32v307_api = {
    .channel_setup = adc_ch32v307_channel_setup,
    .read          = adc_ch32v307_read,
#ifdef CONFIG_ADC_ASYNC
    .read_async    = adc_ch32v307_read_async,
#endif
    .ref_internal  = 3300,  /* mV — Vref interno CH32V307 */
};

/* =========================================================
 * Instanciação via DTS (apenas ADC1 por enquanto)
 * ========================================================= */
#define ADC_CH32V307_INIT(n)                                            \
    static struct adc_ch32v307_data adc_ch32v307_data_##n;             \
    static const struct adc_ch32v307_cfg adc_ch32v307_cfg_##n = {      \
        .base = DT_INST_REG_ADDR(n),                                    \
    };                                                                  \
    DEVICE_DT_INST_DEFINE(n,                                            \
        adc_ch32v307_init,                                              \
        NULL,                                                           \
        &adc_ch32v307_data_##n,                                         \
        &adc_ch32v307_cfg_##n,                                          \
        POST_KERNEL,                                                    \
        CONFIG_ADC_INIT_PRIORITY,                                       \
        &adc_ch32v307_api);

DT_INST_FOREACH_STATUS_OKAY(ADC_CH32V307_INIT)