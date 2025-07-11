/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"

#ifdef CONFIG_AP_PWRSEQ_DRIVER
#include <ap_power/ap_pwrseq_sm.h>
#endif
#include <drivers/rvp_board_id.h>

#define DT_DRV_COMPAT intel_rvp_board_id

#define BOM_GPIOS_COUNT 3
#define FAB_GPIOS_COUNT 2
#define BOARD_GPIOS_COUNT 6

LOG_MODULE_REGISTER(rvp_board_id, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1,
	     "Unsupported RVP Board ID instance");

#if DT_NODE_HAS_PROP(DT_DRV_INST(0), bom_gpios)
BUILD_ASSERT(DT_INST_PROP_LEN(0, bom_gpios) == BOM_GPIOS_COUNT,
	     "Incorrect bom gpios count.");
#endif

#if DT_NODE_HAS_PROP(DT_DRV_INST(0), fab_gpios)
BUILD_ASSERT(DT_INST_PROP_LEN(0, fab_gpios) == FAB_GPIOS_COUNT,
	     "Incorrect fab gpios count.");
#endif

BUILD_ASSERT(DT_INST_PROP_LEN(0, board_gpios) == BOARD_GPIOS_COUNT,
	     "Incorrect board gpios count.");

#define FOREACH_RVP_GPIOS_ELEM(inst, prop) \
	DT_INST_FOREACH_PROP_ELEM_SEP(inst, prop, GPIO_DT_SPEC_GET_BY_IDX, (, ))

const struct rvp_board_id_config *rvp_config;

struct rvp_board_id_config {
	int defer_until_s5;
	const struct gpio_dt_spec *bom_gpios_config;
	const struct gpio_dt_spec *fab_gpios_config;
	const struct gpio_dt_spec *board_gpios_config;
};

/*
 * Returns board_id, bom_id or fab_id information based on the request on
 * success -1 on error.
 */
int get_rvp_id_config(enum rvp_id_type id_type)
{
	const struct device *gpio_port;

	int board_id = -1;
	int bom_id = -1;
	int fab_id = -1;
	int rv = -1;

	gpio_port = rvp_config->board_gpios_config[0].port;
	if (!device_is_ready(gpio_port)) {
		LOG_ERR("gpio controller port is not initialized, cannot access it");
		return rv;
	}

	if (id_type == BOARD_ID) {
		/*
		 * BOARD ID[5:0] : IOEX[13:8]
		 */
		board_id = gpio_pin_get_dt(&rvp_config->board_gpios_config[0]);
		board_id |= gpio_pin_get_dt(&rvp_config->board_gpios_config[1])
			    << 1;
		board_id |= gpio_pin_get_dt(&rvp_config->board_gpios_config[2])
			    << 2;
		board_id |= gpio_pin_get_dt(&rvp_config->board_gpios_config[3])
			    << 3;
		board_id |= gpio_pin_get_dt(&rvp_config->board_gpios_config[4])
			    << 4;
		board_id |= gpio_pin_get_dt(&rvp_config->board_gpios_config[5])
			    << 5;

		LOG_DBG("BOARD_ID:0x%x", board_id);
		return board_id;
	}

	if (id_type == BOM_ID && rvp_config->bom_gpios_config) {
		/*
		 * BOM ID [2]   : IOEX[0]
		 * BOM ID [1:0] : IOEX[15:14]
		 */
		bom_id = gpio_pin_get_dt(&rvp_config->bom_gpios_config[0]);
		bom_id |= gpio_pin_get_dt(&rvp_config->bom_gpios_config[1])
			  << 1;
		bom_id |= gpio_pin_get_dt(&rvp_config->bom_gpios_config[2])
			  << 2;
		LOG_DBG("BOM_ID:0x%x", bom_id);
		return bom_id;
	}

	if (id_type == FAB_ID && rvp_config->fab_gpios_config) {
		/*
		 * FAB ID [1:0] : IOEX[2:1] + 1
		 */
		fab_id = gpio_pin_get_dt(&rvp_config->fab_gpios_config[0]);
		fab_id |= gpio_pin_get_dt(&rvp_config->fab_gpios_config[1])
			  << 1;
		fab_id += 1;

		LOG_DBG("FAB_ID:0x%x", fab_id);
		return fab_id;
	}

	return rv;
}

#ifdef CONFIG_AP_PWRSEQ_DRIVER
static void pca95xx_deferred_init_cb(const struct device *dev,
				     const enum ap_pwrseq_state entry,
				     const enum ap_pwrseq_state exit)
{
	const struct device *gpio_port;

	if (exit == AP_POWER_STATE_G3) {
		LOG_DBG("S5 callback triggered, when exiting G3");
		for (int i = 0; i < BOM_GPIOS_COUNT; i++) {
			gpio_port = rvp_config->bom_gpios_config[i].port;
			if (!device_is_ready(gpio_port)) {
				LOG_DBG("Initializing bom_gpios controller port");
				device_init(gpio_port);
			}
		}
		for (int i = 0; i < FAB_GPIOS_COUNT; i++) {
			gpio_port = rvp_config->fab_gpios_config[i].port;
			if (!device_is_ready(gpio_port)) {
				LOG_DBG("Initializing fab_gpios controller port");
				device_init(gpio_port);
			}
		}
		for (int i = 0; i < BOARD_GPIOS_COUNT; i++) {
			gpio_port = rvp_config->board_gpios_config[i].port;
			if (!device_is_ready(gpio_port)) {
				LOG_DBG("Initializing board_gpios controller port");
				device_init(gpio_port);
			}
		}
	}
}

static int rvp_board_id_init(const struct device *dev)
{
	rvp_config = dev->config;

	if (rvp_config->defer_until_s5) {
		static struct ap_pwrseq_state_callback ap_pwrseq_entry_cb;
		const struct device *ap_pwrseq_dev = ap_pwrseq_get_instance();

		LOG_INF("setup_pca95xx_init_callback");
		ap_pwrseq_entry_cb.cb = pca95xx_deferred_init_cb;
		ap_pwrseq_entry_cb.states_bit_mask = BIT(AP_POWER_STATE_S5);

		ap_pwrseq_register_state_entry_callback(ap_pwrseq_dev,
							&ap_pwrseq_entry_cb);
	}
	return 0;
}
#else
static int rvp_board_id_init(const struct device *dev)
{
	rvp_config = dev->config;
	return 0;
}
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

static const struct rvp_board_id_config rvp_board_id_cfg = {
	.defer_until_s5 = DT_NODE_HAS_PROP(DT_DRV_INST(0), defer_until_s5),
#if DT_NODE_HAS_PROP(DT_DRV_INST(0), bom_gpios)
	.bom_gpios_config =
		(const struct gpio_dt_spec[]){
			FOREACH_RVP_GPIOS_ELEM(0, bom_gpios) },
#else
	.bom_gpios_config = NULL,
#endif
#if DT_NODE_HAS_PROP(DT_DRV_INST(0), fab_gpios)
	.fab_gpios_config =
		(const struct gpio_dt_spec[]){
			FOREACH_RVP_GPIOS_ELEM(0, fab_gpios) },
#else
	.fab_gpios_config = NULL,
#endif
	.board_gpios_config =
		(const struct gpio_dt_spec[]){
			FOREACH_RVP_GPIOS_ELEM(0, board_gpios) },
};

DEVICE_DT_INST_DEFINE(0, rvp_board_id_init, NULL, NULL, &rvp_board_id_cfg,
		      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);
