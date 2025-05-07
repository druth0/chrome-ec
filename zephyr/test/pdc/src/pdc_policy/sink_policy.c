/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file tests the sink policies on type-C ports.
 */

#include "chipset.h"
#include "emul/emul_pdc.h"
#include "test/util.h"
#include "timer.h"
#include "usbc/pdc_power_mgmt.h"
#include "usbc/utils.h"
#include "zephyr/sys/util.h"
#include "zephyr/sys/util_macro.h"

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(pdc_sink_policy);

DECLARE_FAKE_VALUE_FUNC(int, chipset_in_state, int);

BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == 2,
	     "PDC sink policy test suite must supply exactly 2 PDC ports");

#define PDC_TEST_TIMEOUT 2000

/* TODO: b/343760437 - Once the emulator can detect the PDC threads are idle,
 * remove the sleep delay to let the policy code run.
 */
#define PDC_POLICY_DELAY_MS 500
#define PDC_NODE_PORT0 DT_NODELABEL(pdc_emul1)
#define PDC_NODE_PORT1 DT_NODELABEL(pdc_emul2)

#define USBC_NODE0 DT_NODELABEL(usbc0)
#define USBC_NODE1 DT_NODELABEL(usbc1)

#define TEST_USBC_PORT0 USBC_PORT_FROM_PDC_DRIVER_NODE(PDC_NODE_PORT0)
#define TEST_USBC_PORT1 USBC_PORT_FROM_PDC_DRIVER_NODE(PDC_NODE_PORT1)

static void clear_partner_pdos(const struct emul *e, enum pdo_type_t type)
{
	uint32_t clear_pdos[PDO_MAX_OBJECTS] = { 0 };

	emul_pdc_set_pdos(e, type, PDO_OFFSET_0, ARRAY_SIZE(clear_pdos),
			  PARTNER_PDO, clear_pdos);
}

struct pdc_fixture {
	const struct device *dev;
	const struct emul *emul_pdc;
	uint32_t pdos[PDO_MAX_OBJECTS];
	uint8_t port;
};

struct sink_policy_fixture {
	struct pdc_fixture pdc[CONFIG_USB_PD_PORT_MAX_COUNT];
};

static enum chipset_state_mask fake_chipset_state = CHIPSET_STATE_ON;

static int custom_fake_chipset_in_state(int mask)
{
	return !!(fake_chipset_state & mask);
}

static struct sink_policy_fixture fixture = {
	.pdc = {
		[0] = {
			.dev = DEVICE_DT_GET(PDC_NODE_PORT0),
			.emul_pdc = EMUL_DT_GET(PDC_NODE_PORT0),
			.port = TEST_USBC_PORT0,
			.pdos = {
				PDO_FIXED(5000, 1500, 0),
				PDO_FIXED(9000, 3000, 0),
				PDO_FIXED(12000, 3000, 0),
			},
		},
		[1] = {
			.dev = DEVICE_DT_GET(PDC_NODE_PORT1),
			.emul_pdc = EMUL_DT_GET(PDC_NODE_PORT1),
			.port = TEST_USBC_PORT1,
			.pdos = {
				PDO_FIXED(5000, 1500, 0),
				PDO_FIXED(9000, 3000, 0),
				PDO_FIXED(20000, 3000, 0),
			},
		},
	},
};

static void *sink_policy_setup(void)
{
	return &fixture;
};

static int connect_sink(const struct pdc_fixture *pdc)
{
	union connector_status_t cs = { 0 };

	emul_pdc_configure_snk(pdc->emul_pdc, &cs);
	clear_partner_pdos(pdc->emul_pdc, SOURCE_PDO);
	zassert_ok(emul_pdc_set_pdos(pdc->emul_pdc, SOURCE_PDO, PDO_OFFSET_0,
				     ARRAY_SIZE(pdc->pdos), PARTNER_PDO,
				     pdc->pdos));

	emul_pdc_set_rdo(pdc->emul_pdc, RDO_FIXED(1, 1500, 1500, 0));

	zassert_ok(emul_pdc_connect_partner(pdc->emul_pdc, &cs));

	zassert_ok(pdc_power_mgmt_wait_for_sync(pdc->port, -1));

	return 0;
}

static void sink_policy_before(void *f)
{
	RESET_FAKE(chipset_in_state);
	chipset_in_state_fake.custom_fake = custom_fake_chipset_in_state;
}

