#include "ble_task.h"
#include "SensorControl.h"
#include "esp_log.h"

static const char *TAG = "main";

/* Handle van de actietaak zodat BLE deze kan triggeren */
static TaskHandle_t actionTaskHandle = NULL;

/* Args moeten blijven bestaan nadat app_main klaar is */
static ble_args_t ble_args;

/* Voorbeeld actietaak */
void action_task(void *pvParameters)
{
    while (true) {
        /* Wacht op trigger vanuit BLE of lokale detectie */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI("action_task", "Afschrikactie gestart");

        /* Hier start je motor + geluid */
        vTaskDelay(pdMS_TO_TICKS(10000));

        ESP_LOGI("action_task", "Afschrikactie gestopt");
        while (ulTaskNotifyTake(pdTRUE, 0) > 0) {}
    }
}

extern "C" void app_main(void)
{
    QueueHandle_t motionQueue = xQueueCreate(1, sizeof(uint8_t));
    QueueHandle_t lightQueue  = xQueueCreate(1, sizeof(uint16_t));
    EventGroupHandle_t meshEventGroup = xEventGroupCreate();


    ESP_LOGI(TAG, "Start slimme vogelverschrikker");


    /* Eerst actietaak maken, zodat we de handle kunnen doorgeven */
    xTaskCreate(action_task,"action_task",4096,NULL,5,&actionTaskHandle);

    ble_args.ble_tx_queue = motionQueue;
    ble_args.mesh_event_group = meshEventGroup;
    ble_args.action_task_handle = actionTaskHandle;

    xTaskCreate(ble_task,"ble_task",6144,&ble_args,5,NULL);

    //starts the sensor tasks!!!!
    static SensorController sensors(GPIO_NUM_5, ADC1_CHANNEL_3, motionQueue, lightQueue, actionTaskHandle);
    sensors.init();


}