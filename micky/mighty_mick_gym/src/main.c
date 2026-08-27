/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <soc.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/types.h>
#include <zephyr/sys/util.h>

#include <stdio.h>
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/sys/atomic.h>

#include <nrf_edgeai/nrf_edgeai.h>
#include <nrf_edgeai_user_model.h>

#include "button/button.h"
#include "led/led.h"

#include "sensor/imu/imu.h"
#include "sensor/imu/burst_detector.h"


#if IS_ENABLED(CONFIG_BLE_MODE_HID)
#include "ux_state_manager.h"
#endif
#include "inference_postprocessing.h"
#include "ble/ble_common.h"

#if IS_ENABLED(CONFIG_BLE_MODE_HID)
#include "ble/hid/ble_hid.h"
#elif IS_ENABLED(CONFIG_BLE_MODE_NUS)
#include "ble/nus/ble_nus.h"
#elif IS_ENABLED(CONFIG_BLE_MODE_GATT_CUSTOM)
#include "ble/gatt/ble_gatt.h"
#endif

#define PROVIDE_INFERENCE_US_TIME_IN_REPORTS	1

/*
 * Might Mick's Gym -- burst detection settings
 */
#define DATACOLLECTION_BLOCKSIZE		256


/* TRAINING_COLLECTION
 * Define to use training-appropriate settings for burst detection...
 * To do this, set CONFIG_DATA_COLLECTION_MODE=y
 */

/* CONFIG_CONTINUOUS_COLLECTION -- only applies when TRAINING_COLLECTION
 * Use to collect lots of guard-type samples
 */



#if CONFIG_DATA_COLLECTION_MODE
#if CONFIG_IMU_SEND_BURSTS
#define TRAINING_COLLECTION
#else
#error "CONFIG_DATA_COLLECTION_MODE needs CONFIG_IMU_SEND_BURSTS=y"
#endif
#endif


#define AUTO_SAMPLE_REPORT_TIMEOUT_SECONDS		3

#define REARM_CONSERVATIVE			(uint16_t)(DATACOLLECTION_BLOCKSIZE*2)
#define REARM_STANDARD				(uint16_t)(DATACOLLECTION_BLOCKSIZE)
#define REARM_AGGRESSIVE			(uint16_t)(DATACOLLECTION_BLOCKSIZE / 3)

#ifdef TRAINING_COLLECTION
#ifdef CONFIG_CONTINUOUS_COLLECTION
#define DATA_TIMEOUT_SAMPLES	CONFIG_IMU_DATARATE_HZ
#else
#define DATA_TIMEOUT_SAMPLES		0 /* no auto timeout */
#endif
#define REARM_SAMPLES		REARM_STANDARD

#else
#define DATA_TIMEOUT_SAMPLES	(CONFIG_IMU_DATARATE_HZ*AUTO_SAMPLE_REPORT_TIMEOUT_SECONDS)
#define REARM_SAMPLES			REARM_STANDARD
#endif

/*
 * BURST_DETECTOR_CONFIG -- all the settings used with
 * the burst detector
 */
#define BURST_DETECTOR_CONFIG  \
     DATACOLLECTION_BLOCKSIZE, /* block length (number of samples) */ \
     (uint16_t)(DATACOLLECTION_BLOCKSIZE / 2), /* pre-roll, data to include PRIOR to burst trigger */ \
      1400, /* acc thresh high */ \
      800, /* acc thresh low */ \
      220, /* gyro thresh high */ \
      180, /* gyro thresh low */ \
      REARM_SAMPLES, /* number of "quiet" samples to rearm */ \
      DATA_TIMEOUT_SAMPLES, /* Timeout, e.g. CONFIG_IMU_DATARATE_HZ to output every second */



#define NRF_EDGEAI_INPUT_DATA_LEN (ACCEL_AXIS_NUM + GYRO_AXIS_NUM)

