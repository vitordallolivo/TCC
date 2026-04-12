#include "HAL.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>

/* --- LINHA QUE RESOLVE O ERRO --- */
LOG_MODULE_REGISTER(hal_adc, LOG_LEVEL_DBG);

#define ADC_NODE DT_NODELABEL(adc1)
static const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);

#define ADC_RESOLUTION  12
#define ADC_CHANNEL     0
#define BUFFER_SIZE     8 

static uint16_t adc_buffer[BUFFER_SIZE];
static uint16_t last_avg_value = 0;

/* Mantenha essas structs como static para que a thread possa acessá-las */
static const struct adc_channel_cfg ch_cfg = {
    .gain = ADC_GAIN_1,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id = ADC_CHANNEL,
};

static struct adc_sequence seq = {
    .channels = BIT(ADC_CHANNEL),
    .buffer = adc_buffer,
    .buffer_size = sizeof(adc_buffer),
    .resolution = ADC_RESOLUTION,
};

void HAL_thread_fn(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    
    struct k_poll_signal sig;
    struct k_poll_event evt;

    k_poll_signal_init(&sig);
    k_poll_event_init(&evt, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig);

    LOG_INF("HAL: Thread ADC iniciada via TCKEC.");

    while (1) {
        k_poll_signal_reset(&sig);
        
        // Inicia leitura assíncrona (usa DMA se configurado no DT)
        if (adc_read_async(adc_dev, &seq, &sig) == 0) {
            
            k_poll(&evt, 1, K_FOREVER);

            // Calcula a média para suavizar os dados da célula de carga
            uint32_t sum = 0;
            for(int i = 0; i < BUFFER_SIZE; i++) {
                sum += adc_buffer[i];
            }
            last_avg_value = (uint16_t)(sum / BUFFER_SIZE);
        }
        
    }
}

int HAL_ADC_Init(void) {
    if (!device_is_ready(adc_dev)) {
        LOG_ERR("Dispositivo ADC não pronto!");
        return -ENODEV;
    }
    return adc_channel_setup(adc_dev, &ch_cfg);
}

uint16_t HAL_ADC_Get_Last_Value(void) {
    return last_avg_value;
}