#include "ble_task.h"

#include <string.h>


#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "esp_mac.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_health_model_api.h"
#include "esp_ble_mesh_sensor_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"

#define CID_ESP 0x02E5
#define STATUS_INTERVAL_MS          (60 * 1000)

static const char *TAG = "ble_task";

EventGroupHandle_t s_ble_mesh_event_group = NULL;//
static ble_args_t *g_ble_args = NULL;

//Device UUID voor provisioning
static uint8_t dev_uuid[16] = {0};

/* =========================================================
 * Queue/status helpers
 * ========================================================= */


//this function is used to send a messages internally when a message is received via BLE.
static void ble_message_rx(ble_rx_type_t type, uint16_t src_addr, uint8_t value)
{
    if (g_ble_args->ble_rx_queue == NULL) {//check if the queue exists before sending the message
        return;
    }
    ble_rx_msg_t msg = {//message struct
        .type = type,//type of message(see h file for types)
        .src_addr = src_addr,//source address of the message
        .timestamp = 0,
        .value = value//value of message
    };
    xQueueSend(g_ble_args->ble_rx_queue, &msg, 0);
}

/* =========================================================
 * BLE Mesh composition
 * ========================================================= */


/* ---------- Configuration Server ---------- */
static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = 7,
};

/* ---------- Health Server ---------- */
ESP_BLE_MESH_HEALTH_PUB_DEFINE(health_pub, 8, ROLE_NODE);

static uint8_t test_ids[1] = { 0x00 };

static esp_ble_mesh_health_srv_t health_server = {
    .health_test = {
        .id_count = 1,
        .test_ids = test_ids,
    },
};//////stil need to edit. this is only a declaration of the health server. you need to implement the actual health status and faults according to your application.


/* ---------- Sensor Server ---------- */
/*
 * Hier publiceer je detectiestatus.
 * In een echte implementatie moet je sensor states / descriptors
 * verder uitwerken volgens de ESP-IDF sensor model API.
 */
static esp_ble_mesh_sensor_state_t sensor_states[1];//we have only 1 sensor state. this makes the array size 1.
sensor_states[0].sensor_property_id = 0x004E;//motion detected property id.


ESP_BLE_MESH_MODEL_PUB_DEFINE(sensor_pub, 32, ROLE_NODE);//publication struct for the sensor server. this model is allowed to send messages, and here is the buffer/config it will use to do that.

static esp_ble_mesh_sensor_srv_t sensor_server = {
    .state_count = 1,//we have only 1 sensor state.
    .states = sensor_states,//pointer to the sensor array.
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
//run this every time 
uint8_t value = 1;//temp value for the sensor state.    
sensor_states[0].sensor_data = &value;
esp_ble_mesh_model_publish(&sensor_pub.model);



/* ---------- Generic OnOff Server ---------- */
static esp_ble_mesh_gen_onoff_srv_t onoff_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(gen_onoff_pub, 8, ROLE_NODE);

/* ---------- Models per element ---------- */

/* Primary element */
static esp_ble_mesh_model_t primary_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&health_server, &health_pub),
};

/* Detection element */
static esp_ble_mesh_model_t detection_models[] = {
    ESP_BLE_MESH_MODEL_SENSOR_SRV(&sensor_pub, &sensor_server),
};

/* Speaker/actuator element */
static esp_ble_mesh_model_t actuator_models[] = {
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&gen_onoff_pub, &onoff_server),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, primary_models, ESP_BLE_MESH_MODEL_NONE),
    ESP_BLE_MESH_ELEMENT(0, detection_models, ESP_BLE_MESH_MODEL_NONE),
    ESP_BLE_MESH_ELEMENT(0, actuator_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
};

/* =========================================================
 * Callbacks
 * ========================================================= */

