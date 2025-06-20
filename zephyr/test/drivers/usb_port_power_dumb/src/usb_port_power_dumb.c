/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "usb_charge.h"

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

/* Expecting at least one port */
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) >= 1);
BUILD_ASSERT(USB_PORT_COUNT >= 1);

/* Index of the USB-A port under test */
#define PORT_ID 0

static int check_gpio_status_for_port(int port_id)
{
	/* Ensure we don't make any invalid inquiries. These should only trip in
	 * the case of developer error.
	 */
	zassert_true(port_id < ARRAY_SIZE(usb_port_enable),
		     "Out of bounds port_id");
	zassert_true(usb_port_enable[port_id] >= 0,
		     "No valid pin number for this port");

	return gpio_get_level(usb_port_enable[port_id]);
}

ZTEST(usb_port_power_dumb, test_console_command__noargs)
{
	const char *outbuffer;
	size_t buffer_size;

	/* With no args, print current state */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "usbchargemode"), NULL);
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_not_null(strstr(outbuffer, "Port " STRINGIFY(PORT_ID) ": off"),
			 "Actual: '%s'", outbuffer);

	zassert_false(check_gpio_status_for_port(PORT_ID), NULL);
}

ZTEST(usb_port_power_dumb, test_console_command__modify_port_status)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Change the port status to on */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(),
				     "usbchargemode " STRINGIFY(PORT_ID) " on"),
		   NULL);
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_not_null(strstr(outbuffer, "Port " STRINGIFY(PORT_ID) ": on"),
			 "Actual: '%s'", outbuffer);

	zassert_true(check_gpio_status_for_port(PORT_ID), NULL);
}

ZTEST(usb_port_power_dumb, test_console_command__invalid)
{
	/* Various bad input */
	zassert_not_ok(shell_execute_cmd(get_ec_shell(), "usbchargemode NaN"),
		       NULL);
	zassert_not_ok(shell_execute_cmd(get_ec_shell(), "usbchargemode -1"),
		       NULL);
	zassert_not_ok(shell_execute_cmd(get_ec_shell(), "usbchargemode 10000"),
		       NULL);
	zassert_ok(
		!shell_execute_cmd(get_ec_shell(),
				   "usbchargemode " STRINGIFY(PORT_ID) " abc"),
		NULL);
}

ZTEST(usb_port_power_dumb, test_host_command__enable)
{
	int ret;
	struct ec_params_usb_charge_set_mode params = {
		.mode = USB_CHARGE_MODE_ENABLED,
		.usb_port_id = PORT_ID,
	};

	ret = ec_cmd_usb_charge_set_mode(NULL, &params);

	zassert_ok(ret, "Host command returned %d", ret);
	zassert_true(check_gpio_status_for_port(PORT_ID), NULL);
}

ZTEST(usb_port_power_dumb, test_host_command__invalid_port_id)
{
	int ret;
	struct ec_params_usb_charge_set_mode params = {
		.mode = USB_CHARGE_MODE_ENABLED,
		/* This port ID does not exist */
		.usb_port_id = UINT8_MAX,
	};

	ret = ec_cmd_usb_charge_set_mode(NULL, &params);

	zassert_equal(EC_RES_ERROR, ret, "Host command returned %d", ret);
	zassert_false(check_gpio_status_for_port(PORT_ID), NULL);
}

ZTEST(usb_port_power_dumb, test_host_command__invalid_mode)
{
	int ret;
	struct ec_params_usb_charge_set_mode params = {
		.mode = USB_CHARGE_MODE_COUNT, /* Invalid */
		.usb_port_id = PORT_ID,
	};

	ret = ec_cmd_usb_charge_set_mode(NULL, &params);

	zassert_equal(EC_RES_ERROR, ret, "Host command returned %d", ret);
	zassert_false(check_gpio_status_for_port(PORT_ID), NULL);
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	/* Turn the port off */
	zassert_ok(usb_charge_set_mode(PORT_ID, USB_CHARGE_MODE_DISABLED,
				       USB_DISALLOW_SUSPEND_CHARGE),
		   NULL);
}

ZTEST_SUITE(usb_port_power_dumb, drivers_predicate_post_main, NULL, reset,
	    reset, NULL);
