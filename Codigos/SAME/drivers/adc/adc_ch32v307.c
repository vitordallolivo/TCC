/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Driver ADC para CH32V307 — Zephyr RTOS
 * Implementa a API drivers/adc.h via MMIO direto (sem HAL WCH).
 *
 * Suporta múltiplas instâncias — ADC1 (base 0x40012400) e ADC2
 * (base 0x40012800, = ADC1_BASE + 0x400).
 *
 * ⚠ MODO DUAL SIMULTÂNEO (ADC1 mestre + ADC2 escravo) — OBRIGATÓRIO se as
 * duas instâncias existirem no DTS. O manual de referência confirma
 * (Table 11-2 "Peripheral mapping table of each DMA1 channel", cap. 11.2.3):
 * **só o ADC1 tem linha de request de DMA** — ADC2 NÃO aparece em NENHUM
 * dos 7 canais do DMA1. Um ADC2 "independente" com `dmas = <&dma1 N>`
 * próprio nunca completaria uma transferência (join TC nunca dispara) —
 * toda leitura daria timeout na placa real (native_sim não pega isso, já
 * que emula o ADC por outro caminho, sem esse driver).
 *
 * Por isso, quando as DUAS instâncias existem (caso da SAME — 3 tensões no
 * ADC1, 3 correntes no ADC2), o driver configura DUALMOD=Regular
 * Simultaneous (registrador ADC1_CTLR1, bits 19:16 = 0110 — ver manual de
 * referência 12.3.2): a cada passo da sequência, ADC1 e ADC2 convertem SEUS
 * respectivos canais NO MESMO INSTANTE (V_a com i_a, V_b com i_b, V_c com
 * i_c — pareado por fase, essencial pro Clarke/SMO), e o resultado
 * combinado (ADC2 nos 16 bits altos, ADC1 nos 16 bits baixos de um único
 * word de 32 bits) sai por UMA SÓ transferência de DMA de 32 bits, disparada
 * pelo ADC1 (única linha de DMA que existe de verdade). O ADC2 (escravo)
 * não tem `dmas` no DTS — nem precisa, é acionado pelo trigger simultâneo
 * que o hardware manda pra ele quando DUALMOD está configurado no ADC1.
 *
 * Quando só UMA instância existe (caso da SIBA), o driver cai no modo
 * "independente" de sempre — nada muda pra esse caso.
 *
 * Cada instância:
 *   - (mestre, ou única) aloca seu próprio canal de DMA1 (registradores +
 *     IRQ calculados a partir do número do canal informado no DTS — ver
 *     DMA1_CH_BASE/ADC_CH32V307_DMA_IRQN)
 *   - habilita seu próprio clock em RCC_APB2PCENR (ADC1 = bit 9,
 *     ADC2 = bit 10 — calculado a partir do offset do `base` em relação
 *     a ADC1_BASE, blocos de 0x400)
 *
 * Modo de operação (independente, igual para qualquer instância isolada):
 *   - Scan contínuo de N canais configurados via adc_channel_cfg
 *   - DMA em modo circular → sem intervenção da CPU por amostra
 *   - adc_read() síncrono: dispara software trigger e aguarda semáforo
 *   - adc_read_async() assíncrono: usa k_poll_signal sinalizado na ISR DMA TC
 *
 * Registradores (offsets relativos à base do periférico — ADC1 ou ADC2):
 *   STATR   +0x00   Status
 *   CTLR1   +0x04   Control 1  (SCAN, DUALMOD[19:16] — só ADC1_CTLR1 vale)
 *   CTLR2   +0x08   Control 2  (ADON, CONT, DMA, SWSTART, CAL, RSTCAL)
 *   SAMPTR1 +0x0C   Sample time ch10-17
 *   SAMPTR2 +0x10   Sample time ch0-9
 *   RSQR1   +0x2C   Regular sequence 1  (comprimento L)
 *   RSQR2   +0x30   Regular sequence 2
 *   RSQR3   +0x34   Regular sequence 3  (SQ1..SQ6)
 *   RDATAR  +0x4C   Regular data (lido pelo DMA) — em modo dual, os 32 bits
 *                   do ADC1_RDATAR trazem {ADC2[31:16], ADC1[15:0]}
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
 *   CH32FV2x_V3xRM-EN (manual de referência) — cap. 11.2.3 "DMA Request
 *   Mapping" (Table 11-2) e cap. 12.2.7 "Dual ADC Mode" (Regular
 *   simultaneous mode, DUALMOD, Figure 12-7)
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

