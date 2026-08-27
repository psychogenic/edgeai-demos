 /*
 * burst_detector.h, part of the Nordic project
 *
 *  Created on: Aug 5, 2026
 *      Author: Pat Deegan
 *  Copyright (C) 2022 Pat Deegan, https://psychogenic.com
 */

#ifndef NRF54_MIGHTY_MICK_GYM_SRC_HW_MODULES_SENSOR_IMU_BURST_DETECTOR_H_
#define NRF54_MIGHTY_MICK_GYM_SRC_HW_MODULES_SENSOR_IMU_BURST_DETECTOR_H_

/*
 * imu_burst_detector
 *
 * Derived from the original single-channel uint16 burst_detector, retuned
 * for a 6-axis IMU stream (accel_x/y/z, gyro_x/y/z, each int16_t) sampled
 * at ~128 Hz.
 *
 * Design notes / what changed vs. the original:
 *
 * 1. Samples are `imu_sample_t` structs (6x int16_t).
 *    The ring buffer, capture window, and emitted block
 *    are all in units of *samples*
 *
 * 2. No square roots. We never compute a true vector magnitude
 *    sqrt(x^2+y^2+z^2). Instead the three axes within a sensor group are
 *    combined with a plain sum of absolute values (L1 norm) -- a
 *    standard cheap stand-in for L2 magnitude on MCUs. It's rotation-
 *    *variant* (not a true norm) but tracks "how much is moving" well
 *    enough for burst/no-burst detection, at the cost of only
 *    adds/subtracts.
 *
 * 3. Activity signal is a first difference (delta from the previous
 *    sample), NOT a slow DC-tracker/high-pass.  A first
 *    difference (|sample[i] - prev_sample[i]|) is zero-lag: it's large
 *    exactly while something is moving and drops to ~0 the instant
 *    you're steady, in ANY orientation, and it's automatically immune to
 *    any constant bias (gravity-driven or sensor-offset-driven) without
 *    needing a tuned time constant. It's also cheaper -- just the
 *    previous raw sample, no 32-bit accumulate/shift per axis. Tradeoff:
 *    it's more sensitive to per-sample sensor noise than an averaged
 *    baseline; the envelope tracker's own smoothing (ENV_SHIFT) absorbs
 *    most of this
 *
 * 4. Accel and gyro are tracked as two *independent* envelopes
 *    (env_accel, env_gyro), each with its own threshold pair, because
 *    their raw LSB scales are unrelated (e.g. +-2g full-scale accel vs
 *    +-250dps full-scale gyro don't map to the same "burst" magnitude).
 *    A burst is declared if EITHER envelope crosses its high threshold
 *    Re-arming requires *both* envelopes to be quiet, so
 *    a lingering burst in one channel keeps the detector from
 *    re-triggering prematurely on the other.
 *
 * 5. Block length (and pre-roll) are runtime-configurable via
 *    imu_bd_init(), not compile-time constants, bounded by
 *    IMU_BD_MAX_BLOCK_LEN so the fixed ring/scratch buffers stay
 *    static/allocation-free.
 *
 * 6. Optional timeout-forced emission: if nothing has triggered a
 *    capture for `timeout_samples` samples, the detector emits the most
 *    recent block_len samples anyway (listener gets is_timeout=true, no
 *    threshold was crossed) so a caller can still process/flush data on
 *    a cadence even during long quiet stretches. Disabled by passing 0.
 *
 * 7. Accumulators are int32_t rather than int64_t -- for int16_t sample
 *    ranges (+-32768) with the shift amounts used here, int32_t has
 *    comfortable headroom and is much cheaper on MCUs without efficient
 *    native 64-bit shifts/adds.
 *
 * Portability note: this relies on arithmetic
 * (sign-extending) right shift for negative signed integers. This is
 * implementation-defined before C23 but is what every mainstream
 * compiler does on twos-complement targets. Use the ashr32() helper
 * below if you need strict standards compliance.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/logging/log.h>


/* ---------------- Config ---------------- */


