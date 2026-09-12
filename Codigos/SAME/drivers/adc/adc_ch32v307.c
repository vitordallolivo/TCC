/**
 * @file adc_ch32v307.c
 * @brief Driver ADC do CH32V307 (Zephyr), MMIO direto — suporta ADC1/ADC2.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Só o ADC1 tem linha de DMA de verdade (manual ref. Table 11-2). Com as
 * duas instâncias juntas (caso da SAME) o driver usa DUALMOD=Regular
 * Simultaneous: ADC1+ADC2 convertem juntos e saem numa DMA de 32 bits só,
 * disparada pelo ADC1. Com uma instância só (SIBA), modo independente normal.
 * Refs: ch32v30x_adc.c, CH32FV2x_V3xRM-EN cap. 11.2.3 e 12.2.7.
 */

#define DT_DRV_COMPAT wch_adc

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <stdint.h>
#include "Observer_Manager.h"   /* OBS_TS_NS — Ts que o observador assume */
#include "../adc_ch32v307.h"    /* adc_ch32v307_frame_cb_t (API pública) */

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
#define ADC_CTLR1_BUFEN     BIT(26)          /* buffer de entrada */
#define ADC_CTLR1_PGA_MASK  (0x3UL << 27)    /* ganho x1/x4/x16/x64 (exige BUFEN) */

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
#define DMA_CFGR_HTIE       BIT(2)   /* Half Transfer IRQ enable */
#define DMA_CFGR_CIRC       BIT(5)   /* Circular mode */
#define DMA_CFGR_MINC       BIT(7)   /* Memory increment */
#define DMA_CFGR_PSIZE_16   BIT(8)   /* Peripheral size 16 bit */
#define DMA_CFGR_MSIZE_16   BIT(10)  /* Memory size 16 bit */
#define DMA_CFGR_PSIZE_32   BIT(9)   /* Peripheral size 32 bit */
#define DMA_CFGR_MSIZE_32   BIT(11)  /* Memory size 32 bit */
#define DMA_CFGR_PL_HIGH    BIT(13)  /* Priority level high (PL[13:12]=0b10) */

/* ISR/IFCR: 4 bits por canal (GIFx, TCIFx, HTIFx, TEIFx) → canal N usa:
 *   GIF  no bit (4*(N-1) + 0)
 *   TCIF no bit (4*(N-1) + 1)
 *   HTIF no bit (4*(N-1) + 2)
 *   TEIF no bit (4*(N-1) + 3) */
#define DMA1_TCIF(ch)       BIT(4U * ((uint32_t)(ch) - 1U) + 1U)
#define DMA1_HTIF(ch)       BIT(4U * ((uint32_t)(ch) - 1U) + 2U)
#define DMA1_TEIF(ch)       BIT(4U * ((uint32_t)(ch) - 1U) + 3U)

/* RCC */
#define RCC_BASE        0x40021000UL
#define RCC_AHBPCENR    (*(volatile uint32_t *)(RCC_BASE + 0x14))
#define RCC_APB2PCENR   (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_CFGR0       (*(volatile uint32_t *)(RCC_BASE + 0x04))

#define RCC_AHB_DMA1    BIT(0)
/* ADC1 = APB2ENR bit 9, ADC2 = bit 10 — blocos consecutivos de 0x400 a
 * partir de ADC1_BASE mapeiam para bits consecutivos a partir do bit 9 */
#define RCC_APB2_ADCx(adc_index)  BIT(9U + (adc_index))

/* =========================================================
 * Clock do ADC — CFGR0[15:14] = ADCPRE
 *
 * ⚠️ NÃO EXISTE prescaler estendido nesta família. Conferido na SDK da WCH
 * (ch32v30x_rcc.c): CFGR0_ADCPRE_Reset_Mask = 0xFFFF3FFF limpa SÓ os bits
 * [15:14], e ch32v30x_rcc.h define apenas RCC_PCLK2_Div2/4/6/8. Ou seja, o
 * divisor máximo é /8.
 *
 * O limite do periférico é 14 MHz. Com PCLK2 = 144 MHz (o Zephyr deixa
 * HPRE=/1 e não toca em PPRE2 — clock_control_wch_rcc.c), o melhor que /8 dá
 * é 18 MHz: AINDA ACIMA DO LIMITE. A única forma de entrar em spec é baixar
 * o próprio PCLK2 — ver CH32_ADC_LOWER_APB2 abaixo.
 * ========================================================= */
#define RCC_CFGR0_ADCPRE_MASK  (0x3UL << 14)
#define RCC_CFGR0_PPRE2_MASK   (0x7UL << 11)
#define RCC_CFGR0_PPRE2_DIV2   (0x4UL << 11)   /* 0b100 = HCLK/2 */

/* Divisor de PCLK2 para o ADC: 2, 4, 6 ou 8.
 * /8 é o máximo que existe — e é o que o exemplo oficial da WCH usa
 * (RCC_ADCCLKConfig(RCC_PCLK2_Div8) em EVT/EXAM/ADC/DualADC_RegSimul). */
#ifndef CH32_ADC_CLK_DIV
#define CH32_ADC_CLK_DIV   8
#endif

