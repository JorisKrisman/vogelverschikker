#pragma once


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/adc.h"


class SensorController {
public:
    SensorController(gpio_num_t motionPin,adc1_channel_t ldrChannel,QueueHandle_t motionQueue,QueueHandle_t lightQueue,TaskHandle_t actionTaskHandle);

    void init();
    

private:
    gpio_num_t motionPin;
    adc1_channel_t ldrChannel;

    QueueHandle_t motionQueue;
    QueueHandle_t lightQueue;
    TaskHandle_t actionTaskHandle;
    bool systemActive;

    // Task functions
    static void motionTask(void* arg);
    static void ldrTask(void* arg);
    void checkTreshold(uint16_t lightValue);
    void enterSleepMode();
};