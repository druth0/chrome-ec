/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT realtek_rtk_cros_flash

#include "flash.h"
#include "spi_flash_reg.h"
#include "watchdog.h"
#include "write_protect.h"

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/rts5912_flash_api_ex.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_flash.h>
#include <soc.h>

LOG_MODULE_REGISTER(cros_flash, LOG_LEVEL_ERR);

#if !DT_NODE_EXISTS(DT_CHOSEN(zephyr_flash_controller))
#error "No suitable devicetree overlay specified for zephyr_flash_controller"
#endif

struct cros_flash_rtk_data {
	int all_protected;
	int addr_prot_start;
	int addr_prot_length;
};

struct cros_flash_rtk_config {
	const struct device *flash_dev;
};

#define DRV_DATA(dev) ((struct cros_flash_rtk_data *)(dev)->data)
#define DRV_CONFIG(dev) ((const struct cros_flash_rtk_config *)(dev)->config)

#define FLASH_DEV DT_CHOSEN(zephyr_flash_controller)

static const struct cros_flash_rtk_config cros_flash_config = {
	.flash_dev = DEVICE_DT_GET(FLASH_DEV),
};

static struct cros_flash_rtk_data cros_flash_data;

#define FLASH_LOCK 0x1C

static int cros_flash_rtk_set_status_reg(const struct device *dev, uint8_t *reg)
{
	const struct cros_flash_rtk_config *cfg = DRV_CONFIG(dev);
	uint8_t send_sr1 = reg[0];
	uint8_t send_sr2 = reg[1];
	int ret;

	if (cfg == 0) {
		return -EINVAL;
	}

	/* Write status regs */
	ret = flash_ex_op(cfg->flash_dev, FLASH_RTS5912_EX_OP_WR_SR,
			  (uintptr_t)NULL, &send_sr1);
	if (ret != 0) {
		return ret;
	}
	return flash_ex_op(cfg->flash_dev, FLASH_RTS5912_EX_OP_WR_SR2,
			   (uintptr_t)NULL, &send_sr2);
}

static int cros_flash_rtk_write_protection_set(const struct device *dev,
					       bool enable)
{
	const struct cros_flash_rtk_config *cfg = DRV_CONFIG(dev);
	uint8_t op_out = FLASH_LOCK;

	/* Write protection can be cleared only by core domain reset */
	if (!enable) {
		LOG_ERR("WP can be disabled only via core domain reset");
		return -ENOTSUP;
	}
	return flash_ex_op(cfg->flash_dev, FLASH_RTS5912_EX_OP_WR_SR,
			   (uintptr_t)NULL, &op_out);
}

static void flash_get_status(const struct device *dev, uint8_t *sr1,
			     uint8_t *sr2)
{
	const struct cros_flash_rtk_config *cfg = DRV_CONFIG(dev);
	crec_flash_lock_mapped_storage(1);

	/* Read status register1 */
	flash_ex_op(cfg->flash_dev, FLASH_RTS5912_EX_OP_RD_SR, (uintptr_t)sr1,
		    NULL);

	/* Read status register2 */
	flash_ex_op(cfg->flash_dev, FLASH_RTS5912_EX_OP_RD_SR2, (uintptr_t)sr2,
		    NULL);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);
}

/*
 * Check if Status Register Protect 0 (SRP0) bit in the Status 1 Register
 * is set.
 */
static bool flash_check_status_reg_srp(const struct device *dev)
{
	uint8_t sr1, sr2;

	flash_get_status(dev, &sr1, &sr2);
	return (sr1 & SPI_FLASH_SR1_SRP0);
}

static int flash_set_status(const struct device *dev, uint8_t sr1, uint8_t sr2)
{
	int rv;
	uint8_t regs[2] = { sr1, sr2 };

	if (flash_check_status_reg_srp(dev)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);
	rv = cros_flash_rtk_set_status_reg(dev, regs);
	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return rv;
}

static void flash_protect_int_flash(const struct device *dev, int enable)
{
	/*
	 * Please notice the type of WP_IF bit is R/W1S. Once it's set,
	 * only rebooting EC can clear it.
	 */
	if (enable) {
		cros_flash_rtk_write_protection_set(dev, enable);
	}
}

