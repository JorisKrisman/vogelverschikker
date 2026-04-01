#include "ble_task.h"
#include "SensorControl.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define LED_GPIO GPIO_NUM_1
#define MOTOR_GPIO GPIO_NUM_2

//Test gpio for action_task
// #define BUTTON_GPIO GPIO_NUM_0

static const char *TAG = "main";

/* Handle van de actietaak zodat BLE deze kan triggeren */
static TaskHandle_t actionTaskHandle = NULL;

/* Args moeten blijven bestaan nadat app_main klaar is */
static ble_args_t ble_args;

/* Voorbeeld actietaak */
void action_task(void *pvParameters)
{
    gpio_reset_pin(LED_GPIO); // LED OFF
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    gpio_reset_pin(MOTOR_GPIO); // MOTOR OFF
    gpio_set_direction(MOTOR_GPIO, GPIO_MODE_OUTPUT);

    while (true) {
        /* Wacht op trigger vanuit BLE of lokale detectie */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI("action_task", "Afschrikactie gestart");
        gpio_set_level(LED_GPIO, 1); // LED ON
        vTaskDelay(pdMS_TO_TICKS(500));

        gpio_set_level(LED_GPIO, 0); // LED OFF
        vTaskDelay(pdMS_TO_TICKS(500));

        gpio_set_level(MOTOR_GPIO, 1); // MOTOR ON

        /* Hier start je motor + geluid */
        vTaskDelay(pdMS_TO_TICKS(10000));

        ESP_LOGI("action_task", "Afschrikactie gestopt");
        while (ulTaskNotifyTake(pdTRUE, 0) > 0) {}
    }
}

// ISR voor de testen, deze zal de actietaak triggeren
// static void IRAM_ATTR button_isr_handler(void* arg)
// {
//     BaseType_t xHigherPriorityTaskWoken = pdFALSE;

//     if (actionTaskHandle != NULL) {
//         vTaskNotifyGiveFromISR(actionTaskHandle, &xHigherPriorityTaskWoken);
//     }

//     if (xHigherPriorityTaskWoken) {
//         portYIELD_FROM_ISR();
//     }
// }


extern "C" void app_main(void)
{
    QueueHandle_t motionQueue = xQueueCreate(1, sizeof(uint8_t));
    QueueHandle_t lightQueue  = xQueueCreate(1, sizeof(uint16_t));
    EventGroupHandle_t meshEventGroup = xEventGroupCreate();


    ESP_LOGI(TAG, "Start slimme vogelverschrikker");

    // // Test GPIO configureren voor de actietaak trigger 
    // gpio_config_t io_conf = {};
    // io_conf.intr_type = GPIO_INTR_NEGEDGE; 
    // io_conf.mode = GPIO_MODE_INPUT;
    // io_conf.pin_bit_mask = (1ULL << BUTTON_GPIO);
    // io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    // io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;

    // gpio_config(&io_conf);

    // gpio_install_isr_service(0);

    // // Koppel interrupt handler
    // gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);
    // // Einde test GPIO setup

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