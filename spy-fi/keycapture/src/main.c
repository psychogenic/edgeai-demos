/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/drivers/uart.h>



LOG_MODULE_REGISTER(adc_streamcap, LOG_LEVEL_DBG);

/* STEP 2 - Include header for nrfx drivers */
#include <nrfx_saadc.h>
#include <nrfx_timer.h>
#include <helpers/nrfx_gppi.h>

#include "burst_detector.h"

/* STEP 3.1 - Define the SAADC sample interval in microseconds */
#define SAADC_SAMPLE_INTERVAL_US 23


typedef int16_t ADCSampleType;

/* STEP 4.1 - Define the buffer size for the SAADC */

// 2048 SAMPLES PER BLOCK
#define SAADC_BUFFER_NUMSAMPLES   	2048
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

static nrfx_saadc_channel_t channel = NRFX_SAADC_DEFAULT_CHANNEL_SE(SAADC_INPUT_PIN, 0);

static const struct device *stream_uart =
    DEVICE_DT_GET(DT_CHOSEN(adc_stream_uart));

/* STEP 3.2 - Declaring an instance of nrfx_timer for TIMER2. */
#if defined(CONFIG_SOC_NRF54L15) || defined(CONFIG_SOC_NRF54LM20A)
#define TIMER_INSTANCE_NUMBER NRF_TIMER22
#elif defined(CONFIG_SOC_NRF54LS05A) || defined(CONFIG_SOC_NRF54LS05B)
#define TIMER_INSTANCE_NUMBER NRF_TIMER20
#else
#define TIMER_INSTANCE_NUMBER NRF_TIMER2
#endif
nrfx_timer_t timer_instance = NRFX_TIMER_INSTANCE(TIMER_INSTANCE_NUMBER);

/* STEP 4.2 - Declare the buffers for the SAADC */
static int16_t saadc_sample_buffer[2][SAADC_BUFFER_NUMSAMPLES];
static uint8_t uart_last_data_tx[SAADC_BUFFER_TOTAL_BYTES];

/* uart_last_data_tx doubles as the burst detector's output buffer below.
 * That only works because SAADC_BUFFER_NUMSAMPLES happens to equal
 * BLOCK_LEN (both 2048) -- this assert catches it if that ever drifts,
 * instead of silently overflowing/truncating a captured block. */
BUILD_ASSERT(SAADC_BUFFER_TOTAL_BYTES == BLOCK_LEN * (int)sizeof(uint16_t),
             "uart_last_data_tx size must match burst detector BLOCK_LEN");

/* STEP 4.3 - Declare variable used to keep track of which buffer was last assigned to the SAADC driver */
static uint32_t saadc_current_buffer = 0;








