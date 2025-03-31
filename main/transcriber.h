#ifndef TRANSCRIBER_H
#define TRANSCRIBER_H

// default includes
#include <stdio.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <string.h>

// for wifi & http
#include <esp_http_client.h>
#include <esp_tls.h>

// handling JSON
#include <cJSON.h>

// logging for ESP
#include "esp_err.h"

// memory management
#include "esp_heap_caps.h"

// for sdcard
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048
#define API_KEY "806ea076e8e347b197f6a1585336dd9d"

// USE THIS FOR API CALLS!!!
void set_callback(void (*new_update_callback)(char*));
void transcribe_file_with_api(char *file_path);

// Definitions for the C source file itself
void upload_file_to_assembly(esp_http_client_handle_t client, char *file_path);
void transcribe(esp_http_client_handle_t client);
void get_transcript(esp_http_client_handle_t client);
void handle_http_event_finish(esp_http_client_handle_t client, cJSON *json);
void make_sdcard_ready();

#endif // TRANSCRIBER_H
