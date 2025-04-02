#include "transcriber.h"
#include "chatgpt_api.h"
#include "secrets.h"

char *audio_url = "";
char *id = "";
typedef enum {
    STATE_INITIAL,
    STATE_FILE_UPLOADED,
    STATE_TRANSCRIPTION_REQUESTED,
    STATE_TRANSCRIPTION_ID_RECEIVED,
    STATE_TRANSCRIPTION_TEXT_RECEIVED,
} transcription_state_t;

transcription_state_t transcription_state = STATE_INITIAL;

extern const uint8_t server_cert_pem_start[] asm("_binary_server_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_server_cert_pem_end");

void (*return_callback)(char*);

void set_callback(void (*new_update_callback)(char*)){
    return_callback = new_update_callback;
}

static const char *TAG = "Transcriber";

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_ON_DATA:
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                int copy_len = 0;
                if (evt->user_data) {
                    copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    const int buffer_len = esp_http_client_get_content_length(evt->client);
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(buffer_len);
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGI(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    copy_len = MIN(evt->data_len, (buffer_len - output_len));
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }
                output_len += copy_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // printf("received: %s\n", output_buffer);
                handle_http_event_finish(evt->client, cJSON_ParseWithLength(output_buffer, output_len));
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGE(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGE(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
}

void handle_http_event_finish(esp_http_client_handle_t client, cJSON *json) {
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return;
    }

    if (cJSON_GetObjectItem(json, "status") != NULL) {
        char* status = cJSON_GetObjectItem(json, "status")->valuestring;
        if (strcmp(status, "error") == 0) //returns 0 on true
        {
            ESP_LOGE(TAG, "Status error, returning");
            return;
        }
        
    }

    uint8_t state = 0;

    if (cJSON_GetObjectItem(json, "id") != NULL)
    {
        state = 1;
    }
    
    if (cJSON_GetObjectItem(json, "text") != NULL  && !cJSON_IsNull(cJSON_GetObjectItem(json, "text"))) 
    {
        state = 2;
    }
    
    switch (state) {
        case 0: // uploading file, retrieving audio_url
            cJSON *temp_url = cJSON_GetObjectItem(json, "upload_url");
            if (temp_url != NULL) {
                audio_url = cJSON_GetStringValue(temp_url);
                ESP_LOGI(TAG, "extracted audio_url: %s", audio_url);
                transcription_state = STATE_FILE_UPLOADED;
            }
            break;
    
        case 1: // transcript executed, retrieving id transcript
            cJSON *temp_id = cJSON_GetObjectItem(json, "id");
            if (temp_id != NULL) {
                id = cJSON_GetStringValue(temp_id);
                ESP_LOGI(TAG, "extracted id: %s", id);
                transcription_state = STATE_TRANSCRIPTION_ID_RECEIVED;
                esp_http_client_close(client); // reset connection
                get_transcript(client); // move to next phase
            }
            break;
    
        case 2: // transcript text ready
            cJSON *temp_text = cJSON_GetObjectItem(json, "text");
            if (temp_text != NULL) {
                char *text = cJSON_GetStringValue(temp_text);
                ESP_LOGI(TAG, "extracted text:\n%s", text);
                transcription_state = STATE_TRANSCRIPTION_TEXT_RECEIVED;
                ask_chatgpt(text);
                esp_http_client_cleanup(client);
            }
            break;
    }    
}

void check_file_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        ESP_LOGI(TAG, "File exists: %s\n", path);
    } else {
        ESP_LOGE(TAG, "File does NOT exist: %s\n", path);
    }
}

void transcribe_file_with_api(char *file_path){

    esp_http_client_config_t config = {
        .url = "https://api.eu.assemblyai.com/v2",
        .event_handler = _http_event_handler,
        .cert_pem = (const char *)server_cert_pem_start,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    ESP_LOGI(TAG, "Uploading file to assembly");
    upload_file_to_assembly(client, file_path);

    ESP_LOGI(TAG, "Transcribing....");
    transcribe(client);
}

void upload_file_to_assembly(esp_http_client_handle_t client, char *file_path) {
    check_file_exists(file_path);

    //setup connection configs
    esp_http_client_set_url(client, "https://api.eu.assemblyai.com/v2/upload");
    esp_http_client_set_header(client, "Authorization", API_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");

    // Open the audio file & read data
    FILE* file = fopen(file_path, "rb");
    if (!file) {
        ESP_LOGI(TAG, "Failed to open audio file");
        return;
    }
    
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size == 0) {
        ESP_LOGI(TAG, "File is empty");
        fclose(file);
        return;
    }
    
    // Create a buffer for the file content
    printf("Free heap size: %lu\n", esp_get_free_heap_size());
    ESP_LOGE(TAG, "File size: %d", file_size);
    char *file_buffer = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
    if (!file_buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for file buffer");
        fclose(file);
        return;
    }
    
    fread(file_buffer, 1, file_size, file);
    fclose(file);

    // Set the method as POST
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, file_buffer, file_size);

    // Perform the request
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST request failed: %s\n", esp_err_to_name(err));
    }

    free(file_buffer);
}

void transcribe(esp_http_client_handle_t client) {
    if (audio_url == NULL)
    {
        return;
    }
    
    esp_http_client_set_url(client, "https://api.eu.assemblyai.com/v2/transcript");

    // set new content-type header
    esp_http_client_set_header(client, "Content-type", "application/json");

    // printf("Url before making cJSON%s\n", audio_url);
    // ready sending json
    cJSON *jsonToSend = cJSON_CreateObject();
    cJSON_AddItemToObject(jsonToSend, "audio_url", cJSON_CreateString(audio_url));
    cJSON_AddItemToObject(jsonToSend, "language_detection", cJSON_CreateBool(true));

    char *jsonData = cJSON_PrintUnformatted(jsonToSend);
    ESP_LOGI(TAG, "final json:\n%s", jsonData);
    esp_http_client_set_post_field(client, jsonData, strlen(jsonData));

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST request failed: %s\n", esp_err_to_name(err));
    }

    cJSON_Delete(jsonToSend);
    free(jsonData);
}

void get_transcript(esp_http_client_handle_t client) {
    if (transcription_state >= STATE_TRANSCRIPTION_TEXT_RECEIVED) {
        ESP_LOGI(TAG, "Transcript already received, skipping.");
        return;
    }

    sleep(2); // delay for polling

    ESP_LOGI(TAG, "Setting URL");
    char* url = malloc((strlen(id) + 45));
    if (!url) {
        ESP_LOGI(TAG, "Memory allocation failed for URL");
        return;
    }

    snprintf(url, (strlen(id) + 45), "https://api.eu.assemblyai.com/v2/transcript/%s", id);
    ESP_LOGI(TAG, "Generated URL: %s", url);

    esp_http_client_set_url(client, url);
    free(url);

    esp_http_client_set_header(client, "Authorization", API_KEY);
    esp_http_client_delete_header(client, "Content-type");

    esp_http_client_set_method(client, HTTP_METHOD_GET);

    ESP_LOGI(TAG, "Sending request");
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
}

void make_sdcard_ready()
{
    ESP_LOGI(TAG, "SD-kaart mounten...");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5
    };

    sdmmc_card_t *card;
    const char mount_point[] = "/sdcard";
    
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;  // LyraT gebruikt 1-bit SD interface

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Kan SD-kaart niet mounten");
        return;
    }
    ESP_LOGI(TAG, "SD-kaart succesvol gemount!");
}

void transcription_task(void *arg) {
    transcribe_file_with_api("/sdcard/ask.wav");
    transcription_state = STATE_INITIAL;
    vTaskDelete(NULL); 
}