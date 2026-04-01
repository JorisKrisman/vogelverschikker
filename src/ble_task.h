#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_err.h"


#ifdef __cplusplus
extern "C" {
#endif

#define MESH_CONNECTED_BIT BIT0
#define MESH_FAIL_BIT BIT1


typedef struct {
    QueueHandle_t ble_tx_queue;
    TaskHandle_t action_task_handle;
    EventGroupHandle_t mesh_event_group;
} ble_args_t;

extern EventGroupHandle_t s_ble_mesh_event_group;

void ble_init(void);
void ble_task(void *arg);

#ifdef __cplusplus
}
#endif