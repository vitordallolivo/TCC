/**
 * @file Battery_Manager.c
 * @brief SOC por polinômio, auto-detecção de nº de células, relé de
 * potência e limites de segurança da bateria.
 */
#include "Battery_Manager.h"
#include "Security_Manager.h"
#include "HAL.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(battery_mgr, LOG_LEVEL_DBG);

/* Polinômio SOC: SOC = p4*Vc^4 + p3*Vc^3 + p2*Vc^2 + p1*Vc + p0
 * (Vc = tensão por célula). Coeficientes padrão calibrados para
 * células LiPo (3.0V-4.2V/célula). */
static float poly[5] = {
    -307.0f,   /* p0 */
     281.7f,   /* p1 */
     -95.6f,   /* p2 */
      14.3f,   /* p3 */
      -0.8f,   /* p4 */
};

static float temp_limit_c = 60.0f;

static float v_batt = 0.0f;
static float i_batt = 0.0f;

static bool relay_state = false; /* espelha o último Set_Relay — ver Error_Detect(ERROR_BATT_RELAY_MISMATCH) */

/* Limiares de alarme, mais folgados que os limites "duros" do
 * Security_Manager: aqui é alerta cedo, lá é parada de emergência. */
#define BATTERY_LOW_SOC_THRESHOLD_PCT     20.0f  /* abaixo disso, alerta de SOC baixo  */
#define BATTERY_LOW_VOLTAGE_VC_THRESHOLD   3.5f  /* V/célula — abaixo disso, alerta    */
#define BATTERY_RELAY_MISMATCH_CURRENT_A   1.0f  /* corrente fluindo com relé OFF      */

/* Faixa "plausível" de tensão por célula para LiPo — usada para detectar
 * se o polinômio do SOC está produzindo valores absurdos (ver eval_soc) */
#define BATTERY_POLY_PLAUSIBLE_VC_MIN      2.5f
#define BATTERY_POLY_PLAUSIBLE_VC_MAX      4.3f
#define BATTERY_POLY_SANE_SOC_MIN        -20.0f
#define BATTERY_POLY_SANE_SOC_MAX        120.0f

/* Contagem de células: se o usuário não configurar via CMD, é auto-
 * detectada pela tensão do pack na lookup table abaixo (3.5-4.2V/célula). */
#define BATTERY_MAX_CELLS  12U

struct cell_count_lut_entry {
    float   v_min;   /* tensão mínima do pack nessa contagem de células */
    float   v_max;   /* tensão máxima do pack nessa contagem de células */
    uint8_t cells;
};

static const struct cell_count_lut_entry cell_count_lut[] = {
    {  3.5f,   4.2f,  1 },
    {  7.0f,   8.4f,  2 },
    { 10.5f,  12.6f,  3 },
    { 14.0f,  16.8f,  4 },
    { 17.5f,  21.0f,  5 },
    { 21.0f,  25.2f,  6 },
    { 24.5f,  29.4f,  7 },
    { 28.0f,  33.6f,  8 },
};

static uint8_t cell_count        = 6U;    /* valor corrente em uso          */
static bool    cell_count_is_set = false; /* true quando o usuário define   */

/**
 * @brief Detecta o nº de células cuja faixa de tensão "segura" contém v;
 * se nenhuma faixa bater, aproxima pelo nominal de 3.7V/célula.
 * @param v tensão do pack (V)
 * @return número de células estimado (1..BATTERY_MAX_CELLS)
 */
static uint8_t detect_cell_count(float v)
{
    for (size_t i = 0; i < ARRAY_SIZE(cell_count_lut); i++) {
        if (v >= cell_count_lut[i].v_min && v <= cell_count_lut[i].v_max) {
            return cell_count_lut[i].cells;
        }
    }

    uint32_t approx = (uint32_t)(v / 3.7f + 0.5f);
    if (approx < 1U) approx = 1U;
    if (approx > BATTERY_MAX_CELLS) approx = BATTERY_MAX_CELLS;
    return (uint8_t)approx;
}

/**
 * @brief Avalia o polinômio SOC(Vc) e satura em [0, 100].
 * @param vc tensão por célula (V)
 * @param soc_raw_out [out] valor antes do clamp, para teste de plausibilidade
 * @return SOC em % (0-100)
 */
