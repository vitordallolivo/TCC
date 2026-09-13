/**
 * @file NVM.c
 * @brief Persistência de calibração de sensores em EEPROM I2C.
 *
 * Portado da SAME com as seguintes diferenças:
 *   - Magic 0x53494241 ("SIBA") — EEPROM da SAME é incompatível (apagado)
 *   - Sem bloco de observador (SIBA não tem SMO)
 *   - 13 slots de sensor (CMD_CORE_SENSOR_COUNT), incluindo load cells (HX711)
 *   - apply_calibration despacha para HAL_HX711_Set_Calib (ids 0-5) e
 *     HAL_ADC_Set_Calib (ids 6,7,9,10,12); SOC (8) e RPM (11) são ignorados
 */
#include "NVM.h"
#include "CMD_Core_prm.h"   /* SIBA_SENSOR_TABLE, CMD_CORE_SENSOR_COUNT */
#include "Log_Manager.h"    /* SENSOR_ID_* */
#include "HAL.h"
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
#define NVM_ADDR_CAL     0x020U   /* 0x020..0x09F: 4 páginas (128 B) */

/* ─── Identificação ───────────────────────────────────────────────────────── */
#define NVM_MAGIC    0x53494241U   /* "SIBA" */
#define SAME_MAGIC   0x53414D45U   /* "SAME" — firmware anterior no mesmo EEPROM */
#define NVM_VERSION  1U

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
    uint8_t  _reserved[18];
} nvm_header_t;

_Static_assert(sizeof(nvm_header_t) == 32, "nvm_header_t != 32 bytes");

/* ─── Bloco de calibração (4 páginas = 128 bytes) ────────────────────────── */
typedef struct __attribute__((packed)) {
    nvm_sensor_t sensors[NVM_SENSOR_COUNT];   /* 9 B × 13 = 117 B */
    uint8_t      _pad[11];
} nvm_cal_block_t;

_Static_assert(sizeof(nvm_cal_block_t) == 128, "nvm_cal_block_t != 128 bytes");

/* ─── Estado do módulo ────────────────────────────────────────────────────── */
static const struct device *s_i2c_dev;
static uint8_t              s_i2c_addr;
static uint32_t             s_write_count;
static nvm_cal_block_t      s_cal;

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

static int write_header(uint32_t cal_crc)
{
    nvm_header_t hdr = { 0 };
    hdr.magic       = NVM_MAGIC;
    hdr.version     = NVM_VERSION;
    hdr.write_count = s_write_count;
    hdr.cal_crc     = cal_crc;
    return eeprom_write(NVM_ADDR_HEADER, &hdr, sizeof(hdr));
}

/* ─── Defaults ────────────────────────────────────────────────────────────── */

static void fill_cal_defaults(void)
{
    for (int i = 0; i < NVM_SENSOR_COUNT; i++) {
        s_cal.sensors[i].id     = s_cal_defs[i].id;
        s_cal.sensors[i].linear = s_cal_defs[i].linear;
        s_cal.sensors[i].offset = s_cal_defs[i].offset;
    }
}

/* ─── Aplicação dos valores restaurados ──────────────────────────────────── */

static void apply_calibration(void)
{
    for (int i = 0; i < NVM_SENSOR_COUNT; i++) {
        uint8_t id = s_cal.sensors[i].id;
        float   l  = s_cal.sensors[i].linear;
        float   o  = s_cal.sensors[i].offset;

        if (id <= SENSOR_ID_LOAD_CELL_5) {
            /* Load cell: linear = scale, offset = tare_raw */
            HAL_HX711_Set_Calib(id, l, o);
        } else if (id == SENSOR_ID_SOC || id == SENSOR_ID_RPM) {
            /* sem calibração aplicável */
        } else {
            HAL_ADC_Set_Calib(id, l, o);
        }
    }
}

static void report_nondefault(void)
{
    for (int i = 0; i < NVM_SENSOR_COUNT; i++) {
        float dl = s_cal_defs[i].linear;
        float do_ = s_cal_defs[i].offset;
        float l = s_cal.sensors[i].linear;
        float o = s_cal.sensors[i].offset;
        if (fabsf(l - dl) > 1e-9f || fabsf(o - do_) > 1e-9f) {
            printk("[NVM] sensor %u: linear=%.7f offset=%.5f"
                   "  (default: %.7f / %.5f)\n",
                   i, (double)l, (double)o, (double)dl, (double)do_);
        }
    }
}

/* ─── API pública ─────────────────────────────────────────────────────────── */

int NVM_Init(const struct device *i2c_dev, uint8_t i2c_addr)
{
    s_i2c_dev   = i2c_dev;
    s_i2c_addr  = i2c_addr;
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
        /* Migração SAME→SIBA: preserva write_count acumulado para não perder
         * o histórico de desgaste do EEPROM (mesmo chip, firmware diferente). */
        if (hdr.magic == SAME_MAGIC) {
            s_write_count = hdr.write_count;
            LOG_INF("NVM: firmware anterior SAME — write_count=%u preservado",
                    s_write_count);
        } else {
            s_write_count = 0U;
        }
        LOG_INF("NVM: EEPROM vazio/incompativel — salvando defaults de fabrica");
        fill_cal_defaults();
        return NVM_Save_Calibration();
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
        LOG_WRN("NVM: CRC invalido — mantendo defaults de HAL");
        fill_cal_defaults();
    }

    return 0;
}

int NVM_Save_Calibration(void)
{
    uint32_t crc = crc32_ieee((const uint8_t *)&s_cal, sizeof(s_cal));
    int r = eeprom_write(NVM_ADDR_CAL, &s_cal, sizeof(s_cal));
    if (r != 0) { LOG_ERR("NVM: erro gravando calibracao (%d)", r); return r; }
    r = write_header(crc);
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

uint32_t NVM_Get_Write_Count(void)
{
    return s_write_count;
}