/* DESLIGADO, e não ligue sem saber o que vem junto.
 *
 * Baixar PPRE2 para /2 (PCLK2 = 72 MHz) é a única forma de o ADCCLK respeitar
 * o teto de 14 MHz do manual — o divisor do ADC vai só até /8, e 144/8 =
 * 18 MHz ainda estoura. MAS o PCLK2 é compartilhado: `clock_control_wch_rcc.c`
 * devolve sempre o clock de AHB em clock_control_get_rate() ("Assume that the
 * clock tree diagram is correct and always return AHB"), ignorando PPRE2.
 * Resultado: o driver de USART calcula `divn = 144 MHz / current_speed`
 * enquanto o PCLK2 real é 72 MHz, e o console vai para METADE do baud —
 * exigindo compensar `current-speed` no DTS.
 *
 * Ou seja: pôr o ADC em spec custa mexer no console, que funciona. Enquanto
 * essa troca não for decidida, fica em 18 MHz (1,29x acima do limite) e o
 * driver avisa no boot. */
#ifndef CH32_ADC_LOWER_APB2
#define CH32_ADC_LOWER_APB2   0
#endif

#define CH32_PCLK2_HZ   (CH32_ADC_LOWER_APB2 ? \
                         (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 2) : \
                         CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC)
#define CH32_ADCCLK_HZ  (CH32_PCLK2_HZ / CH32_ADC_CLK_DIV)

BUILD_ASSERT(CH32_ADC_CLK_DIV == 2 || CH32_ADC_CLK_DIV == 4 ||
             CH32_ADC_CLK_DIV == 6 || CH32_ADC_CLK_DIV == 8,
             "CH32_ADC_CLK_DIV: o ADCPRE desta familia so aceita 2, 4, 6 ou 8 "
             "(conferido na SDK da WCH, ch32v30x_rcc.h)");

/* Código de ADCPRE: /2=00, /4=01, /6=10, /8=11 */
#define RCC_CFGR0_ADCPRE  ((uint32_t)((CH32_ADC_CLK_DIV / 2 - 1)) << 14)

/* =========================================================
 * Tempo de amostragem (SAMPTRx, 3 bits por canal)
 *
 * 000=1,5  001=7,5  010=13,5  011=28,5  100=41,5  101=55,5  110=71,5  111=239,5
 *
 * Define a taxa por canal junto com o ADCCLK:
 *     f_s = ADCCLK / ((SMP + 11) * n_canais)
 *
 * O padrão anterior era 111 (239,5 ciclos), "máxima precisão" — mas com 3
 * canais e ADCCLK de 24 MHz isso dá 31,9 kSps/canal, contra os 10 µs
 * (100 kSps) que o observador assume em OBS_TS. Como o front-end tem RC
 * antialiasing (o capacitor é quem entrega a carga no instante da
 * amostragem), SMP curto não custa exatidão aqui — ver CLAUDE.md,
 * "Filtro antialiasing".
 *
 * 011 (28,5) é o ponto de operação do projeto: com ADCCLK = 12 MHz dá
 * (28,5+11)*3/12e6 = 9,875 µs, que é o OBS_TS de 10 µs do observador.
 * ========================================================= */
#ifndef CH32_ADC_SMP
#define CH32_ADC_SMP   0x3U    /* 28,5 ciclos */
#endif

BUILD_ASSERT(CH32_ADC_SMP <= 0x7U, "CH32_ADC_SMP: codigo de 3 bits (0..7)");

/* Ciclos de ADCCLK por conversão, ×10 para não perder a meia unidade. */
static const uint16_t ch32_adc_smp_x10[8] = {
    15, 75, 135, 285, 415, 555, 715, 2395
};

/* Período de UM frame (todos os canais da sequência), em nanosegundos.
 * É o Ts real da malha — tem de bater com OBS_TS do Observer_Manager. */
#define CH32_ADC_FRAME_NS(n_ch)                                              \
    ((uint32_t)(((uint64_t)(ch32_adc_smp_x10[CH32_ADC_SMP] + 110U) *         \
                 (n_ch) * 100000000ULL) / CH32_ADCCLK_HZ))

/* =========================================================
 * Limite de canais suportados simultaneamente
 * ========================================================= */
#define CH32_ADC_MAX_CHANNELS  16

/* Número de frames ADC acumulados por metade do buffer ping-pong.
 * ISR dispara a cada BATCH_FRAMES frames; o callback recebe o ponteiro
 * para a metade estável com ch_count * BATCH_FRAMES words de uma vez.
 * O processamento (Clarke + Observer_Step × N) acontece fora da ISR, na
 * work queue, dando tempo ao RTOS entre batches. */
#ifndef CH32_ADC_BATCH_FRAMES
#define CH32_ADC_BATCH_FRAMES  16
#endif

/* Par dual (mestre ADC1 + escravo ADC2), resolvido em runtime comparando
 * cfg->base no init() de cada instância. Na SIBA (só ADC1) fica {NULL,NULL}
 * e tudo cai no caminho independente normal. */
static const struct device *g_adc1_dev;
static const struct device *g_adc2_dev;