static float eval_poly(float vc, float *soc_raw_out)
{
    float soc = poly[0]
              + poly[1] * vc
              + poly[2] * vc * vc
              + poly[3] * vc * vc * vc
              + poly[4] * vc * vc * vc * vc;

    if (soc_raw_out) {
        *soc_raw_out = soc;
    }

    if (soc < 0.0f)   soc = 0.0f;
    if (soc > 100.0f) soc = 100.0f;
    return soc;
}

/**
 * @brief Calcula o SOC a partir da tensão do pack: auto-detecta (ou usa) a
 * contagem de células, avalia o polinômio e sinaliza polinômio
 * desconfigurado / tensão por célula baixa.
 * @param v tensão do pack (V)
 * @return SOC em % (0-100)
 */
static float eval_soc(float v)
{
    if (!cell_count_is_set) {
        uint8_t detected = detect_cell_count(v);
        if (detected != cell_count) {
            cell_count = detected;
            LOG_INF("Battery: contagem de células auto-detectada = %uS", cell_count);
        }
    }

    float vc = v / (float)cell_count;
    float soc_raw;
    float soc = eval_poly(vc, &soc_raw);

    /* Polinômio desconfigurado: para uma tensão de célula plausível, o
     * resultado bruto (pré-clamp) sai bem fora de [0,100] — sintoma de
     * coeficientes errados (ex.: sobrescritos via CMD com valores ruins). */
    bool plausible_vc = (vc >= BATTERY_POLY_PLAUSIBLE_VC_MIN && vc <= BATTERY_POLY_PLAUSIBLE_VC_MAX);
    if (plausible_vc && (soc_raw < BATTERY_POLY_SANE_SOC_MIN || soc_raw > BATTERY_POLY_SANE_SOC_MAX)) {
        if (Security_Manager_Error_Detect(ERROR_BATT_POLY_MISCONFIGURED)) {
            LOG_ERR("Battery: polinomio do SOC desconfigurado (Vc=%.2fV -> soc_bruto=%.1f%%)",
                    (double)vc, (double)soc_raw);
        }
    } else {
        Security_Manager_Error_Erase(ERROR_BATT_POLY_MISCONFIGURED);
    }

    /* Tensão por célula baixa — alerta cedo, antes do limite "duro" do
     * Security_Manager (que dispararia parada de emergência). */
    if (vc < BATTERY_LOW_VOLTAGE_VC_THRESHOLD) {
        if (Security_Manager_Error_Detect(ERROR_BATT_LOW_VOLTAGE)) {
            LOG_WRN("Battery: tensão por célula baixa (Vc=%.2fV < %.2fV)",
                    (double)vc, (double)BATTERY_LOW_VOLTAGE_VC_THRESHOLD);
        }
    } else {
        Security_Manager_Error_Erase(ERROR_BATT_LOW_VOLTAGE);
    }

    return soc;
}

/** @brief Zera o estado do relé; o GPIO já foi configurado por HAL_Relay_Init(). */
void Battery_Manager_Init(void)
{
    relay_state = false;
    LOG_INF("Battery_Manager inicializado");
}

/**
 * @brief Processa uma atualização de sensor relevante à bateria
 * (v_batt/i_batt/temperatura), atualiza SOC e checa alarmes.
 * @param sensor_id SENSOR_ID_V_BATT, SENSOR_ID_I_BATT ou
 * SENSOR_ID_TEMPERATURE_BATTERY (outros são ignorados)
 * @param value valor do sensor
 */
