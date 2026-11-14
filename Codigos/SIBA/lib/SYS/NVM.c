/**
 * @file NVM.c
 * @brief Persistência de calibração de sensores e parâmetros do observador
 * em EEPROM I2C (FT24C32A, 4 KB, páginas de 32 B).
 *
 * Layout v2 (fases do motor e observador vindos da SAME):
 *   0x000  1 pág.  header: magic "SIBA", versão, write_count, CRC de cada bloco
 *   0x020  6 pág.  calibração: CMD_CORE_SENSOR_COUNT (19) × {id, linear, offset}
 *   0x0E0  2 pág.  observador: 10 × {target, valor}
 *
 * v1 (13 sensores em 4 páginas a partir de 0x020, sem observador) é migrada:
 * as 13 calibrações antigas são mantidas, o resto entra com default.
 *
 * Política: EEPROM vazio grava os defaults; válido restaura e aplica, e
 * imprime o que difere do default.
 */
#include "NVM.h"
#include "CMD_Core_prm.h"   /* SIBA_SENSOR_TABLE, CMD_CORE_SENSOR_COUNT */
#include "Log_Manager.h"    /* SENSOR_ID_* */
#include "HAL.h"
#include "Observer_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <math.h>

LOG_MODULE_REGISTER(nvm, LOG_LEVEL_INF);

/* ─── Geometria do EEPROM ─────────────────────────────────────────────────── */
#define NVM_PAGE_SIZE      32U
#define NVM_WRITE_CYCLE_MS  5U

/* ─── Endereços ───────────────────────────────────────────────────────────── */
#define NVM_ADDR_HEADER  0x000U
#define NVM_ADDR_CAL     0x020U   /* 0x020..0x0DF: 6 páginas (192 B) */
#define NVM_ADDR_OBS     0x0E0U   /* 0x0E0..0x11F: 2 páginas (64 B) */

/* ─── Identificação ───────────────────────────────────────────────────────── */
#define NVM_MAGIC    0x53494241U   /* "SIBA" */
#define SAME_MAGIC   0x53414D45U   /* "SAME" — firmware anterior no mesmo EEPROM */
#define NVM_VERSION  2U
#define NVM_V1_SENSOR_COUNT 13U

/* ─── Defaults de calibração (de CMD_Core_prm.h) ─────────────────────────── */
typedef struct { uint8_t id; float linear; float offset; } nvm_cal_def_t;
static const nvm_cal_def_t s_cal_defs[NVM_SENSOR_COUNT] = {
#define X(TID, DEF_LIN, DEF_OFF, MINV, MAXV, DEF_ZERO, LAW, ERRBIT) \
    [(TID)] = { (uint8_t)(TID), (DEF_LIN), (DEF_OFF) },
    SIBA_SENSOR_TABLE(X)
#undef X
};

/* ─── Header (1 página = 32 bytes) ───────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  _pad0;
    uint32_t write_count;
    uint32_t cal_crc;
    uint32_t obs_crc;       /* v2 */
    uint8_t  _reserved[14];
} nvm_header_t;

_Static_assert(sizeof(nvm_header_t) == 32, "nvm_header_t != 32 bytes");

/* ─── Bloco de calibração (6 páginas = 192 bytes) ────────────────────────── */
typedef struct __attribute__((packed)) {
    nvm_sensor_t sensors[NVM_SENSOR_COUNT];   /* 9 B × 19 = 171 B */
    uint8_t      _pad[192U - 9U * NVM_SENSOR_COUNT];
} nvm_cal_block_t;

_Static_assert(sizeof(nvm_cal_block_t) == 192, "nvm_cal_block_t != 192 bytes");

/* Bloco v1, só para a migração. */
typedef struct __attribute__((packed)) {
    nvm_sensor_t sensors[NVM_V1_SENSOR_COUNT];
    uint8_t      _pad[11];
} nvm_cal_block_v1_t;

_Static_assert(sizeof(nvm_cal_block_v1_t) == 128, "nvm_cal_block_v1_t != 128 bytes");

