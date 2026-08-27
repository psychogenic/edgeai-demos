/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/audio/dmic.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "ble/ble_mds.h"
#include "control_output.h"
#include "dmic.h"
#include "kws/kws.h"
#include "leds.h"
#include "ww/wakeword.h"
#include "keycap/keycap.h"
#include "adc/adc.h"

LOG_MODULE_REGISTER(main);

#define DMIC_READ_TIMEOUT 100

#if IS_ENABLED(APP_MODE_KEYSNOOP) 
static const struct device *const dmic_dev = NULL;
#else
static const struct device *const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));
#endif

static int ww_loop(void)
{
	int err;
	void *audio_buffer;
	size_t audio_buffer_size;
	bool ww_detected;

	ww_reset();

	print_control_output((struct control_message){CONTROL_MESSAGE_WAITING_WW});

	while (true) {
		err = dmic_read(dmic_dev, 0, &audio_buffer, &audio_buffer_size, DMIC_READ_TIMEOUT);
		if (err < 0) {
			LOG_ERR("Failed to read from DMIC (err %d)", err);
			return err;
		}

		err = ww_process(audio_buffer, DMIC_SAMPLES_IN_BLOCK, &ww_detected);
		if (err == -EBUSY) {
			/* More data is needed. */
			continue;
		} else if (err < 0) {
			LOG_ERR("Wakeword detection failed (err %d)", err);
			return err;
		}

		if (ww_detected) {
			print_control_output((struct control_message){CONTROL_MESSAGE_WW_DETECTED});
			if (IS_ENABLED(CONFIG_APP_MODE_WW_ONLY)) {
				leds_blink_led0();
			} else {
				return 0;
			}
		}
	}
}

static int kws_loop(void)
{
	int err;
	void *audio_buffer;
	size_t audio_buffer_size;
	struct kws_prediction prediction;

	uint32_t spotting_timeout = k_uptime_get_32() + CONFIG_KWS_PERIOD_MS;

	kws_reset();

	print_control_output((struct control_message){.type = CONTROL_MESSAGE_WAITING_KW});

	while (IS_ENABLED(CONFIG_APP_MODE_KWS_ONLY) || spotting_timeout > k_uptime_get_32()) {
		err = dmic_read(dmic_dev, 0, &audio_buffer, &audio_buffer_size, DMIC_READ_TIMEOUT);
		if (err < 0) {
			LOG_ERR("Failed to read from DMIC (err %d)", err);
			return err;
		}

		err = kws_process(audio_buffer, DMIC_SAMPLES_IN_BLOCK, &prediction);
		if (err == -EBUSY) {
			/* More data is needed. */
			continue;
		} else if (err) {
			LOG_ERR("Keyword spotting failed (err %d)", err);
			return err;
		}

		if (prediction.valid) {
			leds_blink_led1();
			spotting_timeout = k_uptime_get_32() + CONFIG_KWS_PERIOD_MS;
			print_control_output(
				(struct control_message){.type = CONTROL_MESSAGE_KW_SPOTTED,
							 .kw_class = prediction.class,
							 .name = prediction.name});
		}
	}

	print_control_output((struct control_message){.type = CONTROL_MESSAGE_TIMEOUT_KWS});

	return 0;
}




