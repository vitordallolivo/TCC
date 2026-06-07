/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Driver ADC para CH32V307 — Zephyr RTOS
 * Implementa a API drivers/adc.h via MMIO direto (sem HAL WCH).
 *
 * Suporta múltiplas instâncias — ADC1 (base 0x40012400) e ADC2
 * (base 0x40012800, = ADC1_BASE + 0x400) — desde que cada nó `wch,adc`
 * no DTS aponte para um canal DMA1 próprio via `dmas = <&dma1 N>`.
 * Cada instância:
 *   - aloca seu próprio canal de DMA1 (registradores + IRQ calculados a
 *     partir do número do canal informado no DTS — ver DMA1_CH_BASE/
 *     ADC_CH32V307_DMA_IRQN)
 *   - habilita seu próprio clock em RCC_APB2PCENR (ADC1 = bit 9,
 *     ADC2 = bit 10 — calculado a partir do offset do `base` em relação
 *     a ADC1_BASE, blocos de 0x400)
 *
 * Modo de operação (igual para qualquer instância):
 *   - Scan contínuo de N canais configurados via adc_channel_cfg
 *   - DMA em modo circular → sem intervenção da CPU por amostra
 *   - adc_read() síncrono: dispara software trigger e aguarda semáforo
 *   - adc_read_async() assíncrono: usa k_poll_signal sinalizado na ISR DMA TC
 *
 * Registradores (offsets relativos à base do periférico — ADC1 ou ADC2):
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
 * DMA1 (base 0x40020000) — canal N em offset 0x08 + (N-1)*0x14:
 *   CFGR    +0x00   Config
 *   CNTR    +0x04   Count
 *   PADDR   +0x08   Peripheral addr
 *   MADDR   +0x0C   Memory addr
 *   IRQn = 26 + N   (DMA1_CH1=27 .. DMA1_CH7=33, ch32v30x.h openwch)
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
 * Mapa de registradores — MMIO direto, offsets relativos à base
 * (ADC1_BASE para a instância ADC1, ADC1_BASE + 0x400 para ADC2)
 * ========================================================= */

#define ADC1_BASE       0x40012400UL
#define ADC2_BASE       (ADC1_BASE + 0x400UL)

#define ADC_OFF_STATR    0x00UL
#define ADC_OFF_CTLR1    0x04UL
#define ADC_OFF_CTLR2    0x08UL
#define ADC_OFF_SAMPTR1  0x0CUL
#define ADC_OFF_SAMPTR2  0x10UL
#define ADC_OFF_RSQR1    0x2CUL
#define ADC_OFF_RSQR2    0x30UL
#define ADC_OFF_RSQR3    0x34UL
#define ADC_OFF_RDATAR   0x4CUL

#define ADC_REG(base, off)  (*(volatile uint32_t *)((base) + (off)))

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

/* =========================================================
 * DMA1 — canal alocado por instância via DTS (`dmas = <&dma1 N>`)
 * ========================================================= */
#define DMA1_BASE       0x40020000UL
#define DMA1_ISR        (*(volatile uint32_t *)(DMA1_BASE + 0x00))
#define DMA1_IFCR       (*(volatile uint32_t *)(DMA1_BASE + 0x04))

/* Base do bloco de registradores do canal N (1-7) */
#define DMA1_CH_BASE(n)   (DMA1_BASE + 0x08UL + ((uint32_t)(n) - 1U) * 0x14UL)

#define DMA_CH_OFF_CFGR   0x00UL
#define DMA_CH_OFF_CNTR   0x04UL
#define DMA_CH_OFF_PADDR  0x08UL
#define DMA_CH_OFF_MADDR  0x0CUL

#define DMA_CH_REG(chbase, off)  (*(volatile uint32_t *)((chbase) + (off)))

/* CFGR bits */
#define DMA_CFGR_EN         BIT(0)
#define DMA_CFGR_TCIE       BIT(1)   /* Transfer Complete IRQ enable */
#define DMA_CFGR_CIRC       BIT(5)   /* Circular mode */
#define DMA_CFGR_MINC       BIT(7)   /* Memory increment */
#define DMA_CFGR_PSIZE_16   BIT(8)   /* Peripheral size 16 bit */
#define DMA_CFGR_MSIZE_16   BIT(10)  /* Memory size 16 bit */
#define DMA_CFGR_PL_HIGH    BIT(12)  /* Priority level high */

