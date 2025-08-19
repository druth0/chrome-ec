/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "egis_api.h"
#include "fpsensor/fpsensor.h"
#include "gpio.h"
#include "plat_reset.h"
#include "system.h"
#include "task.h"
#include "util.h"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_TAG "RBS-rapwer"

/* recorded error flags */
static uint16_t errors;

/* Sensor description */
static struct ec_response_fp_info egis_fp_sensor_info = {
	/* Sensor identification */
	.vendor_id = FOURCC('E', 'G', 'I', 'S'),
	.product_id = 9,
	.model_id = 1,
	.version = 1,
	/* Image frame characteristics */
	.frame_size = FP_SENSOR_IMAGE_SIZE_EGIS,
	.pixel_format = V4L2_PIX_FMT_GREY,
	.width = FP_SENSOR_RES_X_EGIS,
	.height = FP_SENSOR_RES_Y_EGIS,
	.bpp = 16,
};

static int convert_egis_get_image_error_code(egis_api_return_t code)
{
	switch (code) {
	case EGIS_API_IMAGE_QUALITY_GOOD:
		return EC_SUCCESS;
	case EGIS_API_IMAGE_QUALITY_BAD:
	case EGIS_API_IMAGE_QUALITY_WATER:
		return FP_SENSOR_LOW_IMAGE_QUALITY;
	case EGIS_API_IMAGE_EMPTY:
		return FP_SENSOR_TOO_FAST;
	case EGIS_API_IMAGE_QUALITY_PARTIAL:
		return FP_SENSOR_LOW_SENSOR_COVERAGE;
	default:
		assert(code < 0);
		return code;
	}
}

static egis_capture_mode_t
convert_fp_capture_type_to_egis_capture_type(enum fp_capture_type capture_type)
{
	switch (capture_type) {
	case FP_CAPTURE_VENDOR_FORMAT:
	case FP_CAPTURE_SIMPLE_IMAGE:
		return EGIS_CAPTURE_NORMAL_FORMAT;
	case FP_CAPTURE_PATTERN0:
		return EGIS_CAPTURE_BLACK_PXL_TEST;
	case FP_CAPTURE_PATTERN1:
		return EGIS_CAPTURE_WHITE_PXL_TEST;
	case FP_CAPTURE_QUALITY_TEST:
		return EGIS_CAPTURE_RV_INT_TEST;
	case FP_CAPTURE_DEFECT_PXL_TEST:
		return EGIS_CAPTURE_DEFECT_PXL_TEST;
	case FP_CAPTURE_ABNORMAL_TEST:
		return EGIS_CAPTURE_ABNORMAL_TEST;
	case FP_CAPTURE_NOISE_TEST:
		return EGIS_CAPTURE_NOISE_TEST;
	/*  Egis does not support the reset test. */
	case FP_CAPTURE_RESET_TEST:
	default:
		return EGIS_CAPTURE_TYPE_INVALID;
	}
}

void fp_sensor_low_power(void)
{
	egis_sensor_power_down();
}

int fp_sensor_init(void)
{
	egis_fp_reset_sensor();
	/*
	 * Sensor has two INT pads (INT and INTB), and the polarities of INT and
	 * INTB are opposite, Not sure about the final wiring configuration,
	 * so we use a comparison approach.
	 */
	int int_pin_value = gpio_get_level(GPIO_FPS_INT);
	egis_api_return_t ret = egis_sensor_init();
	errors = 0;
	if (ret == EGIS_API_ERROR_IO_SPI) {
		errors |= FP_ERROR_SPI_COMM;
	} else if (ret == EGIS_API_ERROR_DEVICE_NOT_FOUND) {
		errors |= FP_ERROR_BAD_HWID;
	} else if (ret != EGIS_API_OK) {
		errors |= FP_ERROR_INIT_FAIL;
	}

	if (int_pin_value == gpio_get_level(GPIO_FPS_INT)) {
		CPRINTS("Sensor IRQ not ready");
		errors |= FP_ERROR_NO_IRQ;
	}

	return EC_SUCCESS;
}

