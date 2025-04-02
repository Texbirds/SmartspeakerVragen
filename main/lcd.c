#include "lcd.h"
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c.h>
#include "hd44780.h"
#include "pcf8574.h"
#include "esp_log.h"

#define I2C_ADDRESS  0x27
#define I2C_SDA_PIN  18
#define I2C_SCL_PIN  23

static const char *TAG = "LCD";

static i2c_dev_t pcf8574;
static hd44780_t lcd;

static esp_err_t write_lcd_data(const hd44780_t *lcd_unused, uint8_t data) {
    return pcf8574_port_write(&pcf8574, data);
}

void lcd_init(void) {
    ESP_LOGI(TAG, "Initializing I2C and LCD");

    memset(&pcf8574, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(pcf8574_init_desc(&pcf8574, I2C_ADDRESS, 0, I2C_SDA_PIN, I2C_SCL_PIN));

    lcd.write_cb = write_lcd_data;
    lcd.font = HD44780_FONT_5X8;
    lcd.lines = 4;
    lcd.pins.rs = 0;
    lcd.pins.e  = 2;
    lcd.pins.d4 = 4;
    lcd.pins.d5 = 5;
    lcd.pins.d6 = 6;
    lcd.pins.d7 = 7;
    lcd.pins.bl = 3;

    ESP_ERROR_CHECK(hd44780_init(&lcd));
    hd44780_switch_backlight(&lcd, true);
    hd44780_clear(&lcd);

    ESP_LOGI(TAG, "LCD initialized");
}

void lcd_clear(void) {
    ESP_LOGI(TAG, "Clearing LCD");
    hd44780_clear(&lcd);
}

void lcd_gotoxy(int row, int col) {
    ESP_LOGI(TAG, "Setting cursor to row %d, col %d", row, col);
    hd44780_gotoxy(&lcd, row, col);
}

void lcd_print(const char *text) {
    ESP_LOGI(TAG, "Printing to LCD: \"%s\"", text);
    hd44780_puts(&lcd, text);
}