/* ---------------- UART sending: sync word lives here, only here ---------------- */
static const uint8_t sync_word[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
/* Only one UART transfer in flight at a time. This semaphore gates the
 * consumer loop from starting a new sync+data pair until the previous
 * pair has fully finished transmitting (both phases). */
K_SEM_DEFINE(tx_ready_sem, 1, 1);
K_SEM_DEFINE(data_ready_sem, 0, 1);

/* burst detector instance -- only ever touched from saadc_event_handler's
 * EVT_DONE case (single producer, callback/ISR context), so no locking */
static burst_detector_t bd;


static void configure_timer(void)
{
    int err;

    /* STEP 3.3 - Declaring timer config and intialize nrfx_timer instance. */
    nrfx_timer_config_t timer_config = NRFX_TIMER_DEFAULT_CONFIG(1000000);
    err = nrfx_timer_init(&timer_instance, &timer_config, NULL);
    if (err != 0) {
        LOG_ERR("nrfx_timer_init error: %08x", err);
        return;
    }

    /* Set compare channel 0 to generate event every SAADC_SAMPLE_INTERVAL_US. */
    uint32_t timer_ticks = nrfx_timer_us_to_ticks(&timer_instance, SAADC_SAMPLE_INTERVAL_US);
    nrfx_timer_extended_compare(&timer_instance, NRF_TIMER_CC_CHANNEL0, timer_ticks, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, false);

}


/* Called synchronously from within bd_push(), i.e. from the same
 * callback/ISR context saadc_event_handler already runs in. Reuses the
 * exact hand-off contract the original code used on every EVT_DONE
 * (memcpy into uart_last_data_tx, then give data_ready_sem) -- just
 * gated on an actual burst instead of firing unconditionally. */
static void burst_ready_cb(int64_t start_index, const uint16_t *block,
                            size_t block_len, void *user_data)
{
    ARG_UNUSED(user_data);

    if (k_sem_count_get(&data_ready_sem) != 0) {
        /* Previous captured block hasn't been picked up by the tx loop
         * yet -- about to overwrite it. Only possible if a new burst
         * completes before the prior one finishes transmitting. */
        LOG_WRN("previous burst not yet sent - overwriting (start idx=%lld)",
                (long long)start_index);
    }
    // little endian
    sys_put_le(uart_last_data_tx, block, block_len * sizeof(uint16_t));
    // memcpy(&uart_last_data_tx[0], block, block_len * sizeof(uint16_t));
    k_sem_give(&data_ready_sem);

    // LOG_INF("burst captured at sample idx=%lld", (long long)start_index);
}

static void saadc_event_handler(nrfx_saadc_evt_t const * p_event)
{
    int err;
    switch (p_event->type)
    {
        case NRFX_SAADC_EVT_READY:

           /* STEP 5.1 - Buffer is ready, timer (and sampling) can be started. */
            nrfx_timer_enable(&timer_instance);
            break;

        case NRFX_SAADC_EVT_BUF_REQ:
        	saadc_current_buffer = (saadc_current_buffer++)%2;
            /* STEP 5.2 - Set up the next available buffer. Alternate between buffer 0 and 1 */
            err = nrfx_saadc_buffer_set(saadc_sample_buffer[saadc_current_buffer], SAADC_BUFFER_NUMSAMPLES);
            //err = nrfx_saadc_buffer_set(saadc_sample_buffer[((saadc_current_buffer == 0 )? saadc_current_buffer++ : 0)], SAADC_BUFFER_NUMSAMPLES);
            if (err != 0) {
                LOG_ERR("nrfx_saadc_buffer_set error: %08x", err);
                return;
            }
            break;

        case NRFX_SAADC_EVT_DONE:

            /* Feed every sample into the detector, in order, regardless
             * of whether a burst is currently active. The DC/envelope
             * trackers need an unbroken stream to stay accurate.
             *
             * Assumes ADC samples are non-negative (single-ended
             * channel, 0..4095 for 12-bit resolution), so the
             * ADCSampleType -> uint16_t conversion below is lossless.
             */
            for (size_t i = 0; i < p_event->data.done.size; i++) {
                ADCSampleType s = ((ADCSampleType *)p_event->data.done.p_buffer)[i];
                bd_push(&bd, (uint16_t)s);
            }

            break;
        default:
            LOG_ERR("Unhandled SAADC evt %d", p_event->type);
            break;
    }
}

static void configure_saadc(void)
{
    int err;

    /* Connect ADC interrupt to nrfx interrupt handler */
    IRQ_CONNECT(DT_IRQN(DT_NODELABEL(adc)),
                DT_IRQ(DT_NODELABEL(adc), priority),
                nrfx_isr, nrfx_saadc_irq_handler, 0);


    /* Initialize the nrfx_SAADC driver */
    err = nrfx_saadc_init(DT_IRQ(DT_NODELABEL(adc), priority));
    if (err != 0) {
        LOG_ERR("nrfx_saadc_init error: %08x", err);
        return;
    }

    /* Change gain config in default config and apply channel configuration */
#if defined(CONFIG_SOC_NRF54L15) || defined(CONFIG_SOC_NRF54LM20A) || defined(CONFIG_SOC_NRF54LM20B)
    channel.channel_config.gain = NRF_SAADC_GAIN1_4; // NRF_SAADC_GAIN1_4;
#elif defined(CONFIG_SOC_NRF54LS05A) || defined(CONFIG_SOC_NRF54LS05B)
        channel.channel_config.reference = SAADC_CH_CONFIG_REFSEL_Vdd;
#else
    channel.channel_config.gain = NRF_SAADC_GAIN1_6;
#endif
    err = nrfx_saadc_channels_config(&channel, 1);
    if (err != 0) {
        LOG_ERR("nrfx_saadc_channels_config error: %08x", err);
        return;
    }

    /* Configure channel 0 in advanced mode with event handler (non-blocking mode) */
    nrfx_saadc_adv_config_t saadc_adv_config = NRFX_SAADC_DEFAULT_ADV_CONFIG;
    err = nrfx_saadc_advanced_mode_set(BIT(0),
                                        NRF_SAADC_RESOLUTION_12BIT,
                                        &saadc_adv_config,
                                        saadc_event_handler);
    if (err != 0) {
        LOG_ERR("nrfx_saadc_advanced_mode_set error: %08x", err);
        return;
    }

    /* Configure two buffers to make use of double-buffering feature of SAADC */
    err = nrfx_saadc_buffer_set(saadc_sample_buffer[0], SAADC_BUFFER_NUMSAMPLES);
    if (err != 0) {
        LOG_ERR("nrfx_saadc_buffer_set error: %08x", err);
        return;
    }
    err = nrfx_saadc_buffer_set(saadc_sample_buffer[1], SAADC_BUFFER_NUMSAMPLES);
    if (err != 0) {
        LOG_ERR("nrfx_saadc_buffer_set error: %08x", err);
        return;
    }

    /* Trigger the SAADC. This will not start sampling, but will prepare buffer for sampling triggered through PPI */
    err = nrfx_saadc_mode_trigger();
    if (err != 0) {
        LOG_ERR("nrfx_saadc_mode_trigger error: %08x", err);
        return;
    }

}

static void configure_ppi(void)
{
    int err;
    /* STEP 6.1 - Declare variables used to hold the (D)PPI channel number */
    nrfx_gppi_handle_t gppi_handle_sample;
    nrfx_gppi_handle_t gppi_handle_start;

    /* STEP 6.2 - Trigger task sample from timer */
    err = nrfx_gppi_conn_alloc( nrfx_timer_compare_event_address_get(&timer_instance, NRF_TIMER_CC_CHANNEL0),nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_SAMPLE), &gppi_handle_sample);
    if (err != 0) {
        LOG_ERR("nrfx_gppi_conn_alloc error: %08x", err);
        return;
    }

    /* STEP 6.3 - Trigger task start from end event */
    err = nrfx_gppi_conn_alloc(nrf_saadc_event_address_get(NRF_SAADC, NRF_SAADC_EVENT_END),nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_START), &gppi_handle_start);
    if (err != 0) {
        LOG_ERR("nrfx_gppi_conn_alloc error: %08x", err);
        return;
    }

    /* STEP 6.4 - Enable both (D)PPI channels */
    nrfx_gppi_conn_enable(gppi_handle_sample);
    nrfx_gppi_conn_enable(gppi_handle_start);
}