#if IS_ENABLED(CONFIG_LED_NOTIFICATION_BLINK)
#define LED_NOTIFY_BLINK_COUNT	(3)
#define LED_NOTIFY_BLINK_ON_MS	(100)
#define LED_NOTIFY_BLINK_OFF_MS (200)
#else
#define BLINK_LED_TIMER_PERIOD_MS	 (30)
#define LED_BLINK_CHANGE_BRIGHTNESS_STEP (0.005f)
#endif

#define LED_MAX_BRIGHTNESS (0.2f)

LOG_MODULE_REGISTER(main);

typedef int (*led_set_func_t)(float brightness);


typedef struct {
	int16_t start_ticks;
	int16_t end_tick;
} TickTimerValues_t;

static TickTimerValues_t inference_ticks;



#if IS_ENABLED(CONFIG_NRF_EDGEAI_GESTURE_RECOGNITION_MODEL_AXON)
static void execute_inference(flt32_t *input_data, size_t len);
#else
static void execute_inference(int16_t *input_data, size_t len);
#endif
static void send_imu_data(int16_t *input_data);
static void hw_modules_init(void);
#if IS_ENABLED(CONFIG_LED_NOTIFICATION_BLINK)
static void led_blink_set_active_led(void);
#else
static void led_glowing_timer_handler(struct k_timer *timer);
#endif
static void handle_inference_result(nrf_edgeai_t *model);

static struct k_sem imu_data_ready_sem;

/* Work queue items for deferring interrupt context LED operations to thread context */
#if IS_ENABLED(CONFIG_LED_NOTIFICATION_BLINK)
static struct k_work led_notify_work;
static struct k_work_delayable led_notify_blink_work;
static int led_notify_remaining;
#else
static struct k_work led_update_work;
#endif

static nrf_edgeai_t *p_model;


static imu_burst_detector_t burst_detector;
static uint8_t burst_detect_count;
void imu_burst_listener(int32_t start_index,
                                         const imu_sample_t *block,
                                         size_t block_len,
                                         bool trig_accel,
                                         bool trig_gyro,
                                         bool is_timeout,
                                         void *user_data) {

	LOG_INF("Notified burst (len %u)", block_len);
	led_set_led1(LED_MAX_BRIGHTNESS); // turn on the green
	burst_detect_count = 60; // blink counter for LED, determines how long it's on

	if(IS_ENABLED(CONFIG_IMU_SEND_BURSTS)) {
		ble_nus_send_burst((const uint8_t *)block, 6*sizeof(int16_t)*block_len);
	} else {
		execute_inference((uint16_t*)block, 6*block_len);
	}

}









#if !IS_ENABLED(CONFIG_LED_NOTIFICATION_BLINK)
K_TIMER_DEFINE(led_timer, led_glowing_timer_handler, NULL);
#endif /* !CONFIG_LED_NOTIFICATION_BLINK */

