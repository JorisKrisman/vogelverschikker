#include "ble_task.h"
#include "SensorControl.h"
#include "esp_log.h"

static const char *TAG = "main";




void app_main(void)
{
    QueueHandle_t motionQueue = xQueueCreate(10, sizeof(uint8_t));
    QueueHandle_t lightQueue  = xQueueCreate(10, sizeof(uint16_t));
    EventGroupHandle_t meshEventGroup = xEventGroupCreate();


    ble_args_t ble_args = {
        .ble_tx_queue = motionQueue,
        .ble_rx_queue = lightQueue,
        .mesh_event_group = meshEventGroup
    };
    static SensorController sensors(GPIO_NUM_4, ADC1_CHANNEL_6, motionQueue, lightQueue);

    ESP_LOGI(TAG, "Start slimme vogelverschrikker");

    sensors.init();

    xTaskCreate(ble_task, "ble_task", 4096, &ble_args, 5, NULL);
}
