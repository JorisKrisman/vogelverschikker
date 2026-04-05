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

#define I2C_PORT            0//GPIO numbers for the I2c
#define I2C_SDA_GPIO        42//GPIO numbers for the I2c
#define I2C_SCL_GPIO        41//GPIO numbers for the I2c
#define I2C_FREQ_HZ         400000//frequency for the I2c
#define MCP4725_ADDR        0x60//address for the MCP4725

#define TWO_PI              6.28318530718f//2 * pi for wave generation
#define FREQ_HZ             440//frequency of the generated sound
#define AUDIO_MAX_VOLUME    1800.0f//maximum volume
#define AUDIO_MIDDLE_VALUE   2048//middle value for DAC
#define SAMPLE_RATE         8000//sample rate for the audio output
#define AUDIO_TASK_STACK    4096//stack size for the audio task
#define AUDIO_TASK_PRIO     5//priority for the audio task

static const char *TAG = "audio_control";

static i2c_master_dev_handle_t mcp = NULL;
static TaskHandle_t audioTaskHandle = NULL;
static volatile bool audio_enabled = false;
static volatile bool audio_initialized = false;

/*
the following function converts a 12-bit value to the format required for the MCP4725(2 bytes).
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

//this task generates a peeping sound.
static void audio_task(void *arg)
{
    float phase = 0.0f;//phase for the wave

    float volume = 0.0f;//volume for the wave, will be changed to ceate a pulsing effect
    float volume_speed = 1.0f;//speed at which the volume changes, higher values will create a faster pulsing
    int direction = 1;//direction of the volume change, 1 for increasing, -1 for decreasing

    while (1) {
        if (!audio_enabled) {
            //make the output silent by setting it to the middle value(2048)
            mcp4725_write_raw(2048);
            vTaskDelay(pdMS_TO_TICKS(20));//prevent blocking
            continue;
        }

        volume += direction * volume_speed;//change the volume based on direction and speed

        //clamp the volume and reverse direction if limits are reached
        if (volume >= 1.0f) {
            volume = 1.0f;
            direction = -1;
        }
        if (volume <= 0.0f) {
            volume = 0.0f;
            direction = 1;
        }

        //this part generates a sine wave
        float phase_inc = TWO_PI * FREQ_HZ / SAMPLE_RATE;
        float s = sinf(phase);

        //this part converts the sine wave to a 12-bit value to write to the MCP4725, 1800
        uint16_t sample = (uint16_t)(AUDIO_MIDDLE_VALUE + s * (AUDIO_MAX_VOLUME * volume));
        mcp4725_write_raw(sample);

        //make sure the phase is not too large to prevent floating point issues
        phase += phase_inc;
        if (phase >= TWO_PI) {
            phase -= TWO_PI;
        }

        ets_delay_us(1000000 / SAMPLE_RATE);//delay to get the correct sample rate.
    }
}

//init the audio task
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

//simple start function.
void audio_start(void)
{
    if (!audio_initialized) {
        ESP_LOGW(TAG, "audio_start called before audio_init");
        return;
    }

    audio_enabled = true;//if this is enabled, make a sound.
    ESP_LOGI(TAG, "Audio started");
}
//simple stop function.
void audio_stop(void)
{
    if (!audio_initialized) {
        return;
    }

    audio_enabled = false;//if this is disabled, make no sound.
    mcp4725_write_raw(2048);//set to middle value to prevent noise when stopping.
    ESP_LOGI(TAG, "Audio stopped");
}
//function to check if the audio is currently running.
bool audio_is_running(void)
{
    return audio_enabled;
}