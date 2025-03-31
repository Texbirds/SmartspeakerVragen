#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_crt_bundle.h"   // for TLS certificate bundle (if configured)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "transcriber.h"

#define ASSEMBLYAI_API_KEY   "36074272bf2a401c94c00a4995743d20"
#define ASSEMBLYAI_API_BASE  "https://api.assemblyai.com/v2"
#define MAX_POLL_ATTEMPTS    60          // Max number of polling attempts for transcription result
#define POLL_INTERVAL_MS     1000        // Interval between polling attempts (in milliseconds)

static const char *TAG = "Transcriber";

esp_err_t transcriber_transcribe_file(const char *file_path)
{
    ESP_LOGI(TAG, "Free heap before upload: %" PRIu32, esp_get_free_heap_size());

    // Open the audio file from SD card
    FILE *f = fopen(file_path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    // Determine file size
    if (fseek(f, 0, SEEK_END) != 0) {
        ESP_LOGE(TAG, "Failed to seek file");
        fclose(f);
        return ESP_FAIL;
    }
    long file_size = ftell(f);
    if (file_size < 0) {
        ESP_LOGE(TAG, "Failed to get file size");
        fclose(f);
        return ESP_FAIL;
    }
    rewind(f);

    ESP_LOGI(TAG, "Uploading file (%ld bytes) to AssemblyAI...", file_size);

    // Configure HTTP client for file upload
    esp_http_client_config_t config_upload = {
        .url = ASSEMBLYAI_API_BASE "/upload",
        .method = HTTP_METHOD_POST,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,  // use certificate bundle for HTTPS
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config_upload);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client for upload");
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    // Set required headers
    esp_http_client_set_header(client, "Authorization", ASSEMBLYAI_API_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");

    // Optionally set content length header
    char content_length_str[32];
    snprintf(content_length_str, sizeof(content_length_str), "%ld", file_size);
    esp_http_client_set_header(client, "Content-Length", content_length_str);

    // Open connection and send the file data in chunks
    esp_err_t err = esp_http_client_open(client, file_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection for upload: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        fclose(f);
        return err;
    }
    // Read from file and write to HTTP client
    const size_t CHUNK_SIZE = 1024;
    char *file_buffer = malloc(CHUNK_SIZE);
    if (!file_buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for file upload");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t bytes_read, bytes_written;
    long total_bytes_sent = 0;
    while ((bytes_read = fread(file_buffer, 1, CHUNK_SIZE, f)) > 0) {
        total_bytes_sent += bytes_read;
        // Write this chunk
        bytes_written = 0;
        while (bytes_written < bytes_read) {
            int write_ret = esp_http_client_write(client, file_buffer + bytes_written, bytes_read - bytes_written);
            if (write_ret <= 0) {
                ESP_LOGE(TAG, "Error during HTTP write: %s", esp_err_to_name(write_ret));
                free(file_buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                fclose(f);
                return ESP_FAIL;
            }
            bytes_written += write_ret;
        }
    }
    free(file_buffer);
    fclose(f);
    // Finish the request and get response
    // (Headers are already sent; now read the server's response)
    int status_code = esp_http_client_fetch_headers(client);
    status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "File upload failed, HTTP status code = %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    // Read response body (JSON with upload_url)
    int resp_len = esp_http_client_get_content_length(client);
    if (resp_len <= 0) resp_len = 512;  // if content length not provided, use a default buffer
    char *upload_resp = malloc(resp_len + 1);
    if (!upload_resp) {
        ESP_LOGE(TAG, "Failed to allocate buffer for upload response");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    int read_len = esp_http_client_read(client, upload_resp, resp_len);
    if (read_len < 0) {
        ESP_LOGE(TAG, "Failed to read upload response");
        free(upload_resp);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    upload_resp[read_len] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    // Parse the upload response to get the upload_url
    char *upload_url = NULL;
    char *url_start = strstr(upload_resp, "\"upload_url\"");
    if (url_start) {
        char *quote = strchr(url_start, ':');
        if (quote) {
            // move to the first quote after colon
            quote = strchr(quote, '\"');
            if (quote) {
                char *url_value_start = quote + 1;
                char *url_value_end = strchr(url_value_start, '\"');
                if (url_value_end) {
                    size_t url_len = url_value_end - url_value_start;
                    upload_url = malloc(url_len + 1);
                    if (upload_url) {
                        strncpy(upload_url, url_value_start, url_len);
                        upload_url[url_len] = '\0';
                    }
                }
            }
        }
    }
    free(upload_resp);
    if (upload_url == NULL) {
        ESP_LOGE(TAG, "Failed to parse upload URL from response");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "File uploaded, starting transcription...");

    // Prepare JSON payload for transcription request
    size_t url_len = strlen(upload_url);
    size_t json_payload_len = url_len + 32; // enough for {"audio_url":""} and some padding
    char *json_payload = malloc(json_payload_len);
    if (!json_payload) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON payload");
        free(upload_url);
        return ESP_ERR_NO_MEM;
    }
    snprintf(json_payload, json_payload_len, "{\"audio_url\":\"%s\"}", upload_url);
    free(upload_url);  // no longer needed

    // Configure HTTP client for transcription request
    esp_http_client_config_t config_transcribe = {
        .url = ASSEMBLYAI_API_BASE "/transcript",
        .method = HTTP_METHOD_POST,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    client = esp_http_client_init(&config_transcribe);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client for transcription");
        free(json_payload);
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Authorization", ASSEMBLYAI_API_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    // Set content length for JSON payload
    snprintf(content_length_str, sizeof(content_length_str), "%d", (int)strlen(json_payload));
    esp_http_client_set_header(client, "Content-Length", content_length_str);

    // Send the transcription request
    err = esp_http_client_open(client, strlen(json_payload));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection for transcription: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(json_payload);
        return err;
    }
    // Write JSON payload
    int post_bytes = esp_http_client_write(client, json_payload, strlen(json_payload));
    free(json_payload);
    if (post_bytes < 0) {
        ESP_LOGE(TAG, "Error writing JSON payload to HTTP request");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    // Read transcription request response
    status_code = esp_http_client_fetch_headers(client);
    status_code = esp_http_client_get_status_code(client);
    if (status_code != 200 && status_code != 201) {
        ESP_LOGE(TAG, "Transcription request failed, HTTP status = %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    resp_len = esp_http_client_get_content_length(client);
    if (resp_len <= 0) resp_len = 256;
    char *transcript_resp = malloc(resp_len + 1);
    if (!transcript_resp) {
        ESP_LOGE(TAG, "Failed to allocate buffer for transcription response");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    read_len = esp_http_client_read(client, transcript_resp, resp_len);
    if (read_len < 0) {
        ESP_LOGE(TAG, "Failed to read transcription response");
        free(transcript_resp);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    transcript_resp[read_len] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    // Parse transcription request response to get transcript ID
    char *transcript_id = NULL;
    char *id_start = strstr(transcript_resp, "\"id\"");
    if (id_start) {
        char *colon = strchr(id_start, ':');
        if (colon) {
            char *id_val_start = strchr(colon, '\"');
            if (id_val_start) {
                id_val_start++; // move past quote
                char *id_val_end = strchr(id_val_start, '\"');
                if (id_val_end) {
                    size_t id_len = id_val_end - id_val_start;
                    transcript_id = malloc(id_len + 1);
                    if (transcript_id) {
                        strncpy(transcript_id, id_val_start, id_len);
                        transcript_id[id_len] = '\0';
                    }
                }
            }
        }
    }
    free(transcript_resp);
    if (transcript_id == NULL) {
        ESP_LOGE(TAG, "Failed to get transcript ID from response");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Transcription requested (ID: %s). Waiting for result...", transcript_id);

    // Poll for transcription result
    bool completed = false;
    bool failed = false;
    char poll_url[256];
    for (int attempt = 0; attempt < MAX_POLL_ATTEMPTS; ++attempt) {
        // Construct URL for GET request to check status
        snprintf(poll_url, sizeof(poll_url), "%s/transcript/%s", ASSEMBLYAI_API_BASE, transcript_id);
        esp_http_client_config_t config_get = {
            .url = poll_url,
            .method = HTTP_METHOD_GET,
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };
        client = esp_http_client_init(&config_get);
        if (client == NULL) {
            ESP_LOGE(TAG, "Failed to init HTTP client for polling");
            free(transcript_id);
            return ESP_ERR_NO_MEM;
        }
        esp_http_client_set_header(client, "Authorization", ASSEMBLYAI_API_KEY);

        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP GET connection failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            free(transcript_id);
            return err;
        }
        esp_http_client_fetch_headers(client);
        resp_len = esp_http_client_get_content_length(client);
        if (resp_len <= 0) resp_len = 1024;  // if unknown, use default size
        char *status_resp = malloc(resp_len + 1);
        if (!status_resp) {
            ESP_LOGE(TAG, "Failed to allocate buffer for status response");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            free(transcript_id);
            return ESP_ERR_NO_MEM;
        }
        read_len = esp_http_client_read(client, status_resp, resp_len);
        if (read_len < 0) {
            ESP_LOGE(TAG, "Failed to read status response");
            free(status_resp);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            free(transcript_id);
            return ESP_FAIL;
        }
        status_resp[read_len] = '\0';
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        // Check transcription status
        char *stat_ptr = strstr(status_resp, "\"status\"");
        char status_value[16] = {0};
        if (stat_ptr) {
            char *colon = strchr(stat_ptr, ':');
            if (colon) {
                // move to first quote after colon
                char *stat_val_start = strchr(colon, '\"');
                if (stat_val_start) {
                    stat_val_start++;
                    char *stat_val_end = strchr(stat_val_start, '\"');
                    if (stat_val_end) {
                        size_t stat_len = stat_val_end - stat_val_start;
                        if (stat_len < sizeof(status_value)) {
                            strncpy(status_value, stat_val_start, stat_len);
                            status_value[stat_len] = '\0';
                        }
                    }
                }
            }
        }
        if (strcmp(status_value, "completed") == 0) {
            // Transcription completed, extract text
            char *text_ptr = strstr(status_resp, "\"text\"");
            if (text_ptr) {
                char *colon = strchr(text_ptr, ':');
                if (colon) {
                    char *text_val_start = strchr(colon, '\"');
                    if (text_val_start) {
                        text_val_start++;
                        // Extract text value, handling escaped quotes
                        char *p = text_val_start;
                        char *text_output = malloc(strlen(text_val_start) + 1);
                        if (!text_output) {
                            ESP_LOGE(TAG, "Failed to allocate buffer for transcript text");
                            free(status_resp);
                            free(transcript_id);
                            return ESP_ERR_NO_MEM;
                        }
                        size_t idx = 0;
                        while (*p && *p != '\"') {
                            if (*p == '\\' && *(p+1) == '\"') {
                                // Unescape \"
                                text_output[idx++] = '\"';
                                p += 2;
                                continue;
                            }
                            text_output[idx++] = *p++;
                        }
                        text_output[idx] = '\0';
                        ESP_LOGI(TAG, "Transcription: %s", text_output);
                        free(text_output);
                    }
                }
            }
            free(status_resp);
            completed = true;
            // Break out of polling loop
            break;
        } else if (strcmp(status_value, "error") == 0) {
            // Transcription failed
            char *err_ptr = strstr(status_resp, "\"error\"");
            if (err_ptr) {
                char *colon = strchr(err_ptr, ':');
                if (colon) {
                    char *err_val_start = strchr(colon, '\"');
                    if (err_val_start) {
                        err_val_start++;
                        char *err_val_end = strchr(err_val_start, '\"');
                        if (err_val_end) {
                            *err_val_end = '\0';
                            ESP_LOGE(TAG, "Transcription failed: %s", err_val_start);
                            *err_val_end = '\"';  // restore (not strictly necessary since freeing next)
                        }
                    }
                }
            } else {
                ESP_LOGE(TAG, "Transcription failed (unknown error)");
            }
            free(status_resp);
            failed = true;
            break;
        }
        free(status_resp);
        // Not completed yet, wait and poll again
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }

    free(transcript_id);
    if (!completed && !failed) {
        ESP_LOGE(TAG, "Transcription timed out without completion");
        return ESP_ERR_TIMEOUT;
    }
    return completed ? ESP_OK : ESP_FAIL;
}