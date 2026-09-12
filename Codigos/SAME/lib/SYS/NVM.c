/**
 * @file NVM.c
 * @brief Persistência de calibração de sensores e parâmetros do observador em
 * EEPROM I2C (FT24C32A-ETR-T, 4 KB, páginas de 32 bytes).
 *
 * Política de inicialização:
 *   - EEPROM vazio (primeira execução): salva os defaults de fábrica
 *     (SAME_SENSOR_TABLE + Observer_Init) como ponto de partida.
 *     NÃO modifica HAL nem Observer (eles já têm os defaults).
 *   - EEPROM válido: restaura e aplica os valores que o usuário salvou via NCI.
 *     Se algum valor diferir do default, imprime no log para visibilidade.
 *
 * API genérica: i2c_dev e i2c_addr passados em NVM_Init() → reutilizável na SIBA.
 */
#include "NVM.h"
#include "CMD_Core_prm.h"      /* SAME_SENSOR_TABLE — defaults de calibração */
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
#define NVM_ADDR_CAL     0x020U
#define NVM_ADDR_OBS     0x060U

/* ─── Identificação ───────────────────────────────────────────────────────── */
#define NVM_MAGIC    0x53414D45U   /* "SAME" */
#define NVM_VERSION  1U

/* ─── Defaults de calibração de sensor (de CMD_Core_prm.h) ───────────────── */
typedef struct { uint8_t id; float linear; float offset; } nvm_cal_def_t;
static const nvm_cal_def_t s_cal_defs[NVM_SENSOR_COUNT] = {
#define X(TID, DEF_LIN, DEF_OFF, MINV, MAXV, DEF_ZERO, LAW) \
    { (uint8_t)(TID), (DEF_LIN), (DEF_OFF) },
    SAME_SENSOR_TABLE(X)
#undef X
};

/* ─── Header (1 página = 32 bytes) ───────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  _pad0;
    uint32_t write_count;   /* ciclos totais de gravação de página */
    uint32_t cal_crc;
    uint32_t obs_crc;
    uint8_t  _reserved[14];
} nvm_header_t;

_Static_assert(sizeof(nvm_header_t) == 32, "nvm_header_t != 32 bytes");

/* ─── Bloco de calibração (2 páginas = 64 bytes) ─────────────────────────── */
typedef struct __attribute__((packed)) {
    nvm_sensor_t sensors[NVM_SENSOR_COUNT]; /* 9 B × 6 = 54 B */
    uint8_t      _pad[10];
} nvm_cal_block_t;

_Static_assert(sizeof(nvm_cal_block_t) == 64, "nvm_cal_block_t != 64 bytes");

/* ─── Bloco do observador (2 páginas = 64 bytes) ─────────────────────────── */
typedef struct __attribute__((packed)) {
    nvm_param_t params[NVM_OBS_PARAM_COUNT]; /* 5 B × 10 = 50 B */
    uint8_t     _pad[14];
} nvm_obs_block_t;

_Static_assert(sizeof(nvm_obs_block_t) == 64, "nvm_obs_block_t != 64 bytes");

/* ─── Estado do módulo ────────────────────────────────────────────────────── */
static const struct device *s_i2c_dev;
static uint8_t              s_i2c_addr;
static uint32_t             s_write_count;

static nvm_cal_block_t s_cal;
static nvm_obs_block_t s_obs;

static const target_type OBS_TARGETS[NVM_OBS_PARAM_COUNT] = {
    TARGET_POLES_PAIRS,        TARGET_RESISTANCE,         TARGET_INDUCTANCE,
    TARGET_KV_RATING,          TARGET_GAMMA1,             TARGET_GAMMA2,
    TARGET_GAMMAT,             TARGET_BEMF_LPF_FREQUENCY,
    TARGET_TORQUE_LPF_FREQUENCY, TARGET_TS_ADC,
};

/* ─── CRC32 IEEE 802.3 (sem dependência de biblioteca) ───────────────────── */
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

