#include "ble_task.h"

#include <stdbool.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_bt_main.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_provisioning_api.h"

#define TAG "ble_task"

#define CID_ESP 0x02E5//company identifier for Espressif Systems(by bluetooth SIG)
#define MOTION_QUEUE_WAIT_MS 1000//

EventGroupHandle_t s_ble_mesh_event_group = NULL;

static ble_args_t *g_ble_args = NULL;
static uint8_t dev_uuid[16] = {0};

static uint8_t g_motion_state = 0;
static esp_ble_mesh_client_t onoff_cli;

static uint16_t g_app_idx = ESP_BLE_MESH_KEY_UNUSED;
static uint16_t g_dst_addr = ESP_BLE_MESH_ADDR_UNASSIGNED;
static uint8_t  g_tid = 0;//




//config relay, GATT for provisioning by phone, beacon to find the node.
static esp_ble_mesh_cfg_srv_t cfg_srv = {
    /* 3 transmissions with 20ms interval */
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

//the first var is the name of the msg, second is length, last is the role of the device which is a node.
ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_pub, 4, ROLE_NODE);

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_cli_pub, 2 + 2, ROLE_NODE);

//this code automatically initializes the onoff_srv struct.
static esp_ble_mesh_gen_onoff_srv_t onoff_srv = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
};

//the root element of the node with all the models.
static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&cfg_srv),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_pub, &onoff_srv),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub, &onoff_cli),
};

//we only have one element which is the root element.
static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

//composition of the node, with the company ID, number of elements and the elements itself.
static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

//provisioning properties, with the device UUID which needs to be unique.
static esp_ble_mesh_prov_t provision = {//needs to be random
    .uuid = dev_uuid,
};



static esp_err_t ble_send_motion_state(uint8_t motion)
{
    if (g_app_idx == ESP_BLE_MESH_KEY_UNUSED) {//check if app key is bound to the model.
        ESP_LOGW(TAG, "Cannot send: app_idx not set yet");
        return ESP_FAIL;
    }

    if (g_dst_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {//check if subscription address is set for the model.
        ESP_LOGW(TAG, "Cannot send: destination address not set yet");
        return ESP_FAIL;
    }

    esp_ble_mesh_client_common_param_t common = {0};//message struct, acts as a container for the message we want to send.
    esp_ble_mesh_generic_client_set_state_t set = {0};

    common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK;//what kind of message we want to send.
    common.model = &root_models[2];//which model sends it
    common.ctx.net_idx = 0;//which network to send on, we only have one network so it's 0.
    common.ctx.app_idx = g_app_idx;//which app key to use(security required), we only have one app key so it's the one we stored when we got the model app bind event.
    common.ctx.addr = g_dst_addr;//which address to send to.(subscription address) 
    common.ctx.send_ttl = 3;//how many jumps the messages makes.
    common.msg_timeout = 0;//no response needed.

    set.onoff_set.op_en = false;//
    set.onoff_set.onoff = motion;//the state we want to send.
    set.onoff_set.tid = g_tid++;//transaction ID, needs to  be different for each message.

    esp_err_t err = esp_ble_mesh_generic_client_set_state(&common, &set);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SET_UNACK send failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Sent motion state %u to 0x%04x", motion ? 1 : 0, g_dst_addr);
    return ESP_OK;
}

//callback for provisioning events.
static void prov_cb(esp_ble_mesh_prov_cb_event_t event,esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        if (param->prov_register_comp.err_code == ESP_OK) {
            ESP_LOGI(TAG, "BLE Mesh initialized");
        } else {
            ESP_LOGE(TAG, "BLE Mesh init failed: %d",param->prov_register_comp.err_code);
            xEventGroupSetBits(s_ble_mesh_event_group, MESH_FAIL_BIT);
        }
        break;

    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "Provisioning enabled");
        break;

    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG,
                 "Provisioned: net_idx=0x%04x addr=0x%04x flags=0x%02x iv_index=0x%08" PRIx32,
                 param->node_prov_complete.net_idx,
                 param->node_prov_complete.addr,
                 param->node_prov_complete.flags,
                 param->node_prov_complete.iv_index);

        xEventGroupSetBits(s_ble_mesh_event_group, MESH_CONNECTED_BIT);
        break;

    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGW(TAG, "Provisioning reset; enabling unprovisioned beacon again");

        esp_ble_mesh_node_prov_enable(
            (esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT)
        );
        break;

    default:
        break;
    }
}
//callback for configuration server events.
static void cfg_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                          esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        return;
    }

    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        ESP_LOGI(TAG, "AppKey added");
        break;

    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        ESP_LOGI(TAG,
                "Model bound: elem=0x%04x app_idx=0x%04x cid=0x%04x mod_id=0x%04x",
                param->value.state_change.mod_app_bind.element_addr,
                param->value.state_change.mod_app_bind.app_idx,
                param->value.state_change.mod_app_bind.company_id,
                param->value.state_change.mod_app_bind.model_id);

        if (param->value.state_change.mod_app_bind.model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI &&
            param->value.state_change.mod_app_bind.company_id == ESP_BLE_MESH_CID_NVAL) {
            g_app_idx = param->value.state_change.mod_app_bind.app_idx;
            ESP_LOGI(TAG, "Stored client app_idx = 0x%04x", g_app_idx);
        }
        break;

    case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
        ESP_LOGI(TAG,
                "Subscription added: elem=0x%04x sub=0x%04x cid=0x%04x mod_id=0x%04x",
                param->value.state_change.mod_sub_add.element_addr,
                param->value.state_change.mod_sub_add.sub_addr,
                param->value.state_change.mod_sub_add.company_id,
                param->value.state_change.mod_sub_add.model_id);

        if (param->value.state_change.mod_sub_add.model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV &&
            param->value.state_change.mod_sub_add.company_id == ESP_BLE_MESH_CID_NVAL) {
            g_dst_addr = param->value.state_change.mod_sub_add.sub_addr;
            ESP_LOGI(TAG, "Stored destination addr = 0x%04x", g_dst_addr);
        }
        break;

    default:
        break;
    }
}