/* Buffer do DMA combinado de 32 bits (só usado pelo mestre em modo dual):
 * um word por passo da sequência, {ADC2[31:16], ADC1[15:0]}.
 *
 * PING-PONG com BATCH: cada metade do buffer tem CH32_ADC_BATCH_FRAMES frames
 * completos. O DMA só dispara a ISR após preencher BATCH_FRAMES frames inteiros;
 * a ISR copia a metade estável (memcpy ~192 B) e retorna — o processamento
 * pesado (Clarke + Observer_Step × N) acontece na work queue, longe da ISR.
 *
 *     half-transfer  → DMA escreve a 2ª metade, ISR copia a 1ª (estável)
 *     transfer-comp. → DMA escreve a 1ª metade, ISR copia a 2ª (estável)
 *
 * Tamanho total: 2 × ch_count × BATCH_FRAMES × 4 B.
 * Com ch_count=3 e BATCH_FRAMES=16: 384 B. */
static __attribute__((aligned(4)))
uint32_t g_dual_dma_buf[2U * CH32_ADC_MAX_CHANNELS * CH32_ADC_BATCH_FRAMES];

/**
 * @brief Verifica se ADC1 e ADC2 estão ambos instanciados (par dual).
 * @return true se as duas instâncias existirem (caso da SAME)
 */
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

    /* Sincronização — usada apenas no caminho legado (sem callback) */
    struct k_sem            sem_sync;   /* para adc_read() síncrono */
    struct k_poll_signal   *async_sig;  /* para adc_read_async()     */

    /* Callback direto de frame (ISR, sem overhead de kernel).
     * Quando não-NULL, substitui por completo o caminho semáforo+user_buf. */
    adc_ch32v307_frame_cb_t frame_cb;
    void                   *frame_cb_user;

    /* Offset de calibração desta instância, em LSB, com sinal.
     * Somado a TODA amostra — ver adc_get_calibration_value(). */
    int16_t cal_offset;
    bool    calibrated;   /* já calibrado: não repetir a cada channel_setup */

    /* Diagnóstico do stream (só o mestre atualiza) */
    uint32_t seq;             /* frames entregues desde o boot */
    uint32_t dropped_frames;  /* HT e TC juntos = a ISR perdeu um frame */

    bool initialized;
};

/**
 * @brief Aplica o offset de calibração a uma amostra bruta de 12 bits.
 *
 * Réplica de Get_ConversionVal() do exemplo oficial da WCH
 * (EVT/EXAM/ADC/DualADC_RegSimul): a autocalibração de hardware sozinha NÃO
 * zera o erro — ela deixa um resíduo que o fabricante corrige em software.
 * Os extremos (0 e 4095) passam sem correção de propósito: são amostras
 * saturadas, e corrigi-las inventaria código que o conversor não produziu.
 *
 * @param raw contagem bruta do ADC (0..4095)
 * @param off offset de calibração (LSB, com sinal)
 * @return contagem corrigida, saturada em [0, 4095]
 */
static inline uint16_t adc_apply_cal(uint16_t raw, int16_t off)
{
    if (raw == 0U || raw == 4095U) {
        return raw;
    }
    int32_t v = (int32_t)raw + off;
    if (v < 0)     return 0U;
    if (v > 4095)  return 4095U;
    return (uint16_t)v;
}

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

/**
 * @brief ISR de Transfer Complete do DMA1. Em modo dual só o mestre (ADC1)
 * tem IRQ própria; quando dispara, o word de 32 bits já traz os dois
 * resultados, e esta ISR separa e libera o semáforo/sinal das duas instâncias.
 * @param dev device que disparou a IRQ (mestre em modo dual, ou único)
 */
