#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

static QueueHandle_t speed_queue = NULL;
static TaskHandle_t screen_updater_task_handle = NULL;
// currently i dont need to seperate the screen update and speed calculation
//static TaskHandle_t speed_calc_task_handle = NULL;

volatile uint32_t ulRotationCount = 0;
static spinlock_t xISRLock;
const uint64_t GPIO_SENSOR_INPUT = 18;


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
	ulRotationCount++;
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
		//currently not using the return value
		xWasDelayed = xTaskDelayUntil(&xLastWakeTime, xFrequency);
		if (xWasDelayed == pdFALSE) {
			// dimension=[cm/ms] == 10 m/s == 
			float ulSpeed = 0;
			static float avg_speed = 0.0f;
			// cycle multiplier to account for periods where no rotation happens
			static uint16_t usNoRotationCount = 1;

			// read and then clear rotation counter 
			uint32_t ulRotations = 0;
			taskENTER_CRITICAL(&xISRLock);
			ulRotations = ulRotationCount;
			ulRotationCount = 0;
			taskEXIT_CRITICAL(&xISRLock);

			// calulate the speed if its above 0, else account for empty periods
			if (ulRotations > 0) {
				// i think i have 28-622 tires -> diam = 622mm -> C = d*pi = 62.2cm*3.14 = 195.308cm
				const float cmPerRotation = 195.308f; 
				ulSpeed = (ulRotations * cmPerRotation) / (xFrequency * usNoRotationCount * portTICK_PERIOD_MS);
				// reset the empty period multiplier
				usNoRotationCount = 1;
			} else {
				// no full rotation happened this period, 
				// so next period we devide the rotations by 1 more cycle 
				usNoRotationCount++;
			}
			// speed for which we dont update the avg.speed
			const float IDLE_THRESHOLD = 2;
			if (ulSpeed > IDLE_THRESHOLD) {
				avg_speed += ulSpeed;
				avg_speed /= 2; 
			}
		}
			// TODO screen printing, maybe move to another task
			// then i would need to put a mutex or semaphore on the speed
	}
}



void app_main() {
	gpio_config_t io_conf = {
		.pin_bit_mask = 1<<GPIO_SENSOR_INPUT,
		.mode = GPIO_MODE_INPUT,
		.intr_type = GPIO_INTR_POSEDGE,			// The reed Switch should be default "on" so this would need to be changed
		.pull_down_en = GPIO_PULLDOWN_ENABLE	// The reed Switch should be default "on" so this would need to be changed
	};

	gpio_config(&io_conf);

	// queue for timestamp of sensor activation, or speed not sure yet
	speed_queue = xQueueCreate(2, sizeof(uint32_t));

	// create lock used to access the isr rotation counter
	spinlock_initialize(&xISRLock);
	// screen update task
	xTaskCreatePinnedToCore(screen_update_task, "screen_updater", 2048, NULL, 5, &screen_updater_task_handle, APP_CPU_NUM);

	// register ISR handler
	gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_EDGE);
	gpio_isr_handler_add(GPIO_SENSOR_INPUT, vRotationSensorISR, NULL);
}
