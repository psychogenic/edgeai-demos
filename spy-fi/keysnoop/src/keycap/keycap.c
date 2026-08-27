/*
 * keycap.c, part of the Nordic project
 *
 *  Created on: Aug 2, 2026
 *      Author: Pat Deegan
 *  Copyright (C) 2022 Pat Deegan, https://psychogenic.com
 */


/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/logging/log.h>
#include <nrf_edgeai/nrf_edgeai.h>
#include <nrf_edgeai/rt/nrf_edgeai_runtime_aux.h>
#include <nrf_edgeai_obsv/nrf_edgeai_obsv.h>
#include <nrf_edgeai_obsv/nrf_edgeai_obsv_memfault.h>
#include <nrf_edgeai_obsv/nrf_edgeai_obsv_metrics.h>

//#include "../model_utils.h"
#include "keycap.h"
#include "keycap_model_labels.h"
#include "nrf_edgeai_generated/nrf_edgeai_user_model.h"

LOG_MODULE_REGISTER(keycap);



#define KEY_CLASSES_COUNT ARRAY_SIZE(keypress_detection_ctxs)

struct keypress_detection_ctx {
	const float threshold;
	const uint8_t reserved;
};


#define DEF_CONF_THRESHOLD 0.55f

static const struct keypress_detection_ctx keypress_detection_ctxs[] = {
	[MODEL_LABEL_IDLE] = {},
	[MODEL_LABEL_BACK] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_DOT] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_ENTER] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_SPACE] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_A] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_C] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_D] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_E] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_H] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_I] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_L] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_N] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_O] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_R] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_S] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},
	[MODEL_LABEL_T] = {.threshold = DEF_CONF_THRESHOLD, .reserved = 0},

};

static nrf_edgeai_t *keycap_model;



#if KEYCAP_CALCULATE_INFERENCE_TIME
typedef struct {
	int64_t start_ticks;
	int64_t end_tick;
} TickTimerValues_t;

static TickTimerValues_t inference_ticks;
#endif /* KEYCAP_CALCULATE_INFERENCE_TIME */

#if IS_ENABLED(CONFIG_MODELS_OBSERVABILITY)

static nrf_edgeai_obsv_ctx_t keycap_ctx;

static uint32_t keycap_pd_buf[NRF_EDGEAI_OBSV_PD_STORAGE_BYTES(KEY_CLASSES_COUNT) / sizeof(uint32_t)];
static uint32_t keycap_tm_buf[NRF_EDGEAI_OBSV_TM_STORAGE_BYTES(KEY_CLASSES_COUNT) / sizeof(uint32_t)];
static nrf_edgeai_obsv_metric_t keycap_pd;
static nrf_edgeai_obsv_metric_t keycap_tm;

BUILD_ASSERT(CONFIG_NRF_EDGEAI_OBSV_MAX_CLASSES >= KEY_CLASSES_COUNT,
	     "Observability will not fit all keyword spotting classes");

static int keycap_obsv_init(nrf_edgeai_t *model)
{
	nrf_edgeai_obsv_model_info_t info;
	int err;

	err = obsv_model_info_from_model(model, KEY_CLASSES_COUNT, &info);
	if (err) {
		return err;
	}

	err = nrf_edgeai_obsv_init(&keycap_ctx, &info);
	if (err) {
		LOG_ERR("Observability init failed (err %d)", err);
		return err;
	}

	nrf_edgeai_obsv_metric_pd_create(&keycap_pd, keycap_pd_buf, KEY_CLASSES_COUNT);
	nrf_edgeai_obsv_metric_tm_create(&keycap_tm, keycap_tm_buf, KEY_CLASSES_COUNT);
	err = nrf_edgeai_obsv_register(&keycap_ctx, &keycap_pd, NULL);
	if (err) {
		LOG_ERR("PD metric registration failed (err %d)", err);
		return err;
	}

	err = nrf_edgeai_obsv_register(&keycap_ctx, &keycap_tm, NULL);
	if (err) {
		LOG_ERR("TM metric registration failed (err %d)", err);
		return err;
	}

	err = nrf_edgeai_obsv_memfault_init(&keycap_ctx);
	if (err) {
		LOG_ERR("Memfault transport init failed (err %d)", err);
		return err;
	}

	return 0;
}

#endif /* IS_ENABLED(CONFIG_MODELS_OBSERVABILITY) */