/* ─── Bloco do observador (2 páginas = 64 bytes) ─────────────────────────── */
typedef struct __attribute__((packed)) {
    nvm_param_t params[NVM_OBS_PARAM_COUNT];  /* 5 B × 10 = 50 B */
    /* CRC dos defaults do firmware quando o bloco foi gravado. Se o firmware
     * novo trouxer outros defaults (Γ do autotune, Ts do ADC), o bloco salvo
     * é descartado: senão os valores velhos da EEPROM venceriam os novos. */
    uint32_t    def_sig;
    uint8_t     _pad[10];
} nvm_obs_block_t;

_Static_assert(sizeof(nvm_obs_block_t) == 64, "nvm_obs_block_t != 64 bytes");

static const target_type OBS_TARGETS[NVM_OBS_PARAM_COUNT] = {
    TARGET_POLES_PAIRS,        TARGET_RESISTANCE,         TARGET_INDUCTANCE,
    TARGET_KV_RATING,          TARGET_GAMMA1,             TARGET_GAMMA2,
    TARGET_GAMMAT,             TARGET_BEMF_LPF_FREQUENCY,
    TARGET_TORQUE_LPF_FREQUENCY, TARGET_TS_ADC,
};

/* ─── Estado do módulo ────────────────────────────────────────────────────── */
static const struct device *s_i2c_dev;
static uint8_t              s_i2c_addr;
static uint32_t             s_write_count;
static bool                 s_ready;
static nvm_cal_block_t      s_cal;
static nvm_obs_block_t      s_obs;

/* ─── CRC32 IEEE 802.3 ────────────────────────────────────────────────────── */
static uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1U));
        }
    }
    return ~crc;
}

/* ─── Primitivas de I/O ───────────────────────────────────────────────────── */

static int eeprom_read(uint16_t addr, void *dst, uint16_t len)
{
    uint8_t reg[2] = { (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFFU) };
    return i2c_write_read(s_i2c_dev, s_i2c_addr, reg, sizeof(reg), dst, len);
}

static int eeprom_write_page(uint16_t addr, const void *src, uint8_t chunk)
{
    uint8_t buf[2U + NVM_PAGE_SIZE];
    buf[0] = (uint8_t)(addr >> 8);
    buf[1] = (uint8_t)(addr & 0xFFU);
    memcpy(&buf[2], src, chunk);
    int r = i2c_write(s_i2c_dev, buf, 2U + chunk, s_i2c_addr);
    if (r == 0) {
        k_msleep(NVM_WRITE_CYCLE_MS);
        s_write_count++;
    }
    return r;
}

static int eeprom_write(uint16_t addr, const void *data, uint16_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0U) {
        uint8_t page_off = (uint8_t)(addr % NVM_PAGE_SIZE);
        uint8_t chunk    = (uint8_t)(NVM_PAGE_SIZE - page_off);
        if (chunk > (uint8_t)len) chunk = (uint8_t)len;
        int r = eeprom_write_page(addr, p, chunk);
        if (r != 0) return r;
        addr += chunk; p += chunk; len -= chunk;
    }
    return 0;
}

/* ─── Header ──────────────────────────────────────────────────────────────── */

static int write_header(void)
{
    nvm_header_t hdr = { 0 };
    hdr.magic       = NVM_MAGIC;
    hdr.version     = NVM_VERSION;
    hdr.write_count = s_write_count;
    hdr.cal_crc     = crc32_ieee((const uint8_t *)&s_cal, sizeof(s_cal));
    hdr.obs_crc     = crc32_ieee((const uint8_t *)&s_obs, sizeof(s_obs));
    return eeprom_write(NVM_ADDR_HEADER, &hdr, sizeof(hdr));
}

/* ─── Defaults ────────────────────────────────────────────────────────────── */

static void fill_cal_defaults(void)
{
    memset(&s_cal, 0, sizeof(s_cal));
    for (int i = 0; i < (int)NVM_SENSOR_COUNT; i++) {
        s_cal.sensors[i].id     = s_cal_defs[i].id;
        s_cal.sensors[i].linear = s_cal_defs[i].linear;
        s_cal.sensors[i].offset = s_cal_defs[i].offset;
    }
}

/* Assinatura dos defaults do observador deste firmware (DEF_*), calculada
 * no NVM_Init, antes de qualquer valor da EEPROM ser aplicado. */
static uint32_t s_obs_def_sig;

