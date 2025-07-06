#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_event_base.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/gpio_types.h"
#include <rom/ets_sys.h>
#include <stdint.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "esp_netif.h"
#include "portmacro.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "ssd1306.h"
#include "font8x8_basic.h"

#define DHT_PIN GPIO_NUM_4

#define WIFI_SSID "range"
#define WIFI_PASS "dillyboy11"

SSD1306_t dev;
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
}

void wifi_init() {
    esp_netif_init();
    wifi_event_group = xEventGroupCreate();
    esp_event_loop_create_default();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &wifi_event_handler,
                                        NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT,
                                       IP_EVENT_STA_GOT_IP,
                                       &wifi_event_handler,
                                       NULL,
                                       NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();

    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true,
                        portMAX_DELAY);
}

void send_data_http(float t, float h) {
    char post_data[64];
    snprintf(post_data, sizeof(post_data),"{\"temperature\": %.2f, \"humidity\": %.2f}", t, h);

    esp_http_client_config_t config = {
        .url = "http://192.168.4.107:5000/upload",
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI("HTTP", "POST status = %d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE("HTTP", "POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

}



void delay_us(int us) {
    ets_delay_us(us); // microsecond delay
}

void set_pin_output() {
    gpio_set_direction(DHT_PIN, GPIO_MODE_OUTPUT);
}

void set_pin_input() {
    gpio_set_direction(DHT_PIN, GPIO_MODE_INPUT);
}

void write_pin(int level) {
    gpio_set_level(DHT_PIN, level);
}

int read_pin() {
    return gpio_get_level(DHT_PIN);
}

bool wait_for_pin_level(int level, uint32_t timeout_us) {
    uint32_t elapsed = 0;
    while (read_pin() != level) {
        ets_delay_us(1);
        elapsed++;
        if (elapsed >= timeout_us) {
            return false;  // timeout
        }
        if ((elapsed % 1000) == 0) {
            taskYIELD();   // yield every 1 ms to avoid watchdog trigger
        }
    }
    return true;
}


// We are going to manually read the data for the DHT11
// NOTE: Specs taken from: https://github.com/dhrubasaha08/DHT11


bool dht_read_data(uint8_t *data) {
    char *taskName = pcTaskGetName(NULL);
    int i,j;

    // We send the start signal

    // NOTE: The MCU (like an Arduino) sends a start signal by pulling the data line low for at least 18ms.
    set_pin_output();
    write_pin(0);
    vTaskDelay(pdMS_TO_TICKS(20)); // >=18ms
    // NOTE: The MCU then pulls the line high for 20-40us to indicate that it's ready to receive a response.
    write_pin(1);
    delay_us(30); // 20-40us
    set_pin_input();

    // Part 2: response
    // NOTE: Upon detecting the start signal from the MCU, the DHT11 sends a response signal.
    // This response consists of a 80us low voltage level followed by an 80us high voltage level.
    if (!wait_for_pin_level(0, 85)) return false;
    if (!wait_for_pin_level(1, 85)) return false;
    for (i = 0; i < 85 && read_pin(); i++) {delay_us(1);}

    /* NOTE: The DHT11 transmits its data in a series of pulses. Each bit of data is represented by a specific combination of high and low voltage durations.
     * A '0' is represented by 50us of low voltage followed by 26-28us of high voltage.
     * A '1' is represented by 50us of low voltage followed by 70us of high voltage.
     * The DHT11 sends 40 bits of data in total: 16 bits for humidity, 16 bits for temperature, and 8 bits for checksum.
     * The checksum is the last 8 bits of the sum of the first 32 bits. It's used to verify data integrity
     * */

    // 40 bits => 5 bytes
    for (j = 0; j < 5; j++) {
        data[j] = 0;
        for (i = 0; i < 8; i++) {
            if (!wait_for_pin_level(1, 100)) return false;
            delay_us(30); // wait to sample
            data[j] <<=1;
            if (read_pin()) {
                data[j] |= 1;
            }
            if (!wait_for_pin_level(0, 100)) return false;
        }
    }

    // Checksum time:
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) {
        ESP_LOGW(taskName, "CHECKSUM FAILED...\n");
        return false;
    }

    return true;
}


void display_info(float f, float h) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Temp: %.2fF", f);
    ssd1306_clear_screen(&dev, false);
    ssd1306_display_text(&dev, 3, buf, sizeof(buf), false);
    snprintf(buf, sizeof(buf), "Humid: %.2f%%", h);
    ssd1306_display_text(&dev, 5, buf, sizeof(buf), false);
}


void dht_task(void *pvParameter)
{
    uint8_t data[5];
    char *taskName = pcTaskGetName(NULL);
    while (1)
    {
        if (dht_read_data(data)) {
            float h = data[0] + data[1] / 100.0f;
            float t = data[2] + data[3] / 100.0f;
            float f = (t * 1.8f) + 32;

            ESP_LOGI(taskName, "Temperature: %.2f°F, Humidity: %.2f%%", f, h);
            display_info(f, h);
            send_data_http(f, h);
        } else {
            ESP_LOGW(taskName, "Failed to read from DHT sensor");
        }

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void app_main(void)
{
    ESP_LOGI("APP", "app_main start before ssd init");
    i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
    ssd1306_init(&dev, 128, 64);
    ssd1306_clear_screen(&dev, false);
    ssd1306_contrast(&dev, 0xff);
    ssd1306_display_text(&dev, 0, "Starting up...", 14, false);

    ESP_LOGI("APP", "app_main before wifi init");
    nvs_flash_init();
    wifi_init();

    esp_rom_gpio_pad_select_gpio(DHT_PIN);
    gpio_set_pull_mode(DHT_PIN, GPIO_PULLUP_ONLY);

    ESP_LOGI("APP", "After wifi_init()");
    xTaskCreate(dht_task, "dht_task", 4096, NULL, 5, NULL);
}

