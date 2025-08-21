/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "driver/charger/isl95522.h"
#include "driver/charger/isl95522_public.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(charger_init, LOG_LEVEL_INF);

static enum ec_error_list isl95522_write(int chgnum, int offset, int value)
{
	int rv = i2c_write16(chg_chips[chgnum].i2c_port,
			     chg_chips[chgnum].i2c_addr_flags, offset, value);
	if (rv)
		LOG_INF("%s failed (%d)", __func__, rv);

	return rv;
}

static void set_ac_prochot(void)
{
	int input_current;

	if (extpower_is_present()) {
		if (!charger_get_input_current_limit(0, &input_current)) {
			LOG_INF("set_ac_prochot: %d", input_current);
			isl95522_set_ac_prochot(0, input_current);
		}
	} else {
		/* follow ISL95522 data sheet 0x47H default value */
		LOG_INF("set_ac_prochot: default");
		isl95522_set_ac_prochot(0, 6144);
	}
}
DECLARE_HOOK(HOOK_POWER_SUPPLY_CHANGE, set_ac_prochot, HOOK_PRIO_DEFAULT);

static void set_prochot_debounce(void)
{
	int ctl_val, rv;

	ctl_val = ISL95522_REG_PROCHOT_DEBOUNCE_500US;
	rv = isl95522_write(0, ISL95522_REG_PROCHOT_DEBOUNCE, ctl_val);
}

static void set_prochot_duration(void)
{
	int ctl_val, rv;

	ctl_val = ISL95522_REG_PROCHOT_DURATION_10MS;
	rv = isl95522_write(0, ISL95522_REG_PROCHOT_DURATION, ctl_val);
}

static void set_chg_custom_setting(void)
{
	LOG_INF("kaladin: set_chg_reg_custom");

	set_prochot_debounce();
	set_prochot_duration();
	/* Set dc prochot value by kaladin battery design */
	isl95522_set_dc_prochot(0, 4352);
	/* Set ISL95522 data sheet 0x47H default value */
	isl95522_set_ac_prochot(0, 6144);
}
DECLARE_HOOK(HOOK_INIT, set_chg_custom_setting,
	     HOOK_PRIO_POST_BATTERY_INIT + 1);
