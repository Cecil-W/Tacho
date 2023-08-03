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
	if (local_time_delta < 100) return; // The reed switch is bouncy, 100ms is above 45 kmh which should be enough
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
	// 1 tick seems to be 10ms, gets defined in portTICK_PERIOD_MS
	const TickType_t xFrequency = 100;
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
		taskEXIT_CRITICAL(&xISRLock);
		// if the time didnt get updated we can skip our calculations
		if (local_isr_time_delta > 0) {
			// speed = distance traveled in 1 rotation / time this rotation took in ms
			// [speed] = 1 micrometer/ms = 0.001 mm/ms = 0.001 m/s
			const uint32_t um_per_rotation = 1953000;
			speed = um_per_rotation / local_isr_time_delta;
		}

		// speed formating for displaying
		float speed_kmh = (speed) * 0.0036f;
		printf("delta t=%lu\tCurrent Speed: %luum/ms = %.2f km/h\n", local_isr_time_delta, speed, speed_kmh);
		static char string[256];
		sprintf(string, "Speed: %.1f", speed_kmh);

		// speed for which we dont update the avg.speed
		const uint32_t IDLE_THRESHOLD = 2;
		if (speed > IDLE_THRESHOLD) {
			avg_speed += speed;
			avg_speed /= 2; 
		}

		// display drawing
		oled.startWrite();
		oled.drawString(string, oled.width()>>1, 10);
		oled.endWrite();
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
	printf("init status: %s\n", init_ok ? "Ok" : "Failed!");

	oled.startWrite();
	oled.setTextDatum(textdatum_t::middle_center);
	oled.drawString("Hello World!", oled.width()>>1, oled.height() >> 1);
	oled.setTextDatum(textdatum_t::top_left);
	oled.endWrite();

	// create lock used to access the isr rotation counter
	spinlock_initialize(&xISRLock);
	// screen update task
	xTaskCreatePinnedToCore(screen_update_task, "screen_updater", 2048, NULL, 5, &screen_updater_task_handle, APP_CPU_NUM);

	while (true) {
		vTaskDelay(500);
		printf("main still running!\n");
	}
}