static int write_header(uint32_t cal_crc, uint32_t obs_crc)
{
    nvm_header_t hdr = { 0 };
    hdr.magic       = NVM_MAGIC;
    hdr.version     = NVM_VERSION;
    hdr.write_count = s_write_count;
    hdr.cal_crc     = cal_crc;
    hdr.obs_crc     = obs_crc;
    return eeprom_write(NVM_ADDR_HEADER, &hdr, sizeof(hdr));
}

static void read_header_crcs(uint32_t *cal_crc, uint32_t *obs_crc)
{
    nvm_header_t hdr;
    *cal_crc = 0U; *obs_crc = 0U;
    if (eeprom_read(NVM_ADDR_HEADER, &hdr, sizeof(hdr)) == 0 &&
        hdr.magic == NVM_MAGIC) {
        *cal_crc = hdr.cal_crc;
        *obs_crc = hdr.obs_crc;
    }
}

/* ─── Inicialização dos caches com defaults de fábrica ───────────────────── */

static void fill_cal_defaults(void)
{
    for (int i = 0; i < NVM_SENSOR_COUNT; i++) {
        s_cal.sensors[i].id     = s_cal_defs[i].id;
        s_cal.sensors[i].linear = s_cal_defs[i].linear;
        s_cal.sensors[i].offset = s_cal_defs[i].offset;
    }
}

static void fill_obs_defaults(void)
{
    for (int i = 0; i < NVM_OBS_PARAM_COUNT; i++) {
        float v = 0.0f;
        Observer_Get_Param(OBS_TARGETS[i], &v);
        s_obs.params[i].id    = (uint8_t)OBS_TARGETS[i];
        s_obs.params[i].value = v;
    }
}

/* ─── Aplicação dos valores carregados ───────────────────────────────────── */

static void apply_calibration(void)
{
    for (int i = 0; i < NVM_SENSOR_COUNT; i++) {
        HAL_ADC_Set_Calib(s_cal.sensors[i].id,
                          s_cal.sensors[i].linear,
                          s_cal.sensors[i].offset);
    }
}

static void apply_observer(void)
{
    for (int i = 0; i < NVM_OBS_PARAM_COUNT; i++) {
        Observer_Set_Param((target_type)s_obs.params[i].id,
                           s_obs.params[i].value);
    }
}

/* Imprime sensores cujos valores diferem do default de fábrica. */
static void report_nondefault_cal(void)
{
    for (int i = 0; i < NVM_SENSOR_COUNT; i++) {
        float dl = s_cal_defs[i].linear;
        float doff = s_cal_defs[i].offset;
        float ll = s_cal.sensors[i].linear;
        float loff = s_cal.sensors[i].offset;
        if (fabsf(ll - dl) > 1e-9f || fabsf(loff - doff) > 1e-9f) {
            printk("[NVM] sensor %u: linear=%.7f offset=%.5f  "
                   "(default: linear=%.7f offset=%.5f)\n",
                   s_cal.sensors[i].id,
                   (double)ll, (double)loff,
                   (double)dl, (double)doff);
        }
    }
}

static void report_nondefault_obs(const float def_vals[NVM_OBS_PARAM_COUNT])
{
    for (int i = 0; i < NVM_OBS_PARAM_COUNT; i++) {
        float lv = s_obs.params[i].value;
        float dv = def_vals[i];
        if (fabsf(lv - dv) > 1e-9f) {
            printk("[NVM] obs target %u: %.7f  (default: %.7f)\n",
                   s_obs.params[i].id, (double)lv, (double)dv);
        }
    }
}

/* ─── API pública ─────────────────────────────────────────────────────────── */