static int flash_set_status_for_prot(const struct device *dev, int reg1,
				     int reg2)
{
	struct cros_flash_rtk_data *data = DRV_DATA(dev);
	int rv;

	if (write_protect_is_asserted()) {
		return EC_ERROR_ACCESS_DENIED;
	}

	data->all_protected = 0;

	/*
	 * If WP# is active and ec doesn't protect the status registers of
	 * internal spi-flash, protect it now before setting them.
	 */
	flash_protect_int_flash(dev, write_protect_is_asserted());

	rv = flash_set_status(dev, reg1, reg2);
	if (rv != EC_SUCCESS) {
		return rv;
	}

	spi_flash_reg_to_protect(reg1, reg2, &data->addr_prot_start,
				 &data->addr_prot_length);
	return EC_SUCCESS;
}

static int flash_check_prot_reg(const struct device *dev, unsigned int offset,
				unsigned int bytes)
{
	unsigned int start;
	unsigned int len;
	uint8_t sr1, sr2;
	int rv = EC_SUCCESS;

	/*
	 * If WP# is active and ec doesn't protect the status registers of
	 * internal spi-flash, protect it now.
	 */
	flash_protect_int_flash(dev, write_protect_is_asserted());

	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES) {
		return EC_ERROR_INVAL;
	}

	/* Compute current protect range */
	flash_get_status(dev, &sr1, &sr2);

	rv = spi_flash_reg_to_protect(sr1, sr2, &start, &len);

	if (rv) {
		return rv;
	}

	/* Check if ranges overlap */
	if (MAX(start, offset) < MIN(start + len, offset + bytes)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return EC_SUCCESS;
}

/* set new protect range by writing status regs */
static int flash_write_prot_reg(const struct device *dev, unsigned int offset,
				unsigned int bytes, int hw_protect)
{
	uint8_t sr1, sr2;
	int rv;

	/* Invalid values */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES)
		return EC_ERROR_INVAL;

	/* Compute desired protect range */
	flash_get_status(dev, &sr1, &sr2);

	rv = spi_flash_protect_to_reg(offset, bytes, &sr1, &sr2);
	if (rv) {
		return rv;
	}

	return flash_set_status_for_prot(dev, sr1, sr2);
}

static int flash_check_prot_range(const struct device *dev, unsigned int offset,
				  unsigned int bytes)
{
	struct cros_flash_rtk_data *data = DRV_DATA(dev);

	/* Invalid value */
	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES) {
		return EC_ERROR_INVAL;
	}

	if (MAX(data->addr_prot_start, offset) <
	    MIN(data->addr_prot_start + data->addr_prot_length,
		offset + bytes)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	return EC_SUCCESS;
}

/* cros ec flash driver implementations */
static int cros_flash_rtk_init(const struct device *dev)
{
	struct cros_flash_rtk_data *data = DRV_DATA(dev);
	const struct cros_flash_rtk_config *cfg = DRV_CONFIG(dev);

	data->all_protected = 0;
	data->addr_prot_start = 0;
	data->addr_prot_length = 0;
	/*
	 * Protect status registers of internal spi-flash if WP# is active
	 * during ec initialization.
	 */
	flash_protect_int_flash(dev, write_protect_is_asserted());

	if (!device_is_ready(cfg->flash_dev)) {
		LOG_ERR("device %s not ready", cfg->flash_dev->name);
		return -ENODEV;
	}
	return EC_SUCCESS;
}

