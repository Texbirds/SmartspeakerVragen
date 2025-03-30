// main.c
#include "recorder.h"
#include "button.h"
#include "sdcard.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "App start");

    ESP_ERROR_CHECK(init_sdcard_with_spi());
    init_mcp_button();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}