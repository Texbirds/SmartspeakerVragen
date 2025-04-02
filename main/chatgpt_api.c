#include "chatgpt_api.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "lcd.h"
#include "cJSON.h"  // Required to parse JSON

#define OPENAI_API_KEY   "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"  // Truncated for clarity
#define TAG              "ChatGPT"
#define MODEL            "gpt-3.5-turbo"
#define CHAT_ENDPOINT    "https://api.openai.com/v1/chat/completions"

static char response_buffer[2048];  // Full response buffer
static int response_offset = 0;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!evt->user_data) {
                if (response_offset + evt->data_len < sizeof(response_buffer)) {
                    memcpy(response_buffer + response_offset, evt->data, evt->data_len);
                    response_offset += evt->data_len;
                    response_buffer[response_offset] = '\0'; // Null terminate
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

void ask_chatgpt(const char *question) {
    response_offset = 0;
    memset(response_buffer, 0, sizeof(response_buffer));

    char post_data[1024];
    snprintf(post_data, sizeof(post_data),
        "{\"model\": \"%s\", \"messages\": [{\"role\": \"user\", \"content\": \"%s\"}]}",
        MODEL, question);

    esp_http_client_config_t config = {
        .url = CHAT_ENDPOINT,
        .event_handler = _http_event_handler,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", "Bearer " OPENAI_API_KEY);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %" PRId64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    esp_http_client_cleanup(client);

    // Parse JSON and extract assistant message
    cJSON *root = cJSON_Parse(response_buffer);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse response as JSON");
        return;
    }

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!cJSON_IsArray(choices)) {
        ESP_LOGE(TAG, "Invalid JSON: no 'choices' array");
        cJSON_Delete(root);
        return;
    }

    cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(first_choice, "message");
    cJSON *content = cJSON_GetObjectItem(message, "content");

    if (cJSON_IsString(content)) {
        const char *reply = content->valuestring;
        ESP_LOGI(TAG, "ChatGPT reply: %s", reply);
        lcd_scroll_set_text(reply);
    } else {
        ESP_LOGE(TAG, "Invalid JSON: 'content' is not a string");
    }

    cJSON_Delete(root);
}