/* Upper bound on block_len that bd_init() will accept. This sizes the
 * static ring buffer and emit scratch buffer at compile time. Raise it
 * if you need windows longer than ~2s @128Hz; each extra sample costs
 * sizeof(imu_sample_t) = 12 bytes twice over (ring headroom + scratch). */
#define IMU_BD_MAX_BLOCK_LEN   256u

/* Ring buffer must comfortably outlive MAX_BLOCK_LEN + max pre-roll so
 * that pre-roll samples captured "in the past" are still present, with
 * margin. Must be a power of two. */
#define IMU_BD_RING_BITS       10
#define IMU_BD_RING_SIZE       (1u << IMU_BD_RING_BITS)  /* 1024 samples */
#define IMU_BD_RING_MASK       (IMU_BD_RING_SIZE - 1u)

#if (IMU_BD_MAX_BLOCK_LEN * 2u) > IMU_BD_RING_SIZE
#error "IMU_BD_RING_BITS too small for IMU_BD_MAX_BLOCK_LEN"
#endif

#define IMU_BD_ENV_SHIFT        4    /* envelope tracker time constant (fast) */

/* Default thresholds -- almost certainly need retuning to your sensor's
 * full-scale range and noise floor; exposed via bd_init() so you don't
 * have to edit the header to change them. These are on DELTA magnitude
 * now (sum of |sample-to-sample change| across 3 axes), not on
 * raw-minus-baseline, so if you're porting thresholds forward from an
 * earlier DC-tracker version, expect to retune -- deltas are typically
 * much smaller than raw-minus-slow-baseline residuals. */
#define IMU_BD_DEFAULT_THRESH_HIGH_ACCEL   3500
#define IMU_BD_DEFAULT_THRESH_LOW_ACCEL    1600
#define IMU_BD_DEFAULT_THRESH_HIGH_GYRO    700
#define IMU_BD_DEFAULT_THRESH_LOW_GYRO     400
#define IMU_BD_DEFAULT_REARM_SAMPLES       8

#define IMU_BD_TIMEOUTMARGIN_DELTA_ACCEL	300
#define IMU_BD_TIMEOUTMARGIN_DELTA_GYRO		50

#define IMU_BD_DEFAULT_PREROLL			10


typedef enum {
    IMU_BD_ST_IDLE = 0,
    IMU_BD_ST_CAPTURING
} imu_bd_state_t;

typedef struct {
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
} imu_sample_t;

/* Portable floor-shift, provided for strict-standards builds.
 * Not used by default -- see portability note above. */
static inline int32_t ashr32(int32_t x, int shift) {
    if (x >= 0) return x >> shift;
    return -(((-x - 1) >> shift) + 1);
}

/* Callback signature: fired once per completed capture.
 * `block` is `block_len` samples, valid only for the duration of the
 * call (it may point into caller-supplied scratch, see bd_push).
 * `is_timeout` is true when this emission was forced by the timeout
 * mechanism rather than by an envelope crossing threshold -- in that
 * case trig_accel/trig_gyro are both false. */
typedef void (*imu_bd_burst_listener_t)(int32_t start_index,
                                         const imu_sample_t *block,
                                         size_t block_len,
                                         bool trig_accel,
                                         bool trig_gyro,
                                         bool is_timeout,
                                         void *user_data);

