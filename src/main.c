#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static TaskHandle_t screen_updater_task_handle = NULL;

volatile float isr_speed = 0;
static spinlock_t xISRLock;
const uint32_t GPIO_SENSOR_INPUT = 18;

/// @brief Timer needs to be started before calling this function
/// @return Time since boot in milli seconds
uint64_t millis() {
	return (esp_timer_get_time() / 1000UL);
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
static void vRotationSensorISR(void* arg) {
	static uint64_t time_of_last_ISR = 0;
	uint64_t time = millis();

	// speed = distance traveled in 1 rotation / time this rotation took
	const uint32_t mm_per_rotation = 1953.08f;
	isr_speed = mm_per_rotation / (time - time_of_last_ISR);
	time_of_last_ISR = time;
}

/* TODO: create task to update the screen with:
		- current speed
		- avg. speed
		- battery level?
		- on time
*/ 
static void screen_update_task(void* arg) {
	TickType_t xLastWakeTime;
	BaseType_t xWasDelayed;
	const TickType_t xFrequency = 10; 
	// 1 tick seems to be 10ms, gets defined in portTICK_PERIOD_MS

	// initialize wake time 
	xLastWakeTime  = xTaskGetTickCount();
	for (;;) {
		// currently not using the return value
		xWasDelayed = xTaskDelayUntil(&xLastWakeTime, xFrequency);
		
		static float avg_speed = 0.0f;		
		// read the speed 
		taskENTER_CRITICAL(&xISRLock);
		uint32_t local_speed = isr_speed;
		taskEXIT_CRITICAL(&xISRLock);

		// speed for which we dont update the avg.speed
		const float IDLE_THRESHOLD = 2;
		if (local_speed > IDLE_THRESHOLD) {
			avg_speed += local_speed;
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

	// configuring gpio for the sensor
	gpio_config_t io_conf = {
		.pin_bit_mask = 1<<GPIO_SENSOR_INPUT,
		.mode = GPIO_MODE_INPUT,
		.intr_type = GPIO_INTR_POSEDGE,			// The reed Switch should be default "on" so this would need to be changed
		.pull_down_en = GPIO_PULLDOWN_ENABLE	// The reed Switch should be default "on" so this would need to be changed
	};

	gpio_config(&io_conf);

	// register ISR handler
	gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_EDGE);
	gpio_isr_handler_add(GPIO_SENSOR_INPUT, vRotationSensorISR, NULL);


	// create lock used to access the isr rotation counter
	spinlock_initialize(&xISRLock);
	// screen update task
	xTaskCreatePinnedToCore(screen_update_task, "screen_updater", 2048, NULL, 5, &screen_updater_task_handle, APP_CPU_NUM);
}
