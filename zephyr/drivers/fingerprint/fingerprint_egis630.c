// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define DT_DRV_COMPAT egis_egis630

#include "fingerprint_egis630.h"
#include "fingerprint_egis630_pal.h"
#include "fingerprint_egis630_private.h"

#include <assert.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <drivers/fingerprint.h>
#include <fingerprint/v4l2_types.h>

LOG_MODULE_REGISTER(cros_fingerprint, LOG_LEVEL_INF);

static inline int egis630_enable_irq(const struct device *dev)
{
	const struct egis630_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt,
					     GPIO_INT_EDGE_TO_INACTIVE);
	if (rc < 0) {
		LOG_ERR("Can't enable interrupt: %d", rc);
	}

	return rc;
}

static inline int egis630_disable_irq(const struct device *dev)
{
	const struct egis630_cfg *cfg = dev->config;
	int rc;

	rc = gpio_pin_interrupt_configure_dt(&cfg->interrupt, GPIO_INT_DISABLE);
	if (rc < 0) {
		LOG_ERR("Can't disable interrupt: %d", rc);
	}

	return rc;
}

/* Minimum reset duration */
#define FP_SENSOR_RESET_DURATION_MS (20)

void egis_fp_reset_sensor(const struct egis630_cfg *cfg)
{
	if (cfg == NULL) {
		return;
	}

	int ret = gpio_pin_set_dt(&cfg->reset_pin, 1);
	if (ret < 0) {
		LOG_ERR("Failed to set FP reset pin, status: %d", ret);
		return;
	}
	k_msleep(FP_SENSOR_RESET_DURATION_MS);
	ret = gpio_pin_set_dt(&cfg->reset_pin, 0);
	if (ret < 0) {
		LOG_ERR("Failed to set FP reset pin, status: %d", ret);
		return;
	}
	k_msleep(FP_SENSOR_RESET_DURATION_MS);
	return;
}

static int convert_egis_get_image_error_code(egis_api_return_t code)
{
	switch (code) {
	case EGIS_API_IMAGE_QUALITY_GOOD:
		return FINGERPRINT_SENSOR_SCAN_GOOD;
	case EGIS_API_IMAGE_QUALITY_BAD:
	case EGIS_API_IMAGE_QUALITY_WATER:
		return FINGERPRINT_SENSOR_SCAN_LOW_IMAGE_QUALITY;
	case EGIS_API_IMAGE_EMPTY:
		return FINGERPRINT_SENSOR_SCAN_TOO_FAST;
	case EGIS_API_IMAGE_QUALITY_PARTIAL:
		return FINGERPRINT_SENSOR_SCAN_LOW_SENSOR_COVERAGE;
	default:
		assert(code < 0);
		return code;
	}
}

static uint16_t convert_egis_sensor_init_error_code(egis_api_return_t code)
{
	if (code == EGIS_API_ERROR_IO_SPI) {
		return FINGERPRINT_ERROR_SPI_COMM;
	} else if (code == EGIS_API_ERROR_DEVICE_NOT_FOUND) {
		return FINGERPRINT_ERROR_BAD_HWID;
	} else if (code != EGIS_API_OK) {
		return FINGERPRINT_ERROR_INIT_FAIL;
	}
	return 0;
}

static int egis630_init(const struct device *dev)
{
	const struct egis630_cfg *cfg = dev->config;
	struct egis630_data *data = dev->data;
	egis_api_return_t ret;

	data->errors = FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN;

	egis_fp_reset_sensor(cfg);

	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
		return 0;
	}

	int int_pin_value = gpio_pin_get_dt(&cfg->interrupt);

	ret = egis_sensor_init();

	data->errors |= convert_egis_sensor_init_error_code(ret);

	if (int_pin_value == gpio_pin_get_dt(&cfg->interrupt)) {
		LOG_ERR("Sensor IRQ not ready");
		data->errors |= FINGERPRINT_ERROR_NO_IRQ;
	}

	return 0;
}

static int egis630_deinit(const struct device *dev)
{
	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
		return 0;
	}

	egis_api_return_t ret = egis_sensor_deinit();
	if (ret < 0) {
		LOG_ERR("egis_sensor_deinit() failed, result %d", ret);
		return ret;
	}

	return 0;
}

static int egis630_config(const struct device *dev, fingerprint_callback_t cb)
{
	struct egis630_data *data = dev->data;

	data->callback = cb;

	return 0;
}

static int egis630_get_info(const struct device *dev,
			    struct fingerprint_info *info)
{
	const struct egis630_cfg *cfg = dev->config;
	struct egis630_data *data = dev->data;
	uint16_t sensor_id = 0;
	egis_api_return_t res = EGIS_API_OK;

	memcpy(info, &cfg->info, sizeof(struct fingerprint_info));

	if (IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
		res = egis_get_hwid(&sensor_id);
	}

	if (res != EGIS_API_OK) {
		LOG_ERR("Failed to get EGIS HWID: %d", res);
		return res;
	}

	info->model_id = sensor_id;
	info->errors = data->errors;

	return 0;
}

static int egis630_maintenance(const struct device *dev, uint8_t *buf,
			       size_t size)
{
	if (size < CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE) {
		return -EINVAL;
	}

	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	return 0;
}