typedef struct {
    /* Raw ring buffer -- stores samples completely unmodified. */
    imu_sample_t ring[IMU_BD_RING_SIZE];
    uint32_t wr_idx;

    /* Runtime-configured window shape. pre_roll < block_len always. */
    uint16_t block_len;
    uint16_t pre_roll;

    /* Detection-only scratch state (never written back to raw data).
     * Order: ax, ay, az, gx, gy, gz. Previous raw sample, used to form
     * the first-difference activity signal -- see note 3 above. */
    int16_t prev_raw[6];
    bool    have_prev;

    int32_t env_acc_accel;   /* high-precision accumulator for accel envelope */
    int32_t env_accel;       /* coarse accel envelope, compared to thresholds */
    int32_t env_acc_gyro;
    int32_t env_gyro;

    int16_t thresh_high_accel;
    int16_t thresh_low_accel;
    int16_t thresh_high_gyro;
    int16_t timeout_margin_accel;
    int16_t thresh_low_gyro;
    int16_t timeout_margin_gyro;
    uint16_t rearm_samples;

    imu_bd_state_t state;
    int32_t capture_start;   /* signed: can be negative near startup */
    uint16_t quiet_count;
    bool    armed;

    /* Which envelope(s) crossed threshold at trigger time, passed
     * through to the listener. */
    bool trig_accel;
    bool trig_gyro;

    /* Optional forced-emission-on-idle mechanism (0 = disabled). */
    uint32_t timeout_samples;
    uint32_t last_emit_idx;

    /* scratch buffer used to assemble a contiguous block for the
     * callback when the capture wraps the ring */
    imu_sample_t emit_scratch[IMU_BD_MAX_BLOCK_LEN];

    imu_bd_burst_listener_t listener;
    void *listener_user_data;
} imu_burst_detector_t;

/*
 * Initialize the detector.
 *
 *   block_len      total samples emitted per capture (e.g. 128 = 1s @128Hz).
 *                   Must be 2..IMU_BD_MAX_BLOCK_LEN.
 *   pre_roll        samples of calm kept before the trigger point.
 *                   Must be < block_len. Pass 0 for "trigger sample is
 *                   first sample in block". A common starting point is
 *                   ~20% of block_len, matching the original's ratio.
 *   thresh_high_accel, thresh_low_accel, thresh_high_gyro, thresh_low_gyro
 *                   -- pass 0 to use the IMU_BD_DEFAULT_* values; these
 *                   will need tuning to your sensor.
 *   rearm_samples   consecutive quiet samples (both envelopes below
 *                   their low threshold) required before re-triggering
 *                   is allowed again. Pass 0 to use the default.
 *   timeout_samples  if no capture has been emitted in this many
 *                   samples, force-emit the most recent block_len
 *                   samples with is_timeout=true. Pass 0 to disable
 *                   (default behavior, no forced emissions).
 *
 * Returns false if block_len/pre_roll are out of range (detector is
 * left zeroed/disabled in that case).
 */
bool imu_bd_init(imu_burst_detector_t *bd,
                                uint16_t block_len,
                                uint16_t pre_roll,
                                int16_t thresh_high_accel,
                                int16_t thresh_low_accel,
                                int16_t thresh_high_gyro,
                                int16_t thresh_low_gyro,
                                uint16_t rearm_samples,
                                uint32_t timeout_samples,
                                imu_bd_burst_listener_t listener,
                                void *user_data);

/* ---- raw storage, unmodified ---- */
static inline void imu_bd_ring_push(imu_burst_detector_t *bd, const imu_sample_t *s)
{
    bd->ring[bd->wr_idx & IMU_BD_RING_MASK] = *s;
    bd->wr_idx++;
}

void imu_bd_emit_block(imu_burst_detector_t *bd, int32_t start,
                                      bool trig_accel, bool trig_gyro, bool is_timeout);

static inline int32_t imu_bd_iabs32(int32_t v) { return v < 0 ? -v : v; }

/* ---- cheap fixed-point detection math ---- */
void imu_bd_detect_step(imu_burst_detector_t *bd, const imu_sample_t *s);

/* call once per incoming IMU sample, exactly like the ISR-driven
 * samples_in() this was modeled on */
static inline void imu_bd_push(imu_burst_detector_t *bd, const imu_sample_t *s)
{
    imu_bd_ring_push(bd, s);
    imu_bd_detect_step(bd, s);
}

#endif /* NRF54_KEYCAPTURE_SRC_IMU_BURST_DETECTOR_H_ */