static void prov_cb(esp_ble_mesh_prov_cb_event_t event,
                    esp_ble_mesh_prov_cb_param_t *param)
{
    int 
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        if (param->prov_register_comp.err_code == ESP_OK) {
            ESP_LOGI(TAG, "BLE Mesh registratie gelukt");
        } else {
            ESP_LOGE(TAG, "BLE Mesh registratie fout: %d",
                     param->prov_register_comp.err_code);
            xEventGroupSetBits(s_ble_mesh_event_group, MESH_FAIL_BIT);
        }
        break;

    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "Provisioning enabled");
        break;

    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "Node geprovisioned, unicast addr: 0x%04X",param->node_prov_complete.addr);
        xEventGroupSetBits(s_ble_mesh_event_group,MESH_CONNECTED_BIT);
        break;

    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGW(TAG, "Provisioning reset");
        esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV |
                                      ESP_BLE_MESH_PROV_GATT);
        break;

    default:
        break;
    }
}

static void cfg_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                          esp_ble_mesh_cfg_server_cb_param_t *param)
{
if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");//a new appkey is added to the node.
            ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x",
                param->value.state_change.appkey_add.net_idx,
                param->value.state_change.appkey_add.app_idx);
            ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND://triggers when an appkey is bound to a model, which is necessary for the model to be able to publish/subscribe and send/receive messages.
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
            ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_app_bind.element_addr,
                param->value.state_change.mod_app_bind.app_idx,
                param->value.state_change.mod_app_bind.company_id,
                param->value.state_change.mod_app_bind.model_id);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD://
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD");
            ESP_LOGI(TAG, "elem_addr 0x%04x, sub_addr 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_sub_add.element_addr,
                param->value.state_change.mod_sub_add.sub_addr,
                param->value.state_change.mod_sub_add.company_id,
                param->value.state_change.mod_sub_add.model_id);
            break;
        default:
            break;
        }
    }
}

static void health_server_cb(esp_ble_mesh_health_server_cb_event_t event,
                             esp_ble_mesh_health_server_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_HEALTH_SERVER_ATTENTION_ON_EVT:
        ESP_LOGI(TAG, "Health attention on");
        break;

    case ESP_BLE_MESH_HEALTH_SERVER_ATTENTION_OFF_EVT:
        ESP_LOGI(TAG, "Health attention off");
        break;

    default:
        break;
    }
}

static void sensor_server_cb(esp_ble_mesh_sensor_server_cb_event_t event,
                             esp_ble_mesh_sensor_server_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_SENSOR_SERVER_STATE_CHANGE_EVT:
        ESP_LOGI(TAG, "Sensor state veranderd");//state of the sensor has changed.
        break;

    case ESP_BLE_MESH_SENSOR_SERVER_RECV_GET_MSG_EVT:
        ESP_LOGI(TAG, "Sensor GET ontvangen");
        break;
    default:
        break;
    }
}

/*
 * Generic OnOff callback:
 * als een andere node een OnOff SET publiceert,
 * kun jij daarop lokaal reageren.
 */
static void generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                              esp_ble_mesh_generic_server_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT:
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
            param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {

            uint8_t onoff = param->value.state_change.onoff_set.onoff;
            ESP_LOGI(TAG, "Generic OnOff ontvangen: %d", onoff);

            if (onoff) {
                ble_message_rx(BLE_RX_GENERIC_ONOFF_SET, param->ctx.addr, onoff);
            }
        }
        break;

    default:
        break;
    }
}

/* =========================================================
 * Init
 * ========================================================= */

void ble_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    memset(dev_uuid, 0, 16);
    ESP_ERROR_CHECK(esp_read_mac(&dev_uuid[10], ESP_MAC_BT));

    if (s_ble_mesh_event_group == NULL) {
        s_ble_mesh_event_group = xEventGroupCreate();
    }

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
}

