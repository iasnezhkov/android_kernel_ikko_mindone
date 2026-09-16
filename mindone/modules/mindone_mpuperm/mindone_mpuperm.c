// SPDX-License-Identifier: GPL-2.0
/* MINDONE (F3198): experiment -- add an EMI-MPU region with NO_PROTECTION for all domains
 * over the page start..end (default 0x40000000..0x40000FFF), to test whether this is the
 * culprit (MD reads 0x40000044, F3148). Tries region ids top-down until ATF accepts one.
 * Unloading the module removes the region. */
#include <linux/module.h>
#include <linux/slab.h>
#include "emi.h"

static unsigned long long start = 0x40000000ULL, end = 0x40000FFFULL;
static int rg_first = 30, rg_last = 8;
module_param(start, ullong, 0444); module_param(end, ullong, 0444);
module_param(rg_first, int, 0444); module_param(rg_last, int, 0444);
static struct emimpu_region_t *rg; static int rg_used = -1;

static int __init mpuperm_init(void)
{
	int id, d, ret;
	rg = kzalloc(sizeof(*rg), GFP_KERNEL); if (!rg) return -ENOMEM;
	for (id = rg_first; id >= rg_last; id--) {
		memset(rg, 0, sizeof(*rg));
		ret = mtk_emimpu_init_region(rg, id);
		if (ret) { pr_notice("MINDONE-MPUPERM: init_region(%d) ret=%d\n", id, ret); continue; }
		mtk_emimpu_set_addr(rg, start, end);
		for (d = 0; d < 16; d++) mtk_emimpu_set_apc(rg, d, MTK_EMIMPU_NO_PROTECTION);
		ret = mtk_emimpu_set_protection(rg);
		pr_notice("MINDONE-MPUPERM: region %d [%#llx..%#llx] all-domains NO_PROTECTION: set_protection ret=%d\n", id, start, end, ret);
		if (ret == 0) { rg_used = id; return 0; }
		mtk_emimpu_free_region(rg);
	}
	kfree(rg); rg = NULL; return -EIO;
}
static void __exit mpuperm_exit(void)
{
	if (rg) { int r = mtk_emimpu_clear_protection(rg); pr_notice("MINDONE-MPUPERM: clear region %d ret=%d\n", rg_used, r); mtk_emimpu_free_region(rg); kfree(rg); }
}
module_init(mpuperm_init); module_exit(mpuperm_exit);
MODULE_LICENSE("GPL");
