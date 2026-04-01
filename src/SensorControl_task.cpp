#include "SensorControl.h"
#include "esp_log.h"
#include "esp_sleep.h"

#define THRESHOLD 1000
#define WAKEUP_TIME_SECONDS 1 //wake up after 

static const char* TAG = "SensorController";

SensorController::SensorController(gpio_num_t motionPin,adc1_channel_t ldrChannel,QueueHandle_t motionQueue,QueueHandle_t lightQueue, TaskHandle_t actionTaskHandle)
{
    this->motionPin = motionPin;
    this->ldrChannel = ldrChannel;
    this->motionQueue = motionQueue;
    this->lightQueue = lightQueue;
    this->actionTaskHandle = actionTaskHandle;
    this->systemActive = true;//system starts active by default.
}

void SensorController::init()
{
    //Motion sensor
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << motionPin);
    io_conf.mode = GPIO_MODE_INPUT;//input only
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;//keep the pin low when no motion is detected
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    //LDR
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ldrChannel, ADC_ATTEN_DB_12);

    ESP_LOGI(TAG, "Sensors initialized");

    //Create tasks
    xTaskCreate(motionTask, "motionTask", 4096, this, 5, NULL);
    xTaskCreate(ldrTask, "ldrTask", 4096, this, 5, NULL);
}


void SensorController::enterSleepMode()
{
    esp_sleep_enable_timer_wakeup(WAKEUP_TIME_SECONDS*1000000);
    ESP_LOGI(TAG, "Entering sleep mode for 10 minutes");
    esp_deep_sleep_start();
}

void SensorController::checkTreshold(uint16_t lightValue)
{
    if (lightValue < THRESHOLD) //go to sleep when it's dark
    {
        ESP_LOGI(TAG, "Light below threshold, entering light sleep for 1 minute");
        this->systemActive = false;


        esp_sleep_enable_timer_wakeup(10000000);//wake up after 10 seconds, can be adjusted as needed.

        //enter sleepmode
        esp_light_sleep_start();

        ESP_LOGI(TAG, "Woke up from light sleep");
        this->systemActive = true;
        return;
    }

    if (lightValue > THRESHOLD) //if its bright, turn on
    {
        ESP_LOGI(TAG, "Light above threshold, waking up");
        this->systemActive = true;
    }
}



void SensorController::motionTask(void* arg)
{
    SensorController* self = static_cast<SensorController*>(arg);

    while (true) {
        uint8_t motion = gpio_get_level(self->motionPin);
        ESP_LOGI(TAG, "Motion: %d", motion);
        xQueueOverwrite(self->motionQueue, &motion);
        if(motion)
        {
            xTaskNotifyGive(self->actionTaskHandle);//Trigger action task on motion detection
        }


        vTaskDelay(pdMS_TO_TICKS(500)); 
    }
}

void SensorController::ldrTask(void* arg)
{
    SensorController* self = static_cast<SensorController*>(arg);

    while (true) {
        uint16_t light = adc1_get_raw(self->ldrChannel);
        ESP_LOGI(TAG, "Light: %d", light);
        self->checkTreshold(light);
        xQueueOverwrite(self->lightQueue, &light);


        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}