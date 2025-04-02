// button.c
#include "button.h"
#include "lcd.h"
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

#define BUTTON_PIN_RECORD 2
#define BUTTON_PIN_SCROLL_DOWN 0
#define BUTTON_PIN_SCROLL_UP 1

#define DEBOUNCE_TIME_MS 200

static const char *TAG = "MCP_Button";
static mcp23x17_t mcp;

void check_mcp_buttons_task(void *arg) {
    uint32_t last_press_time_record = 0;
    uint32_t last_press_time_scroll_down = 0;
    uint32_t last_press_time_scroll_up = 0;
    static bool recording = false;

    while (1) {
        uint8_t level;

        // Check recording button
        ESP_ERROR_CHECK(mcp23x17_get_level(&mcp, BUTTON_PIN_RECORD, &level));
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (level == 0 && (now - last_press_time_record > DEBOUNCE_TIME_MS)) {
            last_press_time_record = now;
            recording = !recording;
            ESP_LOGI(TAG, "Recording button pressed. Recording: %s", recording ? "START" : "STOP");
            if (recording) {
                start_recording();
            } else {
                stop_recording();
            }
        }

        // Check scroll down button
        ESP_ERROR_CHECK(mcp23x17_get_level(&mcp, BUTTON_PIN_SCROLL_DOWN, &level));
        if (level == 0 && (now - last_press_time_scroll_down > DEBOUNCE_TIME_MS)) {
            last_press_time_scroll_down = now;
            ESP_LOGI(TAG, "Scroll Down button pressed.");
            lcd_scroll_down();
        }

        // Check scroll up button
        ESP_ERROR_CHECK(mcp23x17_get_level(&mcp, BUTTON_PIN_SCROLL_UP, &level));
        if (level == 0 && (now - last_press_time_scroll_up > DEBOUNCE_TIME_MS)) {
            last_press_time_scroll_up = now;
            ESP_LOGI(TAG, "Scroll Up button pressed.");
            lcd_scroll_up();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void init_mcp_button() {
    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(mcp23x17_init_desc(&mcp, MCP23017_ADDR, I2C_PORT, I2C_SDA_PIN, I2C_SCL_PIN));

    ESP_ERROR_CHECK(mcp23x17_set_mode(&mcp, BUTTON_PIN_RECORD, MCP23X17_GPIO_INPUT));
    ESP_ERROR_CHECK(mcp23x17_set_mode(&mcp, BUTTON_PIN_SCROLL_DOWN, MCP23X17_GPIO_INPUT));
    ESP_ERROR_CHECK(mcp23x17_set_mode(&mcp, BUTTON_PIN_SCROLL_UP, MCP23X17_GPIO_INPUT));

    ESP_LOGI(TAG, "MCP23017 buttons initialized (Record:%d, ScrollDown:%d, ScrollUp:%d)",
             BUTTON_PIN_RECORD, BUTTON_PIN_SCROLL_DOWN, BUTTON_PIN_SCROLL_UP);

    xTaskCreate(check_mcp_buttons_task, "check_mcp_buttons_task", 16392, NULL, 5, NULL);
}