/* Defaults do observador = o que o Observer_Init carregou (DEF_*). */
static void fill_obs_defaults(void)
{
    memset(&s_obs, 0, sizeof(s_obs));
    for (int i = 0; i < (int)NVM_OBS_PARAM_COUNT; i++) {
        float v = 0.0f;
        Observer_Get_Param(OBS_TARGETS[i], &v);
        s_obs.params[i].id    = (uint8_t)OBS_TARGETS[i];
        s_obs.params[i].value = v;
    }
    s_obs.def_sig = crc32_ieee((const uint8_t *)s_obs.params, sizeof(s_obs.params));
}

/* ─── Aplicação dos valores restaurados ──────────────────────────────────── */

static void apply_calibration(void)
{
    for (int i = 0; i < (int)NVM_SENSOR_COUNT; i++) {
        uint8_t id = s_cal.sensors[i].id;
        float   l  = s_cal.sensors[i].linear;
        float   o  = s_cal.sensors[i].offset;

        if (id <= SENSOR_ID_LOAD_CELL_5) {
            HAL_HX711_Set_Calib(id, l, o);   /* linear = scale, offset = tare */
        } else if (id == SENSOR_ID_SOC || id == SENSOR_ID_RPM) {
            /* sem calibração aplicável */
        } else {
            HAL_ADC_Set_Calib(id, l, o);     /* inclui as fases V_A..I_C */
        }
    }
}

static void apply_observer(void)
{
    for (int i = 0; i < (int)NVM_OBS_PARAM_COUNT; i++) {
        Observer_Set_Param((target_type)s_obs.params[i].id, s_obs.params[i].value);
    }
}

static void report_nondefault(void)
{
    for (int i = 0; i < (int)NVM_SENSOR_COUNT; i++) {
        float dl = s_cal_defs[i].linear, doff = s_cal_defs[i].offset;
        float l = s_cal.sensors[i].linear, o = s_cal.sensors[i].offset;
        if (fabsf(l - dl) > 1e-9f || fabsf(o - doff) > 1e-9f) {
            printk("[NVM] sensor %u: linear=%.7f offset=%.5f"
                   "  (default: %.7f / %.5f)\n",
                   i, (double)l, (double)o, (double)dl, (double)doff);
        }
    }
}

/* ─── API pública ─────────────────────────────────────────────────────────── */

