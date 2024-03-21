/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <task.h>

#include "gfx.h"
#include "ssd1306.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>

const uint BTN_1_OLED = 28;
const uint BTN_2_OLED = 26;
const uint BTN_3_OLED = 27;

const uint LED_1_OLED = 20;
const uint LED_2_OLED = 21;
const uint LED_3_OLED = 22;

const uint TRIGGER_PIN = 2;
const uint ECHO_PIN = 3;

QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;
SemaphoreHandle_t xSemaphoreTrigger;

void setup_oled_pins() {
    gpio_init(LED_1_OLED);
    gpio_set_dir(LED_1_OLED, GPIO_OUT);

    gpio_init(LED_2_OLED);
    gpio_set_dir(LED_2_OLED, GPIO_OUT);

    gpio_init(LED_3_OLED);
    gpio_set_dir(LED_3_OLED, GPIO_OUT);

    gpio_init(BTN_1_OLED);
    gpio_set_dir(BTN_1_OLED, GPIO_IN);
    gpio_pull_up(BTN_1_OLED);

    gpio_init(BTN_2_OLED);
    gpio_set_dir(BTN_2_OLED, GPIO_IN);
    gpio_pull_up(BTN_2_OLED);

    gpio_init(BTN_3_OLED);
    gpio_set_dir(BTN_3_OLED, GPIO_IN);
    gpio_pull_up(BTN_3_OLED);
}

void pin_callback(uint gpio, uint32_t events) {
    int64_t timestamp = to_us_since_boot(get_absolute_time());

    if (gpio == ECHO_PIN) {
        if (events == GPIO_IRQ_EDGE_FALL || events == GPIO_IRQ_EDGE_RISE) {
            xQueueSendFromISR(xQueueTime, &timestamp, NULL);
        }
    }
}

void trigger_task(void *p) {
    printf("Trigger task\n");
    while (true) {
        gpio_put(TRIGGER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_put(TRIGGER_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(900));
        xSemaphoreGive(xSemaphoreTrigger);
    }
}

void echo_task(void *p) {
    // does the reading of the time the echo pin is high and calculates the distance
    // gets information from the time queue
    // sends the distance in cm to the distance queue

    int64_t time;
    static int64_t start = 0, end = 0;
    float distance;
    bool start_time_set = false;

    printf("Echo task\n");

    while (true) {
        if (xQueueReceive(xQueueTime, &time, portMAX_DELAY)) {
            if (!start_time_set) {
                start = time;
                start_time_set = true;
            } else {
                end = time;
                if (end > start) {
                    int64_t duration = end - start;
                    if (duration > 4000) {
                        distance = -1; // error
                    } else {
                        distance = duration * 0.034 / 2;
                    }
                    xQueueSend(xQueueDistance, &distance, portMAX_DELAY);
                    start_time_set = false;
                }
            }
        }
    }
}

void oled_task(void *p) {
    // displays the distance on the OLED and a line that represents the distance
    // uses the semaphore and the distance queue

    float distance;
    char str[20];

    printf("OLED task\n");

    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    printf("Inicializando btn and LEDs\n");
    setup_oled_pins();

    while (true) {
        if (xSemaphoreTake(xSemaphoreTrigger, portMAX_DELAY)) {
            if (xQueueReceive(xQueueDistance, &distance, portMAX_DELAY)) {
                gfx_clear_buffer(&disp);
                if (distance == -1) {
                    snprintf(str, sizeof(str), "Dist: Error");
                } else {
                    snprintf(str, sizeof(str), "Dist: %.2f cm", distance);
                }

                gfx_draw_string(&disp, 0, 0, 1, str);

                int line_length = (int)(distance * (128.0f / 400.0f));
                gfx_draw_line(&disp, 0, 20, line_length, 20);
                gfx_show(&disp);
            }
        }
    }
}

int main() {
    stdio_init_all();

    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_pull_up(TRIGGER_PIN);

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);

    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &pin_callback);

    xQueueTime = xQueueCreate(32, sizeof(int64_t));
    xQueueDistance = xQueueCreate(32, sizeof(float));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(trigger_task, "Trigger", 1024, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo", 1024, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED", 1024, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
