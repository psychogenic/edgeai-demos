/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "inference_postprocessing.h"

#include <stdbool.h>
#include <string.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(inference, LOG_LEVEL_INF);

/**
 * @brief Class prediction conditions for postprocessing
 *
 */
typedef struct class_prediction_condition_s {
	/* Minimum number of repetitions of a class for prediction */
	uint16_t min_repeat_count;

	/* Minimum probability threshold for prediction */
	float probability_threshold;
} class_prediction_condition_t;

static const char *get_name_by_target(uint8_t predicted_target)
{
	static const char * const LABEL_VS_NAME[] = {
		[CLASS_LABEL_DOWN]          = "IDLE/DOWN",
		[CLASS_LABEL_GUARD]        	= "GUARD",
		[CLASS_LABEL_LOWGUARD]     	= "LOW GUARD",
		[CLASS_LABEL_CROSS]    		= "CROSS",
		[CLASS_LABEL_JAB]   		= "JAB",
		[CLASS_LABEL_LEAD_HOOK]   	= "LEAD HOOK",
		[CLASS_LABEL_REAR_HOOK] 	= "REAR HOOK",
		[CLASS_LABEL_UPPERCUT]  	= "UPPERCUT"
	};

	static const uint8_t LABELS_CNT = ARRAY_SIZE(LABEL_VS_NAME);

	__ASSERT_NO_MSG(LABELS_CNT == CLASS_LABEL_COUNT);

	return (predicted_target < LABELS_CNT) ? LABEL_VS_NAME[predicted_target] : NULL;
}

static const class_prediction_condition_t *get_class_condition(uint8_t predicted_target)
{
	static const class_prediction_condition_t LABEL_VS_CONFIG[] = {
		[CLASS_LABEL_DOWN]           	= {0, 0.2},
		[CLASS_LABEL_GUARD]        		= {0, 0.5},
		[CLASS_LABEL_LOWGUARD]     		= {2, 0.5},
		[CLASS_LABEL_CROSS]    			= {2, 0.8},
		[CLASS_LABEL_JAB]   			= {2, 0.8},
		[CLASS_LABEL_LEAD_HOOK]   		= {3, 0.8},
		[CLASS_LABEL_REAR_HOOK] 		= {2, 0.8},
		[CLASS_LABEL_UPPERCUT]  		= {2, 0.8},
	};

	static const uint8_t LABELS_CNT = ARRAY_SIZE(LABEL_VS_CONFIG);

	return (predicted_target < LABELS_CNT) ? &LABEL_VS_CONFIG[predicted_target] : NULL;
}



prediction_ctx_t inference_postprocess(uint16_t target, float probability)
{
	prediction_ctx_t result = {
		.target = CLASS_LABEL_DOWN,
		.probability = 0.0f,
	};

	const class_prediction_condition_t *class_condition =
				get_class_condition(target);

	if (class_condition == NULL) {
		return result;
	}

	if (probability < class_condition->probability_threshold) {
		LOG_INF("Ignore pred class %u (prob too low @ %f)", target, (double)probability);
		return result;
	}
	result.target = target;
	result.probability = probability;
	return result;
}

const char *inference_get_class_name(const class_label_t class_label)
{
	return get_name_by_target((uint8_t)class_label);
}