#if !IS_ENABLED(CONFIG_BLE_MODE_NONE)
void main_ble_connection_notification(bool connected)
{
	(void)connected;
	led_off();
#if IS_ENABLED(CONFIG_LED_NOTIFICATION_BLINK)
	k_work_submit(&led_notify_work);
#endif
}
#endif
static imu_sample_t cur_burtdetect_sample;
int main(void)
{
	hw_modules_init();

	int err = button_init();

	if (err != 0) {
		LOG_ERR("Failed to initialize button module (err %d)", err);
		return err;
	}

#if IS_ENABLED(CONFIG_BLE_MODE_HID)
	err = uxsm_init();
	if (err != 0) {
		LOG_ERR("Failed to initialize UX state manager (err %d)", err);
		return err;
	}
#endif

	p_model = nrf_edgeai_user_model();
	__ASSERT_NO_MSG(p_model != NULL);
	__ASSERT_NO_MSG(nrf_edgeai_is_runtime_compatible(p_model));

	__maybe_unused nrf_edgeai_err_t res = nrf_edgeai_init(p_model);

	__ASSERT_NO_MSG(res == NRF_EDGEAI_ERR_SUCCESS);

	nrf_edgeai_rt_version_t version = nrf_edgeai_runtime_version();

	LOG_INF("nRF Edge AI Micky's Gym Demo:");
	LOG_INF("nRF Edge AI Runtime Version: %d.%d.%d", version.field.major, version.field.minor,
		version.field.patch);
	LOG_INF("nRF Edge AI Lab Solution id: %s", nrf_edgeai_solution_id_str(p_model));

	imu_data_t imu_data = {0};

	int16_t input_data_i16[NRF_EDGEAI_INPUT_DATA_LEN];

	for (;;) {
		/* Wait for the semaphore to be released by IMU data ready interrupt */
		k_sem_take(&imu_data_ready_sem, K_FOREVER);

		if (imu_read(&imu_data) != STATUS_SUCCESS) {
			continue;
		}

#if IS_ENABLED(CONFIG_NRF_EDGEAI_GESTURE_RECOGNITION_MODEL_AXON)
		flt32_t input_data_f32[NRF_EDGEAI_INPUT_DATA_LEN];
		input_data_f32[0] = (flt32_t)(imu_data.accel[0].phys * 1000);
		input_data_f32[1] = (flt32_t)(imu_data.accel[1].phys * 1000);
		input_data_f32[2] = (flt32_t)(imu_data.accel[2].phys * 1000);
		input_data_f32[3] = (flt32_t)(imu_data.gyro[0].phys * 1000);
		input_data_f32[4] = (flt32_t)(imu_data.gyro[1].phys * 1000);
		input_data_f32[5] = (flt32_t)(imu_data.gyro[2].phys * 1000);
#endif

		input_data_i16[0] = imu_data.accel[0].raw;
		input_data_i16[1] = imu_data.accel[1].raw;
		input_data_i16[2] = imu_data.accel[2].raw;
		input_data_i16[3] = imu_data.gyro[0].raw;
		input_data_i16[4] = imu_data.gyro[1].raw;
		input_data_i16[5] = imu_data.gyro[2].raw;

		cur_burtdetect_sample.ax = input_data_i16[0] ;
		cur_burtdetect_sample.ay = input_data_i16[1] ;
		cur_burtdetect_sample.az = input_data_i16[2] ;
		cur_burtdetect_sample.gx = input_data_i16[3] ;
		cur_burtdetect_sample.gy = input_data_i16[4] ;
		cur_burtdetect_sample.gz = input_data_i16[5] ;

		// always detect bursts
		imu_bd_push(&burst_detector, &cur_burtdetect_sample);
		// and handle the LED
		if (burst_detect_count) {
			if (burst_detect_count == 1) {

				led_set_led1(0);
			}
			burst_detect_count --;
		}
		if (IS_ENABLED(CONFIG_DATA_COLLECTION_MODE)) {
			if (! IS_ENABLED(CONFIG_IMU_SEND_BURSTS)) {
				// not sending raw bursts but in data collection
				// mode, so send the data
				send_imu_data(input_data_i16);
			} //
			// else: bursts are sent out in data coll mode, in the listener above
		} else {

			// DEADBEEF: execute_inference(input_data_i16);
			// we don't want to execute inference on single samples, we do it on the
			// whole bursts, again in the listener above

		}
	}

	return 0;
}

static void imu_data_ready_cb(void)
{
	k_sem_give(&imu_data_ready_sem);
}

static void led_set_active(float brightness)
{
	/* ble_common_is_connected() is safe to call in any BLE mode,
	 * including CONFIG_BLE_MODE_NONE, because ble_common_init() is
	 * always invoked during hw_modules_init().
	 */
	if (ble_common_is_connected()) {
		led_set_led0(0);
		led_set_led2(brightness);
	} else {
		led_set_led0(brightness);
		led_set_led2(0);
	}
}

#if IS_ENABLED(CONFIG_LED_NOTIFICATION_BLINK)

static void led_blink_set_active_led(void)
{
	led_set_active(LED_MAX_BRIGHTNESS);
}

/**
 * Notification blink state machine.
 * Total steps = LED_NOTIFY_BLINK_COUNT * 2 (on + off per blink).
 * Even remaining → turn LED on, schedule off.
 * Odd remaining  → turn LED off, schedule next on (or stop).
 */