void Battery_Manager_Update(uint32_t sensor_id, float value)
{
    if (sensor_id == SENSOR_ID_V_BATT) {
        v_batt = value;
        float soc = eval_soc(v_batt);

        if (soc < BATTERY_LOW_SOC_THRESHOLD_PCT) {
            if (Security_Manager_Error_Detect(ERROR_BATT_LOW_SOC)) {
                LOG_WRN("Battery: SOC baixo (%.1f%% < %.1f%%)",
                        (double)soc, (double)BATTERY_LOW_SOC_THRESHOLD_PCT);
            }
        } else {
            Security_Manager_Error_Erase(ERROR_BATT_LOW_SOC);
        }

        Log_Manager_Update(SENSOR_ID_SOC, soc);

        // LOG_DBG("Battery: V=%.2f SOC=%.1f%%", (double)v_batt, (double)soc);
    }

    if (sensor_id == SENSOR_ID_I_BATT) {
        i_batt = value;

        /* Corrente fluindo com o relé de potência desligado — indica
         * falha do próprio relé (contatos colados) ou leitura de corrente
         * incoerente com o estado comandado. */
        if (i_batt > BATTERY_RELAY_MISMATCH_CURRENT_A && !relay_state) {
            if (Security_Manager_Error_Detect(ERROR_BATT_RELAY_MISMATCH)) {
                LOG_ERR("Battery: corrente de %.1fA com rele desligado — possivel falha no rele",
                        (double)i_batt);
            }
        } else {
            Security_Manager_Error_Erase(ERROR_BATT_RELAY_MISMATCH);
        }
    }

    if (sensor_id == SENSOR_ID_TEMPERATURE_BATTERY) {
        if (value > temp_limit_c) {
            if (Security_Manager_Error_Detect(ERROR_BATT_OVER_TEMPERATURE)) {
                LOG_ERR("Battery ALARME: temp=%.1f°C > limite=%.1f°C",
                        (double)value, (double)temp_limit_c);
            }
            Security_Manager_Trigger_Emergency();
        } else {
            Security_Manager_Error_Erase(ERROR_BATT_OVER_TEMPERATURE);
        }
    }
}

/**
 * @brief Sobrescreve um coeficiente do polinômio SOC.
 * @param idx índice do coeficiente (0..4); fora do range é ignorado
 * @param coeff novo valor
 */
void Battery_Manager_Set_Poly(uint8_t idx, float coeff)
{
    if (idx > 4) return;
    poly[idx] = coeff;
    LOG_INF("Battery: poly[%d]=%.4f", idx, (double)coeff);
}

/**
 * @brief Sobrescreve o limite de temperatura que dispara emergência.
 * @param limit_c novo limite em °C
 */
void Battery_Manager_Set_Temp_Limit(float limit_c)
{
    temp_limit_c = limit_c;
    LOG_INF("Battery: temp_limit=%.1f°C", (double)limit_c);
}

/**
 * @brief Fixa manualmente o nº de células (0 = volta a auto-detectar).
 * @param cells número de células; 0 reativa auto-detecção; >BATTERY_MAX_CELLS
 * é ignorado
 */
void Battery_Manager_Set_Cell_Count(uint8_t cells)
{
    if (cells == 0U) {
        cell_count_is_set = false;
        LOG_INF("Battery: contagem de células voltou a ser auto-detectada");
        return;
    }

    if (cells > BATTERY_MAX_CELLS) {
        LOG_WRN("Battery: contagem de células %u inválida (max %u)",
                cells, BATTERY_MAX_CELLS);
        return;
    }

    cell_count        = cells;
    cell_count_is_set = true;
    LOG_INF("Battery: contagem de células fixada em %uS", cell_count);
}

/** @brief Contagem de células em uso (fixada ou auto-detectada). @return nº de células. */
uint8_t Battery_Manager_Get_Cell_Count(void)
{
    return cell_count;
}

/**
 * @brief Liga/desliga o relé de potência da bateria via HAL.
 * Energizar executa a sequência de pré-carga (precharge → 3 s → power),
 * bloqueando a thread chamadora durante esse tempo.
 * @param on true = energizar, false = desenergizar
 */
void Battery_Manager_Set_Relay(bool on)
{
    relay_state = on;
    if (on) {
        HAL_Relay_Energize();
        /* Relay energizado — bancada pronta para operar */
        Security_Manager_Error_Erase(ERROR_MCU_POWER_READY);
        LOG_INF("Battery: relé energizado (sequência de pré-carga concluída)");
    } else {
        HAL_Relay_Deenergize();
        /* Relay aberto — bancada não está pronta */
        Security_Manager_Error_Detect(ERROR_MCU_POWER_READY);
        LOG_INF("Battery: relé desenergizado");
    }
}

/** @brief Último estado comandado do relé. @return true = ligado. */
bool Battery_Manager_Get_Relay(void)
{
    return relay_state;
}