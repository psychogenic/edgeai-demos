#include "burst_detector.h"


LOG_MODULE_REGISTER(burstdet, LOG_LEVEL_INF);

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
                                void *user_data)
{
    if (block_len < 2 || block_len > IMU_BD_MAX_BLOCK_LEN) return false;
    if (pre_roll >= block_len) return false;

    memset(bd, 0, sizeof(*bd));
    bd->block_len = block_len;
    bd->pre_roll  = pre_roll;
    bd->state     = IMU_BD_ST_IDLE;
    bd->armed     = true;         /* must re-earn this after each capture */
    bd->have_prev = false;

    bd->thresh_high_accel = thresh_high_accel ? thresh_high_accel : IMU_BD_DEFAULT_THRESH_HIGH_ACCEL;
    bd->thresh_low_accel  = thresh_low_accel  ? thresh_low_accel  : IMU_BD_DEFAULT_THRESH_LOW_ACCEL;
    bd->timeout_margin_accel = bd->thresh_high_accel - IMU_BD_TIMEOUTMARGIN_DELTA_ACCEL;
    bd->thresh_high_gyro  = thresh_high_gyro  ? thresh_high_gyro  : IMU_BD_DEFAULT_THRESH_HIGH_GYRO;
    bd->thresh_low_gyro   = thresh_low_gyro   ? thresh_low_gyro   : IMU_BD_DEFAULT_THRESH_LOW_GYRO;
    bd->timeout_margin_gyro = bd->thresh_high_gyro - IMU_BD_TIMEOUTMARGIN_DELTA_GYRO;
    bd->rearm_samples     = rearm_samples;
    bd->timeout_samples   = timeout_samples;  /* 0 = disabled, left as-is */
    bd->last_emit_idx     = 0;

    bd->listener = listener;
    bd->listener_user_data = user_data;
    return true;
}
void imu_bd_emit_block(imu_burst_detector_t *bd, int32_t start,
                                      bool trig_accel, bool trig_gyro, bool is_timeout)
{
    uint32_t begin = (uint32_t)start & IMU_BD_RING_MASK;
    uint32_t first_chunk = IMU_BD_RING_SIZE - begin;
    uint16_t len = bd->block_len;

    const imu_sample_t *block_ptr;

    if (first_chunk >= len) {
        block_ptr = &bd->ring[begin];
    } else {
        memcpy(bd->emit_scratch, &bd->ring[begin],
               first_chunk * sizeof(imu_sample_t));
        memcpy(bd->emit_scratch + first_chunk, &bd->ring[0],
               (len - first_chunk) * sizeof(imu_sample_t));
        block_ptr = bd->emit_scratch;
    }

    if (bd->listener) {
    	LOG_INF("Notify listener");
        bd->listener(start, block_ptr, len, trig_accel, trig_gyro, is_timeout,
                     bd->listener_user_data);
    } else {
    	LOG_INF("Burst but not listener");
    }
    bd->last_emit_idx = bd->wr_idx;
}