/* ISR/IFCR: 4 bits por canal (GIFx, TCIFx, HTIFx, TEIFx), TCIF é o bit 1
 * de cada grupo → canal N usa o bit (4*(N-1) + 1) */
#define DMA1_TCIF(ch)       BIT(4U * ((uint32_t)(ch) - 1U) + 1U)

/* RCC */
#define RCC_BASE        0x40021000UL
#define RCC_AHBPCENR    (*(volatile uint32_t *)(RCC_BASE + 0x14))
#define RCC_APB2PCENR   (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_CFGR0       (*(volatile uint32_t *)(RCC_BASE + 0x04))

#define RCC_AHB_DMA1    BIT(0)
/* ADC1 = APB2ENR bit 9, ADC2 = bit 10 — blocos consecutivos de 0x400 a
 * partir de ADC1_BASE mapeiam para bits consecutivos a partir do bit 9 */
#define RCC_APB2_ADCx(adc_index)  BIT(9U + (adc_index))
/* ADC clock prescaler: bits 14:15 de CFGR0 → /6 = 0b10 */
#define RCC_CFGR0_ADCPRE_DIV6  (0x2UL << 14)
#define RCC_CFGR0_ADCPRE_MASK  (0x3UL << 14)

/* =========================================================
 * Limite de canais suportados simultaneamente
 * ========================================================= */
#define CH32_ADC_MAX_CHANNELS  16

/* =========================================================
 * Estado do driver (uma instância por ADC1/ADC2)
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
    uint32_t base;          /* Base do periférico — ADC1_BASE ou ADC2_BASE */
    uint32_t dma_ch_base;   /* Base dos registradores do canal DMA1 alocado */
    uint8_t  dma_channel;   /* Número do canal DMA1 (1-7) — bits ISR/IFCR   */
    void (*irq_config_func)(const struct device *dev);
};

/* =========================================================
 * ISR do canal DMA1 alocado para esta instância — Transfer Complete
 * ========================================================= */
static void adc_ch32v307_dma_isr(const struct device *dev)
{
    const struct adc_ch32v307_cfg *cfg = dev->config;
    struct adc_ch32v307_data *data = dev->data;
    uint32_t tcif = DMA1_TCIF(cfg->dma_channel);

    if (!(DMA1_ISR & tcif)) {
        return;
    }
    /* Limpa flag TC do canal */
    DMA1_IFCR = tcif;

    /* Copia DMA → buffer do usuário na ordem dos canais configurados */
    if (data->user_buf) {
        for (int i = 0; i < data->ch_count; i++) {
            data->user_buf[i] = data->dma_buf[i];
        }
    }

    /* Sinaliza quem estiver aguardando */
    if (data->async_sig) {
        k_poll_signal_raise(data->async_sig, 0);
        data->async_sig = NULL;
    } else {
        k_sem_give(&data->sem_sync);
    }
}

/* =========================================================
 * Funções internas de HW — todas recebem a base do periférico/canal,
 * permitindo reuso idêntico entre instâncias ADC1 e ADC2
 * ========================================================= */

static void adc_rcc_enable(const struct adc_ch32v307_cfg *cfg)
{
    uint32_t adc_index = (cfg->base - ADC1_BASE) / 0x400UL; /* 0=ADC1, 1=ADC2 */

    /* Habilita clock DMA1 e o clock do ADC desta instância */
    RCC_AHBPCENR  |= RCC_AHB_DMA1;
    RCC_APB2PCENR |= RCC_APB2_ADCx(adc_index);

    /* ADC clock = PCLK2 / 6  (144 MHz / 6 = 24 MHz, dentro do limite 14 MHz
     * do datasheet — ajuste para /8 se usar HSE < 112 MHz). Prescaler é
     * compartilhado entre ADC1/ADC2 — última instância a inicializar
     * apenas reafirma o mesmo valor. */
    RCC_CFGR0 = (RCC_CFGR0 & ~RCC_CFGR0_ADCPRE_MASK) | RCC_CFGR0_ADCPRE_DIV6;
}

