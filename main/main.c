// main.c
#include "recorder.h"
#include "button.h"
#include "sdcard.h"
#include "wifi.h"
#include "transcriber.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

static const char *TAG = "MAIN";

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    make_sdcard_ready();
    init_mcp_button();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}