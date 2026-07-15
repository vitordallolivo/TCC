/**
 * @file adc_ch32v307.c
 * @brief Driver ADC do CH32V307 (Zephyr), MMIO direto — suporta ADC1/ADC2,
 * cada instância independente (sem modo dual, ver versão SAME/SIBA).
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Cada instância tem seu próprio canal DMA1 (`dmas = <&dma1 N>` no DTS) e
 * clock em RCC_APB2PCENR. Scan contínuo em DMA circular; adc_read() bloqueia
 * num semáforo, adc_read_async() usa k_poll_signal sinalizado na ISR DMA TC.
 * Refs: openwch/ch32v307 (ch32v30x_adc.c).
 */

#define DT_DRV_COMPAT wch_adc

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <stdint.h>

LOG_MODULE_REGISTER(adc_ch32v307, CONFIG_ADC_LOG_LEVEL);

/* Registradores, offsets relativos à base (ADC1_BASE ou ADC1_BASE+0x400). */

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

/**
 * @brief ISR de Transfer Complete do canal DMA1 alocado a esta instância:
 * copia o buffer DMA pro buffer do usuário e sinaliza quem espera.
 * @param dev instância dona da IRQ
 */
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

/* Funções de HW, recebem a base do periférico/canal — reusadas por ADC1 e ADC2. */

/**
 * @brief Habilita o clock do DMA1 e do ADC desta instância, e o prescaler.
 * @param cfg config da instância (usa cfg->base pra achar o índice ADC1/ADC2)
 */
static void adc_rcc_enable(const struct adc_ch32v307_cfg *cfg)
{
    uint32_t adc_index = (cfg->base - ADC1_BASE) / 0x400UL; /* 0=ADC1, 1=ADC2 */

    /* Habilita clock DMA1 e o clock do ADC desta instância */
    RCC_AHBPCENR  |= RCC_AHB_DMA1;
    RCC_APB2PCENR |= RCC_APB2_ADCx(adc_index);

    /* ADC clock = PCLK2/6 = 24 MHz (limite datasheet 14 MHz... ajustar p/
     * /8 se HSE < 112 MHz). Prescaler é compartilhado; última instância a
     * inicializar só reafirma o mesmo valor. */
    RCC_CFGR0 = (RCC_CFGR0 & ~RCC_CFGR0_ADCPRE_MASK) | RCC_CFGR0_ADCPRE_DIV6;
}

/**
 * @brief Configura o canal DMA1 em modo circular, 16 bits.
 * @param cfg config da instância (dma_ch_base, base do periférico)
 * @param buf buffer de destino (uint16_t por canal)
 * @param count número de canais na sequência
 */
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

/**
 * @brief Executa a sequência de calibração de hardware do ADC (RSTCAL+CAL).
 * @param base endereço base do periférico (ADC1_BASE ou ADC2_BASE)
 */
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

/**
 * @brief Configura RSQR3/2/1 com a sequência de canais (5 bits por SQx).
 * @param base endereço base do periférico
 * @param ids ordem dos IDs de canal na sequência
 * @param count número de canais (1..16)
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

/**
 * @brief Configura sample time = 239.5 ciclos (máx. precisão) pra todos os canais.
 * @param base endereço base do periférico
 * @param ids IDs de canal a configurar
 * @param count número de canais
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

static void adc_hw_reconfigure(const struct device *dev);

/**
 * @brief API Zephyr channel_setup: adiciona um canal à sequência de scan
 * desta instância e reprograma o hardware.
 * @param dev instância do driver
 * @param cfg config do canal (só ADC_GAIN_1/ADC_REF_INTERNAL suportados)
 * @return 0 em sucesso, -EINVAL/-ENOTSUP/-ENOMEM em erro
 */
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

        /* Reprograma RSQR/SAMPTR/DMA a cada canal adicionado. */
        adc_hw_reconfigure(dev);
    }

    return 0;
}

/**
 * @brief Reprograma sequência/sample-time/DMA/trigger desta instância
 * quando os canais mudam.
 * @param dev instância do driver
 */
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

/**
 * @brief API Zephyr read (síncrono): dispara/aguarda uma conversão completa.
 * @param dev instância do driver
 * @param seq sequência solicitada (buffer + máscara de canais)
 * @return 0 em sucesso, -ENODEV/-EINVAL/-ETIMEDOUT em erro
 */
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

/**
 * @brief API Zephyr read_async: registra o buffer/sinal e retorna sem
 * bloquear; o sinal é levantado na ISR do DMA quando a conversão terminar.
 * @param dev instância do driver
 * @param seq sequência solicitada (buffer + máscara de canais)
 * @param async sinal a levantar na conclusão
 * @return 0 em sucesso, -ENODEV/-EINVAL em erro
 */
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

/**
 * @brief API Zephyr init: liga o clock/ADC desta instância e conecta a IRQ
 * do canal DMA1 alocado. Calibração de verdade ocorre depois, no primeiro
 * adc_hw_reconfigure() (channel_setup).
 * @param dev instância do driver
 * @return sempre 0
 */
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

    /* Habilita a IRQ do canal DMA1; ainda não dispara pois dma_init() só
     * roda depois, em adc_hw_reconfigure(). */
    cfg->irq_config_func(dev);

    data->initialized = true;
    LOG_INF("ADC CH32V307 inicializado (base=0x%08X, DMA1 canal %u)",
            cfg->base, cfg->dma_channel);
    return 0;
}

/** @brief Tabela de API do driver ADC (interface Zephyr adc_driver_api). */
static const struct adc_driver_api adc_ch32v307_api = {
    .channel_setup = adc_ch32v307_channel_setup,
    .read          = adc_ch32v307_read,
#ifdef CONFIG_ADC_ASYNC
    .read_async    = adc_ch32v307_read_async,
#endif
    .ref_internal  = 3300,  /* mV — Vref interno CH32V307 */
};

/* Instanciação via DTS — cada nó `wch,adc` com seu canal DMA1 próprio. */
#define ADC_CH32V307_DMA_CHANNEL(n)  DT_INST_DMAS_CELL_BY_IDX(n, 0, channel)
/* IRQn do canal N de DMA1 = 26 + N (DMA1_CH1=27 .. DMA1_CH7=33) */
#define ADC_CH32V307_DMA_IRQN(n)     (26 + ADC_CH32V307_DMA_CHANNEL(n))

/**
 * @brief Instancia o driver pra instância DTS `n`: gera a função de IRQ,
 * dados/config e registro no dispositivo Zephyr (DEVICE_DT_INST_DEFINE).
 * @param n índice da instância DTS
 */
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