static void ble_mesh_register_callbacks(void)
{
    ESP_ERROR_CHECK(esp_ble_mesh_register_prov_callback(prov_cb));
    ESP_ERROR_CHECK(esp_ble_mesh_register_config_server_callback(cfg_server_cb));
    ESP_ERROR_CHECK(esp_ble_mesh_register_health_server_callback(health_server_cb));
    ESP_ERROR_CHECK(esp_ble_mesh_register_sensor_server_callback(sensor_server_cb));
    ESP_ERROR_CHECK(esp_ble_mesh_register_generic_server_callback(generic_server_cb));
}

static void ble_mesh_start(void)
{
    ble_mesh_register_callbacks();

    ESP_ERROR_CHECK(esp_ble_mesh_init(&provision, &composition));
    ESP_ERROR_CHECK(esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV |
                                                  ESP_BLE_MESH_PROV_GATT));
}

/* =========================================================
 * Publish helpers
 * ========================================================= */

/*
 * Deze functie is bewust simpel gehouden.
 * In een echte Sensor Server implementatie publiceer je een echte sensor state.
 * Hier laat ik de structuur zien waarin je dat doet.
 */
static esp_err_t publish_bird_detected(void)
{
    ESP_LOGI(TAG, "Bird detectie publiceren via Sensor Server");

    /*
     * TODO:
     * - vul sensor state in
     * - gebruik de juiste ESP-IDF sensor API call
     * - publiceer motion detectie als sensorwaarde/status
     */

    return ESP_OK;
}

static esp_err_t publish_health_status(void)
{
    ESP_LOGI(TAG, "Health/status update publiceren");

    /*
     * TODO:
     * - faults/status invullen
     * - optioneel health publication gebruiken
     */

    return ESP_OK;
}

/* =========================================================
 * Task
 * ========================================================= */

void ble_task(void *arg)
{
    ble_args_t *arg_ptrs = (ble_args_t *)arg;
    g_ble_args = arg_ptrs;

    if (arg_ptrs->mesh_event_group != NULL) {
        s_ble_mesh_event_group = arg_ptrs->mesh_event_group;
    } else if (s_ble_mesh_event_group == NULL) {
        s_ble_mesh_event_group = xEventGroupCreate();
    }


    ble_init();
    ble_mesh_start();

    EventBits_t bits = xEventGroupWaitBits(
        s_ble_mesh_event_group,
        MESH_CONNECTED_BIT | MESH_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    while (1) {
        if (bits & MESH_FAIL_BIT) {
            ESP_LOGE(TAG, "Mesh start mislukt");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ble_cmd_t cmd;
        if (xQueueReceive(arg_ptrs->ble_tx_queue, &cmd, pdMS_TO_TICKS(500)) == pdTRUE) {
            switch (cmd.type) {
            case BLE_CMD_PUBLISH_BIRD_DETECTION:
                if (xEventGroupGetBits(s_ble_mesh_event_group) & MESH_CONNECTED_BIT) {
                    if (publish_bird_detected() != ESP_OK) {
                        ESP_LOGE(TAG, "Bird detectie publish mislukt");
                    } else {
                        ble_message_rx(BLE_RX_BIRD_DETECTED, 0x0000, 1);
                    }
                }
                break;

            case BLE_CMD_SEND_HEALTH:
                if (xEventGroupGetBits(s_ble_mesh_event_group) & MESH_CONNECTED_BIT) {
                    if (publish_health_status() != ESP_OK) {
                        ESP_LOGE(TAG, "Health publish mislukt");
                    }
                }
                break;

            case BLE_CMD_SLEEP:
                ESP_LOGI(TAG, "BLE task in low activity mode");
                break;

            case BLE_CMD_WAKE:
                ESP_LOGI(TAG, "BLE task terug actief");
                break;

            default:
                break;
            }
        }

        static TickType_t last_health = 0;
        TickType_t now = xTaskGetTickCount();

        if ((now - last_health) >= pdMS_TO_TICKS(STATUS_INTERVAL_MS)) {
            if (xEventGroupGetBits(s_ble_mesh_event_group) & MESH_CONNECTED_BIT) {
                publish_health_status();
            }
            last_health = now;
        }
    }
}