static int keycap_loop(void)
{
	int err;
	uint16_t *audio_buffer;
	size_t audio_buffer_size;
	keycap_prediction_t prediction;

	uint32_t spotting_timeout = k_uptime_get_32() + CONFIG_KWS_PERIOD_MS;


	keycap_reset();

	print_control_output((struct control_message){.type = CONTROL_MESSAGE_WAITING_KW});
	for (;;) {
		if (k_sem_take(&burst_data_ready, K_NO_WAIT) != 0) {
			k_sleep(K_MSEC(5));
			continue;
		}
		audio_buffer = (uint16_t*)adc_burst_data;
		err = keycap_process(audio_buffer, SAADC_BUFFER_NUMSAMPLES, &prediction);
		if (err == -EBUSY) {
			/* More data is needed. */
			LOG_INF("bizzy");
			continue;
		} else if (err) {
			LOG_ERR("Keycapture failed (err %d)", err);
			return err;
		}

		if (prediction.valid) {
			leds_blink_led1();
			// LOG_WRN("Pred %s", prediction.name);
			print_control_output(
				(struct control_message){.type = CONTROL_MESSAGE_KEY_SNOOPED_LOG,
							 .kw_class = prediction.class,
							 .name = prediction.name,
							 .confidence = prediction.confidence,
							 .inference_time_us = prediction.inference_us});
		} else {
			// LOG_INF("No pred");
		}
	}

	print_control_output((struct control_message){.type = CONTROL_MESSAGE_TIMEOUT_KWS});

	return 0;
}

int main_loop(void)
{
	int err;

	if (IS_ENABLED(CONFIG_APP_MODE_KEYSNOOP)) {
		LOG_ERR("Keyboard snoop mode!");
	} else {
		
		LOG_ERR("mic init!");
		err = dmic_init();
		if (err) {
			LOG_ERR("Err with dmic");
			return err;
		}
	}

	LOG_INF("LEDS init");
	err = leds_init();
	if (err) {
			LOG_ERR("Err with leds");
		return err;
	}

	LOG_INF("ctrlout init");
	err = control_output_init();
	if (err) {
			LOG_ERR("Err with ctrlout");
		return err;
	} else {
		LOG_INF("ALL GOOD");
	}

	if (IS_ENABLED(CONFIG_APP_MODE_KEYSNOOP)) {
		LOG_INF("keycap  init");
		err = keycap_init();
		if (err) {
			LOG_ERR("Err with keycap");
			return err;
		}
	} else {
		LOG_INF("NO KEYSNOOP?");
	}


	if (IS_ENABLED(CONFIG_APP_MODE_WW_GATED_KWS) || IS_ENABLED(CONFIG_APP_MODE_WW_ONLY)) {
		LOG_INF("ww  init");
		err = ww_init();
		if (err) {
			return err;
		}
	}

	if (IS_ENABLED(CONFIG_APP_MODE_WW_GATED_KWS) || IS_ENABLED(CONFIG_APP_MODE_KWS_ONLY)) {
		LOG_INF("kws  init");
		err = kws_init();
		if (err) {
			return err;
		}
	}

	LOG_INF("ble  init");
	err = init_app_ble();
	if (err) {
		return err;
	}

	LOG_INF("Initialization completed, check output on VCOM0");

	if (IS_ENABLED(CONFIG_APP_MODE_KEYSNOOP) ) {
		adc_init();
	} else {
		LOG_INF("dmic trig");
		err = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
		if (err < 0) {
			LOG_ERR("Failed to start DMIC (err %d)", err);
			return err;
		}
	}

	while (true) {
		if (IS_ENABLED(CONFIG_APP_MODE_WW_GATED_KWS) ||
		    IS_ENABLED(CONFIG_APP_MODE_WW_ONLY)) {
			err = ww_loop();
			if (err) {
				return err;
			}
		}

		if (IS_ENABLED(CONFIG_APP_MODE_WW_GATED_KWS)) {
			leds_on_led0();
		}

		if (IS_ENABLED(CONFIG_APP_MODE_WW_GATED_KWS) ||
		    IS_ENABLED(CONFIG_APP_MODE_KWS_ONLY)) {
			err = kws_loop();
			if (err) {
				return err;
			}
		}

		if (IS_ENABLED(CONFIG_APP_MODE_KEYSNOOP) ) {
			LOG_INF("snoop loop ");
			err = keycap_loop();
			if (err) {
				return err;
			}

		}

		if (IS_ENABLED(CONFIG_APP_MODE_WW_GATED_KWS)) {
			leds_off_led0();
		}
	}

	return 0;
}
int main(void) {
	for(;;) {
	main_loop();
	}
}
