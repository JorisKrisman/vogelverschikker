#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_init(void);
void audio_start(void);
void audio_stop(void);
bool audio_is_running(void);

#ifdef __cplusplus
}
#endif