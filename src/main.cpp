#include <inttypes.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "oled_LGFX.h"

static TaskHandle_t screen_updater_task_handle = NULL;
static LGFX oled;

static spinlock_t xISRLock;
static uint32_t isr_time_delta = 0;

static const gpio_num_t GPIO_SENSOR_INPUT = GPIO_NUM_27;

/// @brief Returns the time since boot in milliseconds. The timer is automatically started before app_main
/// @return Time since boot in milli seconds
uint32_t inline millis() {
    return (uint32_t)(esp_timer_get_time() / 1000UL);
}

/*		recorde the time when 1 wheel rototation was completed
        calculate the speed? or save the timestamp to reduce time in the ISR
        BUT this could cause problmes if the queue is full, 
        if i calculate the speed it does not matter if the queue is full
        ----- changed 
        now i simply increment a rotation counter and then calculate rotation/duration

        maybe use task notification (see freeRTOS) to update the scree? No
        i think a constant refresh rate is better
*/
static void IRAM_ATTR vRotationSensorISR(void *arg) {
    static uint32_t time_of_last_ISR = 0;

    uint32_t time = millis();
    uint32_t local_time_delta = time - time_of_last_ISR;
    // The reed switch is bouncy, less than 100ms means above 45 kmh which is unreasonable
    if (local_time_delta < 100) return; 
    isr_time_delta = local_time_delta;
    time_of_last_ISR = time;
}

/* TODO: create task to update the screen with:
        - current speed
        - avg. speed
        - battery level?
        - on time
*/
static void screen_update_task(void *arg) {
    TickType_t xLastWakeTime;
    BaseType_t xWasDelayed;
    // 1 tick is 10ms, see portTICK_PERIOD_MS; so period freq. is 1s 
    const TickType_t xFrequency = 100;

    static uint32_t speed = 0;
    static float speed_kmh = 0; 
    static float avg_speed = 0;
    static uint32_t last_time_delta = 0;
    static size_t clear_counter = 0;

    printf("Screenhandler started.\n");
    // initialize wake time 
    xLastWakeTime  = xTaskGetTickCount();
    for (;;) {
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, xFrequency);
        if (xWasDelayed == pdFALSE) printf("ERROR: Task was not delayed! Code took too long!\n");
        
        // read the time between the last two sensor activations
        taskENTER_CRITICAL(&xISRLock);
        uint32_t time_delta = isr_time_delta;
        taskEXIT_CRITICAL(&xISRLock);
        
        
        if (time_delta != last_time_delta) {
            // speed = distance traveled in 1 rotation / time this rotation took in ms
            // [speed] = 1 micrometer/ms = 0.001 mm/ms = 0.001 m/s
            const uint32_t um_per_rotation = 1953000;
            speed = um_per_rotation / time_delta;
            speed_kmh = speed * 0.0036f;
        } else if (clear_counter > 1) { // if the time is the exact same for 3 updates(seconds) we can assume that it hasnt been updated
            speed = 0;
            speed_kmh = 0;
            clear_counter = 0;
        } else {
            clear_counter++;
        }

        last_time_delta = time_delta;

        // speed for which we dont update the avg.speed
        const uint32_t UPDATE_THRESHOLD = 2;
        if (speed > UPDATE_THRESHOLD) {
            avg_speed += speed_kmh;
            avg_speed /= 2;
        }

        printf("delta t=%lu\tCurrent Speed: %luum/ms = %.2f km/h\n", time_delta, speed, speed_kmh);
        
        // display drawing
        // speed formating for displaying
        static char display_string_buffer[256];

        oled.setTextSize(1.6f);
        oled.drawString("Speed  |", 2, 2);
        oled.setTextDatum(top_right);
        constexpr uint32_t ms_to_min = 1000 * 60 ;
        constexpr uint32_t ms_to_h = ms_to_min * 60;
        uint32_t current_time = millis();
        uint32_t minutes = current_time/ms_to_min;
        uint32_t hours = current_time/ms_to_h;
        sprintf(display_string_buffer, "%02lu:%02lu", hours, minutes);
        oled.drawString(display_string_buffer, 120, 2);
        oled.setTextDatum(top_left);

        oled.setTextSize(2);
        sprintf(display_string_buffer, "%.1fkmh", speed_kmh);
        oled.drawString(display_string_buffer, 2, 18);
        
        //average speed
        sprintf(display_string_buffer, "Avg %.1fkmh", avg_speed);
        oled.drawString(display_string_buffer, 2, 46);
    }
}

// app_main needs to be visable to c linkage
extern "C" {
    void app_main();
}

void app_main() {
    /* disabled for testing */
    // configuring gpio for the sensor
    gpio_config_t io_conf = {
        .pin_bit_mask = 1<<GPIO_SENSOR_INPUT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,	// The reed Switch should be default "on" so this would need to be changed
        .intr_type = GPIO_INTR_POSEDGE,			// The reed Switch should be default "on" so this would need to be changed
    };

    gpio_config(&io_conf);

    // register ISR handler
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_EDGE);
    gpio_isr_handler_add(GPIO_SENSOR_INPUT, vRotationSensorISR, NULL);

    bool init_ok = oled.init();
    printf("OLED init status: %s\n", init_ok ? "Ok" : "Failed!");

    oled.clear();

    oled.setTextWrap(true, true);
    oled.setTextSize(4, 4);
    oled.drawCenterString("Tacho", oled.width()>>1, 16);
    

    oled.setTextSize(2);
    oled.setTextDatum(textdatum_t::top_left);
    
    /*
    oled.setTextDatum(textdatum_t::middle_center);
    oled.setTextSize(2);
    oled.drawString("Hello\nWorld!", oled.width()>>1, oled.height() >> 1);
    oled.setTextDatum(textdatum_t::top_left);
    */
    /*
    // Text size demo
    oled.setTextDatum(textdatum_t::top_left);
    oled.setTextSize(1);
    oled.drawString("Size 1\n", 2, 2);
    oled.setTextSize(2);
    oled.drawString("Size 2\n", 2, 10);
    oled.setTextSize(3);
    oled.drawString("Size 3\n", 2, 30);
    oled.endWrite();
    */	
    vTaskDelay(250);
    
    oled.clear();
    

    // create lock used to access the isr rotation counter
    spinlock_initialize(&xISRLock);
    // screen update task
    xTaskCreatePinnedToCore(screen_update_task, "screen_updater", 2048, NULL, 5, &screen_updater_task_handle, APP_CPU_NUM);
}