static void led_notify_blink_handler(struct k_work *work)
{
	if (led_notify_remaining <= 0) {
		return;
	}

	if (led_notify_remaining % 2 == 0) {
		/* LED-on phase */
		led_blink_set_active_led();
		led_notify_remaining--;
		k_work_schedule(&led_notify_blink_work, K_MSEC(LED_NOTIFY_BLINK_ON_MS));
	} else {
		/* LED-off phase */
		led_off();
		led_notify_remaining--;
		if (led_notify_remaining > 0) {
			k_work_schedule(&led_notify_blink_work, K_MSEC(LED_NOTIFY_BLINK_OFF_MS));
		}
	}
}

static void led_notify_start_work_handler(struct k_work *work)
{
	/* Cancel any ongoing sequence before starting a new one */
	k_work_cancel_delayable(&led_notify_blink_work);
	led_off();

	/* Each blink = on + off step */
	led_notify_remaining = LED_NOTIFY_BLINK_COUNT * 2;

	/* Kick off immediately */
	led_notify_blink_handler(NULL);
}

#else /* !CONFIG_LED_NOTIFICATION_BLINK */

static void led_glowing_timer_handler(struct k_timer *timer)
{
	(void)timer;
	k_work_submit(&led_update_work);
}

static void led_update_work_handler(struct k_work *work)
{
	static bool rising = true;
	static float brightness;

	led_set_active(brightness);

	if (rising) {
		brightness += LED_BLINK_CHANGE_BRIGHTNESS_STEP;
		if (brightness >= LED_MAX_BRIGHTNESS) {
			rising = false;
		}
	} else {
		brightness -= LED_BLINK_CHANGE_BRIGHTNESS_STEP;
		if (brightness <= 0) {
			rising = true;
		}
	}
}

#endif /* CONFIG_LED_NOTIFICATION_BLINK */


static void hw_modules_init(void)
{
	int ret;

#if IS_ENABLED(CONFIG_LED_NOTIFICATION_BLINK)
	k_work_init(&led_notify_work, led_notify_start_work_handler);
	k_work_init_delayable(&led_notify_blink_work, led_notify_blink_handler);
#else
	k_work_init(&led_update_work, led_update_work_handler);
#endif

	ret = led_init();
	if (ret != 0) {
		LOG_ERR("Failed to initialize LEDs module, error = %d", ret);
	}
#if IS_ENABLED(CONFIG_LED_NOTIFICATION_BLINK)
	/* Blink once at startup to confirm the device is alive */
	k_work_submit(&led_notify_work);
#else
	k_timer_start(&led_timer, K_MSEC(BLINK_LED_TIMER_PERIOD_MS),
		      K_MSEC(BLINK_LED_TIMER_PERIOD_MS));
#endif

	imu_config_t imu_config = {.accel_fs_g = IMU_ACCEL_SCALE_16G,
				   .gyro_fs_dps = IMU_GYRO_SCALE_2000DPS,
				   .data_rate_hz = CONFIG_IMU_DATARATE_HZ};

	status_t status = imu_init(&imu_config, imu_data_ready_cb);

	if (status != STATUS_SUCCESS) {
		LOG_ERR("Failed to initialize IMU sensor, error = %d", (int)status);
		__ASSERT_NO_MSG(false);
	}
	k_sem_init(&imu_data_ready_sem, 0, 1);

	imu_bd_init(&burst_detector,
			BURST_DETECTOR_CONFIG
			imu_burst_listener,
			NULL /* user data */);


	/* Always initialize the common BLE state holder.
	 * It is mode-agnostic and must be ready before any other module
	 * (e.g. led_set_active()) queries ble_common_is_connected().
	 */
	ble_common_init();

#if IS_ENABLED(CONFIG_BLE_MODE_HID)
	ret = ble_hid_init();
	if (ret != 0) {
		LOG_ERR("Failed to initialize BLE HID service");
	}
#elif IS_ENABLED(CONFIG_BLE_MODE_NUS)
	ret = ble_nus_init();
	if (ret != 0) {
		LOG_ERR("Failed to initialize BLE NUS service");
	}
#elif IS_ENABLED(CONFIG_BLE_MODE_GATT_CUSTOM)
	ret = ble_gatt_init(NULL);
	if (ret != 0) {
		LOG_ERR("Failed to initialize BLE GATT service");
	}
#endif
}


