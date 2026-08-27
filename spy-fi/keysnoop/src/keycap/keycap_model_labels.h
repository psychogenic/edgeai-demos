/*
 * keycap_model_labels.h, part of the Nordic project
 *
 *  Created on: Aug 2, 2026
 *      Author: Pat Deegan
 *  Copyright (C) 2022 Pat Deegan, https://psychogenic.com
 */

#ifndef NRF54_KEYSNOOP_SRC_KEYCAP_KEYCAP_MODEL_LABELS_H_
#define NRF54_KEYSNOOP_SRC_KEYCAP_KEYCAP_MODEL_LABELS_H_


typedef enum nrf_edgeai_user_label_e {
    MODEL_LABEL_IDLE,
	MODEL_LABEL_BACK,
	MODEL_LABEL_DOT,
	MODEL_LABEL_ENTER,
	MODEL_LABEL_SPACE,
	MODEL_LABEL_A,
	MODEL_LABEL_C,
	MODEL_LABEL_D,
	MODEL_LABEL_E,
	MODEL_LABEL_H,
	MODEL_LABEL_I,
	MODEL_LABEL_L,
	MODEL_LABEL_N,
	MODEL_LABEL_O,
	MODEL_LABEL_R,
	MODEL_LABEL_S,
	MODEL_LABEL_T,

} nrf_edgeai_user_label_t;

static const char* NRF_EDGEAI_USER_LABELS_NAME[] = {
    "-", "\b", ".", "\n", " ",
	"A", "C", "D", "E", "H", "I", "L", "N", "O", "R", "S", "T"
};

static const char* NRF_EDGEAI_USER_LABELS_NAME_LOWCONFIDENCE[] = {
    "-", "\b", ".", "\n", " ",
	"a", "c", "d", "e", "h", "i", "l", "n", "o", "r", "s", "t"
};

#endif /* NRF54_KEYSNOOP_SRC_KEYCAP_KEYCAP_MODEL_LABELS_H_ */