/* cheap fixed-point detection math  */
void imu_bd_detect_step(imu_burst_detector_t *bd, const imu_sample_t *s)
{
    int16_t raw[6] = { s->ax, s->ay, s->az, s->gx, s->gy, s->gz };

    if (!bd->have_prev) {
        /* First sample ever: no delta yet, just seed prev_raw. Envelope
         * stays at 0 for this one step, which is fine (can't have moved
         * relative to a sample that doesn't exist yet). */
        memcpy(bd->prev_raw, raw, sizeof(raw));
        bd->have_prev = true;
    }

    int32_t abs_delta[6];
    for (int i = 0; i < 6; i++) {
        abs_delta[i] = imu_bd_iabs32((int32_t)raw[i] - (int32_t)bd->prev_raw[i]);
    }
    memcpy(bd->prev_raw, raw, sizeof(raw));

    /* L1-norm stand-in for delta-vector magnitude, per group -- no sqrt. */
    int32_t accel_abs_sum = abs_delta[0] + abs_delta[1] + abs_delta[2];
    int32_t gyro_abs_sum  = abs_delta[3] + abs_delta[4] + abs_delta[5];

    bd->env_acc_accel += accel_abs_sum - (bd->env_acc_accel >> IMU_BD_ENV_SHIFT);
    bd->env_accel = bd->env_acc_accel >> IMU_BD_ENV_SHIFT;

    bd->env_acc_gyro += gyro_abs_sum - (bd->env_acc_gyro >> IMU_BD_ENV_SHIFT);
    bd->env_gyro = bd->env_acc_gyro >> IMU_BD_ENV_SHIFT;

    if (bd->state == IMU_BD_ST_IDLE) {
    	// do check if we have a hit
		bool hit_accel = bd->env_accel > bd->thresh_high_accel;
		bool hit_gyro  = bd->env_gyro  > bd->thresh_high_gyro;
		if (bd->armed && (hit_accel || hit_gyro))  {
			/* Trigger: window starts pre_roll samples back from "now".
			 * Those samples already exist in the ring because the ring
			 * is sized well beyond MAX_BLOCK_LEN + max pre-roll. */
			LOG_INF("Triggered");
			bd->capture_start = (int32_t)bd->wr_idx - 1 - (int32_t)bd->pre_roll;
			bd->state = IMU_BD_ST_CAPTURING;
			bd->trig_accel = hit_accel;
			bd->trig_gyro  = hit_gyro;
		} else {
			LOG_DBG("%u < %u, %u < %u", bd->env_accel, bd->thresh_high_accel, bd->env_gyro, bd->thresh_high_gyro);

		}

    } else { /* IMU_BD_ST_CAPTURING */
        if ((int32_t)bd->wr_idx - bd->capture_start >= (int32_t)bd->block_len) {
            imu_bd_emit_block(bd, bd->capture_start, bd->trig_accel, bd->trig_gyro, false);
            bd->state = IMU_BD_ST_IDLE;
            bd->armed = false;   /* must see sustained quiet on BOTH channels to re-trigger */
            bd->quiet_count = 0;
        	LOG_DBG("DONE");
        } else {
        	LOG_DBG("collect");
        }
    }

    /* Re-arm guard: once disarmed (right after a capture), require
     * rearm_samples *consecutive* samples with BOTH env_accel and
     * env_gyro below their low thresholds before the detector is
     * allowed to trigger again. Any excursion back above either low
     * threshold resets the count. This intentionally makes it harder
     * to re-arm than to trigger, so a burst that's dying down in one
     * channel but still active in the other won't cause an immediate
     * re-trigger/chatter. */
    if (!bd->armed) {
    	if (!bd->rearm_samples) {
    		bd->armed = true;
    	} else {
			bool quiet = (bd->env_accel < bd->thresh_low_accel) &&
						 (bd->env_gyro  < bd->thresh_low_gyro);
			if (quiet) {
				bd->quiet_count++;
				if (bd->quiet_count >= bd->rearm_samples) {
					bd->armed = true;
				}

			} else {
				LOG_DBG("%u > %u or %u > %u", bd->env_accel, bd->thresh_low_accel, bd->env_gyro , bd->thresh_low_gyro);

				bd->quiet_count = 0;
			}
    	}
    }

    /* Optional: force an emission if nothing has happened for a while.
     * Only fires while IDLE (never interrupts an in-progress capture),
     * and only once block_len samples actually exist to emit. Any
     * emission (forced or triggered) resets the clock -- see
     * imu_bd_emit_block(). */

	if (bd->timeout_samples > 0 &&
		bd->state == IMU_BD_ST_IDLE &&
		bd->wr_idx >= bd->block_len &&
		(bd->wr_idx - bd->last_emit_idx) >= bd->timeout_samples) {
		if (bd->env_accel < bd->timeout_margin_accel &&
		        bd->env_gyro  < bd->timeout_margin_gyro)
		{
			LOG_INF("Timoutsend");
			int32_t start = (int32_t)bd->wr_idx - (int32_t)bd->block_len;
			imu_bd_emit_block(bd, start, false, false, true);
		}
	}
}