static void adc_ch32v307_dma_isr(const struct device *dev)
{
    const struct adc_ch32v307_cfg *cfg = dev->config;
    struct adc_ch32v307_data *data = dev->data;
    uint32_t tcif = DMA1_TCIF(cfg->dma_channel);
    uint32_t htif = DMA1_HTIF(cfg->dma_channel);
    uint32_t isr  = DMA1_ISR;

    bool half = (isr & htif) != 0;
    bool full = (isr & tcif) != 0;

    if (!half && !full) {
        /* Pode ser TEIF ou GIF espúrio (reset suave deixa TEIF ativo no DMA).
         * Sem limpar, o PFIC re-despacha a ISR imediatamente ao retornar —
         * tempestade de interrupções que trava o CPU antes de main(). */
        DMA1_IFCR = DMA1_TCIF(cfg->dma_channel) | DMA1_HTIF(cfg->dma_channel)
                  | DMA1_TEIF(cfg->dma_channel);
        return;
    }
    /* Limpa os flags atendidos */
    DMA1_IFCR = (half ? htif : 0U) | (full ? tcif : 0U);

    if (adc_dual_is_paired() && dev == g_adc1_dev) {
        /* Mestre em modo dual: 1 word por canal da sequência —
         *   bits [15: 0] = código bruto ADC1 (canal i)
         *   bits [31:16] = código bruto ADC2 (canal i)
         *
         * Ping-pong: half-transfer → metade estável = words 0..nch-1
         *            transfer-complete → metade estável = words nch..2*nch-1
         * Se as duas flags chegam juntas a ISR perdeu um frame (dado novo
         * está na segunda metade). */
        struct adc_ch32v307_data *sdata = g_adc2_dev->data;
        /* Cada metade do ping-pong tem ch_count × BATCH_FRAMES words. */
        uint16_t half_len = (uint16_t)(data->ch_count * CH32_ADC_BATCH_FRAMES);
        const uint32_t *frame = full ? &g_dual_dma_buf[half_len]
                                     : &g_dual_dma_buf[0];

        if (half && full) {
            data->dropped_frames++;
        }
        data->seq++;

        /* ── Callback de frame ────────────────────────────────────────────
         * Entregamos o ponteiro para o início da metade estável + tamanho
         * total em words (ch_count × BATCH_FRAMES). O HAL copia esse bloco
         * para um buffer próprio e processa na work queue, sem bloquear a ISR. */
        if (data->frame_cb) {
            data->frame_cb(frame, (uint8_t)half_len,
                           data->cal_offset, sdata->cal_offset,
                           data->frame_cb_user);
            return;
        }

        /* ── Caminho legado (semáforo / adc_read bloqueante) ─────────────
         * Mantido para compatibilidade com native_sim e testes unitários
         * que usam a API padrão Zephyr (adc_read). */
        for (int i = 0; i < data->ch_count; i++) {
            uint32_t w = frame[i];
            if (data->user_buf) {
                data->user_buf[i] =
                    adc_apply_cal((uint16_t)(w & 0x0FFFU), data->cal_offset);
            }
            if (sdata->user_buf) {
                sdata->user_buf[i] =
                    adc_apply_cal((uint16_t)((w >> 16) & 0x0FFFU), sdata->cal_offset);
            }
        }

        if (data->async_sig) {
            k_poll_signal_raise(data->async_sig, 0);
            data->async_sig = NULL;
        } else {
            k_sem_give(&data->sem_sync);
        }
        return;
    }

    /* Instância única/independente (caso da SIBA, ou qualquer instância
     * fora de um par dual) — comportamento de sempre. */
    if (data->user_buf) {
        for (int i = 0; i < data->ch_count; i++) {
            data->user_buf[i] = adc_apply_cal(data->dma_buf[i] & 0x0FFFU,
                                              data->cal_offset);
        }
    }
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

    /* PPRE2 e ADCPRE são compartilhados pelas duas instâncias; a segunda a
     * inicializar só reafirma o mesmo valor. */
    uint32_t cfgr0 = RCC_CFGR0;

#if CH32_ADC_LOWER_APB2
    cfgr0 = (cfgr0 & ~RCC_CFGR0_PPRE2_MASK) | RCC_CFGR0_PPRE2_DIV2;
#endif
    cfgr0 = (cfgr0 & ~RCC_CFGR0_ADCPRE_MASK) | RCC_CFGR0_ADCPRE;
    RCC_CFGR0 = cfgr0;
}

/**
 * @brief Configura o canal DMA1 em modo circular, 16 bits (modo independente).
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
 * @brief Igual a dma_init(), mas pro word combinado de 32 bits do modo dual
 * (ADC1_RDATAR traz {ADC2[31:16], ADC1[15:0]}). Só chamada pelo mestre (ADC1).
 * @param cfg config do mestre (ADC1)
 * @param buf buffer de destino (uint32_t por canal)
 * @param count número de canais na sequência
 */
static void dma_init32(const struct adc_ch32v307_cfg *cfg, uint32_t *buf, uint16_t count)
{
    uint32_t chb = cfg->dma_ch_base;

    DMA_CH_REG(chb, DMA_CH_OFF_CFGR) &= ~DMA_CFGR_EN;

    DMA_CH_REG(chb, DMA_CH_OFF_PADDR) = cfg->base + ADC_OFF_RDATAR;
    DMA_CH_REG(chb, DMA_CH_OFF_MADDR) = (uint32_t)buf;
    /* 2 × count × BATCH_FRAMES: cada metade contém BATCH_FRAMES frames
     * completos (ch_count words cada). ISR dispara a cada BATCH_FRAMES frames;
     * a metade oposta é a estável que será copiada para o trabalho na fila. */
    DMA_CH_REG(chb, DMA_CH_OFF_CNTR)  = (uint32_t)count * 2U * CH32_ADC_BATCH_FRAMES;

    DMA_CH_REG(chb, DMA_CH_OFF_CFGR) =
                    DMA_CFGR_TCIE       |
                    DMA_CFGR_HTIE       |   /* metade -> 1º frame pronto */
                    DMA_CFGR_CIRC       |
                    DMA_CFGR_MINC       |
                    DMA_CFGR_PSIZE_32   |
                    DMA_CFGR_MSIZE_32   |
                    DMA_CFGR_PL_HIGH;

    DMA_CH_REG(chb, DMA_CH_OFF_CFGR) |= DMA_CFGR_EN;
}

/**
 * @brief Executa a sequência de calibração de hardware do ADC (RSTCAL+CAL).
 * @param base endereço base do periférico (ADC1_BASE ou ADC2_BASE)
 */
static void adc_busy_delay(int loops)
{
    for (volatile int i = 0; i < loops; i++) {
        __asm__ volatile("nop");
    }
}

/**
 * @brief Uma passada de autocalibração de hardware (RSTCAL -> CAL).
 * @param base endereço base do periférico
 * @return o código de calibração, que o HW deixa em RDATAR (§12.2.2, item 4)
 */