// #define mymodel nrf_edgeai_user_model_95153
// #define mymodel nrf_edgeai_user_model_95168
// nrf_edgeai_t* nrf_edgeai_user_model_95207(void);
#define mymodel nrf_edgeai_user_model_95207

int keycap_init(void)
{

	keycap_model = mymodel();
	__ASSERT_NO_MSG(keycap_model);
	__ASSERT_NO_MSG(nrf_edgeai_model_outputs_num(keycap_model) == KEY_CLASSES_COUNT);
	__ASSERT_NO_MSG(nrf_edgeai_input_window_size(keycap_model) == KEYCAP_SAMPLES_IN_BLOCK);

	nrf_edgeai_err_t err = nrf_edgeai_init(keycap_model);

	if (err) {
		LOG_ERR("Model initialization failed (err %d)", err);
		return -ENOENT;
	}
	LOG_ERR("MODEL's setup");

	return 0;
}

static void keycap_postprocess(keycap_prediction_t *const prediction)
{
	prediction->valid = false;

	const uint16_t predicted_class = keycap_model->decoded_output.classif.predicted_class;

	__ASSERT_NO_MSG(predicted_class < KEY_CLASSES_COUNT);

	const flt32_t class_probability =
		keycap_model->decoded_output.classif.probabilities.p_f32[predicted_class];
	const struct keypress_detection_ctx *class_ctx = &keypress_detection_ctxs[predicted_class];
	const char *class_name = NRF_EDGEAI_USER_LABELS_NAME[predicted_class];

	prediction->class = predicted_class;
	prediction->confidence = class_probability;
	prediction->name = class_name;

#if KEYCAP_CALCULATE_INFERENCE_TIME
	prediction->inference_us = k_ticks_to_us_floor64(inference_ticks.end_tick - inference_ticks.start_ticks);

#else
	prediction->inference_us = 0;
#endif

	/*
	for (uint8_t i=0; i<KEY_CLASSES_COUNT; i++)
	{
		LOG_INF("Pred for %i: %f", i, (double)keycap_model->decoded_output.classif.probabilities.p_f32[i]);
		k_msleep(2);
	}
	*/
	if (predicted_class == MODEL_LABEL_IDLE) {
		return;
	}

	if (class_probability >= class_ctx->threshold) {
		prediction->valid = true;
	}
}



static float as_floats[KEYCAP_SAMPLES_IN_BLOCK];
int keycap_process(uint16_t *const audio_buffer, const uint16_t num_samples,
		struct keycap_prediction *const prediction)
{
	__ASSERT_NO_MSG(audio_buffer);
	__ASSERT_NO_MSG(num_samples == nrf_edgeai_input_window_size(keycap_model));
	__ASSERT_NO_MSG(prediction);

	nrf_edgeai_err_t err;

	// the model was trained on floats, we need to
	// feed it floats... convert here
	for (uint16_t i=0; i<num_samples; i++) {
		as_floats[i] = audio_buffer[i];
	}

	// feed the model and check if it's ready
	err = nrf_edgeai_feed_inputs(keycap_model, as_floats, num_samples);

	if (err == NRF_EDGEAI_ERR_INPROGRESS) {
		/* Skip inference, not enough data. */
		return -EBUSY;
	} else if (err) {
		LOG_ERR("Failed to feed inputs (err %d)", err);
		return -EPERM;
	}

#if KEYCAP_CALCULATE_INFERENCE_TIME
	inference_ticks.start_ticks = k_uptime_ticks();
#endif
	// all right: looks ready to go--run inference
	err = nrf_edgeai_run_inference(keycap_model);
	if (err == NRF_EDGEAI_ERR_INPROGRESS) {
		/* Skip output extraction, not enough data. */
		return -EBUSY;
	} else if (err) {
		LOG_ERR("Failed to run inference (err %d)", err);
		return -EPERM;
	}
#if KEYCAP_CALCULATE_INFERENCE_TIME
	inference_ticks.end_tick = k_uptime_ticks();
#endif
	keycap_postprocess(prediction);

#if IS_ENABLED(CONFIG_MODELS_OBSERVABILITY)
	err = nrf_edgeai_obsv_update_probs(&keycap_ctx,
					   keycap_model->decoded_output.classif.probabilities.p_f32);
	if (err) {
		LOG_ERR("Failed to update obsv (err %d)", err);
	}
#endif /* IS_ENABLED(CONFIG_MODELS_OBSERVABILITY) */

	return 0;
}

void keycap_reset(void)
{
	nrf_edgeai_model_axon_init_persistent_vars(keycap_model);
}
