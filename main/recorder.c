// recorder.c
#include "recorder.h"
#include "esp_log.h"
#include "audio_pipeline.h"
#include "audio_element.h"
#include "i2s_stream.h"
#include "wav_encoder.h"
#include "fatfs_stream.h"
#include "board.h"
#include "transcriber.h"

#define SAMPLE_RATE 44100
#define FILE_PATH "/sdcard/ask.wav"

static const char *TAG = "Recorder";

static bool is_recording = false;
static audio_pipeline_handle_t pipeline = NULL;
static audio_element_handle_t i2s_stream_reader = NULL;
static audio_element_handle_t wav_encoder = NULL;
static audio_element_handle_t fatfs_stream_writer = NULL;

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

    xTaskCreate(transcription_task, "transcription_task", 16384, NULL, 5, NULL);
}