int NVM_Init(const struct device *i2c_dev, uint8_t i2c_addr)
{
    s_i2c_dev  = i2c_dev;
    s_i2c_addr = i2c_addr;
    s_write_count = 0U;

    if (!device_is_ready(s_i2c_dev)) {
        LOG_ERR("NVM: I2C nao pronto");
        return -ENODEV;
    }
    uint8_t probe;
    if (i2c_read(s_i2c_dev, &probe, 1U, s_i2c_addr) != 0) {
        LOG_ERR("NVM: EEPROM 0x%02X nao responde", s_i2c_addr);
        return -ENODEV;
    }
    LOG_INF("NVM: EEPROM 0x%02X OK", s_i2c_addr);

    nvm_header_t hdr;
    if (eeprom_read(NVM_ADDR_HEADER, &hdr, sizeof(hdr)) != 0) {
        LOG_ERR("NVM: falha ao ler header");
        return -EIO;
    }

    if (hdr.magic != NVM_MAGIC || hdr.version != NVM_VERSION) {
        /* Primeira execução: salva defaults sem tocar em HAL nem Observer. */
        LOG_INF("NVM: EEPROM vazio — salvando defaults de fabrica");
        s_write_count = 0U;
        fill_cal_defaults();
        fill_obs_defaults();
        NVM_Save_Calibration();
        NVM_Save_Observer();
        return 0;
    }

    s_write_count = hdr.write_count;
    LOG_INF("NVM: write_count=%u", s_write_count);

    /* Captura defaults do observador ANTES de aplicar valores do EEPROM,
     * para poder comparar e reportar o que mudou. */
    float obs_defaults[NVM_OBS_PARAM_COUNT];
    for (int i = 0; i < NVM_OBS_PARAM_COUNT; i++) {
        obs_defaults[i] = 0.0f;
        Observer_Get_Param(OBS_TARGETS[i], &obs_defaults[i]);
    }

    /* Calibração */
    nvm_cal_block_t tmp_cal;
    if (eeprom_read(NVM_ADDR_CAL, &tmp_cal, sizeof(tmp_cal)) == 0 &&
        crc32_ieee((const uint8_t *)&tmp_cal, sizeof(tmp_cal)) == hdr.cal_crc) {
        memcpy(&s_cal, &tmp_cal, sizeof(tmp_cal));
        apply_calibration();
        LOG_INF("NVM: calibracao restaurada");
        report_nondefault_cal();
    } else {
        LOG_WRN("NVM: CRC de calibracao invalido — mantendo defaults");
        fill_cal_defaults();
    }

    /* Parâmetros do observador */
    nvm_obs_block_t tmp_obs;
    if (eeprom_read(NVM_ADDR_OBS, &tmp_obs, sizeof(tmp_obs)) == 0 &&
        crc32_ieee((const uint8_t *)&tmp_obs, sizeof(tmp_obs)) == hdr.obs_crc) {
        memcpy(&s_obs, &tmp_obs, sizeof(tmp_obs));
        apply_observer();
        LOG_INF("NVM: parametros do observador restaurados");
        report_nondefault_obs(obs_defaults);
    } else {
        LOG_WRN("NVM: CRC do observador invalido — mantendo defaults");
        fill_obs_defaults();
    }

    return 0;
}

int NVM_Save_Calibration(void)
{
    uint32_t cal_crc = crc32_ieee((const uint8_t *)&s_cal, sizeof(s_cal));
    uint32_t cal_old, obs_crc;
    read_header_crcs(&cal_old, &obs_crc);

    int r = eeprom_write(NVM_ADDR_CAL, &s_cal, sizeof(s_cal));
    if (r != 0) { LOG_ERR("NVM: erro gravando calibracao (%d)", r); return r; }

    r = write_header(cal_crc, obs_crc);
    if (r != 0) LOG_ERR("NVM: erro gravando header (%d)", r);
    return r;
}

int NVM_Save_Observer(void)
{
    uint32_t obs_crc = crc32_ieee((const uint8_t *)&s_obs, sizeof(s_obs));
    uint32_t cal_crc, obs_old;
    read_header_crcs(&cal_crc, &obs_old);

    int r = eeprom_write(NVM_ADDR_OBS, &s_obs, sizeof(s_obs));
    if (r != 0) { LOG_ERR("NVM: erro gravando observador (%d)", r); return r; }

    r = write_header(cal_crc, obs_crc);
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
    for (int i = 0; i < NVM_OBS_PARAM_COUNT; i++) {
        if ((target_type)s_obs.params[i].id == target) {
            s_obs.params[i].value = value;
            return;
        }
    }
}

uint32_t NVM_Get_Write_Count(void)
{
    return s_write_count;
}
