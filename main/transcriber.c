#include "transcriber.h"
#include "chatgpt_api.h"

char *audio_url = "";
char *id = "";

void (*return_callback)(char*);

void set_callback(void (*new_update_callback)(char*)){
    return_callback = new_update_callback;
}

static const char *TAG = "Text-to-speech";

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
            if (temp_url != NULL)
            {
                audio_url = cJSON_GetStringValue(temp_url);
                ESP_LOGI(TAG, "extracted audio_url: %s\n", audio_url);
            }
        break;
        case 1: // transcript executed, retrieving id transcript
            cJSON *temp_id = cJSON_GetObjectItem(json, "id");
            if (temp_id != NULL)
            {
                // printf(cJSON_PrintUnformatted(temp_id));
                id = cJSON_GetStringValue(temp_id);
                ESP_LOGI(TAG, "extracted id:\n%s\n", id);
                esp_http_client_close(client); // close client so reset connection
                get_transcript(client);
            }
            break;
            case 2:
                cJSON *temp_text = cJSON_GetObjectItem(json, "text");
                if (temp_text != NULL)
                {
                    char *text = cJSON_GetStringValue(temp_text);
                    ESP_LOGI(TAG, "extracted text:\n%s\n", text);
                    ask_chatgpt(text);
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

const char *server_cert_pem_start = 
"-----BEGIN CERTIFICATE-----\n"
"MIIEkjCCA3qgAwIBAgITBn+USionzfP6wq4rAfkI7rnExjANBgkqhkiG9w0BAQsF\n"
"ADCBmDELMAkGA1UEBhMCVVMxEDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNj\n"
"b3R0c2RhbGUxJTAjBgNVBAoTHFN0YXJmaWVsZCBUZWNobm9sb2dpZXMsIEluYy4x\n"
"OzA5BgNVBAMTMlN0YXJmaWVsZCBTZXJ2aWNlcyBSb290IENlcnRpZmljYXRlIEF1\n"
"dGhvcml0eSAtIEcyMB4XDTE1MDUyNTEyMDAwMFoXDTM3MTIzMTAxMDAwMFowOTEL\n"
"MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
"9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
"IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
"VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
"93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
"jgSubJrIqg0CAwEAAaOCATEwggEtMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/\n"
"BAQDAgGGMB0GA1UdDgQWBBSEGMyFNOy8DJSULghZnMeyEE4KCDAfBgNVHSMEGDAW\n"
"gBScXwDfqgHXMCs4iKK4bUqc8hGRgzB4BggrBgEFBQcBAQRsMGowLgYIKwYBBQUH\n"
"MAGGImh0dHA6Ly9vY3NwLnJvb3RnMi5hbWF6b250cnVzdC5jb20wOAYIKwYBBQUH\n"
"MAKGLGh0dHA6Ly9jcnQucm9vdGcyLmFtYXpvbnRydXN0LmNvbS9yb290ZzIuY2Vy\n"
"MD0GA1UdHwQ2MDQwMqAwoC6GLGh0dHA6Ly9jcmwucm9vdGcyLmFtYXpvbnRydXN0\n"
"LmNvbS9yb290ZzIuY3JsMBEGA1UdIAQKMAgwBgYEVR0gADANBgkqhkiG9w0BAQsF\n"
"AAOCAQEAYjdCXLwQtT6LLOkMm2xF4gcAevnFWAu5CIw+7bMlPLVvUOTNNWqnkzSW\n"
"MiGpSESrnO09tKpzbeR/FoCJbM8oAxiDR3mjEH4wW6w7sGDgd9QIpuEdfF7Au/ma\n"
"eyKdpwAJfqxGF4PcnCZXmTA5YpaP7dreqsXMGz7KQ2hsVxa81Q4gLv7/wmpdLqBK\n"
"bRRYh5TmOTFffHPLkIhqhBGWJ6bt2YFGpn6jcgAKUj6DiAdjd4lpFw85hdKrCEVN\n"
"0FE6/V1dN2RMfjCyVSRCnTawXZwXgWHxyvkQAiSr6w10kY17RSlQOYiypok1JR4U\n"
"akcjMS9cmvqtmg5iUaQqqcT5NJ0hGA==\n"
"-----END CERTIFICATE-----\n";

    
// opzetten basis configuratie

    esp_http_client_config_t config = {
        .url = "https://api.eu.assemblyai.com/v2",
        .event_handler = _http_event_handler,
        .cert_pem = server_cert_pem_start,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    ESP_LOGI(TAG, "Uploading file to assembly");
    upload_file_to_assembly(client, file_path);

    ESP_LOGI(TAG, "Transcribing....");
    transcribe(client);

    esp_http_client_cleanup(client);
    vTaskDelete(0);
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
    sleep(2);

    ESP_LOGI(TAG, "Setting URL");
    char* url = malloc((strlen(id) + 45));
    if (!url) {
        // Foutafhandeling als malloc niet lukt
        ESP_LOGI(TAG, "Error: Memory allocation failed for URL");
        return;
    }

    //Formatteer de URL met de gegeven id
    snprintf(url, (strlen(id) + 45), "https://api.eu.assemblyai.com/v2/transcript/%s", id);

    //Log de URL voor debugging
    ESP_LOGI(TAG, "Generated URL: %s\n", url);

    esp_http_client_set_url(client, url);
    free(url);

    esp_http_client_set_header(client, "Authorization", API_KEY);

    ESP_LOGI(TAG, "Setting Client");
    // remove header
    esp_http_client_delete_header(client, "Content-type");

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    
    ESP_LOGI(TAG, "Sending request");
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST request failed: %s\n", esp_err_to_name(err));
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