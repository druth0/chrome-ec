/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola daughter board detection */

#ifndef __CROS_EC_BASEBOARD_USBC_CONFIG_H
#define __CROS_EC_BASEBOARD_USBC_CONFIG_H

#include "gpio.h"
#include "usb_mux.h"

#include <zephyr/sys/util_macro.h>

#ifdef CONFIG_PLATFORM_EC_USB_PD_TCPM_RT1718S
#define GPIO_EN_USB_C1_SINK RT1718S_GPIO1
#define GPIO_EN_USB_C1_SOURCE RT1718S_GPIO2
#define GPIO_EN_USB_C1_FRS RT1718S_GPIO3
#endif

void ppc_interrupt(enum gpio_signal signal);
void ccd_interrupt(enum gpio_signal signal);
void hdmi_hpd_interrupt(enum gpio_signal signal);
void ps185_hdmi_hpd_mux_set(void);
int corsola_is_dp_muxable(int port);
int ps8743_eq_c1_setting(const struct usb_mux *me);

/* USB-A ports */
enum usba_port { USBA_PORT_A0 = 0, USBA_PORT_COUNT };

/* USB-C ports */
#define USBC_PORT_N(i, _) USBC_PORT_C##i = i
enum usbc_port {
	LISTIFY(CONFIG_USB_PD_PORT_MAX_COUNT, USBC_PORT_N, (, )),
	USBC_PORT_COUNT
};

/**
 * Is the port fine to be muxed its DisplayPort lines?
 *
 * Only one port can be muxed to DisplayPort at a time.
 *
 * @param port	Port number of TCPC.
 * @return	1 is fine; 0 is bad as other port is already muxed;
 */
int corsola_is_dp_muxable(int port);

#endif /* __CROS_EC_BASEBOARD_USBC_CONFIG_H */
