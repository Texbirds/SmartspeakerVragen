// button.c
#include "button.h"
#include "recorder.h"
#include "mcp23x17.h"
#include "i2cdev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define I2C_PORT I2C_NUM_0
#define I2C_SDA_PIN 18
#define I2C_SCL_PIN 23
#define MCP23017_ADDR 0x20
#define BUTTON_PIN 2  // GPA2
#define DEBOUNCE_TIME_MS 200

static const char *TAG = "MCP_Button";
static mcp23x17_t mcp;

void check_mcp_button_task(void *arg) {
    uint32_t last_press_time = 0;
    static bool pressed = false;

    while (1) {
        uint8_t level;
        ESP_ERROR_CHECK(mcp23x17_get_level(&mcp, BUTTON_PIN, &level));

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (level == 0 && (now - last_press_time > DEBOUNCE_TIME_MS)) {
            last_press_time = now;
            pressed = !pressed;
            ESP_LOGI(TAG, "CJMCU Button press. Recording: %s", pressed ? "START" : "STOP");
            if (pressed) {
                start_recording();
            } else {
                stop_recording();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void init_mcp_button() {
    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(mcp23x17_init_desc(&mcp, MCP23017_ADDR, I2C_PORT, I2C_SDA_PIN, I2C_SCL_PIN));
    ESP_ERROR_CHECK(mcp23x17_set_mode(&mcp, BUTTON_PIN, MCP23X17_GPIO_INPUT));
    ESP_LOGI(TAG, "CJMCU (MCP23017) button initialized on GPA%d", BUTTON_PIN);
    xTaskCreate(check_mcp_button_task, "check_mcp_button_task", 4096, NULL, 5, NULL);
}