int fp_sensor_deinit(void)
{
	return egis_sensor_deinit();
}

int fp_sensor_get_info(struct ec_response_fp_info *resp)
{
	uint16_t sensor_id;
	memcpy(resp, &egis_fp_sensor_info, sizeof(struct ec_response_fp_info));
	if (egis_get_hwid(&sensor_id) != EGIS_API_OK)
		return EC_RES_ERROR;

	resp->model_id = sensor_id;
	resp->errors = errors;
	return EC_SUCCESS;
}

__overridable int fp_finger_match(void *templ, uint32_t templ_count,
				  uint8_t *image, int32_t *match_index,
				  uint32_t *update_bitmap)
{
	egis_api_return_t ret = egis_finger_match(templ, templ_count, image,
						  match_index, update_bitmap);

	switch (ret) {
	case EGIS_API_MATCH_MATCHED:
		return EC_MKBP_FP_ERR_MATCH_YES;
	case EGIS_API_MATCH_MATCHED_UPDATED:
		return EC_MKBP_FP_ERR_MATCH_YES_UPDATED;
	case EGIS_API_MATCH_MATCHED_UPDATED_FAILED:
		return EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED;
	case EGIS_API_MATCH_NOT_MATCHED:
		return EC_MKBP_FP_ERR_MATCH_NO;
	case EGIS_API_MATCH_LOW_QUALITY:
		return EC_MKBP_FP_ERR_MATCH_NO_LOW_QUALITY;
	case EGIS_API_MATCH_LOW_COVERAGE:
		return EC_MKBP_FP_ERR_MATCH_NO_LOW_COVERAGE;
	default:
		assert(ret < 0);
		return ret;
	}
}

__overridable int fp_enrollment_begin(void)
{
	return egis_enrollment_begin();
}

__overridable int fp_enrollment_finish(void *templ)
{
	return egis_enrollment_finish(templ);
}

__overridable int fp_finger_enroll(uint8_t *image, int *completion)
{
	egis_api_return_t ret = egis_finger_enroll(image, completion);
	switch (ret) {
	case EGIS_API_ENROLL_FINISH:
	case EGIS_API_ENROLL_IMAGE_OK:
		return EC_MKBP_FP_ERR_ENROLL_OK;
	case EGIS_API_ENROLL_REDUNDANT_INPUT:
		return EC_MKBP_FP_ERR_ENROLL_IMMOBILE;
	case EGIS_API_ENROLL_LOW_QUALITY:
		return EC_MKBP_FP_ERR_ENROLL_LOW_QUALITY;
	case EGIS_API_ENROLL_LOW_COVERAGE:
		return EC_MKBP_FP_ERR_ENROLL_LOW_COVERAGE;
	default:
		assert(ret < 0);
		return ret;
	}
}

int fp_maintenance(void)
{
	return EC_SUCCESS;
}

int fp_acquire_image(uint8_t *image_data, enum fp_capture_type capture_type)
{
	egis_capture_mode_t rc =
		convert_fp_capture_type_to_egis_capture_type(capture_type);

	if (rc == EGIS_CAPTURE_TYPE_INVALID) {
		CPRINTS("Unsupported capture_type %d provided", capture_type);
		return -EINVAL;
	}
	return convert_egis_get_image_error_code(
		egis_get_image_with_mode(image_data, rc));
}

enum finger_state fp_finger_status(void)
{
	egislog_i("");
	egis_api_return_t ret = egis_check_int_status();

	switch (ret) {
	case EGIS_API_FINGER_PRESENT:
		return FINGER_PRESENT;
	case EGIS_API_FINGER_LOST:
	default:
		return FINGER_NONE;
	}
}

void fp_configure_detect(void)
{
	egislog_i("");
	egis_set_detect_mode();
}