/* Dual mode — DUALMOD[19:16], só é interpretado no CTLR1 do ADC1 (mestre).
 * 0110 = Regular simultaneous mode (manual de referência 12.3.2). */
#define ADC_CTLR1_DUALMOD_SHIFT     16
#define ADC_CTLR1_DUALMOD_MASK      (0xFUL << ADC_CTLR1_DUALMOD_SHIFT)
#define ADC_CTLR1_DUALMOD_REGSIMUL  (0x6UL << ADC_CTLR1_DUALMOD_SHIFT)

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
#define DMA_CFGR_PSIZE_32   BIT(9)   /* Peripheral size 32 bit */
#define DMA_CFGR_MSIZE_32   BIT(11)  /* Memory size 32 bit */
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
 * Par dual (mestre ADC1 + escravo ADC2) — resolvido em runtime comparando
 * cfg->base no init() de cada instância, SEM precisar de propriedade nova
 * no DTS. Só existe de verdade se as DUAS instâncias forem instanciadas
 * (caso da SAME); na SIBA (só ADC1) fica sempre {NULL, NULL} e todo o
 * código abaixo cai no caminho independente de sempre.
 * ========================================================= */
static const struct device *g_adc1_dev;
static const struct device *g_adc2_dev;

/* Buffer do DMA combinado de 32 bits (só usado pelo mestre em modo dual):
 * um word por passo da sequência, {ADC2[31:16], ADC1[15:0]}. */
static __attribute__((aligned(4))) uint32_t g_dual_dma_buf[CH32_ADC_MAX_CHANNELS];

static inline bool adc_dual_is_paired(void)
{
    return g_adc1_dev != NULL && g_adc2_dev != NULL;
}

/* =========================================================
 * Estado do driver (uma instância por ADC1/ADC2)
 * ========================================================= */
struct adc_ch32v307_data {
    /* Buffer DMA (modo independente) — alinhado em 4 bytes, escrito pelo DMA */
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
    uint32_t dma_ch_base;   /* Base dos registradores do canal DMA1 alocado (0 se !has_dma) */
    uint8_t  dma_channel;   /* Número do canal DMA1 (1-7) — bits ISR/IFCR (0 se !has_dma) */
    bool     has_dma;       /* false só pro escravo (ADC2) em modo dual — ver topo do arquivo */
    void (*irq_config_func)(const struct device *dev);  /* NULL se !has_dma */
};

/* =========================================================
 * ISR do canal DMA1 alocado — Transfer Complete
 *
 * Em modo dual, só o MESTRE (ADC1) tem IRQ própria conectada (o escravo
 * não tem canal de DMA nenhum) — quando ela dispara, o word de 32 bits já
 * traz os resultados dos DOIS ADCs combinados, então esta mesma ISR separa
 * e libera o semáforo/sinal das DUAS instâncias.
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

    if (adc_dual_is_paired() && dev == g_adc1_dev) {
        /* Mestre em modo dual: g_dual_dma_buf tem 1 word/passo — 16 bits
         * baixos = ADC1 (este dev), 16 bits altos = ADC2 (escravo). */
        struct adc_ch32v307_data *sdata = g_adc2_dev->data;

        for (int i = 0; i < data->ch_count; i++) {
            uint32_t w = g_dual_dma_buf[i];
            if (data->user_buf)  data->user_buf[i]  = (uint16_t)(w & 0xFFFFU);
            if (sdata->user_buf) sdata->user_buf[i] = (uint16_t)(w >> 16);
        }

        if (data->async_sig) {
            k_poll_signal_raise(data->async_sig, 0);
            data->async_sig = NULL;
        } else {
            k_sem_give(&data->sem_sync);
        }
        if (sdata->async_sig) {
            k_poll_signal_raise(sdata->async_sig, 0);
            sdata->async_sig = NULL;
        } else {
            k_sem_give(&sdata->sem_sync);
        }
        return;
    }

    /* Instância única/independente (caso da SIBA, ou qualquer instância
     * fora de um par dual) — comportamento de sempre. */
    if (data->user_buf) {
        for (int i = 0; i < data->ch_count; i++) {
            data->user_buf[i] = data->dma_buf[i];
        }
    }
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