static int egis630_set_mode(const struct device *dev,
			    enum fingerprint_sensor_mode mode)
{
	int rc = 0;

	switch (mode) {
	case FINGERPRINT_SENSOR_MODE_DETECT:
		if (IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
			LOG_INF("Sensor changes mode to finger detect");
			egis_set_detect_mode();
			rc = egis630_enable_irq(dev);
		} else {
			rc = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_LOW_POWER:
		if (IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
			egis_sensor_power_down();
			rc = egis630_disable_irq(dev);
		} else {
			rc = -ENOTSUP;
		}
		break;

	case FINGERPRINT_SENSOR_MODE_IDLE:
		rc = egis630_disable_irq(dev);
		break;

	default:
		rc = -ENOTSUP;
	}

	return rc;
}

static int egis630_acquire_image(const struct device *dev,
				 enum fingerprint_capture_type capture_type,
				 uint8_t *image_buf, size_t image_buf_size)
{
	if (image_buf_size < CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE)
		return -EINVAL;

	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	return convert_egis_get_image_error_code(
		egis_get_image_with_mode(image_buf, capture_type));
}

static int egis630_finger_status(const struct device *dev)
{
	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	egis_api_return_t ret = egis_check_int_status();

	switch (ret) {
	case EGIS_API_FINGER_PRESENT:
		return FINGERPRINT_FINGER_STATE_PRESENT;
	case EGIS_API_FINGER_LOST:
	default:
		return FINGERPRINT_FINGER_STATE_NONE;
	}
}

static const struct fingerprint_driver_api cros_fp_egis630_driver_api = {
	.init = egis630_init,
	.deinit = egis630_deinit,
	.config = egis630_config,
	.get_info = egis630_get_info,
	.maintenance = egis630_maintenance,
	.set_mode = egis630_set_mode,
	.acquire_image = egis630_acquire_image,
	.finger_status = egis630_finger_status,
};

static void egis630_irq(const struct device *dev, struct gpio_callback *cb,
			uint32_t pins)
{
	struct egis630_data *data =
		CONTAINER_OF(cb, struct egis630_data, irq_cb);

	egis630_disable_irq(data->dev);

	if (data->callback != NULL) {
		data->callback(dev);
	}
}

static int egis630_init_driver(const struct device *dev)
{
	const struct egis630_cfg *cfg = dev->config;
	struct egis630_data *data = dev->data;
	int ret;

	if (!spi_is_ready_dt(&cfg->spi)) {
		LOG_ERR("SPI bus is not ready");
		return -EINVAL;
	}

	if (!gpio_is_ready_dt(&cfg->reset_pin)) {
		LOG_ERR("Port for sensor reset GPIO is not ready");
		return -EINVAL;
	}

	ret = gpio_pin_configure_dt(&cfg->reset_pin, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Can't configure sensor reset pin");
		return ret;
	}

	if (!gpio_is_ready_dt(&cfg->interrupt)) {
		LOG_ERR("Port for interrupt GPIO is not ready");
		return -EINVAL;
	}

	ret = gpio_pin_configure_dt(&cfg->interrupt, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Can't configure interrupt pin");
		return ret;
	}

	data->dev = dev;
	gpio_init_callback(&data->irq_cb, egis630_irq, BIT(cfg->interrupt.pin));
	gpio_add_callback_dt(&cfg->interrupt, &data->irq_cb);

	return 0;
}

#define EGIS630_SENSOR_INFO(inst)                                      \
	{                                                              \
		.vendor_id = FOURCC('E', 'G', 'I', 'S'),               \
		.product_id = 9,                                       \
		.model_id = 1,                                         \
		.version = 1,                                          \
		.frame_size = CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE,    \
		.pixel_format = FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(  \
			DT_DRV_INST(inst)),                            \
		.width = FINGERPRINT_SENSOR_RES_X(DT_DRV_INST(inst)),  \
		.height = FINGERPRINT_SENSOR_RES_Y(DT_DRV_INST(inst)), \
		.bpp = FINGERPRINT_SENSOR_RES_BPP(DT_DRV_INST(inst)),  \
	}

#define EGIS630_DEFINE(inst)                                                   \
	static struct egis630_data egis630_data_##inst;                        \
	static const struct egis630_cfg egis630_cfg_##inst = {                 \
		.spi = SPI_DT_SPEC_INST_GET(                                   \
			inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8), 0),        \
		.interrupt = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),           \
		.reset_pin = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),         \
		.info = EGIS630_SENSOR_INFO(inst),                             \
	};                                                                     \
	BUILD_ASSERT(                                                          \
		CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE >=                        \
			FINGERPRINT_SENSOR_REAL_IMAGE_SIZE(DT_DRV_INST(inst)), \
		"FP image buffer size is smaller than raw image size");        \
	DEVICE_DT_INST_DEFINE(inst, egis630_init_driver, NULL,                 \
			      &egis630_data_##inst, &egis630_cfg_##inst,       \
			      POST_KERNEL,                                     \
			      CONFIG_FINGERPRINT_SENSOR_INIT_PRIORITY,         \
			      &cros_fp_egis630_driver_api)

DT_INST_FOREACH_STATUS_OKAY(EGIS630_DEFINE);