int NVM_Init(const struct device *i2c_dev, uint8_t i2c_addr)
{
    s_i2c_dev     = i2c_dev;
    s_i2c_addr    = i2c_addr;
    s_write_count = 0U;
    s_ready       = false;

    /* Caches sempre com algo coerente, mesmo se o EEPROM falhar. */
    fill_cal_defaults();
    fill_obs_defaults();
    s_obs_def_sig = s_obs.def_sig;

    if (!device_is_ready(s_i2c_dev)) {
        LOG_ERR("NVM: I2C nao pronto");
        return -ENODEV;
    }
    uint8_t probe;
    if (i2c_read(s_i2c_dev, &probe, 1U, s_i2c_addr) != 0) {
        LOG_ERR("NVM: EEPROM 0x%02X nao responde", s_i2c_addr);
        return -ENODEV;
    }
    s_ready = true;
    LOG_INF("NVM: EEPROM 0x%02X OK", s_i2c_addr);

    nvm_header_t hdr;
    if (eeprom_read(NVM_ADDR_HEADER, &hdr, sizeof(hdr)) != 0) {
        LOG_ERR("NVM: falha ao ler header");
        return -EIO;
    }

    if (hdr.magic == NVM_MAGIC && hdr.version == 1U) {
        /* Migração v1 → v2: mantém as 13 calibrações antigas (células de
         * carga, bateria...), fases e observador entram com default. */
        s_write_count = hdr.write_count;
        nvm_cal_block_v1_t old;
        if (eeprom_read(NVM_ADDR_CAL, &old, sizeof(old)) == 0 &&
            crc32_ieee((const uint8_t *)&old, sizeof(old)) == hdr.cal_crc) {
            memcpy(s_cal.sensors, old.sensors, sizeof(old.sensors));
            apply_calibration();
            LOG_INF("NVM: layout v1 migrado para v2 (13 calibracoes mantidas)");
        } else {
            LOG_WRN("NVM: v1 com CRC invalido — gravando defaults");
        }
        int r = NVM_Save_Calibration();
        return r ? r : NVM_Save_Observer();
    }

    if (hdr.magic != NVM_MAGIC || hdr.version != NVM_VERSION) {
        /* Mesmo chip vindo da SAME: preserva o histórico de desgaste. */
        s_write_count = (hdr.magic == SAME_MAGIC) ? hdr.write_count : 0U;
        LOG_INF("NVM: EEPROM vazio/incompativel — salvando defaults de fabrica");
        int r = NVM_Save_Calibration();
        return r ? r : NVM_Save_Observer();
    }

    s_write_count = hdr.write_count;
    LOG_INF("NVM: write_count=%u", s_write_count);

    nvm_cal_block_t tmp;
    if (eeprom_read(NVM_ADDR_CAL, &tmp, sizeof(tmp)) == 0 &&
        crc32_ieee((const uint8_t *)&tmp, sizeof(tmp)) == hdr.cal_crc) {
        memcpy(&s_cal, &tmp, sizeof(tmp));
        apply_calibration();
        LOG_INF("NVM: calibracao restaurada");
        report_nondefault();
    } else {
        LOG_WRN("NVM: CRC da calibracao invalido — mantendo defaults");
        fill_cal_defaults();
    }

    nvm_obs_block_t otmp;
    if (eeprom_read(NVM_ADDR_OBS, &otmp, sizeof(otmp)) == 0 &&
        crc32_ieee((const uint8_t *)&otmp, sizeof(otmp)) == hdr.obs_crc) {
        if (otmp.def_sig == s_obs_def_sig) {
            memcpy(&s_obs, &otmp, sizeof(otmp));
            apply_observer();
            LOG_INF("NVM: parametros do observador restaurados");
        } else {
            /* Firmware com defaults novos: os da EEPROM são de outra versão. */
            LOG_WRN("NVM: defaults do observador mudaram no firmware — "
                    "descartando os salvos e gravando os novos");
            fill_obs_defaults();
            return NVM_Save_Observer();
        }
    } else {
        LOG_WRN("NVM: CRC do observador invalido — mantendo defaults");
        fill_obs_defaults();
    }

    return 0;
}

int NVM_Save_Calibration(void)
{
    if (!s_ready) return -ENODEV;
    int r = eeprom_write(NVM_ADDR_CAL, &s_cal, sizeof(s_cal));
    if (r != 0) { LOG_ERR("NVM: erro gravando calibracao (%d)", r); return r; }
    r = write_header();
    if (r != 0) LOG_ERR("NVM: erro gravando header (%d)", r);
    return r;
}

int NVM_Save_Observer(void)
{
    if (!s_ready) return -ENODEV;
    int r = eeprom_write(NVM_ADDR_OBS, &s_obs, sizeof(s_obs));
    if (r != 0) { LOG_ERR("NVM: erro gravando observador (%d)", r); return r; }
    r = write_header();
    if (r != 0) LOG_ERR("NVM: erro gravando header (%d)", r);
    return r;
}

void NVM_Set_Sensor_Calib(uint8_t sensor_id, float linear, float offset)
{
    if (sensor_id >= NVM_SENSOR_COUNT) return;
    s_cal.sensors[sensor_id].id     = sensor_id;
    s_cal.sensors[sensor_id].linear = linear;
    s_cal.sensors[sensor_id].offset = offset;
}

void NVM_Set_Observer_Param(target_type target, float value)
{
    for (int i = 0; i < (int)NVM_OBS_PARAM_COUNT; i++) {
        if (OBS_TARGETS[i] == target) {
            s_obs.params[i].id    = (uint8_t)target;
            s_obs.params[i].value = value;
            return;
        }
    }
}

uint32_t NVM_Get_Write_Count(void)
{
    return s_write_count;
}

void NVM_Get_Sensor_Calib(uint8_t sensor_id, float *linear, float *offset)
{
    if (sensor_id >= NVM_SENSOR_COUNT || !linear || !offset) {
        return;
    }
    *linear = s_cal.sensors[sensor_id].linear;
    *offset = s_cal.sensors[sensor_id].offset;
}
