#include "transcriber.h"

#include <stdio.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <string.h>
#include <esp_http_client.h>
#include <esp_tls.h>
#include <cJSON.h>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

void check_file_exists(const char *path);
char *audio_url = "";
char *id = "";

void (*return_callback)(char*);

void set_callback(void (*new_update_callback)(char*)){
    return_callback = new_update_callback;
}

static const char *TAG = "Transcriber";

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer;
    static int output_len;
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
            if (!esp_http_client_is_chunked_response(evt->client)) {
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

void transcribe(esp_http_client_handle_t client) {
    if (audio_url == NULL || strlen(audio_url) == 0) {
        ESP_LOGE(TAG, "No audio URL found, cannot transcribe.");
        return;
    }

    esp_http_client_set_url(client, "https://api.eu.assemblyai.com/v2/transcript");
    esp_http_client_set_header(client, "Authorization", API_KEY);
    esp_http_client_set_header(client, "Content-type", "application/json");

    cJSON *json = cJSON_CreateObject();
    cJSON_AddItemToObject(json, "audio_url", cJSON_CreateString(audio_url));
    cJSON_AddItemToObject(json, "language_detection", cJSON_CreateBool(true));

    char *jsonData = cJSON_PrintUnformatted(json);
    esp_http_client_set_post_field(client, jsonData, strlen(jsonData));
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP transcription request failed: %s", esp_err_to_name(err));
    }

    cJSON_Delete(json);
    free(jsonData);
}


void transcribe_file_with_api(char *file_path) {
    const char *server_cert_pem_start = "-----BEGIN CERTIFICATE-----\n"
"MIIFBjCCAu6gAwIBAgIRAIp9PhPWLzDvI4a9KQdrNPgwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjQwMzEzMDAwMDAw\n"
"WhcNMjcwMzEyMjM1OTU5WjAzMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3Mg\n"
"RW5jcnlwdDEMMAoGA1UEAxMDUjExMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIB\n"
"CgKCAQEAuoe8XBsAOcvKCs3UZxD5ATylTqVhyybKUvsVAbe5KPUoHu0nsyQYOWcJ\n"
"DAjs4DqwO3cOvfPlOVRBDE6uQdaZdN5R2+97/1i9qLcT9t4x1fJyyXJqC4N0lZxG\n"
"AGQUmfOx2SLZzaiSqhwmej/+71gFewiVgdtxD4774zEJuwm+UE1fj5F2PVqdnoPy\n"
"6cRms+EGZkNIGIBloDcYmpuEMpexsr3E+BUAnSeI++JjF5ZsmydnS8TbKF5pwnnw\n"
"SVzgJFDhxLyhBax7QG0AtMJBP6dYuC/FXJuluwme8f7rsIU5/agK70XEeOtlKsLP\n"
"Xzze41xNG/cLJyuqC0J3U095ah2H2QIDAQABo4H4MIH1MA4GA1UdDwEB/wQEAwIB\n"
"hjAdBgNVHSUEFjAUBggrBgEFBQcDAgYIKwYBBQUHAwEwEgYDVR0TAQH/BAgwBgEB\n"
"/wIBADAdBgNVHQ4EFgQUxc9GpOr0w8B6bJXELbBeki8m47kwHwYDVR0jBBgwFoAU\n"
"ebRZ5nu25eQBc4AIiMgaWPbpm24wMgYIKwYBBQUHAQEEJjAkMCIGCCsGAQUFBzAC\n"
"hhZodHRwOi8veDEuaS5sZW5jci5vcmcvMBMGA1UdIAQMMAowCAYGZ4EMAQIBMCcG\n"
"A1UdHwQgMB4wHKAaoBiGFmh0dHA6Ly94MS5jLmxlbmNyLm9yZy8wDQYJKoZIhvcN\n"
"AQELBQADggIBAE7iiV0KAxyQOND1H/lxXPjDj7I3iHpvsCUf7b632IYGjukJhM1y\n"
"v4Hz/MrPU0jtvfZpQtSlET41yBOykh0FX+ou1Nj4ScOt9ZmWnO8m2OG0JAtIIE38\n"
"01S0qcYhyOE2G/93ZCkXufBL713qzXnQv5C/viOykNpKqUgxdKlEC+Hi9i2DcaR1\n"
"e9KUwQUZRhy5j/PEdEglKg3l9dtD4tuTm7kZtB8v32oOjzHTYw+7KdzdZiw/sBtn\n"
"UfhBPORNuay4pJxmY/WrhSMdzFO2q3Gu3MUBcdo27goYKjL9CTF8j/Zz55yctUoV\n"
"aneCWs/ajUX+HypkBTA+c8LGDLnWO2NKq0YD/pnARkAnYGPfUDoHR9gVSp/qRx+Z\n"
"WghiDLZsMwhN1zjtSC0uBWiugF3vTNzYIEFfaPG7Ws3jDrAMMYebQ95JQ+HIBD/R\n"
"PBuHRTBpqKlyDnkSHDHYPiNX3adPoPAcgdF3H2/W0rmoswMWgTlLn1Wu0mrks7/q\n"
"pdWfS6PJ1jty80r2VKsM/Dj3YIDfbjXKdaFU5C+8bhfJGqU3taKauuz0wHVGT3eo\n"
"6FlWkWYtbt4pgdamlwVeZEW+LM7qZEJEsMNPrfC03APKmZsJgpWCDWOKZvkZcvjV\n"
"uYkQ4omYCTX5ohy+knMjdOmdH9c7SpqEWBDC86fiNex+O0XOMEZSa8DA\n"
"-----END CERTIFICATE-----\n";

esp_http_client_config_t config = {
    .url = "https://api.eu.assemblyai.com/v2",
    .event_handler = _http_event_handler,
    .cert_pem = server_cert_pem_start,
};

esp_http_client_handle_t client = esp_http_client_init(&config);
if (client == NULL) {
    ESP_LOGE(TAG, "Failed to init HTTP client");
    return;
}

ESP_LOGI(TAG, "Uploading file to AssemblyAI");
upload_file_to_assembly(client, file_path);

ESP_LOGI(TAG, "Sending transcription request...");
transcribe(client);

esp_http_client_cleanup(client);
vTaskDelete(NULL);
}