/* Igual a dma_init(), mas pro word combinado de 32 bits do modo dual
 * (ADC1_RDATAR traz {ADC2[31:16], ADC1[15:0]} quando DUALMOD=Regular
 * simultaneous). Só chamada pelo mestre (ADC1). */
static void dma_init32(const struct adc_ch32v307_cfg *cfg, uint32_t *buf, uint16_t count)
{
    uint32_t chb = cfg->dma_ch_base;

    DMA_CH_REG(chb, DMA_CH_OFF_CFGR) &= ~DMA_CFGR_EN;

    DMA_CH_REG(chb, DMA_CH_OFF_PADDR) = cfg->base + ADC_OFF_RDATAR;
    DMA_CH_REG(chb, DMA_CH_OFF_MADDR) = (uint32_t)buf;
    DMA_CH_REG(chb, DMA_CH_OFF_CNTR)  = count;

    DMA_CH_REG(chb, DMA_CH_OFF_CFGR) =
                    DMA_CFGR_TCIE      |
                    DMA_CFGR_CIRC       |
                    DMA_CFGR_MINC       |
                    DMA_CFGR_PSIZE_32   |
                    DMA_CFGR_MSIZE_32   |
                    DMA_CFGR_PL_HIGH;

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

/*
 * Re-arma o PAR dual inteiro (mestre + escravo) — chamada sempre que
 * qualquer um dos dois tiver sua sequência de canais alterada, pra que a
 * config final reflita SEMPRE o estado atual dos dois lados (não importa
 * se o mestre ou o escravo terminou de configurar por último).
 */
static void adc_arm_dual_master(void)
{
    const struct adc_ch32v307_cfg *mcfg = g_adc1_dev->config;
    const struct adc_ch32v307_cfg *scfg = g_adc2_dev->config;
    struct adc_ch32v307_data *mdata = g_adc1_dev->data;

    /* Desliga os dois ADCs e o DMA do mestre antes de reconfigurar */
    DMA_CH_REG(mcfg->dma_ch_base, DMA_CH_OFF_CFGR) &= ~DMA_CFGR_EN;
    ADC_REG(mcfg->base, ADC_OFF_CTLR2) &= ~ADC_CTLR2_ADON;
    ADC_REG(scfg->base, ADC_OFF_CTLR2) &= ~ADC_CTLR2_ADON;

    /* DUALMOD só é interpretado no CTLR1 do ADC1 (mestre) — Regular
     * simultaneous mode (manual de referência 12.3.2, valor 0110). */
    uint32_t ctlr1 = ADC_REG(mcfg->base, ADC_OFF_CTLR1);
    ctlr1 = (ctlr1 & ~ADC_CTLR1_DUALMOD_MASK) | ADC_CTLR1_DUALMOD_REGSIMUL | ADC_CTLR1_SCAN;
    ADC_REG(mcfg->base, ADC_OFF_CTLR1) = ctlr1;
    ADC_REG(scfg->base, ADC_OFF_CTLR1) |= ADC_CTLR1_SCAN;

    /* DMA combinado de 32 bits — só o mestre tem linha de DMA de verdade
     * (ver nota grande no topo do arquivo). */
    dma_init32(mcfg, g_dual_dma_buf, mdata->ch_count);

    /* Mestre: ADON+CONT+DMA+trigger por software — SWSTART no ADC1 dispara
     * os DOIS simultaneamente uma vez DUALMOD configurado (a placa envia o
     * trigger simultâneo pro ADC2 sozinha, em hardware).
     * Escravo: ADON+CONT apenas — SEM DMA própria, SEM trigger próprio. */
    ADC_REG(mcfg->base, ADC_OFF_CTLR2) = ADC_CTLR2_ADON | ADC_CTLR2_CONT | ADC_CTLR2_DMA |
                                        ADC_CTLR2_EXTTRIG | (0x7UL << 17); /* EXTSEL=111: SWSTART */
    ADC_REG(mcfg->base, ADC_OFF_CTLR2) |= ADC_CTLR2_ADON;

    ADC_REG(scfg->base, ADC_OFF_CTLR2) = ADC_CTLR2_ADON | ADC_CTLR2_CONT;
    ADC_REG(scfg->base, ADC_OFF_CTLR2) |= ADC_CTLR2_ADON;

    adc_calibrate(mcfg->base);
    adc_calibrate(scfg->base);

    /* Dispara a primeira conversão — só no mestre; o escravo é acionado
     * junto, em hardware, pelo trigger simultâneo do modo dual. */
    ADC_REG(mcfg->base, ADC_OFF_CTLR2) |= ADC_CTLR2_SWSTART;
}

static void adc_hw_reconfigure(const struct device *dev);

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

        /* Reprograma RSQR/SAMPTR/DMA com a sequência atualizada — chamado
         * uma vez por canal adicionado; a última chamada (todos os canais
         * já registrados) deixa o scan circular rodando com a sequência
         * completa. */
        adc_hw_reconfigure(dev);
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

    /* Desliga só este ADC antes de reprogramar a sequência DELE — quem
     * re-arma o par inteiro (DMA/DUALMOD/trigger) é adc_arm_dual_master(),
     * chamada abaixo (em modo dual) depois da sequência atualizada. */
    ADC_REG(base, ADC_OFF_CTLR2) &= ~ADC_CTLR2_ADON;
    if (!adc_dual_is_paired()) {
        DMA_CH_REG(cfg->dma_ch_base, DMA_CH_OFF_CFGR) &= ~DMA_CFGR_EN;
    }

    adc_set_sequence(base, data->ch_ids, data->ch_count);
    adc_set_sample_times(base, data->ch_ids, data->ch_count);

    if (adc_dual_is_paired()) {
        /* Re-arma os DOIS lados com o estado atual — não importa se quem
         * chamou foi o mestre ou o escravo. */
        adc_arm_dual_master();
        return;
    }

    /* Modo independente de sempre (SIBA, ou qualquer instância isolada) */
    dma_init(cfg, data->dma_buf, data->ch_count);

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

    /* O DMA circular já está rodando (mestre, em modo dual, ou este mesmo
     * ADC, em modo independente); semáforo é dado na próxima TC — em modo
     * dual, quem dá este semáforo é a ISR do MESTRE (ver adc_ch32v307_dma_isr). */
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

    /* DMA circular → ISR (do mestre, em modo dual) sinalizará async quando
     * TC ocorrer. */
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

    /* Registra este device como mestre (ADC1) ou escravo (ADC2) do par
     * dual — resolvido só comparando a base física, sem depender de
     * propriedade nova no DTS nem da ordem de inicialização (as duas
     * instâncias terminam o init() antes de HAL_Init() chamar
     * channel_setup(), então não há corrida aqui). */
    if (cfg->base == ADC1_BASE) {
        g_adc1_dev = dev;
    } else if (cfg->base == ADC2_BASE) {
        g_adc2_dev = dev;
    }

    adc_rcc_enable(cfg);

    /* Liga ADC pela primeira vez (necessário antes da calibração) */
    ADC_REG(cfg->base, ADC_OFF_CTLR2) |= ADC_CTLR2_ADON;

    /* Pequeno delay de estabilização (~1µs @ 144 MHz) */
    for (volatile int i = 0; i < 200; i++) {
        __asm__ volatile("nop");
    }

    /* Conecta/habilita a IRQ do canal DMA1 alocado (transfer-complete) —
     * só existe se esta instância TEM DMA própria (o escravo ADC2, em modo
     * dual, não tem — recebe o resultado via ISR do mestre). A partir
     * daqui adc_ch32v307_dma_isr() pode disparar, mas o canal ainda está
     * com CFGR.EN=0 (dma_init()/dma_init32() só rodam em
     * adc_hw_reconfigure()/adc_arm_dual_master(), chamados por
     * adc_channel_setup() depois que HAL_ADC_Init() configurar os canais)
     * — sem transferência em andamento, sem TC, sem ISR. */
    if (cfg->has_dma) {
        cfg->irq_config_func(dev);
    }

    data->initialized = true;
    LOG_INF("ADC CH32V307 inicializado (base=0x%08X, DMA1 canal %u%s)",
            cfg->base, cfg->dma_channel,
            cfg->has_dma ? "" : " -- escravo dual, sem DMA própria");
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
 * Instanciação via DTS — suporta múltiplos nós `wch,adc` (ADC1 e/ou ADC2).
 * Só a instância que tiver `dmas = <&dma1 N>` no DTS ganha canal de DMA e
 * IRQ próprios — o escravo dual (ADC2, quando houver par) não declara
 * `dmas` no DTS de propósito (ver nota grande no topo do arquivo).
 * ========================================================= */
#define ADC_CH32V307_HAS_DMA(n)      DT_INST_NODE_HAS_PROP(n, dmas)
#define ADC_CH32V307_DMA_CHANNEL(n)  DT_INST_DMAS_CELL_BY_IDX(n, 0, channel)
/* IRQn do canal N de DMA1 = 26 + N (DMA1_CH1=27 .. DMA1_CH7=33) */
#define ADC_CH32V307_DMA_IRQN(n)     (26 + ADC_CH32V307_DMA_CHANNEL(n))

#define ADC_CH32V307_IRQ_CONFIG_FN(n)                                          \
    IF_ENABLED(ADC_CH32V307_HAS_DMA(n), (                                     \
        static void adc_ch32v307_irq_config_##n(const struct device *dev)    \
        {                                                                     \
            IRQ_CONNECT(ADC_CH32V307_DMA_IRQN(n), 1,                         \
                        adc_ch32v307_dma_isr, DEVICE_DT_INST_GET(n), 0);      \
            irq_enable(ADC_CH32V307_DMA_IRQN(n));                            \
        }                                                                     \
    ))

#define ADC_CH32V307_INIT(n)                                                   \
    ADC_CH32V307_IRQ_CONFIG_FN(n)                                              \
    static struct adc_ch32v307_data adc_ch32v307_data_##n;                    \
    static const struct adc_ch32v307_cfg adc_ch32v307_cfg_##n = {             \
        .base            = DT_INST_REG_ADDR(n),                               \
        .has_dma         = ADC_CH32V307_HAS_DMA(n),                           \
        .dma_channel     = COND_CODE_1(ADC_CH32V307_HAS_DMA(n),               \
                               (ADC_CH32V307_DMA_CHANNEL(n)), (0)),            \
        .dma_ch_base     = COND_CODE_1(ADC_CH32V307_HAS_DMA(n),               \
                               (DMA1_CH_BASE(ADC_CH32V307_DMA_CHANNEL(n))), (0)), \
        .irq_config_func = COND_CODE_1(ADC_CH32V307_HAS_DMA(n),               \
                               (adc_ch32v307_irq_config_##n), (NULL)),         \
    };                                                                         \
    DEVICE_DT_INST_DEFINE(n,                                                   \
        adc_ch32v307_init,                                                     \
        NULL,                                                                  \
        &adc_ch32v307_data_##n,                                                \
        &adc_ch32v307_cfg_##n,                                                 \
        POST_KERNEL,                                                           \
        CONFIG_ADC_INIT_PRIORITY,                                              \
        &adc_ch32v307_api);

DT_INST_FOREACH_STATUS_OKAY(ADC_CH32V307_INIT)
