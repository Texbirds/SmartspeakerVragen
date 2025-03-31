#pragma once

#include "esp_err.h"

/**
 * @brief Transcribe an audio file using AssemblyAI API and print the result.
 *
 * This function uploads the given WAV audio file to AssemblyAI, requests a transcription, 
 * polls for the result, and prints the transcribed text to the serial console.
 *
 * @param file_path Path to the WAV audio file (e.g. "/sdcard/ask.wav").
 * @return ESP_OK on success, or an error code if any step fails.
 */
esp_err_t transcriber_transcribe_file(const char *file_path);
