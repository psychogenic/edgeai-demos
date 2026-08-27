/*
 * keycap.h, part of the Nordic project
 *
 *  Created on: Aug 2, 2026
 *      Author: Pat Deegan
 *  Copyright (C) 2022 Pat Deegan, https://psychogenic.com
 */

#ifndef NRF54_KEYSNOOP_SRC_KEYCAP_KEYCAP_H_
#define NRF54_KEYSNOOP_SRC_KEYCAP_KEYCAP_H_


#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define KEYCAP_SAMPLES_IN_BLOCK		2048


#define KEYCAP_CALCULATE_INFERENCE_TIME		1
/**
 * @brief Prediction from keyword spotting model.
 */
typedef struct keycap_prediction {
	/** Prediction valid flag. */
	bool valid;
	/** Predicted class. */
	uint16_t class;
	/** Predicted class name. */
	const char *name;
	

	float confidence;
	uint64_t inference_us;
} keycap_prediction_t;

/**
 * @brief Initialize model.
 *
 * @return Operation status, 0 for success.
 */
int keycap_init(void);

/**
 * @brief Process audio data by keycapture model.
 *
 * @param audio_buffer Buffer of audio samples from dmic_read. Function takes ownership of pointer.
 * @param num_samples Number of audio samples.
 * @param[out] ww_detected Result of wakeword detection. Valid if operation completed successfully.

 * @retval 0 Operation successful.
 * @retval -EPERM Operation failed on nRF Edge AI Lib level.
 * @retval -EBUSY Model needs more data.
 */
int keycap_process(uint16_t *const audio_buffer, const uint16_t num_samples,
		keycap_prediction_t *const key_detected);

/**
 * @brief Reset Wakeword model state.
 */
void keycap_reset(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* NRF54_KEYSNOOP_SRC_KEYCAP_KEYCAP_H_ */
