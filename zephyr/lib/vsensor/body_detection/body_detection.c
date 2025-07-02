/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "body_detection.h"
#include "cros/math/exp_smoothing.h"
#include "cros/math/iir_decimator.h"
#include "cros/math/iir_filter.h"
#include "hooks.h"
#include "hwtimer.h"
#include "mkbp_input_devices.h"
#include "motion_sense_fifo.h"
#include "timer.h"

#include <zephyr/logging/log.h>

/*
 * Filter parameters are calculated to work with 15Hz sampling frequency.
 * However, the range for which the algorithm provides reliable detection is
 * wider (min 10Hz, max 20Hz). These are set in mHz.
 */
#define ALGORITHM_FREQ_MIN 10000
#define ALGORITHM_FREQ_OPTIMAL 15000
#define ALGORITHM_FREQ_MAX 20000
/*
 * How many samples have to be ignored for variance estimator
 * calculation after boot. We definitely don't want to change
 * default on-body state too early when filters and smoothers
 * do not contain valid data yet.
 * Use sample count for 5s period as default.
 */
#define INITIAL_INACTIVITY_SAMPLES (5 * ALGORITHM_FREQ_MAX / 1000)

LOG_MODULE_REGISTER(body_detection, CONFIG_LOG_DEFAULT_LEVEL);

test_export_static float var_threshold;
test_export_static float confidence_delta;

test_export_static struct motion_sensor_t *body_sensor =
	&motion_sensors[CONFIG_BODY_DETECTION_SENSOR];

static const struct body_detect_params default_body_detect_params = {
	.var_threshold = CONFIG_BODY_DETECTION_VAR_THRESHOLD,
	.confidence_delta = CONFIG_BODY_DETECTION_CONFIDENCE_DELTA,
};

static bool body_detect_enable = IS_ENABLED(CONFIG_BODY_DETECTION);
static bool body_detect_initialized;
static bool body_detect_rate_is_set;
static enum body_detect_states motion_state = BODY_DETECTION_ON_BODY;
static uint64_t onbody_lasttime;
STATIC_IF(CONFIG_ACCEL_SPOOF_MODE) bool spoof_enable;

/* The decimator using ALGORITHM_FREQ_OPTIMAL*/
static int decimator_valid;
static IIR_DECIMATOR(decimator, ALGORITHM_FREQ_OPTIMAL, 3);

static struct iir_filter_t main_filter;
static struct exp_smooth_t smooth_x, smooth_y, smooth_z, smooth_var;

/* Clock */
test_export_static timestamp_t (*get_time_ptr)() = get_time;

static uint64_t get_curtime(void)
{
	timestamp_t time = get_time_ptr();

	return time.val;
}

static void body_detect_get_params(void)
{
	/* If body detection params haven't been set, use the default ones. */
	if (!body_sensor->bd_params) {
		body_sensor->bd_params = &default_body_detect_params;
	}

	/*
	 * In case only some of the parameters have been specified use
	 * the default values for the rest of them.
	 */
	if (body_sensor->bd_params->var_threshold != 0) {
		var_threshold = body_sensor->bd_params->var_threshold;
	} else {
		var_threshold = default_body_detect_params.var_threshold;
	}

	if (body_sensor->bd_params->confidence_delta != 0) {
		confidence_delta = body_sensor->bd_params->confidence_delta;
	} else {
		confidence_delta = default_body_detect_params.confidence_delta;
	}

	LOG_INF("var_threshold: %d, confidence_delta: %d", (int)var_threshold,
		(int)confidence_delta);
}

static void print_body_detect_mode(void)
{
	if (body_detect_get_state()) {
		LOG_INF("On body");
	} else {
		LOG_INF("Off body");
	}
}

/* Change the motion state and commit the change to AP. */
void body_detect_change_state(enum body_detect_states state, bool spoof)
{
	if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) && spoof_enable && !spoof) {
		return;
	}
	if (IS_ENABLED(CONFIG_GESTURE_HOST_DETECTION)) {
		struct ec_response_motion_sensor_data vector = {
			.flags = MOTIONSENSE_SENSOR_FLAG_BYPASS_FIFO,
			.activity_data = {
				.activity = MOTIONSENSE_ACTIVITY_BODY_DETECTION,
				.state = state,
			},
			.sensor_num = MOTION_SENSE_ACTIVITY_SENSOR_ID,
		};
		motion_sense_fifo_stage_data(&vector, NULL, 0,
					     __hw_clock_source_read());
		motion_sense_fifo_commit_data();
	}
	/* change the motion state */
	motion_state = state;
	if (state == BODY_DETECTION_ON_BODY) {
		/* reset time counting of stationary */
		onbody_lasttime = get_curtime();
	}

	/* state changing log */
	print_body_detect_mode();

	if (IS_ENABLED(CONFIG_BODY_DETECTION_NOTIFY_MODE_CHANGE) &&
	    motion_sense_get_ec_config() == SENSOR_CONFIG_EC_S0) {
		host_set_single_event(EC_HOST_EVENT_BODY_DETECT_CHANGE);
	}

	hook_notify(HOOK_BODY_DETECT_CHANGE);
}