static int cros_flash_rtk_write(const struct device *dev, int offset, int size,
				const char *src_data)
{
	struct cros_flash_rtk_data *data = DRV_DATA(dev);
	const struct cros_flash_rtk_config *cfg = DRV_CONFIG(dev);
	int ret;

	/* check protection */
	if (data->all_protected) {
		return EC_ERROR_ACCESS_DENIED;
	}

	if (flash_check_prot_range(dev, offset, size)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Invalid data pointer? */
	if (src_data == 0) {
		return -EINVAL;
	}

	/*
	 * If the AP sends a sequence of write commands, we may not have time to
	 * reload the watchdog normally.  Force a reload here to avoid the
	 * watchdog triggering in the middle of flashing.
	 */
	watchdog_reload();

	/* Lock physical flash operations */
	crec_flash_lock_mapped_storage(1);

	LOG_DBG("fwrite %x, %x", offset, size);

	ret = flash_write(cfg->flash_dev, offset, src_data, size);

	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	return ret;
}

static int cros_flash_rtk_erase(const struct device *dev, int offset, int size)
{
	struct cros_flash_rtk_data *data = DRV_DATA(dev);
	const struct cros_flash_rtk_config *cfg = DRV_CONFIG(dev);
	int ret = 0;

	if (data->all_protected) {
		return EC_ERROR_ACCESS_DENIED;
	}

	if (flash_check_prot_range(dev, offset, size)) {
		return EC_ERROR_ACCESS_DENIED;
	}

	if (size <= 0) {
		return -EINVAL;
	}

	crec_flash_lock_mapped_storage(1);

	for (; size > 0; size -= CONFIG_FLASH_ERASE_SIZE) {
		ret = flash_erase(cfg->flash_dev, offset,
				  CONFIG_FLASH_ERASE_SIZE);
		if (ret) {
			break;
		}

		offset += CONFIG_FLASH_ERASE_SIZE;

		/*
		 * Reload the watchdog timer, so that erasing many flash pages
		 * doesn't cause a watchdog reset
		 */
		watchdog_reload();
	}
	/* Unlock physical flash operations */
	crec_flash_lock_mapped_storage(0);

	if (ret) {
		LOG_ERR("Erase Failed, code %d.", ret);
	} else {
		LOG_DBG("Erase success.");
	}
	return ret;
}

static int cros_flash_rtk_get_protect(const struct device *dev, int bank)
{
	unsigned int addr = bank * CONFIG_FLASH_BANK_SIZE;
	int ret;

	ret = flash_check_prot_reg(dev, addr, CONFIG_FLASH_BANK_SIZE);

	return ret;
}

static uint32_t cros_flash_rtk_get_protect_flags(const struct device *dev)
{
	uint32_t flags = 0;
	int rv;
	uint8_t sr1, sr2;
	unsigned int start, len;

	/* Check if WP region is protected in status register */
	rv = flash_check_prot_reg(dev, WP_BANK_OFFSET * CONFIG_FLASH_BANK_SIZE,
				  WP_BANK_COUNT * CONFIG_FLASH_BANK_SIZE);
	if (rv == EC_ERROR_ACCESS_DENIED) {
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;
	} else if (rv) {
		return EC_FLASH_PROTECT_ERROR_UNKNOWN;
	}

	/*
	 * If the status register protects a range, but SRP0 is not set,
	 * or Quad Enable (QE) is set,
	 * flags should indicate EC_FLASH_PROTECT_ERROR_INCONSISTENT.
	 */
	flash_get_status(dev, &sr1, &sr2);
	rv = spi_flash_reg_to_protect(sr1, sr2, &start, &len);
	if (rv) {
		return EC_FLASH_PROTECT_ERROR_UNKNOWN;
	}

	if (DRV_DATA(dev)->all_protected) {
		flags |= EC_FLASH_PROTECT_ALL_NOW;
	}

	return flags;
}

static int cros_flash_rtk_protect_at_boot(const struct device *dev,
					  uint32_t new_flags)
{
	struct cros_flash_rtk_data *data = DRV_DATA(dev);
	int ret;

	if ((new_flags & (EC_FLASH_PROTECT_RO_AT_BOOT |
			  EC_FLASH_PROTECT_ALL_AT_BOOT)) == 0) {
		/* Clear protection bits in status register */
		return flash_set_status_for_prot(dev, 0x04, 0);
	}

	ret = flash_write_prot_reg(dev, CONFIG_WP_STORAGE_OFF,
				   CONFIG_WP_STORAGE_SIZE, 1);
	if (new_flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
		/* TODO: b/390237764 - add flash protection */
		data->all_protected = 1;
	}
	return ret;
}

static int cros_flash_rtk_protect_now(const struct device *dev, int all)
{
	struct cros_flash_rtk_data *data = DRV_DATA(dev);
	int ret = EC_SUCCESS;

	if (all) {
		/* TODO: b/390237764 - add flash protection */
		data->all_protected = 1;
	} else {
		ret = flash_write_prot_reg(dev, CONFIG_WP_STORAGE_OFF,
					   CONFIG_WP_STORAGE_SIZE, 1);
	}

	return ret;
}

static int cros_flash_rtk_get_status(const struct device *dev, uint8_t *sr1,
				     uint8_t *sr2)
{
	flash_get_status(dev, sr1, sr2);

	return EC_SUCCESS;
}

/* cros ec flash driver registration */
static const struct cros_flash_driver_api cros_flash_rtk_driver_api = {
	.init = cros_flash_rtk_init,
	.physical_write = cros_flash_rtk_write,
	.physical_erase = cros_flash_rtk_erase,
	.physical_get_protect = cros_flash_rtk_get_protect,
	.physical_get_protect_flags = cros_flash_rtk_get_protect_flags,
	.physical_protect_at_boot = cros_flash_rtk_protect_at_boot,
	.physical_protect_now = cros_flash_rtk_protect_now,
	.physical_get_status = cros_flash_rtk_get_status,
};

DEVICE_DT_INST_DEFINE(0, cros_flash_rtk_init, NULL, &cros_flash_data,
		      &cros_flash_config, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		      &cros_flash_rtk_driver_api);