static uint16_t adc_calibrate_once(uint32_t base)
{
    ADC_REG(base, ADC_OFF_CTLR2) |= ADC_CTLR2_RSTCAL;
    while (ADC_REG(base, ADC_OFF_CTLR2) & ADC_CTLR2_RSTCAL) {
        /* HW limpa ao concluir */
    }
    ADC_REG(base, ADC_OFF_CTLR2) |= ADC_CTLR2_CAL;
    while (ADC_REG(base, ADC_OFF_CTLR2) & ADC_CTLR2_CAL) {
        /* HW limpa ao concluir */
    }
    return (uint16_t)(ADC_REG(base, ADC_OFF_RDATAR) & 0x0FFFU);
}

/**
 * @brief Calibra UM ADC e devolve o offset residual, em LSB com sinal.
 *
 * Réplica de Get_CalibrationValue() da SDK oficial da WCH
 * (EVT/EXAM/SRC/Peripheral/src/ch32v30x_adc.c): roda a autocalibração
 * DEZ vezes, ordena os códigos, descarta os 2 menores e os 2 maiores e tira
 * a média dos 6 do meio — média aparada, robusta a outlier. O offset é a
 * distância do resultado até o meio de escala (2048).
 *
 * Por que isso importa aqui: a autocalibração de hardware sozinha NÃO zera o
 * erro do banco de capacitores; sobra um resíduo que o próprio fabricante
 * corrige em software em TODAS as amostras. Num nó de medição elétrica esse
 * resíduo não é cosmético — ele é comum às três fases, atravessa a Clarke
 * como componente de sequência zero e vira BEMF fantasma no SMO.
 *
 * PRÉ-CONDIÇÕES (§12.2.2 item 4 e §12.3.2 do RM):
 *  - ADON = 1 há mais de 2 ciclos de ADCCLK;
 *  - BUFEN = 0 (com TKENABLE/TSVREFE ligados o buffer não pode ser
 *    desligado, então a calibração tem de vir antes deles);
 *  - nenhuma conversão em curso (senão o RSTCAL leva ciclos extras) e o DMA
 *    ainda NÃO habilitado — a leitura de RDATAR pelo DMA roubaria o código.
 *
 * @param base endereço base do periférico (ADC1_BASE ou ADC2_BASE)
 * @return offset a somar em cada amostra (2048 − mediana dos códigos)
 */
static int16_t adc_get_calibration_value(uint32_t base)
{
    uint16_t buf[10];

    for (int i = 0; i < 10; i++) {
        buf[i] = adc_calibrate_once(base);
    }

    /* Ordena (bubble sort — 10 elementos, roda uma vez no boot) */
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 9; j++) {
            if (buf[j] > buf[j + 1]) {
                uint16_t t = buf[j];
                buf[j] = buf[j + 1];
                buf[j + 1] = t;
            }
        }
    }

    /* Média dos 6 centrais (descarta 2 de cada ponta), com arredondamento */
    uint32_t t = 0;
    for (int i = 0; i < 6; i++) {
        t += buf[i + 2];
    }
    t = (t / 6U) + ((t % 6U) / 3U);

    return (int16_t)(2048 - (int16_t)t);
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
        /* LIMPA o campo antes de gravar. A versão anterior só fazia |= com
         * 0x7, o que funcionava por acidente (0x7 tem todos os bits) mas
         * tornava impossível programar qualquer valor MENOR — o campo ficava
         * preso em 239,5 ciclos para sempre. */
        if (ch <= 9) {
            smp2 &= ~(0x7U << (ch * 3));
            smp2 |=  ((uint32_t)CH32_ADC_SMP << (ch * 3));
        } else if (ch <= 17) {
            smp1 &= ~(0x7U << ((ch - 10) * 3));
            smp1 |=  ((uint32_t)CH32_ADC_SMP << ((ch - 10) * 3));
        }
    }
    ADC_REG(base, ADC_OFF_SAMPTR2) = smp2;
    ADC_REG(base, ADC_OFF_SAMPTR1) = smp1;
}

/**
 * @brief Re-arma o par dual inteiro (DMA/DUALMOD/trigger/calibração);
 * chamada quando a sequência de um dos dois lados muda.
 */
