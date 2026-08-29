#ifndef NRF54_KEYCAPTURE_SRC_BURST_DETECTOR_H_


/*
 * burst_detector
 * I started with burst detections in the keycapture.py
 * and tried to have the C version match as closely as possible.
 * Traces of that remain but are now irrelevant.
 *
 * As explained in the video, that was backwards, a mistake
 * and a big waste of time.
 * Still, it's been left mostly as-is and the important thing is to
 * tweak this code so you're happy with your burst prior to collecting
 * a bunch of data for training.
 *
 * This is a copy from the keycapture C program--really messy that
 * there are 2, as they must behave identically (except maybe the rearm
 * delay)
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define RING_BITS      13
#define RING_SIZE      (1u << RING_BITS)      /* 8192 samples */
#define RING_MASK      (RING_SIZE - 1u)

#define BLOCK_LEN      2048
#define PRE_ROLL       400   /* samples of calm kept before trigger */
/* POST = BLOCK_LEN - PRE_ROLL = 1248, must cover max burst + margin */

#define DC_SHIFT       10    /* DC tracker time constant (slow) */
#define ENV_SHIFT      4     /* envelope tracker time constant (fast) */

#define THRESH_HIGH    65
#define THRESH_LOW     20
#define REARM_SAMPLES  3000


typedef enum {
    ST_IDLE = 0,
    ST_ARMED,
    ST_CAPTURING
} bd_state_t;


/* Callback signature: fired once per completed capture.
 * `block` is BLOCK_LEN samples, valid only for the duration of the call
 * (it points into caller-supplied scratch, see bd_push). */
typedef void (*bd_burst_listener_t)(int64_t start_index,
                                     const uint16_t *block,
                                     size_t block_len,
                                     void *user_data);

typedef struct {
    /* Raw ring buffer -- stores samples completely unmodified. */
    uint16_t ring[RING_SIZE];
    uint64_t wr_idx;

    /* Detection-only scratch state (never written back to raw data) */
    int64_t dc_acc;       /* higher-precision accumulator, see note */
    bool    dc_seeded;
    int64_t dc_est;
    int64_t env;           /* coarse envelope value, compared to thresholds */
    int64_t env_acc;       /* high-precision accumulator, env_acc = env << ENV_SHIFT
                               plus fractional remainder, mirroring dc_acc/dc_est */

    bd_state_t state;
    int64_t capture_start;  /* signed: can be negative near startup */
    int     quiet_count;
    bool    armed;

    /* scratch buffer used to assemble a contiguous block for the
     * callback when the capture wraps the ring */
    uint16_t emit_scratch[BLOCK_LEN];

    bd_burst_listener_t listener;
    void *listener_user_data;
} burst_detector_t;

static inline void bd_init(burst_detector_t *bd,
                            bd_burst_listener_t listener,
                            void *user_data)
{
    memset(bd, 0, sizeof(*bd));
    bd->state   = ST_IDLE;
    bd->armed   = true;          /* must re-earn this after each capture */
    bd->dc_seeded = false;
    bd->listener = listener;
    bd->listener_user_data = user_data;
}

/* ---- raw storage, unmodified ---- */
static inline void bd_ring_push(burst_detector_t *bd, uint16_t sample)
{
    bd->ring[bd->wr_idx & RING_MASK] = sample;
    bd->wr_idx++;
}

static inline void bd_emit_block(burst_detector_t *bd, int64_t start)
{
    /* start may be negative (burst near stream start); mask handles the
     * wrap the same way Python's `&` on a negative int does, because
     * RING_MASK arithmetic here is done on the unsigned wrapped index. */
    uint32_t begin = (uint32_t)((uint64_t)start) & RING_MASK;
    uint32_t first_chunk = RING_SIZE - begin;

    const uint16_t *block_ptr;

    if (first_chunk >= BLOCK_LEN) {
        block_ptr = &bd->ring[begin];
    } else {
        memcpy(bd->emit_scratch, &bd->ring[begin],
               first_chunk * sizeof(uint16_t));
        memcpy(bd->emit_scratch + first_chunk, &bd->ring[0],
               (BLOCK_LEN - first_chunk) * sizeof(uint16_t));
        block_ptr = bd->emit_scratch;
    }

    if (bd->listener) {
        bd->listener(start, block_ptr, BLOCK_LEN, bd->listener_user_data);
    }
    /* else: no-op. Python appends to self.emitted when no listener is
     * given; add your own storage here if you want that behavior. */
}

/* ---- cheap fixed-point detection math ---- */
static inline void bd_detect_step(burst_detector_t *bd, uint16_t sample)
{
    if (!bd->dc_seeded) {
        /* Seed the DC tracker with the first sample so it starts near
         * the true baseline instead of ramping up from 0. */
        bd->dc_acc = ((int64_t)sample) << DC_SHIFT;
        bd->dc_seeded = true;
    }

    /* IMPORTANT: this is NOT "dc_est += (x - dc_est) >> DC_SHIFT".
     * That naive form throws away the fractional remainder of the
     * update every sample, so once the residual error is smaller than
     * 2^DC_SHIFT the shift truncates to 0 and the estimate can freeze
     * permanently. Keeping a higher-precision accumulator retains that
     * remainder across samples so small errors eventually accumulate
     * past a shift boundary and get corrected. */
    bd->dc_acc += (int64_t)sample - (bd->dc_acc >> DC_SHIFT);
    bd->dc_est = bd->dc_acc >> DC_SHIFT;

    int64_t ac = (int64_t)sample - bd->dc_est;
    int64_t abs_ac = ac < 0 ? -ac : ac;

    /* same higher-precision-accumulator trick as dc_acc, applied
     * to env */
    bd->env_acc += abs_ac - (bd->env_acc >> ENV_SHIFT);
    bd->env = bd->env_acc >> ENV_SHIFT;

    if (bd->state == ST_IDLE) {
        if (bd->armed && bd->env > THRESH_HIGH) {
            /* Trigger: window starts PRE_ROLL samples back from "now".
             * Those samples already exist in the ring because the ring
             * is sized well beyond PRE_ROLL + BLOCK_LEN. */
            bd->capture_start = (int64_t)bd->wr_idx - 1 - PRE_ROLL;
            bd->state = ST_CAPTURING;
        }
    } else if (bd->state == ST_CAPTURING) {
        if ((int64_t)bd->wr_idx - bd->capture_start >= BLOCK_LEN) {
            bd_emit_block(bd, bd->capture_start);
            bd->state = ST_IDLE;
            bd->armed = false;   /* must see sustained quiet to re-trigger */
            bd->quiet_count = 0;
        }
    }

    /* Re-arm guard: once disarmed (right after a capture), require
     * REARM_SAMPLES *consecutive* samples with env < THRESH_LOW before
     * the detector is allowed to trigger again. Any excursion back
     * above THRESH_LOW resets the count. */
    if (!bd->armed) {
        if (bd->env < THRESH_LOW) {
            bd->quiet_count++;
            if (bd->quiet_count >= REARM_SAMPLES) {
                bd->armed = true;
            }
        } else {
            bd->quiet_count = 0;
        }
    }
}

/* call once per incoming sample, exactly like the ISR-driven
 * samples_in() this was modeled on */
static inline void bd_push(burst_detector_t *bd, uint16_t sample)
{
    bd_ring_push(bd, sample);
    bd_detect_step(bd, sample);
}



#endif