static void sink_policy_after(void *f)
{
	struct sink_policy_fixture *fixture = f;

	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
		zassert_ok(emul_pdc_disconnect(fixture->pdc[port].emul_pdc));
		zassert_ok(emul_pdc_reset(fixture->pdc[port].emul_pdc));
		zassert_ok(pdc_power_mgmt_wait_for_sync(fixture->pdc[port].port,
							-1));
	}
}

ZTEST_SUITE(sink_policy, NULL, sink_policy_setup, sink_policy_before,
	    sink_policy_after, NULL);

ZTEST_USER_F(sink_policy, test_sink_policy)
{
	union connector_status_t connector_status;
	uint32_t rdo;

	connect_sink(&fixture->pdc[TEST_USBC_PORT0]);

	zassert_ok(pdc_power_mgmt_get_connector_status(TEST_USBC_PORT0,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify correct RDO is selected on PORT0 */
	zassert_ok(
		emul_pdc_get_rdo(fixture->pdc[TEST_USBC_PORT0].emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);
}

ZTEST_USER_F(sink_policy, test_sink_policy_attach_better_charger)
{
	union connector_status_t connector_status;
	uint32_t rdo;
	const struct pdc_fixture *charger = &fixture->pdc[TEST_USBC_PORT0];
	const struct pdc_fixture *better_charger =
		&fixture->pdc[TEST_USBC_PORT1];

	connect_sink(charger);
	connect_sink(better_charger);

	/* Verify charger sink path is disabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 0);

	/* Verify better charger sink path is enabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(better_charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify correct RDO is selected */
	zassert_ok(emul_pdc_get_rdo(better_charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);

	/* Verify correct RDO is selected for disabled charger */
	zassert_ok(emul_pdc_get_rdo(charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);
}

ZTEST_USER_F(sink_policy, test_sink_policy_attach_worse_charger)
{
	union connector_status_t connector_status;
	uint32_t rdo;
	const struct pdc_fixture *charger = &fixture->pdc[TEST_USBC_PORT1];
	const struct pdc_fixture *worse_charger =
		&fixture->pdc[TEST_USBC_PORT0];

	connect_sink(charger);
	connect_sink(worse_charger);

	/* Verify charger sink path stays enabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify worse charger sink path is disabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(worse_charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 0);

	/* Verify correct RDO is selected */
	zassert_ok(emul_pdc_get_rdo(charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);

	/* Verify correct RDO is selected for worse charger although not
	 * enabled. */
	zassert_ok(emul_pdc_get_rdo(worse_charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);
}

ZTEST_USER_F(sink_policy, test_sink_policy_detach_better_charger)
{
	union connector_status_t connector_status;
	uint32_t rdo;
	const struct pdc_fixture *charger = &fixture->pdc[TEST_USBC_PORT0];
	const struct pdc_fixture *better_charger =
		&fixture->pdc[TEST_USBC_PORT1];

	connect_sink(charger);
	connect_sink(better_charger);

	/* Verify charger sink path is disabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 0);

	/* Verify better charger sink path is enabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(better_charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify correct RDO is selected */
	zassert_ok(emul_pdc_get_rdo(better_charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);

	zassert_ok(emul_pdc_disconnect(better_charger->emul_pdc));
	zassert_ok(pdc_power_mgmt_wait_for_sync(better_charger->port, -1));
	zassert_ok(pdc_power_mgmt_wait_for_sync(charger->port, -1));

	/* Verify charger sink path is enabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify correct RDO is selected */
	zassert_ok(emul_pdc_get_rdo(charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);
}

ZTEST_USER_F(sink_policy, test_sink_policy_detach_worse_charger)
{
	union connector_status_t connector_status;
	uint32_t rdo;
	const struct pdc_fixture *charger = &fixture->pdc[TEST_USBC_PORT1];
	const struct pdc_fixture *worse_charger =
		&fixture->pdc[TEST_USBC_PORT0];

	connect_sink(charger);
	connect_sink(worse_charger);

	/* Verify charger sink path stays enabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify worse charger sink path is disabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(worse_charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 0);

	zassert_ok(emul_pdc_disconnect(worse_charger->emul_pdc));
	zassert_ok(pdc_power_mgmt_wait_for_sync(worse_charger->port, -1));

	/* Verify charger sink path stays enabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify correct RDO is selected */
	zassert_ok(emul_pdc_get_rdo(charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);
}
