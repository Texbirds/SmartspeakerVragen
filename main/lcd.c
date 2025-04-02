#include "lcd.h"
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

#define MAX_LINES   50
#define LINE_WIDTH  20

static const char *TAG = "LCD";

static i2c_dev_t pcf8574;
static hd44780_t lcd;

static char lines[MAX_LINES][LINE_WIDTH + 1];
static int total_lines = 0;
static int current_offset = 0;

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
    hd44780_gotoxy(&lcd, col, row);
}

void lcd_print(const char *text) {
    ESP_LOGI(TAG, "Printing to LCD: \"%s\"", text);
    hd44780_puts(&lcd, text);
}

void lcd_scroll_set_text(const char *text) {
    total_lines = 0;
    current_offset = 0;

    while (*text && total_lines < MAX_LINES) {
        while (*text == ' ') text++;  // skip leading spaces

        const char *start = text;
        const char *last_space = NULL;
        int len = 0;

        while (*text && len < LINE_WIDTH) {
            if (*text == ' ') last_space = text;
            if (*text == '\n') {
                last_space = text;
                break;
            }
            text++;
            len++;
        }

        const char *end;
        if (*text == '\n') {
            end = text;  
        } else if (*text && last_space && last_space > start) {
            end = last_space;
        } else {
            end = text;
        }

        int copy_len = end - start;
        if (copy_len > LINE_WIDTH) copy_len = LINE_WIDTH;

        memset(lines[total_lines], ' ', LINE_WIDTH);         // clear line
        strncpy(lines[total_lines], start, copy_len);        // copy word(s)
        lines[total_lines][LINE_WIDTH] = '\0';               // ensure null termination
        total_lines++;

        text = *end ? end + 1 : end;  // skip space if any
    }

    lcd_scroll_show();
}

void lcd_scroll_show(void) {
    lcd_clear();
    for (int i = 0; i < 4; i++) {
        int line_idx = current_offset + i;
        if (line_idx < total_lines) {
            lcd_gotoxy(i, 0);
            char padded_line[LINE_WIDTH + 1];
            snprintf(padded_line, sizeof(padded_line), "%-20s", lines[line_idx]);
            lcd_print(padded_line);
        }
    }
}

void lcd_scroll_up(void) {
    if (current_offset > 0) {
        current_offset--;
        lcd_scroll_show();
    }
}

void lcd_scroll_down(void) {
    if (current_offset < total_lines - 4) {
        current_offset++;
        lcd_scroll_show();
    }
}
