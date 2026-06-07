#include "Battery_Manager.h"
#include "OSSI.h"
#include "Security_Manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(battery_mgr, LOG_LEVEL_DBG);

/* Relé de potência — ajuste o GPIO conforme esquemático */
#define RELAY_GPIO_NODE DT_ALIAS(relay0)
static const struct gpio_dt_spec relay_gpio =
    GPIO_DT_SPEC_GET_OR(RELAY_GPIO_NODE, gpios, {0});

/* Polinômio SOC: SOC = p4*V^4 + p3*V^3 + p2*V^2 + p1*V + p0
 * Coeficientes padrão para LiPo 6S (3.3V-4.2V/célula) */
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

static float eval_soc(float v)
{
    /* Tensão por célula (6S) */
    float vc = v / 6.0f;
    float soc = poly[0]
              + poly[1] * vc
              + poly[2] * vc * vc
              + poly[3] * vc * vc * vc
              + poly[4] * vc * vc * vc * vc;

    if (soc < 0.0f)   soc = 0.0f;
    if (soc > 100.0f) soc = 100.0f;
    return soc;
}

void Battery_Manager_Init(void)
{
    if (relay_gpio.port && gpio_is_ready_dt(&relay_gpio)) {
        gpio_pin_configure_dt(&relay_gpio, GPIO_OUTPUT_INACTIVE);
        LOG_INF("Battery: relé configurado");
    } else {
        LOG_WRN("Battery: relé GPIO não disponível");
    }
    LOG_INF("Battery_Manager inicializado");
}

void Battery_Manager_Update(uint32_t sensor_id, float value)
{
    if (sensor_id == SENSOR_ID_V_BATT) {
        v_batt = value;
        float soc = eval_soc(v_batt);

        ossi_msg_t msg = { .target = TARGET_SYS,
                           .req_type = REQ_TYPE_SYS_UPDATE_SENSOR };
        msg.payload.sys_req.sensor_id = SENSOR_ID_SOC;
        msg.payload.sys_req.value     = soc;
        OSSI_Send(&msg);

        LOG_DBG("Battery: V=%.2f SOC=%.1f%%", (double)v_batt, (double)soc);
    }

    if (sensor_id == SENSOR_ID_I_BATT) {
        i_batt = value;
    }

    if (sensor_id == SENSOR_ID_TEMPERATURE_BATTERY) {
        if (value > temp_limit_c) {
            LOG_ERR("Battery ALARME: temp=%.1f°C > limite=%.1f°C",
                    (double)value, (double)temp_limit_c);
            Security_Manager_Trigger_Emergency();
        }
    }
}

void Battery_Manager_Set_Poly(uint8_t idx, float coeff)
{
    if (idx > 4) return;
    poly[idx] = coeff;
    LOG_INF("Battery: poly[%d]=%.4f", idx, (double)coeff);
}

void Battery_Manager_Set_Temp_Limit(float limit_c)
{
    temp_limit_c = limit_c;
    LOG_INF("Battery: temp_limit=%.1f°C", (double)limit_c);
}

void Battery_Manager_Set_Relay(bool on)
{
    if (!relay_gpio.port) return;
    gpio_pin_set_dt(&relay_gpio, on ? 1 : 0);
    LOG_INF("Battery: relé %s", on ? "ON" : "OFF");
}