void body_detect_set_enable(int enable)
{
	body_detect_enable = enable;
	body_detect_change_state(BODY_DETECTION_ON_BODY, false);
}

int body_detect_get_enable(void)
{
	return body_detect_enable;
}

enum body_detect_states body_detect_get_state(void)
{
	return motion_state;
}

static void body_detect_set_update_rate(int odr)
{
	if (iir_decimator_init(&decimator, odr) != 0) {
		return;
	}

	body_detect_get_params();
	decimator_valid = 0;
	body_detect_rate_is_set = true;
}

static void body_detect_init(float x0, float y0, float z0)
{
	int ret;

	/* Initialize X,Y,Z averaging filters */
	exp_smooth_init(&smooth_x, 0.95, x0);
	exp_smooth_init(&smooth_y, 0.95, y0);
	exp_smooth_init(&smooth_z, 0.95, z0);

	/*
	 * Initialize LPF (fcut = nyquist/ALGORITHM_FREQ_OPTIMAL)
	 * ALGORITHM_FREQ_OPTIMAL needs to be converted to Hz from mHz
	 */
	ret = filter_butterworth_lpf_init(
		&main_filter, 5, 1.0f / (ALGORITHM_FREQ_OPTIMAL / 1000));
	if (ret != 0) {
		LOG_ERR("BD ERR: failed to initialize IIR filter, ret = %d",
			ret);
		return;
	}

	/* Initialize Variance smoothing */
	exp_smooth_init(&smooth_var, 0.9f, 0);

	body_detect_initialized = true;
}
test_export_static void body_detect_step(float x, float y, float z,
					 uint64_t curtime)
{
	static int inactivity_cnt;
	float x_avg = 0, y_avg = 0, z_avg = 0;
	float var = 0;
	float motion_confidence = 0;

	if (body_detect_initialized == false) {
		body_detect_init(x, y, z);
		body_detect_change_state(BODY_DETECTION_ON_BODY, false);
		return;
	}

	/* Do not process if rate is unknown */
	if (body_detect_rate_is_set == false) {
		return;
	}

	/* Check if we need to decimate */
	if (iir_decimator_step(&decimator, &x, &y, &z)) {
		/* Value was decimated */
		return;
	}

	/* Wait INITIAL_INACTIVITY_SAMPLES before filter value is stable */
	if (decimator_valid < INITIAL_INACTIVITY_SAMPLES) {
		decimator_valid++;
		return;
	}

	/* Smooth X, Y, Z data */
	x_avg = exp_smooth_step(&smooth_x, x);
	y_avg = exp_smooth_step(&smooth_y, y);
	z_avg = exp_smooth_step(&smooth_z, z);

	/*
	 * Ignore first INITIAL_INACTIVITY_SAMPLES to ensure all
	 * filters are up and running after the boot.
	 */
	if (inactivity_cnt < INITIAL_INACTIVITY_SAMPLES) {
		var = 0.0f;
		inactivity_cnt++;
	} else {
		/*
		 * Calculate scalar from vector.
		 *
		 * This function is not a proper metric, but has significant
		 * advantages over Euclidean vector length.
		 * 1. Average value in steady state is zero.
		 * 2. Functional is linear and symmetrical, so assuming the
		 * sensor noise is Gaussian then the noise cancels over some
		 * longer period of time (adding, smoothing); no need to
		 * calculate RMS noise and put it to the equation.
		 * 3. No need to store measurement history to calculate moving
		 * average or moving variance.
		 */
		var = (x - x_avg) + (y - y_avg) + (z - z_avg);

		/* Filter variance */
		var = iir_filter_step(&main_filter, var);
	}

	/*
	 * Moving Variance estimator: use exponential smoothing
	 */
	var = exp_smooth_step(&smooth_var, var);
	if (var < 0.0f) {
		var = -var;
	}

	/*
	 * Calculate motion confidence in %, thresholds are in uGs but var
	 * is in mGs.
	 * confidence_delta defines a hysteresis range around var_threshold:
	 *      -------------------------------------------------------
	 *      | 100%      | var > var_threshold + confidence_delta  |
	 *      -------------------------------------------------------
	 *      | 0% - 100% | proporitonaly when var is between       |
	 *      -------------------------------------------------------
	 *      | 0%        | var < var_threshold - confidence_delta  |
	 *      -------------------------------------------------------
	 */
	var = var * 1000.0f;
	if (var < var_threshold - confidence_delta) {
		motion_confidence = 0;
	} else if (var > var_threshold + confidence_delta) {
		motion_confidence = 100;
	} else {
		motion_confidence = 100.0f *
				    (var - var_threshold + confidence_delta) /
				    (2 * confidence_delta);
	}

	LOG_DBG("time=%lld conf=%d var=%d x=%d/%d y=%d/%d z=%d/%d [%d %d %d]",
		curtime, (int)motion_confidence, (int)var, (int)x, (int)x_avg,
		(int)y, (int)y_avg, (int)z, (int)z_avg, motion_state,
		(int)var_threshold, (int)confidence_delta);

	/*
	 * Body detection
	 */
	if (motion_state == BODY_DETECTION_ON_BODY) {
		if (motion_confidence > CONFIG_BODY_DETECTION_ON_BODY_CON) {
			LOG_DBG("Confidence(%d) is above threshold(%d), "
				"updating timestamp to %llu",
				(int)motion_confidence,
				CONFIG_BODY_DETECTION_ON_BODY_CON, curtime);
			onbody_lasttime = curtime;
		}
		if (curtime - onbody_lasttime >
		    (CONFIG_BODY_DETECTION_STATIONARY_DURATION *
		     USEC_PER_SEC)) {
			LOG_DBG("Going OFF body");
			body_detect_change_state(BODY_DETECTION_OFF_BODY,
						 false);
		} else {
			LOG_DBG("Not enough time passed %llu - %llu > %u",
				curtime, onbody_lasttime,
				(CONFIG_BODY_DETECTION_STATIONARY_DURATION *
				 USEC_PER_SEC));
		}
	} else {
		if (motion_confidence > CONFIG_BODY_DETECTION_ON_BODY_CON) {
			body_detect_change_state(BODY_DETECTION_ON_BODY, false);
		}
	}
}

