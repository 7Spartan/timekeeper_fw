#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "encoder.h"

static const char *TAG = "encoder";
#define ENCODER_CLK 34
#define ENCODER_DT 35

static volatile int32_t encoder_count = 0;
static volatile int lastStateCLK;
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR encoder_isr_handler(void *arg) {
    int currentStateCLK = gpio_get_level(ENCODER_CLK);
    if (currentStateCLK != lastStateCLK) {
        if (gpio_get_level(ENCODER_DT) != currentStateCLK) {
            portENTER_CRITICAL_ISR(&spinlock);
            encoder_count--;
            portEXIT_CRITICAL_ISR(&spinlock);
        } else {
            portENTER_CRITICAL_ISR(&spinlock);
            encoder_count++;
            portEXIT_CRITICAL_ISR(&spinlock);
        }
    }
    lastStateCLK = currentStateCLK;
}

void encoder_init(void) {
    ESP_LOGI(TAG, "Initializing encoder");
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << ENCODER_CLK) | (1ULL << ENCODER_DT),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);

    lastStateCLK = gpio_get_level(ENCODER_CLK);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(ENCODER_CLK, encoder_isr_handler, NULL);
}

int32_t encoder_get_count(void) {
    int32_t count;
    portENTER_CRITICAL(&spinlock);
    count = encoder_count;
    portEXIT_CRITICAL(&spinlock);
    return count;
}

void encoder_print_task(void *pvParameters) {
    while (1) {
        ESP_LOGI(TAG, "Encoder Value: %ld", encoder_get_count());
        vTaskDelay(pdMS_TO_TICKS(1000)); // Print every second
    }
}