//callbacks for generic server.
static void generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                              esp_ble_mesh_generic_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT) {
        return;
    }

    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
    case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK: {
        uint8_t onoff = param->value.state_change.onoff_set.onoff ? 1 : 0;

        onoff_srv.state.onoff = onoff;
        g_motion_state = onoff;

        ESP_LOGI(TAG, "Generic OnOff set from 0x%04x -> %u", param->ctx.addr, onoff);

        /* Alleen bij detectie de actietaak triggeren */
        if (onoff == 1 && g_ble_args != NULL && g_ble_args->action_task_handle != NULL) {
            xTaskNotifyGive(g_ble_args->action_task_handle);//trigger when motion is detected from the ble network
        }
        break;
    }

    default:
        break;
    }
}

//init ble
void ble_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    //set the dev_uuid based on the device MAC address.
    memset(dev_uuid, 0, sizeof(dev_uuid));
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
    ESP_ERROR_CHECK(esp_ble_mesh_register_generic_server_callback(generic_server_cb));
}

static void ble_mesh_start(void)
{
    ble_mesh_register_callbacks();

    ESP_ERROR_CHECK(esp_ble_mesh_init(&provision, &composition));
    ESP_ERROR_CHECK(
        esp_ble_mesh_node_prov_enable(
            (esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT)
        )
    );
}

//ble task.
void ble_task(void *arg)
{
    g_ble_args = (ble_args_t *)arg;

    if (g_ble_args != NULL && g_ble_args->mesh_event_group != NULL) {
        s_ble_mesh_event_group = g_ble_args->mesh_event_group;
    } else if (s_ble_mesh_event_group == NULL) {
        s_ble_mesh_event_group = xEventGroupCreate();
    }

    ble_init();
    ble_mesh_start();

    while (true) {
        uint8_t motion = 0;

        
        if (xQueueReceive(g_ble_args->ble_tx_queue,&motion,0) == pdTRUE) {
            motion = motion ? 1 : 0;//is motion detected or not.

            if (motion) {
                ESP_LOGI(TAG, "Motion queue update: %u", motion);

                if (xEventGroupGetBits(s_ble_mesh_event_group) & MESH_CONNECTED_BIT) {
                    ble_send_motion_state(motion);
                }
        }
    }vTaskDelay(pdMS_TO_TICKS(MOTION_QUEUE_WAIT_MS));
    
    }
}