void body_detect(void)
{
	if (!body_detect_enable) {
		return;
	}

	/*
	 * Motion sensor returns 16-bit RAW value with 1-bit sign. Convert
	 * to mG using formula:
	 *               value_RAW * range[G] * 1000
	 *   value[mG] = ---------------------------
	 *                          2^15
	 */
	body_detect_step(
		(body_sensor->xyz[X] * body_sensor->current_range * 1000) >> 15,
		(body_sensor->xyz[Y] * body_sensor->current_range * 1000) >> 15,
		(body_sensor->xyz[Z] * body_sensor->current_range * 1000) >> 15,
		get_curtime());
}

void body_detect_reset(void)
{
	int odr = body_sensor->drv->get_data_rate(body_sensor);

	LOG_DBG("Resetting body detection");
	if (motion_state == BODY_DETECTION_ON_BODY) {
		onbody_lasttime = get_curtime();
	} else if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE)) {
		body_detect_change_state(BODY_DETECTION_ON_BODY, spoof_enable);
	}

	body_detect_get_params();
	body_detect_set_update_rate(odr);
}

#ifdef CONFIG_ACCEL_SPOOF_MODE
void body_detect_set_spoof(int enable)
{
	spoof_enable = enable;
	/* After disabling spoof mode, commit current state. */
	if (!enable) {
		body_detect_change_state(motion_state, false);
	}
}

bool body_detect_get_spoof(void)
{
	return spoof_enable;
}

static int command_setbodydetectionmode(int argc, const char **argv)
{
	if (argc == 1) {
		print_body_detect_mode();
		return EC_SUCCESS;
	}

	if (argc != 2) {
		return EC_ERROR_PARAM_COUNT;
	}

	/* |+1| to also make sure the strings the same length. */
	if (strncmp(argv[1], "on", strlen("on") + 1) == 0) {
		body_detect_change_state(BODY_DETECTION_ON_BODY, true);
		spoof_enable = true;
	} else if (strncmp(argv[1], "off", strlen("off") + 1) == 0) {
		body_detect_change_state(BODY_DETECTION_OFF_BODY, true);
		spoof_enable = true;
	} else if (strncmp(argv[1], "reset", strlen("reset") + 1) == 0) {
		body_detect_reset();
		/*
		 * Don't call body_detect_set_spoof(), since
		 * body_detect_change_state() was already called by
		 * body_detect_reset().
		 */
		spoof_enable = false;
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(
	bodydetectmode, command_setbodydetectionmode, "[on | off | reset]",
	"Manually force body detect mode to on (body), off (body) or reset.");
#endif /* CONFIG_ACCEL_SPOOF_MODE */
