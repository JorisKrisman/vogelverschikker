#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "rom/ets_sys.h"

#include "audio_control.h"

#define I2C_PORT            0
#define I2C_SDA_GPIO        42
#define I2C_SCL_GPIO        41  
#define I2C_FREQ_HZ         400000
#define MCP4725_ADDR        0x60

#define SAMPLE_RATE         8000
#define AUDIO_TASK_STACK    4096
#define AUDIO_TASK_PRIO     5

static const char *TAG = "audio_control";

static i2c_master_dev_handle_t mcp = NULL;
static TaskHandle_t audioTaskHandle = NULL;
static volatile bool audio_enabled = false;
static volatile bool audio_initialized = false;

/*
 MCP4725 fast write:
 byte0: C2 C1 PD1 PD0 D11 D10 D9 D8
 byte1: D7 D6 D5 D4 D3 D2 D1 D0
*/
static esp_err_t mcp4725_write_raw(uint16_t value)
{
    value &= 0x0FFF;

    uint8_t data[2];
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)(value & 0xFF);

    return i2c_master_transmit(mcp, data, sizeof(data), -1);
}

static void audio_task(void *arg)
{
    const float two_pi = 6.28318530718f;
    float phase = 0.0f;

    const float freq = 440.0f;

    float volume = 0.0f;
    float volume_speed = 1.0f;
    int direction = 1;

    while (1) {
        if (!audio_enabled) {
            // uitgang terug naar midden, zodat hij stil is
            mcp4725_write_raw(2048);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        volume += direction * volume_speed;

        if (volume >= 1.0f) {
            volume = 1.0f;
            direction = -1;
        }
        if (volume <= 0.0f) {
            volume = 0.0f;
            direction = 1;
        }

        float phase_inc = two_pi * freq / SAMPLE_RATE;
        float s = sinf(phase);

        uint16_t sample = (uint16_t)(2048.0f + s * (1800.0f * volume));
        mcp4725_write_raw(sample);

        phase += phase_inc;
        if (phase >= two_pi) {
            phase -= two_pi;
        }

        ets_delay_us(1000000 / SAMPLE_RATE);
    }
}

esp_err_t audio_init(void)
{
    if (audio_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus_handle;

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bus_handle), TAG, "i2c_new_master_bus failed");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MCP4725_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &dev_cfg, &mcp), TAG, "i2c_master_bus_add_device failed");

    BaseType_t ok = xTaskCreate(audio_task, "audio_task", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIO, &audioTaskHandle);
    if (ok != pdPASS) {
        return ESP_FAIL;
    }

    audio_initialized = true;
    audio_enabled = false;

    ESP_LOGI(TAG, "Audio initialized");
    return ESP_OK;
}

void audio_start(void)
{
    if (!audio_initialized) {
        ESP_LOGW(TAG, "audio_start called before audio_init");
        return;
    }

    audio_enabled = true;
    ESP_LOGI(TAG, "Audio started");
}

void audio_stop(void)
{
    if (!audio_initialized) {
        return;
    }

    audio_enabled = false;
    mcp4725_write_raw(2048);
    ESP_LOGI(TAG, "Audio stopped");
}

bool audio_is_running(void)
{
    return audio_enabled;
}