static void dma_init(const struct adc_ch32v307_cfg *cfg, uint16_t *buf, uint16_t count)
{
    uint32_t chb = cfg->dma_ch_base;

    /* Desabilita canal antes de reconfigurar */
    DMA_CH_REG(chb, DMA_CH_OFF_CFGR) &= ~DMA_CFGR_EN;

    DMA_CH_REG(chb, DMA_CH_OFF_PADDR) = cfg->base + ADC_OFF_RDATAR;
    DMA_CH_REG(chb, DMA_CH_OFF_MADDR) = (uint32_t)buf;
    DMA_CH_REG(chb, DMA_CH_OFF_CNTR)  = count;

    DMA_CH_REG(chb, DMA_CH_OFF_CFGR) =
                    DMA_CFGR_TCIE      |   /* IRQ TC */
                    DMA_CFGR_CIRC       |   /* circular */
                    DMA_CFGR_MINC       |   /* incrementa memória */
                    DMA_CFGR_PSIZE_16   |   /* periférico 16 bits */
                    DMA_CFGR_MSIZE_16   |   /* memória 16 bits */
                    DMA_CFGR_PL_HIGH;       /* prioridade alta */

    /* Habilita canal */
    DMA_CH_REG(chb, DMA_CH_OFF_CFGR) |= DMA_CFGR_EN;
}

static void adc_calibrate(uint32_t base)
{
    /* Reset calibração */
    ADC_REG(base, ADC_OFF_CTLR2) |= ADC_CTLR2_RSTCAL;
    while (ADC_REG(base, ADC_OFF_CTLR2) & ADC_CTLR2_RSTCAL) {
        /* aguarda */
    }
    /* Inicia calibração */
    ADC_REG(base, ADC_OFF_CTLR2) |= ADC_CTLR2_CAL;
    while (ADC_REG(base, ADC_OFF_CTLR2) & ADC_CTLR2_CAL) {
        /* aguarda */
    }
}

/*
 * Configura RSQR3/2/1 com a sequência de canais.
 * Cada SQx é 5 bits; RSQR3 contém SQ1..SQ6 (canais 1 a 6 da sequência),
 * RSQR2 contém SQ7..SQ12, RSQR1 contém SQ13..SQ16 + L (comprimento-1).
 */
static void adc_set_sequence(uint32_t base, const uint8_t *ids, uint8_t count)
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

    ADC_REG(base, ADC_OFF_RSQR3) = rsqr3;
    ADC_REG(base, ADC_OFF_RSQR2) = rsqr2;
    ADC_REG(base, ADC_OFF_RSQR1) = rsqr1;
}

/*
 * Sample time: 239.5 ciclos para todos os canais (máxima precisão).
 * Bits por canal: 3 bits; SAMPTR2 cobre ch0-ch9, SAMPTR1 ch10-ch17.
 * Valor 0b111 = 239.5 ciclos.
 */
