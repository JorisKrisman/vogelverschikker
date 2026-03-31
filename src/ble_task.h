#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_err.h"
#include "esp_ble_mesh_defs.h"



#define MESH_CONNECTED_BIT      BIT0
#define MESH_FAIL_BIT           BIT1


typedef enum {
    BLE_CMD_NONE = 0,
    BLE_CMD_PUBLISH_BIRD_DETECTION,
    BLE_CMD_SEND_HEALTH,
    BLE_CMD_SLEEP,
    BLE_CMD_WAKE
} ble_cmd_type_t;

typedef enum {
    BLE_RX_NONE = 0,
    BLE_RX_BIRD_DETECTED,
    BLE_RX_GENERIC_ONOFF_SET,
    BLE_RX_HEALTH_REQUEST
} ble_rx_type_t;

typedef struct {
    ble_cmd_type_t type;
    uint32_t timestamp;
    uint8_t value;
} ble_cmd_t;

typedef struct {
    ble_rx_type_t type;
    uint16_t src_addr;
    uint32_t timestamp;
    uint8_t value;
} ble_rx_msg_t;

typedef struct {
    QueueHandle_t ble_tx_queue;
    QueueHandle_t ble_rx_queue;
    EventGroupHandle_t mesh_event_group;
} ble_args_t;

extern EventGroupHandle_t s_ble_mesh_event_group;

void ble_init(void);
void ble_task(void *arg);