static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
    switch (evt->type) {
    case UART_TX_DONE:
#if 0
        if (evt->data.tx.buf == sync_word) {
            /* Sync word is out - now send the actual sampl/ es for this packet. */
            int err = uart_tx(stream_uart, (uint8_t *)tx_active_buf,
            		SAADC_BUFFER_NUMSAMPLES * sizeof(int16_t), SYS_FOREVER_US);
            if (err) {
                LOG_ERR("uart_tx(data) failed: %d", err);
                atomic_clear_bit(&buf_busy[buf_index_of(tx_active_buf)], 0);
                k_sem_give(&tx_ready_sem);
            }
        } else {
            /* Sample data is out - full packet done, free the buffer and
             * let the next queued buffer start. */
            atomic_clear_bit(&buf_busy[buf_index_of(tx_active_buf)], 0);
            k_sem_give(&tx_ready_sem);
        }
#endif
        k_sem_give(&tx_ready_sem);
        break;

    case UART_TX_ABORTED:
    	/*
        LOG_WRN("uart_tx aborted");
        atomic_clear_bit(&buf_busy[buf_index_of(tx_active_buf)], 0);
        */
        k_sem_give(&tx_ready_sem);
        break;

    default:
        break;
    }
}


int main(void)
{
	if (!device_is_ready(stream_uart)) {
		LOG_ERR(
				"stream_uart device not ready - check devicetree chosen node / status");
		return -1;
	}

	if (uart_callback_set(stream_uart, uart_cb, NULL) != 0) {
		LOG_ERR("uart_callback_set failed:");
		return -1;
	}


	k_msleep(250);

    bd_init(&bd, burst_ready_cb, NULL);

    configure_timer();
    configure_saadc();
    configure_ppi();

	for(;;) {

		k_sem_take(&tx_ready_sem, K_FOREVER);
		uart_tx(stream_uart, (uint8_t*)sync_word, 4, SYS_FOREVER_US);
		k_sem_take(&data_ready_sem, K_FOREVER);
		k_sem_take(&tx_ready_sem, K_FOREVER);
		int err = uart_tx(stream_uart, uart_last_data_tx,
		            		sizeof(uart_last_data_tx), SYS_FOREVER_US);
		if (err) {
			LOG_ERR("uart_tx(data) failed: %d", err);
		}


	}
}
