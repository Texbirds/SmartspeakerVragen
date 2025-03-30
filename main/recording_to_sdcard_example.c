#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "audio_sys.h"
#include "board.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"

#include "wav_encoder.h"

#define RECORD_TIME_SECONDS (10)
#define MOUNT_POINT "/sdcard"
static const char *TAG = "RECORD_TO_SDCARD";

// 👇 Replace these pin numbers with the ones used on your board
#define PIN_NUM_MISO  2
#define PIN_NUM_MOSI  15
#define PIN_NUM_CLK   14
#define PIN_NUM_CS    13

static esp_err_t init_sdcard_with_spi(sdmmc_card_t **out_card) {
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing SD card via SPI");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) return ret;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    return esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, out_card);
}

void app_main(void) {
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t fatfs_stream_writer, i2s_stream_reader, wav_encoder;
    sdmmc_card_t *card;

    ESP_LOGI(TAG, "[1] Mounting SD Card");
    ESP_ERROR_CHECK(init_sdcard_with_spi(&card));
    sdmmc_card_print_info(stdout, card);

    ESP_LOGI(TAG, "[2] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_ENCODE, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG, "[3] Create audio pipeline");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);

    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_WRITER;
    fatfs_stream_writer = fatfs_stream_init(&fatfs_cfg);

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_READER;
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);

    wav_encoder_cfg_t wav_cfg = DEFAULT_WAV_ENCODER_CONFIG();
    wav_encoder = wav_encoder_init(&wav_cfg);

    ESP_LOGI(TAG, "[4] Register elements to pipeline");
    audio_pipeline_register(pipeline, i2s_stream_reader, "i2s");
    audio_pipeline_register(pipeline, wav_encoder, "wav");
    audio_pipeline_register(pipeline, fatfs_stream_writer, "file");

    ESP_LOGI(TAG, "[5] Link pipeline i2s -> wav -> file");
    const char *link_tag[3] = {"i2s", "wav", "file"};
    audio_pipeline_link(pipeline, link_tag, 3);

    ESP_LOGI(TAG, "[6] Set URI to /sdcard/rec.wav");
    audio_element_set_uri(fatfs_stream_writer, MOUNT_POINT "/rec.wav");

    ESP_LOGI(TAG, "[7] Start pipeline");
    audio_pipeline_run(pipeline);

    int seconds = 0;
    while (seconds < RECORD_TIME_SECONDS) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "Recording... %d", ++seconds);
    }

    audio_element_set_ringbuf_done(i2s_stream_reader);

    ESP_LOGI(TAG, "[8] Stop pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, i2s_stream_reader);
    audio_pipeline_unregister(pipeline, wav_encoder);
    audio_pipeline_unregister(pipeline, fatfs_stream_writer);
    audio_pipeline_remove_listener(pipeline);

    audio_pipeline_deinit(pipeline);
    audio_element_deinit(i2s_stream_reader);
    audio_element_deinit(wav_encoder);
    audio_element_deinit(fatfs_stream_writer);

    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_free(host.slot);
    ESP_LOGI(TAG, "Done.");
}