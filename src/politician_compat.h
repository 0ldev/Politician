#pragma once

#if defined(POLITICIAN_HOST_TEST)
    #include <stdint.h>
#elif defined(ARDUINO)
    #include <Arduino.h>
#else
    #include <esp_timer.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/task.h>
    #include <stdint.h>
    inline uint32_t millis() {
        return (uint32_t)(esp_timer_get_time() / 1000ULL);
    }
    inline void delay(uint32_t ms) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
#endif
