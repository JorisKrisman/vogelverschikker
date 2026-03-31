#include "SensorController.h"
#include "esp_log.h"

#define S1 GPIO_NUM_4
#define LDR_CHANNEL ADC1_CHANNEL_6

static const char* TAG = "SensorController";

SensorController::SensorController(gpio_num_t motionPin,adc1_channel_t ldrChannel,QueueHandle_t motionQueue,QueueHandle_t lightQueue)
{
    this->motionPin = motionPin;
    this->ldrChannel = ldrChannel;
    this->motionQueue = motionQueue;
    this->lightQueue = lightQueue;
}

void SensorController::init()
{
    //Motion sensor
    gpio_set_direction(motionPin, GPIO_MODE_INPUT);

    //LDR
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ldrChannel, ADC_ATTEN_DB_11);

    ESP_LOGI(TAG, "Sensors initialized");

    //Create tasks
    xTaskCreate(motionTask, "motionTask", 2048, this, 5, NULL);
    xTaskCreate(ldrTask, "ldrTask", 2048, this, 5, NULL);
}

void SensorController::motionTask(void* arg)
{
    SensorController* self = static_cast<SensorController*>(arg);

    while (true) {
        uint8_t motion = gpio_get_level(self->motionPin);

        xQueueSend(self->motionQueue, &motion, 0);

        ESP_LOGI(TAG, "Motion: %d", motion);

        vTaskDelay(pdMS_TO_TICKS(500)); // faster response
    }
}

void SensorController::ldrTask(void* arg)
{
    SensorController* self = static_cast<SensorController*>(arg);

    while (true) {
        uint16_t light = adc1_get_raw(self->ldrChannel);

        xQueueSend(self->lightQueue, &light, 0);

        ESP_LOGI(TAG, "Light: %d", light);

        vTaskDelay(pdMS_TO_TICKS(1000)); //slower sampling
    }
}