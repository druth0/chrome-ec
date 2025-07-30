/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/fingerprint/fingerprint_elan80sg_private.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <fingerprint/fingerprint_alg.h>

LOG_MODULE_REGISTER(elan_elan80sg_alg, LOG_LEVEL_INF);

/*
 * This file contains implementation of the algorithm API of the ELAN elan80sg
 * library (algorithm).
 *
 */

static int
elan_elan80sg_algorithm_init(const struct fingerprint_algorithm *const alg)
{
	return 0;
}

static int
elan_elan80sg_algorithm_exit(const struct fingerprint_algorithm *const alg)
{
	return 0;
}

static int
elan_elan80sg_enroll_start(const struct fingerprint_algorithm *const alg)
{
	if (!IS_ENABLED(CONFIG_HAVE_ELAN80SG_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	return elan_enrollment_begin();
}

static int
elan_elan80sg_enroll_step(const struct fingerprint_algorithm *const alg,
			  const uint8_t *const image, int *completion)
{
	if (!IS_ENABLED(CONFIG_HAVE_ELAN80SG_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	return elan_enroll((uint8_t *)image, completion);
}

static int
elan_elan80sg_enroll_finish(const struct fingerprint_algorithm *const alg,
			    void *templ)
{
	if (!IS_ENABLED(CONFIG_HAVE_ELAN80SG_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	return elan_enrollment_finish(templ);
}

static int elan_elan80sg_match(const struct fingerprint_algorithm *const alg,
			       void *templ, uint32_t templ_count,
			       const uint8_t *const image, int32_t *match_index,
			       uint32_t *update_bitmap)
{
	if (!IS_ENABLED(CONFIG_HAVE_ELAN80SG_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	int res = elan_match(templ, templ_count, (uint8_t *)image, match_index,
			     update_bitmap);
	if (res == FP_MATCH_RESULT_MATCH)
		res = elan_template_update(templ, *match_index);

	return res;
}

const struct fingerprint_algorithm_api elan_elan80sg_api = {
	.init = elan_elan80sg_algorithm_init,
	.exit = elan_elan80sg_algorithm_exit,
	.enroll_start = elan_elan80sg_enroll_start,
	.enroll_step = elan_elan80sg_enroll_step,
	.enroll_finish = elan_elan80sg_enroll_finish,
	.match = elan_elan80sg_match,
};

FINGERPRINT_ALGORITHM_DEFINE(elan_elan80sg_algorithm, NULL, &elan_elan80sg_api);
