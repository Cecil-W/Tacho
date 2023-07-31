#include <inttypes.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "driver/i2c.h"

static TaskHandle_t screen_updater_task_handle = NULL;

static spinlock_t xISRLock;
static uint32_t isr_time_delta = 0;

static const uint32_t GPIO_SENSOR_INPUT = 27;

/// @brief Timer needs to be started before calling this function
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
static void IRAM_ATTR vRotationSensorISR() {
	static uint32_t time_of_last_ISR = 0;

	uint32_t time = millis();
	uint32_t local_time_delta = time - time_of_last_ISR;
	if (local_time_delta < 100) return;// assuming bouncing from the input 
	isr_time_delta = local_time_delta;
	time_of_last_ISR = time;
}

/* TODO: create task to update the screen with:
		- current speed
		- avg. speed
		- battery level?
		- on time
*/
static void screen_update_task() {
	TickType_t xLastWakeTime;
	BaseType_t xWasDelayed;
	const TickType_t xFrequency = 100; 
	// 1 tick seems to be 10ms, gets defined in portTICK_PERIOD_MS
	static uint32_t avg_speed = 0;

    printf("Hello from screen update task\n");
	// initialize wake time 
	xLastWakeTime  = xTaskGetTickCount();
	for (;;) {
		// currently not using the return value
		xWasDelayed = xTaskDelayUntil(&xLastWakeTime, xFrequency);
		uint32_t speed = 0;
		
		// read the time between the last two sensor activations
		taskENTER_CRITICAL(&xISRLock);
		uint32_t local_isr_time_delta = isr_time_delta;
		isr_time_delta = 0;
		taskEXIT_CRITICAL(&xISRLock);
		// if the time didnt get updated we can skip our calculations
		if (local_isr_time_delta > 0) {
			// speed = distance traveled in 1 rotation / time this rotation took in ms
			// [speed] = 1 mm/ms = 1 m/s
			const uint32_t mm_per_rotation = 1953;
			speed = mm_per_rotation / local_isr_time_delta;
		}

		float speed_kmh = speed * 3.6f;
		printf("delta t=%lu\tCurrent Speed: %fmm/ms = %f km/h\n", local_isr_time_delta, speed, speed_kmh);
		// speed for which we dont update the avg.speed
		const uint32_t IDLE_THRESHOLD = 2;
		if (speed > IDLE_THRESHOLD) {
			avg_speed += speed;
			avg_speed /= 2; 
		}
	}
}



void app_main() {
	// starting the timer, used by the sensor ISR
	/* looks like the timer get initialized automaticly before app_main
	esp_timer_create_args_t timer_conf = {};
	timer_conf.arg = 
	esp_timer_create(&timer_conf, &timer_handle)
	*/

	

	/* disabled for testing */
	// configuring gpio for the sensor
	gpio_config_t io_conf = {
		.pin_bit_mask = 1<<GPIO_SENSOR_INPUT,
		.mode = GPIO_MODE_INPUT,
		.intr_type = GPIO_INTR_POSEDGE,			// The reed Switch should be default "on" so this would need to be changed
		.pull_down_en = GPIO_PULLDOWN_DISABLE,	// The reed Switch should be default "on" so this would need to be changed
        .pull_up_en = GPIO_PULLUP_ENABLE
	};

	gpio_config(&io_conf);

	// register ISR handler
	gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_EDGE);
	gpio_isr_handler_add(GPIO_SENSOR_INPUT, vRotationSensorISR, NULL);


    
	// create lock used to access the isr rotation counter
	spinlock_initialize(&xISRLock);
	// screen update task
	xTaskCreatePinnedToCore(screen_update_task, "screen_updater", 2048, NULL, 5, &screen_updater_task_handle, APP_CPU_NUM);

    while (true) {
        vTaskDelay(500);
        printf("main still running!\n");
    }
}
