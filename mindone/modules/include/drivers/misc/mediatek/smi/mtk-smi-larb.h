/* SPDX-License-Identifier: GPL-2.0 */
/* Entry points exported by the SMI driver (mtk_smi). */
#ifndef __MTK_SMI_LARB_H__
#define __MTK_SMI_LARB_H__

#include <linux/types.h>

struct device;
struct notifier_block;

int mtk_smi_larb_get(struct device *larbdev);
void mtk_smi_larb_put(struct device *larbdev);
int mtk_smi_larb_ultra_dis(struct device *larbdev, bool is_dis);
void mtk_smi_larb_clamp(struct device *larbdev, bool on);
void mtk_smi_add_device_link(struct device *dev, struct device *larbdev);
void mtk_smi_common_bw_set(struct device *dev, const u32 port, const u32 val);
void mtk_smi_larb_bw_set(struct device *dev, const u32 port, const u32 val);
void mtk_smi_check_comm_ref_cnt(struct device *dev);
void mtk_smi_check_larb_ref_cnt(struct device *dev);
void mtk_smi_init_power_off(void);
void mtk_smi_dump_last_pd(const char *user);
s32 smi_sysram_enable(struct device *larbdev, const u32 master_id, const bool enable, const char *user);
int mtk_smi_driver_register_notifier(struct notifier_block *nb);
int mtk_smi_driver_unregister_notifier(struct notifier_block *nb);

#endif /* __MTK_SMI_LARB_H__ */
