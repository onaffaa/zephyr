/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT riscv_aplic

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/interrupt_controller/riscv_aplic.h>
#ifdef CONFIG_RISCV_APLIC_DIRECT
#include <zephyr/drivers/interrupt_controller/riscv_aplic_direct.h>
#endif
#include <zephyr/devicetree/interrupt_controller.h>
#include <zephyr/logging/log.h>
#include <zephyr/sw_isr_table.h>
#include <zephyr/sys/util.h>

#include "intc_riscv_aplic_priv.h"

LOG_MODULE_REGISTER(intc_riscv_aplic, CONFIG_LOG_DEFAULT_LEVEL);

void riscv_aplic_domain_enable(const struct device *dev, bool enable)
{
	const struct aplic_cfg *cfg = dev->config;
	struct aplic_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	uint32_t v = rd32(cfg->base, APLIC_DOMAINCFG);

	if (enable) {
		/* Enable domain; keep delivery mode as configured by platform.
		 * Do not modify DM bit - respect hardware/props configuration.
		 */
		v |= APLIC_DOMAINCFG_IE;
	} else {
		v &= ~APLIC_DOMAINCFG_IE;
	}
	wr32(cfg->base, APLIC_DOMAINCFG, v);

	k_spin_unlock(&data->lock, key);
}

void riscv_aplic_config_src(const struct device *dev, unsigned int src, unsigned int sm)
{
	const struct aplic_cfg *cfg = dev->config;

	__ASSERT_NO_MSG(IN_RANGE(src, 1, cfg->num_sources));

	/* Validate sm parameter - 0x2 and 0x3 are reserved (RISC-V AIA spec, section 4.5.2) */
	__ASSERT_NO_MSG((sm != 0x2 && sm != 0x3 && sm <= APLIC_SM_LEVEL_LOW));

	/* D = 0: the remaining bits hold only the source mode */
	wr32(cfg->base, aplic_sourcecfg_off(src), sm & APLIC_SOURCECFG_SM_MASK);
}

void riscv_aplic_delegate_src(const struct device *dev, unsigned int src, unsigned int child)
{
	const struct aplic_cfg *cfg = dev->config;

	__ASSERT_NO_MSG(IN_RANGE(src, 1, cfg->num_sources));
	__ASSERT_NO_MSG(child <= APLIC_SOURCECFG_CI_MASK);

	wr32(cfg->base, aplic_sourcecfg_off(src), APLIC_SOURCECFG_D | child);
}

void aplic_apply_delegation(const struct device *dev)
{
	const struct aplic_cfg *cfg = dev->config;

	for (size_t i = 0; i < cfg->num_delegations; i++) {
		const struct aplic_delegation *d = &cfg->delegations[i];

		for (unsigned int src = d->first; src <= d->last; src++) {
			riscv_aplic_delegate_src(dev, src, d->child);
		}
	}
}

void riscv_aplic_enable_src(const struct device *dev, unsigned int src, bool enable)
{
	const struct aplic_cfg *cfg = dev->config;

	__ASSERT_NO_MSG(IN_RANGE(src, 1, cfg->num_sources));

	wr32(cfg->base, enable ? APLIC_SETIENUM : APLIC_CLRIENUM, src);
}

#ifdef CONFIG_RISCV_APLIC_MSI
#define aplic_init aplic_msi_init
#else
#define aplic_init aplic_direct_init
#endif

uint32_t riscv_aplic_get_num_sources(const struct device *dev)
{
	const struct aplic_cfg *cfg = dev->config;

	return cfg->num_sources;
}

/* Child index of a delegation target: its position in the parent's riscv,children */
#define APLIC_CHILD_IDX_IF_SAME(node_id, prop, idx, target)                                        \
	(DT_SAME_NODE(DT_PHANDLE_BY_IDX(node_id, prop, idx), target) ? (idx) : 0)

#define APLIC_CHILD_IDX(node_id, target)                                                           \
	(DT_FOREACH_PROP_ELEM_SEP_VARGS(node_id, riscv_children, APLIC_CHILD_IDX_IF_SAME, (+),     \
					target))

#define APLIC_IS_CHILD(node_id, prop, idx, target)                                                 \
	DT_SAME_NODE(DT_PHANDLE_BY_IDX(node_id, prop, idx), target)

#define APLIC_DELEGATION_CHECK(node_id, prop, idx)                                                 \
	BUILD_ASSERT((DT_FOREACH_PROP_ELEM_SEP_VARGS(node_id, riscv_children, APLIC_IS_CHILD, (+), \
						     DT_PHANDLE_BY_IDX(node_id, prop, idx))) == 1, \
		     "riscv,delegation target must be listed once in riscv,children");             \
	BUILD_ASSERT(IN_RANGE(DT_PHA_BY_IDX(node_id, prop, idx, first_source), 1,                  \
			      DT_PHA_BY_IDX(node_id, prop, idx, last_source)) &&                   \
			     DT_PHA_BY_IDX(node_id, prop, idx, last_source) <=                     \
				     DT_PROP(node_id, riscv_num_sources),                          \
		     "riscv,delegation source range out of bounds");