#if IS_ENABLED(CONFIG_NRF_EDGEAI_GESTURE_RECOGNITION_MODEL_AXON)
static void execute_inference(flt32_t *input_data, size_t len)
#else
static void execute_inference(int16_t *input_data, size_t len)
#endif
{
	// uint8_t buf[24];
	nrf_edgeai_err_t res;

	/*
	int16_t* ptr = input_data;
	for (uint8_t i=0; i<6*6; i+=6) {
		LOG_INF("DAT: %i %i %i %i %i %i", ptr[i+0], ptr[i+1], ptr[i+2], ptr[i+3], ptr[i+4], ptr[i+5]);
	}
	*/


	res = nrf_edgeai_feed_inputs(p_model, (void *)input_data, len);

	if (res == NRF_EDGEAI_ERR_SUCCESS) {
	#if PROVIDE_INFERENCE_US_TIME_IN_REPORTS
		inference_ticks.start_ticks = k_uptime_ticks();
	#endif
		res = nrf_edgeai_run_inference(p_model);

		if (res == NRF_EDGEAI_ERR_SUCCESS) {
#if PROVIDE_INFERENCE_US_TIME_IN_REPORTS
			inference_ticks.end_tick = k_uptime_ticks();
#endif
			handle_inference_result(p_model);
		} else {
			LOG_WRN("Failed to run inference, error = %d", (int)res);
		}
		/* INPROGRESS is expected 32/33 times, as we have 33 samples in the
		 * INPUT_WINDOW_SHIFT
		 */
	} else if (res != NRF_EDGEAI_ERR_INPROGRESS) {
		LOG_WRN("Failed to feed inputs, error = %d", (int)res);
	}
}

static void send_imu_data(int16_t *input_data)
{
#if IS_ENABLED(CONFIG_BLE_MODE_NUS)
	(void)ble_nus_send(input_data);
#else
	printk("%d,%d,%d,%d,%d,%d\r\n", input_data[0], input_data[1], input_data[2], input_data[3],
	       input_data[4], input_data[5]);
#endif
}


static void log_prediction_message(const char *class_name, const float probability, uint64_t inference_us)
{
	__ASSERT_NO_MSG(class_name != NULL);
	LOG_INF("Predicted class: %s, with probability %d %% %"PRId64 " us", class_name,
		(int)(100 * probability), inference_us);
}

static void handle_inference_result(nrf_edgeai_t *model)
{
	uint16_t predicted_target;
	const flt32_t *p_probabilities;
	uint8_t buffer[64];



	__ASSERT_NO_MSG(model != NULL);

	predicted_target = model->decoded_output.classif.predicted_class;
	p_probabilities = model->decoded_output.classif.probabilities.p_f32;
	__ASSERT_NO_MSG(p_probabilities != NULL);

	LOG_DBG("Predicted target: %d, Probability: %f", predicted_target,
		(double)p_probabilities[predicted_target]);

	prediction_ctx_t result =
		inference_postprocess(predicted_target, p_probabilities[predicted_target]);
	if (true) {
		const char *class_name = inference_get_class_name((class_label_t)result.target);
		uint64_t us = 0;

#if PROVIDE_INFERENCE_US_TIME_IN_REPORTS
		us = k_ticks_to_us_floor64(inference_ticks.end_tick - inference_ticks.start_ticks);

		int message_len = snprintf(buffer, sizeof(buffer), "%s,%d,%" PRId64, class_name,
						   (int)(result.probability*100), us);
#else
		int message_len = snprintf(buffer, sizeof(buffer), "%s,%d", class_name,
						   (int)(result.probability*100));
#endif

		log_prediction_message(class_name, result.probability, us);


		ble_nus_send_array(buffer, message_len);

	}
}