static void adc_arm_dual_master(void)
{
    const struct adc_ch32v307_cfg *mcfg = g_adc1_dev->config;
    const struct adc_ch32v307_cfg *scfg = g_adc2_dev->config;
    struct adc_ch32v307_data *mdata = g_adc1_dev->data;
    struct adc_ch32v307_data *sdata = g_adc2_dev->data;

    /* O RM exige que ambos os ADCs tenham o mesmo número de canais na sequência
     * em modo simultâneo (L igual nos RSQRx). Além disso, ch_count=0 causaria
     * underflow em adc_set_sequence: (uint8_t)(0-1)=255 → L=15 → ADC2 tentaria
     * 16 conversões de canal indefinido. Retorna aqui; o arme acontece
     * automaticamente na última chamada de adc_channel_setup quando os dois
     * lados tiverem a mesma contagem. */
    if (mdata->ch_count == 0 || sdata->ch_count == 0 ||
        mdata->ch_count != sdata->ch_count) {
        return;
    }

    /* Ordem seguida abaixo = a do exemplo oficial da WCH
     * (EVT/EXAM/ADC/DualADC_RegSimul): modo dual e bit de DMA primeiro,
     * ADON, buffer desligado, calibração, e só então o canal de DMA é
     * habilitado. O canal de DMA vem por último de propósito: a calibração
     * lê o código em RDATAR, e um DMA já ativo consumiria esse valor antes
     * (o próprio RM avisa que a leitura de RDATAR pelo DMA limpa o EOC). */

    /* --- 1. Tudo parado -------------------------------------------------- */
    DMA_CH_REG(mcfg->dma_ch_base, DMA_CH_OFF_CFGR) &= ~DMA_CFGR_EN;
    ADC_REG(mcfg->base, ADC_OFF_CTLR2) &= ~ADC_CTLR2_ADON;
    ADC_REG(scfg->base, ADC_OFF_CTLR2) &= ~ADC_CTLR2_ADON;

    /* --- 2. CTLR1: modo dual + scan, BUFEN/PGA desligados ---------------- *
     * DUALMOD só é interpretado no CTLR1 do ADC1 (é reservado no ADC2) e o
     * RM exige alterá-lo com o modo dual desabilitado — que é o caso, já que
     * acabamos de zerar ADON nos dois.
     *
     * BUFEN = 0 é PRÉ-CONDIÇÃO da calibração (§12.3.2): com TKENABLE ou
     * TSVREFE ligados o buffer não pode mais ser desligado, então a
     * calibração tem de vir antes deles. Se um dia o PGA for usado nos
     * shunts, lembrar que BUFEN=1 pede sampling >= 7,5T — o default daqui
     * (28,5T) já satisfaz. */
    uint32_t ctlr1 = ADC_REG(mcfg->base, ADC_OFF_CTLR1);
    ctlr1 &= ~(ADC_CTLR1_DUALMOD_MASK | ADC_CTLR1_BUFEN | ADC_CTLR1_PGA_MASK);
    ctlr1 |= ADC_CTLR1_DUALMOD_REGSIMUL | ADC_CTLR1_SCAN;
    ADC_REG(mcfg->base, ADC_OFF_CTLR1) = ctlr1;

    ADC_REG(scfg->base, ADC_OFF_CTLR1) =
        (ADC_REG(scfg->base, ADC_OFF_CTLR1) & ~(ADC_CTLR1_BUFEN | ADC_CTLR1_PGA_MASK))
        | ADC_CTLR1_SCAN;

    /* --- 3. CTLR2 sem ADON ---------------------------------------------- *
     * ALIGN = 0 (direita). EXTTRIG=1 + EXTSEL=111 (SWSTART) nos DOIS: o RM
     * é explícito que, em modo dual com disparo externo, o trigger tem de
     * estar habilitado no mestre E no escravo, com o escravo em software
     * trigger, "to prevent spurious triggers to start unwanted slave
     * conversion" (§12.2.7). O escravo NÃO recebe o bit DMA: só o ADC1 tem
     * linha de DMA, e é pelo RDATAR dele que o dado do ADC2 sai. */
    ADC_REG(mcfg->base, ADC_OFF_CTLR2) = ADC_CTLR2_CONT | ADC_CTLR2_DMA |
                                         ADC_CTLR2_EXTTRIG | (0x7UL << 17);
    ADC_REG(scfg->base, ADC_OFF_CTLR2) = ADC_CTLR2_CONT |
                                         ADC_CTLR2_EXTTRIG | (0x7UL << 17);

    /* --- 4. ADON sozinho, e espera tSTAB + >= 2 ADCCLK ------------------- *
     * "When only ADON changes in the register, a conversion will be started.
     * If any other bits are sent to change, a new conversion will not be
     * started" (§12.3.3) — daí a config acima ir num acesso com ADON = 0.
     * Aqui ADON vai de 0 para 1, o que ACORDA o módulo (não inicia
     * conversão); a conversão começa no SWSTART do passo 6. */
    ADC_REG(mcfg->base, ADC_OFF_CTLR2) |= ADC_CTLR2_ADON;
    ADC_REG(scfg->base, ADC_OFF_CTLR2) |= ADC_CTLR2_ADON;
    adc_busy_delay(2000);   /* tSTAB, com folga */

    /* --- 5. Calibração dos dois, ANTES de o canal de DMA subir -----------
     * UMA vez por ADC: o guarda de contagem acima faz esta função só rodar
     * quando ambos os lados têm o mesmo ch_count, o que acontece apenas na
     * última chamada de adc_channel_setup — portanto uma calibração no boot. */
    if (!mdata->calibrated) {
        mdata->cal_offset = adc_get_calibration_value(mcfg->base);
        mdata->calibrated = true;
    }
    if (!sdata->calibrated) {
        sdata->cal_offset = adc_get_calibration_value(scfg->base);
        sdata->calibrated = true;
        LOG_INF("Calibracao HW: offset ADC1=%d LSB, ADC2=%d LSB",
                (int)mdata->cal_offset, (int)sdata->cal_offset);
    }

    /* --- 6. Agora sim o DMA combinado de 32 bits (ping-pong) ------------- */
    dma_init32(mcfg, g_dual_dma_buf, mdata->ch_count);
    mdata->seq = 0;
    mdata->dropped_frames = 0;

    /* --- 7. Dispara a conversão contínua (só no mestre; o escravo é
     * acionado junto, em hardware, pelo trigger simultâneo) -------------- */
    ADC_REG(mcfg->base, ADC_OFF_CTLR2) |= ADC_CTLR2_SWSTART;

    uint32_t frame_ns = CH32_ADC_FRAME_NS(mdata->ch_count);

    LOG_INF("ADC dual armado: ADCCLK=%u Hz SMP=%u %u canais -> frame=%u ns "
            "(%u kSps/canal)",
            (unsigned)CH32_ADCCLK_HZ, (unsigned)CH32_ADC_SMP,
            (unsigned)mdata->ch_count, (unsigned)frame_ns,
            (unsigned)(1000000U / frame_ns));

    /* O ADC está fora da spec? O limite do periférico é 14 MHz e o divisor
     * máximo desta família é /8 — com PCLK2 = 144 MHz não dá para chegar lá
     * sem baixar o próprio PCLK2 (CH32_ADC_LOWER_APB2). */
    if (CH32_ADCCLK_HZ > 14000000U) {
        LOG_WRN("ADCCLK=%u Hz ACIMA do limite de 14 MHz do periferico "
                "(exatidao/linearidade degradadas). Divisor maximo e /8; "
                "para entrar em spec use CH32_ADC_LOWER_APB2=1.",
                (unsigned)CH32_ADCCLK_HZ);
    }

    /* O Ts do hardware bate com o que o observador assume? Se não bater, os
     * Γ do autotuning (que carregam Ts embutido) não descrevem este
     * observador — é erro de modelo, não de ganho, e não aparece de outra
     * forma. 2 % de tolerância. */
    if (frame_ns > OBS_TS_NS + OBS_TS_NS / 50U ||
        frame_ns < OBS_TS_NS - OBS_TS_NS / 50U) {
        LOG_WRN("Ts do ADC (%u ns) != OBS_TS do observador (%u ns): os Gamma "
                "otimizados assumem %u ns. Ajuste SMP/ADCCLK ou reotimize "
                "com Ts_sim = %u ns.",
                (unsigned)frame_ns, (unsigned)OBS_TS_NS,
                (unsigned)OBS_TS_NS, (unsigned)frame_ns);
    }
}

