#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "encoder.h"
#include "gatttest.h"
#include "i2coled.h"
#include "sdkconfig.h"

/* Flask Server IP Address & Port */
#define DB_WEB_SERVER "192.168.13.140"  // Replace with your Flask server's IP
#define DB_WEB_PORT "5000"
#define DB_WEB_PATH "/post-data"
#define ENC_BUTTON 32
#define ESP_INTR_FLAG_DEFAULT 0
#define DEBOUNCE_TIME_MS 50

/* JSON Data */
static const char *TAG = "http_post_example";
static QueueHandle_t button_press_wifi_evt_queue = NULL;
static QueueHandle_t button_press_ble_evt_queue = NULL;
static TimerHandle_t debounce_timer = NULL;
static volatile bool button_pressed = false;


bool MONITOR_ENCODER = false;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
   if (!button_pressed) {
        button_pressed = true;
        xTimerStartFromISR(debounce_timer, NULL);
    }
}

static void debounce_timer_callback(TimerHandle_t xTimer){
    uint32_t gpio_level = gpio_get_level(ENC_BUTTON);
    if(gpio_level == 0){
        uint32_t gpio_num = ENC_BUTTON;
        xQueueSend(button_press_wifi_evt_queue, &gpio_num, 0);
        xQueueSend(button_press_ble_evt_queue, &gpio_num, 0);
    }
    button_pressed = false;
}

static void http_post_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[128];

    while(1) {
        int io_num;
        if(xQueueReceive(button_press_wifi_evt_queue, &io_num, portMAX_DELAY)){
            ESP_LOGI(TAG, "Button pressed, sending POST request");
            int err = getaddrinfo(DB_WEB_SERVER, DB_WEB_PORT, &hints, &res);
            if(err != 0 || res == NULL) {
                ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }

            addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
            ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

            s = socket(res->ai_family, res->ai_socktype, 0);
            if(s < 0) {
                ESP_LOGE(TAG, "... Failed to allocate socket.");
                freeaddrinfo(res);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }
            ESP_LOGI(TAG, "... allocated socket");

            if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
                ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
                close(s);
                freeaddrinfo(res);
                vTaskDelay(4000 / portTICK_PERIOD_MS);
                continue;
            }

            ESP_LOGI(TAG, "... connected");
            freeaddrinfo(res);

            /* Prepare HTTP POST Request */
            char request[512];
            int32_t duration = encoder_get_count();
            char post_data[128]; 

            // Format the JSON data separately
            snprintf(post_data, sizeof(post_data), "{\"time\":\"14,14,14\",\"start\":1,\"duration\":%ld}", duration);

            // Calculate Content-Length correctly
            int content_length = strlen(post_data);

            snprintf(request, sizeof(request),
                    "POST %s HTTP/1.1\r\n"
                    "Host: %s:%s\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: %d\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "%s",
                    DB_WEB_PATH, DB_WEB_SERVER, DB_WEB_PORT, content_length, post_data);

            /* Send HTTP POST Request */
            if (write(s, request, strlen(request)) < 0) {
                ESP_LOGE(TAG, "... socket send failed");
                close(s);
                vTaskDelay(4000 / portTICK_PERIOD_MS);
                continue;
            }
            ESP_LOGI(TAG, "... socket send success");


            struct timeval receiving_timeout;
            receiving_timeout.tv_sec = 5;
            receiving_timeout.tv_usec = 0;
            if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                    sizeof(receiving_timeout)) < 0) {
                ESP_LOGE(TAG, "... failed to set socket receiving timeout");
                close(s);
                vTaskDelay(4000 / portTICK_PERIOD_MS);
                continue;
            }
            ESP_LOGI(TAG, "... set socket receiving timeout success");

            /* Read HTTP Response */
            do {
                bzero(recv_buf, sizeof(recv_buf));
                r = read(s, recv_buf, sizeof(recv_buf)-1);
                if (r > 0) {
                    recv_buf[r] = 0; // Null-terminate the received data
                    ESP_LOGI(TAG, "Received: %s", recv_buf);
                }
            } while(r > 0);

            ESP_LOGI(TAG, "... done reading from socket.");
            close(s);
        }

        // vTaskDelay(5000 / portTICK_PERIOD_MS);  // Wait before sending the next request
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK( nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /* Connect to Wi-Fi */
    ESP_ERROR_CHECK(example_connect());
    encoder_init();

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << ENC_BUTTON;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    button_press_wifi_evt_queue = xQueueCreate(1, sizeof(uint32_t));
    button_press_ble_evt_queue = xQueueCreate(1, sizeof(uint32_t));

    debounce_timer = xTimerCreate("debounce_timer", pdMS_TO_TICKS(DEBOUNCE_TIME_MS), pdFALSE, NULL, debounce_timer_callback);
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(ENC_BUTTON, gpio_isr_handler, (void*) ENC_BUTTON);

    /* Start HTTP POST Task */
    xTaskCreate(&http_post_task, "http_post_task", 4096, NULL, 5, NULL);
    if (MONITOR_ENCODER){
        xTaskCreate(encoder_print_task, "encoder_print_task", 2048, NULL, 5, NULL);
    }
    xTaskCreate(gatttest, "gatttest", 4096, NULL, 5, NULL);
    xTaskCreatePinnedToCore(i2coled,"i2coled",4096,NULL,5,NULL,1);
}
