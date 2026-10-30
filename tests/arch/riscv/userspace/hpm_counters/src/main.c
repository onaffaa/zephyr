/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>
#include <zephyr/arch/riscv/csr.h>

/* Called for the expected fault in test_user_hpmcounter_denied() */
void ztest_post_fatal_error_hook(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	zassert_equal(reason, K_ERR_CPU_EXCEPTION, "Unexpected fatal error reason %u", reason);
	ztest_test_pass();
}

ZTEST_SUITE(riscv_hpm_counters, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(riscv_hpm_counters, test_user_cycle_readable)
{
	unsigned long first;
	unsigned long second;

	zassert_true(k_is_user_context(), "Test must run in user mode");

	first = csr_read(cycle);
	for (volatile uint32_t i = 0U; i < 1000U; i++) {
	}
	second = csr_read(cycle);

	zassert_not_equal(first, second, "cycle counter did not advance");
}

ZTEST_USER(riscv_hpm_counters, test_user_hpmcounter_denied)
{
	if (IS_ENABLED(CONFIG_RISCV_USER_HPM_COUNTER_ACCESS)) {
		ztest_test_skip();
	}

	zassert_true(k_is_user_context(), "Test must run in user mode");

	ztest_set_fault_valid(true);
	(void)csr_read(hpmcounter3);

	zassert_unreachable("hpmcounter3 read from user mode did not fault");
}

ZTEST_USER(riscv_hpm_counters, test_user_hpmcounter_allowed)
{
	if (!IS_ENABLED(CONFIG_RISCV_USER_HPM_COUNTER_ACCESS)) {
		ztest_test_skip();
	}

	zassert_true(k_is_user_context(), "Test must run in user mode");

	/* No event is programmed, so only check that the read does not fault */
	(void)csr_read(hpmcounter3);
#ifndef CONFIG_64BIT
	(void)csr_read(hpmcounter3h);
#endif
}