void handle_http_event_finish(esp_http_client_handle_t client, cJSON *json) {
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return;
    }

    if (cJSON_GetObjectItem(json, "status") != NULL) {
        char* status = cJSON_GetObjectItem(json, "status")->valuestring;
        if (strcmp(status, "error") == 0) {
            ESP_LOGE(TAG, "Status error, returning");
            return;
        }
    }

    uint8_t state = 0;

    if (cJSON_GetObjectItem(json, "id") != NULL) {
        state = 1;
    }
    
    if (cJSON_GetObjectItem(json, "text") != NULL  && !cJSON_IsNull(cJSON_GetObjectItem(json, "text"))) {
        state = 2;
    }
    
    switch (state) {
        case 0:
            cJSON *temp_url = cJSON_GetObjectItem(json, "upload_url");
            if (temp_url != NULL) {
                audio_url = cJSON_GetStringValue(temp_url);
                ESP_LOGI(TAG, "extracted audio_url: %s\n", audio_url);
            }
            break;
        case 1:
            cJSON *temp_id = cJSON_GetObjectItem(json, "id");
            if (temp_id != NULL) {
                id = cJSON_GetStringValue(temp_id);
                ESP_LOGI(TAG, "extracted id:\n%s\n", id);
                esp_http_client_close(client);
                get_transcript(client);
            }
            break;
        case 2:
            cJSON *temp_text = cJSON_GetObjectItem(json, "text");
            if (temp_text != NULL) {
                char *text = cJSON_GetStringValue(temp_text);
                ESP_LOGI(TAG, "extracted text:\n%s\n", text);
                return_callback(text);
            }
            break;
    }
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

void check_file_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        ESP_LOGI(TAG, "File exists: %s\n", path);
    } else {
        ESP_LOGE(TAG, "File does NOT exist: %s\n", path);
    }
}

esp_err_t transcriber_transcribe_file(const char *file_path) {
    if (file_path == NULL) {
        ESP_LOGE(TAG, "File path is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    transcribe_file_with_api((char *)file_path);
    return ESP_OK;
}