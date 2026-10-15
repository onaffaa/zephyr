/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * APLIC domain delegation tests, enabled when the root domain has a riscv,delegation property
 * (see delegation.overlay).
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/interrupt_controller/riscv_aia.h>
#include <zephyr/drivers/interrupt_controller/riscv_aplic.h>
#include <zephyr/irq_multilevel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/ztest.h>

#define ROOT_NODE  DT_NODELABEL(aplic)
#define ROOT_BASE  DT_REG_ADDR(ROOT_NODE)
#define CHILD_NODE DT_NODELABEL(aplic_s)
#define CHILD_BASE DT_REG_ADDR(CHILD_NODE)

#if DT_NODE_HAS_PROP(ROOT_NODE, riscv_delegation)

#define DELEG_FIRST DT_PHA_BY_IDX(ROOT_NODE, riscv_delegation, 0, first_source)
#define DELEG_LAST  DT_PHA_BY_IDX(ROOT_NODE, riscv_delegation, 0, last_source)

static uint32_t root_sourcecfg(uint32_t src)
{
	return sys_read32(ROOT_BASE + aplic_sourcecfg_off(src));
}

ZTEST(aplic_delegation, test_sourcecfg_fields)
{
	zassert_equal(BIT(10), APLIC_SOURCECFG_D, "D is sourcecfg bit 10");
	zassert_equal(0x3FFU, APLIC_SOURCECFG_CI_MASK, "child index is sourcecfg bits 9:0");
}

ZTEST(aplic_delegation, test_root_sources_delegated)
{
	for (uint32_t src = DELEG_FIRST; src <= DELEG_LAST; src++) {
		/* aplic_s is child index 0 of aplic */
		zassert_equal(APLIC_SOURCECFG_D | 0U, root_sourcecfg(src),
			      "root sourcecfg[%u] = 0x%x", src, root_sourcecfg(src));
	}

	zassert_equal(APLIC_SM_INACTIVE, root_sourcecfg(DELEG_FIRST - 1U),
		      "source below the range is not delegated");
}

ZTEST(aplic_delegation, test_config_src_clears_delegation)
{
	const struct device *root = DEVICE_DT_GET(ROOT_NODE);
	const uint32_t src = DELEG_FIRST - 1U;

	riscv_aplic_delegate_src(root, src, 0);
	zassert_equal(APLIC_SOURCECFG_D, root_sourcecfg(src));

	riscv_aplic_config_src(root, src, APLIC_SM_EDGE_RISE);
	zassert_equal(APLIC_SM_EDGE_RISE, root_sourcecfg(src), "D and child index not cleared");

	riscv_aplic_config_src(root, src, APLIC_SM_INACTIVE);
}

#if DT_NODE_HAS_STATUS_OKAY(CHILD_NODE) && defined(CONFIG_RISCV_APLIC_DIRECT)
ZTEST(aplic_delegation, test_child_domain_owns_delegated_source)
{
	const uint32_t irq = irq_to_level_2(DELEG_FIRST) | DT_IRQN(CHILD_NODE);

	riscv_aia_config_source(irq, APLIC_SM_EDGE_RISE);

	zassert_equal(APLIC_SM_EDGE_RISE, sys_read32(CHILD_BASE + aplic_sourcecfg_off(DELEG_FIRST)),
		      "source mode not written to the child domain");
	zassert_equal(APLIC_SOURCECFG_D, root_sourcecfg(DELEG_FIRST),
		      "root domain sourcecfg modified");

	riscv_aia_config_source(irq, APLIC_SM_INACTIVE);
}
#endif

ZTEST_SUITE(aplic_delegation, NULL, NULL, NULL, NULL, NULL);

#endif /* DT_NODE_HAS_PROP(ROOT_NODE, riscv_delegation) */