static void adc_set_sample_times(uint32_t base, const uint8_t *ids, uint8_t count)
{
    uint32_t smp2 = ADC_REG(base, ADC_OFF_SAMPTR2);
    uint32_t smp1 = ADC_REG(base, ADC_OFF_SAMPTR1);

    for (int i = 0; i < count; i++) {
        uint8_t ch = ids[i];
        if (ch <= 9) {
            smp2 |= (0x7U << (ch * 3));
        } else if (ch <= 17) {
            smp1 |= (0x7U << ((ch - 10) * 3));
        }
    }
    ADC_REG(base, ADC_OFF_SAMPTR2) = smp2;
    ADC_REG(base, ADC_OFF_SAMPTR1) = smp1;
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
static void adc_hw_reconfigure(const struct device *dev)
{
    const struct adc_ch32v307_cfg *cfg = dev->config;
    struct adc_ch32v307_data *data = dev->data;
    uint32_t base = cfg->base;

    /* Desliga ADC e DMA antes de reconfigurar */
    DMA_CH_REG(cfg->dma_ch_base, DMA_CH_OFF_CFGR) &= ~DMA_CFGR_EN;
    ADC_REG(base, ADC_OFF_CTLR2) &= ~ADC_CTLR2_ADON;

    adc_set_sequence(base, data->ch_ids, data->ch_count);
    adc_set_sample_times(base, data->ch_ids, data->ch_count);

    dma_init(cfg, data->dma_buf, data->ch_count);

    /* Liga ADC + scan + DMA + continuous */
    ADC_REG(base, ADC_OFF_CTLR1) |= ADC_CTLR1_SCAN;
    ADC_REG(base, ADC_OFF_CTLR2) = ADC_CTLR2_ADON | ADC_CTLR2_CONT | ADC_CTLR2_DMA |
                                   ADC_CTLR2_EXTTRIG | (0x7UL << 17); /* EXTSEL=111: SWSTART */

    /* Necessário: ligar novamente após setar bits */
    ADC_REG(base, ADC_OFF_CTLR2) |= ADC_CTLR2_ADON;

    adc_calibrate(base);

    /* Dispara primeira conversão */
    ADC_REG(base, ADC_OFF_CTLR2) |= ADC_CTLR2_SWSTART;
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
    const struct adc_ch32v307_cfg *cfg = dev->config;
    struct adc_ch32v307_data *data = dev->data;

    k_sem_init(&data->sem_sync, 0, 1);
    data->ch_mask   = 0;
    data->ch_count  = 0;
    data->async_sig = NULL;
    data->user_buf  = NULL;

    adc_rcc_enable(cfg);

    /* Liga ADC pela primeira vez (necessário antes da calibração) */
    ADC_REG(cfg->base, ADC_OFF_CTLR2) |= ADC_CTLR2_ADON;

    /* Pequeno delay de estabilização (~1µs @ 144 MHz) */
    for (volatile int i = 0; i < 200; i++) {
        __asm__ volatile("nop");
    }

    /* Conecta e habilita a IRQ do canal DMA1 alocado para esta instância
     * (gerada por instância via ADC_CH32V307_INIT — IRQ = 26 + canal) */
    cfg->irq_config_func(dev);

    data->initialized = true;
    LOG_INF("ADC CH32V307 inicializado (base=0x%08X, DMA1 canal %u)",
            cfg->base, cfg->dma_channel);
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
 * Instanciação via DTS — suporta múltiplos nós `wch,adc` (ADC1 e/ou ADC2),
 * cada um com seu próprio canal DMA1 (`dmas = <&dma1 N>` no DTS)
 * ========================================================= */
#define ADC_CH32V307_DMA_CHANNEL(n)  DT_INST_DMAS_CELL_BY_IDX(n, 0, channel)
/* IRQn do canal N de DMA1 = 26 + N (DMA1_CH1=27 .. DMA1_CH7=33) */
#define ADC_CH32V307_DMA_IRQN(n)     (26 + ADC_CH32V307_DMA_CHANNEL(n))

#define ADC_CH32V307_INIT(n)                                                \
    static void adc_ch32v307_irq_config_##n(const struct device *dev)      \
    {                                                                       \
        IRQ_CONNECT(ADC_CH32V307_DMA_IRQN(n), 1,                           \
                    adc_ch32v307_dma_isr, DEVICE_DT_INST_GET(n), 0);        \
        irq_enable(ADC_CH32V307_DMA_IRQN(n));                              \
    }                                                                       \
    static struct adc_ch32v307_data adc_ch32v307_data_##n;                 \
    static const struct adc_ch32v307_cfg adc_ch32v307_cfg_##n = {          \
        .base            = DT_INST_REG_ADDR(n),                            \
        .dma_channel     = ADC_CH32V307_DMA_CHANNEL(n),                    \
        .dma_ch_base     = DMA1_CH_BASE(ADC_CH32V307_DMA_CHANNEL(n)),      \
        .irq_config_func = adc_ch32v307_irq_config_##n,                    \
    };                                                                      \
    DEVICE_DT_INST_DEFINE(n,                                                \
        adc_ch32v307_init,                                                  \
        NULL,                                                               \
        &adc_ch32v307_data_##n,                                             \
        &adc_ch32v307_cfg_##n,                                              \
        POST_KERNEL,                                                        \
        CONFIG_ADC_INIT_PRIORITY,                                           \
        &adc_ch32v307_api);

DT_INST_FOREACH_STATUS_OKAY(ADC_CH32V307_INIT)