static void adc_hw_reconfigure(const struct device *dev);

/**
 * @brief API Zephyr channel_setup: adiciona um canal à sequência de scan
 * desta instância e reprograma o hardware.
 * @param dev instância do driver (ADC1 ou ADC2)
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
 * @brief Reprograma sequência/sample-time/DMA desta instância quando os
 * canais mudam; em modo dual, re-arma o par inteiro.
 * @param dev instância do driver (ADC1 ou ADC2)
 */
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

    /* Modo independente de sempre (SIBA, ou qualquer instância isolada).
     * Mesma sequência do caminho dual: CTLR1 com BUFEN/PGA off, CTLR2 sem
     * ADON, ADON sozinho, calibra, e só então o canal de DMA sobe. */
    ADC_REG(base, ADC_OFF_CTLR1) =
        (ADC_REG(base, ADC_OFF_CTLR1) & ~(ADC_CTLR1_BUFEN | ADC_CTLR1_PGA_MASK))
        | ADC_CTLR1_SCAN;

    ADC_REG(base, ADC_OFF_CTLR2) = ADC_CTLR2_CONT | ADC_CTLR2_DMA |
                                   ADC_CTLR2_EXTTRIG | (0x7UL << 17); /* EXTSEL=111: SWSTART */
    ADC_REG(base, ADC_OFF_CTLR2) |= ADC_CTLR2_ADON;   /* ADON sozinho */
    adc_busy_delay(2000);                              /* tSTAB */

    if (!data->calibrated) {          /* uma vez só — ver nota no caminho dual */
        data->cal_offset = adc_get_calibration_value(base);
        data->calibrated = true;
        LOG_INF("Calibracao HW (independente): offset=%d LSB", (int)data->cal_offset);
    }

    dma_init(cfg, data->dma_buf, data->ch_count);

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

    /* ESCRAVO do par dual: NÃO espera nada.
     *
     * Em modo dual simultâneo os dois resultados chegam no MESMO word de
     * DMA e a ISR do mestre já preencheu os dois user_buf. Quem chama faz
     *     adc_read(adc1);   // espera o frame N
     *     adc_read(adc2);   // <- aqui
     * e nesse ponto o buffer do escravo JÁ contém o frame N.
     *
     * A versão anterior fazia k_sem_reset() + k_sem_take() aqui, o que
     * descartava o semáforo do frame N e ficava esperando o N+1: o
     * observador consumia DOIS transfer-complete por passo e rodava na
     * METADE da taxa de conversão — com o Ts calculado pela constante,
     * não pelo hardware. Era o maior erro de Ts da cadeia.
     *
     * Único efeito residual: no primeiríssimo frame após o boot o user_buf
     * do escravo ainda não estava registrado quando a ISR rodou, então a
     * primeira leitura de corrente sai zerada. A calibração de offset
     * descarta os primeiros frames de qualquer forma. */
    if (adc_dual_is_paired() && dev == g_adc2_dev) {
        return 0;
    }

    /* O DMA circular já está rodando; o semáforo vem no próximo evento de
     * half-transfer ou transfer-complete (ver adc_ch32v307_dma_isr). */
    k_sem_reset(&data->sem_sync);
    int ret = k_sem_take(&data->sem_sync, K_MSEC(100));
    if (ret) {
        LOG_ERR("Timeout aguardando conversão ADC");
        return -ETIMEDOUT;
    }

    return 0;
}

