/*
 * adc.h, part of the Nordic project
 *
 *  Created on: Aug 2, 2026
 *      Author: Pat Deegan
 *
 * Basic SAADC code lifted from some NordicSemi example
 * I've lost track of.
 */

#ifndef NRF54_KEYSNOOP_SRC_ADC_ADC_H_
#define NRF54_KEYSNOOP_SRC_ADC_ADC_H_


#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

extern struct k_sem burst_data_ready;


typedef int16_t ADCSampleType;

/* STEP 4.1 - Define the buffer size for the SAADC */
#define SAADC_BUFFER_NUMSAMPLES   	1024*2
#define SAADC_BYTES_PER_SAMPLE		sizeof(ADCSampleType)
#define SAADC_BUFFER_TOTAL_BYTES	(SAADC_BUFFER_NUMSAMPLES * SAADC_BYTES_PER_SAMPLE)

#define CONFIG_SOC_NRF54LM20A
/* STEP 4.6 - Declare the struct to hold the configuration for the SAADC channel used to sample the battery voltage */
#if NRF_SAADC_HAS_AIN_AS_PIN
#if defined(CONFIG_SOC_NRF54L15 ) || defined(CONFIG_SOC_NRF54LM20A)
#define SAADC_INPUT_PIN NRFX_ANALOG_EXTERNAL_AIN4
#elif defined(CONFIG_SOC_NRF54LS05A) || defined(CONFIG_SOC_NRF54LS05B)
#define SAADC_INPUT_PIN NRFX_ANALOG_EXTERNAL_AIN3
#else
BUILD_ASSERT(0, "Unsupported device family");
#endif
#else
#define SAADC_INPUT_PIN NRFX_ANALOG_EXTERNAL_AIN0
#endif






/* STEP 3.2 - Declaring an instance of nrfx_timer for TIMER2. */
#if defined(CONFIG_SOC_NRF54L15) || defined(CONFIG_SOC_NRF54LM20A)
#define TIMER_INSTANCE_NUMBER NRF_TIMER22
#elif defined(CONFIG_SOC_NRF54LS05A) || defined(CONFIG_SOC_NRF54LS05B)
#define TIMER_INSTANCE_NUMBER NRF_TIMER20
#else
#define TIMER_INSTANCE_NUMBER NRF_TIMER2
#endif


extern uint8_t adc_burst_data[SAADC_BUFFER_TOTAL_BYTES];

int adc_init(void);

#endif /* NRF54_KEYSNOOP_SRC_ADC_ADC_H_ */
