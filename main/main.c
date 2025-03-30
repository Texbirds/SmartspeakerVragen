#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "audio_pipeline.h"
#include "audio_element.h"
#include "i2s_stream.h"
#include "wav_encoder.h"
#include "fatfs_stream.h"
#include "board.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "mcp23x17.h"

#define SAMPLE_RATE 44100
#define FILE_PATH "/sdcard/ask.wav"
#define MOUNT_POINT "/sdcard"

#define PIN_NUM_MISO  2
#define PIN_NUM_MOSI  15
#define PIN_NUM_CLK   14
#define PIN_NUM_CS    13

#define I2C_PORT I2C_NUM_0
#define I2C_SDA_PIN 18
#define I2C_SCL_PIN 23
#define MCP23017_ADDR 0x20

#define BUTTON_PIN 2  // A2 = GPA2

#define DEBOUNCE_TIME_MS 200

static const char *TAG = "CJMCU_ButtonRecorder";

static bool is_recording = false;
static audio_pipeline_handle_t pipeline = NULL;
static audio_element_handle_t i2s_stream_reader = NULL;
static audio_element_handle_t wav_encoder = NULL;
static audio_element_handle_t fatfs_stream_writer = NULL;
static sdmmc_card_t *sdcard = NULL;
static mcp23x17_t mcp;

static esp_err_t init_sdcard_with_spi(sdmmc_card_t **out_card) {
    ESP_LOGI(TAG, "Initializing SD card via SPI...");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA));

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    return esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, out_card);
}

void start_recording() {
    ESP_LOGI(TAG, "Start recording...");

    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_ENCODE, AUDIO_HAL_CTRL_START);

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_READER;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = SAMPLE_RATE;
#else
    i2s_cfg.i2s_config.sample_rate = SAMPLE_RATE;
#endif
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);

    wav_encoder_cfg_t wav_cfg = DEFAULT_WAV_ENCODER_CONFIG();
    wav_encoder = wav_encoder_init(&wav_cfg);

    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_WRITER;
    fatfs_stream_writer = fatfs_stream_init(&fatfs_cfg);
    audio_element_set_uri(fatfs_stream_writer, FILE_PATH);

    audio_pipeline_register(pipeline, i2s_stream_reader, "i2s");
    audio_pipeline_register(pipeline, wav_encoder, "wav");
    audio_pipeline_register(pipeline, fatfs_stream_writer, "file");

    const char *link_tag[3] = {"i2s", "wav", "file"};
    audio_pipeline_link(pipeline, link_tag, 3);
    audio_pipeline_run(pipeline);

    is_recording = true;
}

void stop_recording() {
    ESP_LOGI(TAG, "Stop recording...");

    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, i2s_stream_reader);
    audio_pipeline_unregister(pipeline, wav_encoder);
    audio_pipeline_unregister(pipeline, fatfs_stream_writer);

    audio_pipeline_deinit(pipeline);
    audio_element_deinit(i2s_stream_reader);
    audio_element_deinit(wav_encoder);
    audio_element_deinit(fatfs_stream_writer);

    pipeline = NULL;
    i2s_stream_reader = NULL;
    wav_encoder = NULL;
    fatfs_stream_writer = NULL;
    is_recording = false;
}

void check_mcp_button_task(void *arg) {
    uint32_t last_press_time = 0;
    static bool pressed = false;

    while (1) {
        uint8_t level;
        ESP_ERROR_CHECK(mcp23x17_get_level(&mcp, BUTTON_PIN, &level));

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (level == 0 && (now - last_press_time > DEBOUNCE_TIME_MS)) { // Active LOW
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

void app_main(void) {
    ESP_LOGI(TAG, "App start");

    ESP_ERROR_CHECK(init_sdcard_with_spi(&sdcard));
    sdmmc_card_print_info(stdout, sdcard);

    init_mcp_button();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