/**
 * @brief Diagnóstico do stream de conversão (só faz sentido no mestre dual).
 * @param dev instância do driver
 * @param seq [out] frames entregues desde o boot (pode ser NULL)
 * @param dropped [out] frames perdidos por atraso da ISR (pode ser NULL)
 *
 * Não há flag de overrun nos canais regulares deste ADC: sem este contador,
 * um frame perdido é sobrescrito em silêncio e o observador recebe uma
 * amostra deslocada no tempo sem nenhum sinal de erro.
 */
void adc_ch32v307_get_stats(const struct device *dev, uint32_t *seq, uint32_t *dropped)
{
    const struct adc_ch32v307_data *data = dev->data;

    if (seq)     *seq     = data->seq;
    if (dropped) *dropped = data->dropped_frames;
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

    /* DMA circular → ISR (do mestre, em modo dual) sinalizará async quando
     * TC ocorrer. */
    return 0;
}
#endif /* CONFIG_ADC_ASYNC */

/**
 * @brief API Zephyr init: registra a instância (mestre/escravo dual), liga
 * o clock/ADC e conecta a IRQ do DMA (se esta instância tiver). Calibração
 * de verdade só ocorre depois, em adc_hw_reconfigure/adc_arm_dual_master.
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
    data->calibrated = false;
    data->cal_offset = 0;

    /* Registra este device como mestre (ADC1) ou escravo (ADC2) do par
     * dual, comparando só a base física. */
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

    /* Habilita a IRQ do canal DMA1, só se esta instância tiver DMA própria
     * (o escravo ADC2 recebe o resultado via ISR do mestre).
     * Limpa flags residuais ANTES de habilitar: após reset suave o canal pode
     * ter TEIF pendente; sem esse clear o PFIC dispararia a ISR imediatamente
     * ao sair de irq_enable e travaria em tempestade antes de main(). */
    if (cfg->has_dma) {
        DMA1_IFCR = DMA1_TCIF(cfg->dma_channel) | DMA1_HTIF(cfg->dma_channel)
                  | DMA1_TEIF(cfg->dma_channel);
        cfg->irq_config_func(dev);
    }

    data->initialized = true;
    LOG_INF("ADC CH32V307 inicializado (base=0x%08X, DMA1 canal %u%s)",
            cfg->base, cfg->dma_channel,
            cfg->has_dma ? "" : " -- escravo dual, sem DMA própria");
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

/* Instanciação via DTS: só quem tem `dmas = <&dma1 N>` ganha DMA/IRQ
 * próprios — o escravo dual (ADC2) não declara `dmas` de propósito. */
#define ADC_CH32V307_HAS_DMA(n)      DT_INST_NODE_HAS_PROP(n, dmas)
#define ADC_CH32V307_DMA_CHANNEL(n)  DT_INST_DMAS_CELL_BY_IDX(n, 0, channel)
/* IRQn do canal N de DMA1 = 26 + N (DMA1_CH1=27 .. DMA1_CH7=33) */
#define ADC_CH32V307_DMA_IRQN(n)     (26 + ADC_CH32V307_DMA_CHANNEL(n))

/**
 * @brief Gera (se a instância `n` tiver `dmas` no DTS) a função de conexão
 * da IRQ de DMA dessa instância: adc_ch32v307_irq_config_<n>(dev).
 * @param n índice da instância DTS
 */
#define ADC_CH32V307_IRQ_CONFIG_FN(n)                                          \
    IF_ENABLED(ADC_CH32V307_HAS_DMA(n), (                                     \
        static void adc_ch32v307_irq_config_##n(const struct device *dev)    \
        {                                                                     \
            IRQ_CONNECT(ADC_CH32V307_DMA_IRQN(n), 1,                         \
                        adc_ch32v307_dma_isr, DEVICE_DT_INST_GET(n), 0);      \
            irq_enable(ADC_CH32V307_DMA_IRQN(n));                            \
        }                                                                     \
    ))

/**
 * @brief Instancia o driver pra instância DTS `n`: dados/config + registro
 * no dispositivo Zephyr (DEVICE_DT_INST_DEFINE).
 * @param n índice da instância DTS
 */
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

/**
 * @brief Registra (ou remove) callback direto de frame para o ADC mestre.
 *
 * Quando @p cb != NULL, a ISR de DMA chama @p cb diretamente após cada
 * conversão, sem k_sem_give — eliminando o context-switch do caminho crítico.
 * Passar @p cb = NULL restaura o caminho legado (semáforo → adc_read).
 *
 * @param dev  Instância ADC1 (mestre do par dual; retorna imediatamente para
 *             ADC2 pois o escravo não tem ISR própria).
 * @param cb   Função chamada em ISR a cada frame DMA, ou NULL.
 * @param user Ponteiro opaco repassado a @p cb.
 */
void adc_ch32v307_set_frame_callback(const struct device *dev,
                                     adc_ch32v307_frame_cb_t cb,
                                     void *user)
{
    struct adc_ch32v307_data *data = dev->data;
    /* Escrita atômica: o compilador não pode reordenar as duas atribuições
     * acima de frame_cb — user_data deve estar visível antes do ponteiro
     * de função, senão a ISR pode chamar cb com user=NULL. */
    data->frame_cb_user = user;
    compiler_barrier();
    data->frame_cb = cb;
}