#define APLIC_DELEGATION_ENTRY(node_id, prop, idx)                                                 \
	{                                                                                          \
		.child = APLIC_CHILD_IDX(node_id, DT_PHANDLE_BY_IDX(node_id, prop, idx)),          \
		.first = DT_PHA_BY_IDX(node_id, prop, idx, first_source),                          \
		.last = DT_PHA_BY_IDX(node_id, prop, idx, last_source),                            \
	},

#define APLIC_DELEGATIONS_DEFINE(inst)                                                             \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(inst, riscv_delegation),                                  \
		   (DT_INST_FOREACH_PROP_ELEM(inst, riscv_delegation, APLIC_DELEGATION_CHECK)      \
		    static const struct aplic_delegation aplic_delegations_##inst[] = {           \
			    DT_INST_FOREACH_PROP_ELEM(inst, riscv_delegation,                      \
						      APLIC_DELEGATION_ENTRY)};))

#define APLIC_DELEGATIONS_INIT(inst)                                                               \
	.delegations = COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, riscv_delegation),                  \
				   (aplic_delegations_##inst), (NULL)),                            \
	.num_delegations = DT_INST_PROP_LEN_OR(inst, riscv_delegation, 0),

#ifdef CONFIG_RISCV_APLIC_DIRECT
/* With direct delivery mode enabled the APLIC must register an IRQ handler
 * on initialization
 */
#define APLIC_INTC_IRQ_FUNC_DECLARE(inst) static void aplic_irq_config_func_##inst(void)
#define APLIC_INTC_IRQ_FUNC_DEFINE(inst)                                                           \
	static void aplic_irq_config_func_##inst(void)                                             \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(inst), 0, aplic_irq_handler, DEVICE_DT_INST_GET(inst),    \
			    0);                                                                    \
		irq_enable(DT_INST_IRQN(inst));                                                    \
	}

#define APLIC_INIT(inst)                                                                           \
	BUILD_ASSERT(DT_INST_NODE_HAS_PROP(inst, riscv_max_priority),                              \
		     "max_prio is required for APLIC direct-delivery mode but is not present");    \
	IRQ_PARENT_ENTRY_DEFINE(aplic##inst, DEVICE_DT_INST_GET(inst), DT_INST_IRQN(inst),         \
				INTC_INST_ISR_TBL_OFFSET(inst),                                    \
				DT_INST_INTC_GET_AGGREGATOR_LEVEL(inst));                          \
	APLIC_INTC_IRQ_FUNC_DECLARE(inst);                                                         \
	static struct aplic_data aplic_data_##inst;                                                \
	APLIC_DELEGATIONS_DEFINE(inst)                                                             \
	static const struct aplic_cfg aplic_cfg_##inst = {                                         \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.num_sources = DT_INST_PROP(inst, riscv_num_sources),                              \
		APLIC_DELEGATIONS_INIT(inst)                                                       \
		.max_prio = DT_INST_PROP(inst, riscv_max_priority),                                \
		.irq_config_func = aplic_irq_config_func_##inst,                                   \
		.isr_table = &_sw_isr_table[INTC_INST_ISR_TBL_OFFSET(inst)],                       \
	};                                                                                         \
	APLIC_INTC_IRQ_FUNC_DEFINE(inst)                                                           \
	DEVICE_DT_INST_DEFINE(inst, aplic_init, NULL, &aplic_data_##inst, &aplic_cfg_##inst,       \
			      PRE_KERNEL_1, CONFIG_INTC_INIT_PRIORITY, NULL);
#else
/* APLIC_MSI interrupts are serviced by separate IMSIC driver */
#define APLIC_INIT(inst)                                                                           \
	static struct aplic_data aplic_data_##inst;                                                \
	APLIC_DELEGATIONS_DEFINE(inst)                                                             \
	static const struct aplic_cfg aplic_cfg_##inst = {                                         \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.num_sources = DT_INST_PROP(inst, riscv_num_sources),                              \
		APLIC_DELEGATIONS_INIT(inst)                                                       \
		IF_ENABLED(CONFIG_RISCV_APLIC_MSI,                                                 \
			(.imsic_addr = DT_REG_ADDR(DT_INST_PHANDLE(inst, msi_parent)),)) };        \
	DEVICE_DT_INST_DEFINE(inst, aplic_init, NULL, &aplic_data_##inst, &aplic_cfg_##inst,       \
			      PRE_KERNEL_1, CONFIG_INTC_INIT_PRIORITY, NULL);
#endif

DT_INST_FOREACH_STATUS_OKAY(APLIC_INIT)
