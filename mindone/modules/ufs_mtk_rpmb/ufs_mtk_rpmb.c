// SPDX-License-Identifier: GPL-2.0
/*
 * ufs_mtk_rpmb -- MINDONE-RPMB-GLUE (F3135): UFS RPMB glue for kernel 6.1. Stock
 * 5.10 keeps this inside ufs-mediatek.c; on 6.1 ufs-mediatek is built-in with no
 * RPMB code, and the rpmb core is an out-of-tree module (rpmb.ko), so this glue
 * lives in a module too: finds the UFS host by DT compatible, looks up the RPMB
 * W-LUN scsi_device, registers an rpmb_dev with stock's ops, and exports
 * ufs_mtk_rpmb_get_raw_dev() for rpmb_mtk.ko.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <mindone/compat.h>
#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_proto.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_common.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dbg.h>
#include <ufs/ufshcd.h>
#include <ufs/ufs.h>
#include "rpmb.h"

#define SEC_PROTOCOL_UFS		0xEC
#define SEC_SPECIFIC_UFS_RPMB		0x0001
#define SEC_PROTOCOL_CMD_SIZE		12
#define SEC_PROTOCOL_RETRIES		3
#define SEC_PROTOCOL_RETRIES_ON_RESET	10
#define SEC_PROTOCOL_TIMEOUT		msecs_to_jiffies(30000)
#define UFS_RPMB_SCSI_LUN ((UFS_UPIU_RPMB_WLUN & ~UFS_UPIU_WLUN_ID) | SCSI_W_LUN_BASE)

static char *compat = "mediatek,mt8183-ufshci";
module_param(compat, charp, 0444);
MODULE_PARM_DESC(compat, "DT compatible of the UFS host (default mediatek,mt8183-ufshci)");
/* RPMB region selector.
 *
 * In the UFS SECURITY PROTOCOL command the "protocol specific" field is
 * <region><operation>: 0x0001 is region 0, 0x0101 region 1, and so on. Every
 * region has its OWN authentication key, so addressing the wrong one gives
 * exactly the symptom seen here - a transaction that succeeds at the transport
 * level (result 0, meaning the key IS programmed) while the TEE rejects the
 * response with "MAC mismatched", because the HMAC came from a different key
 * than the one it holds.
 *
 * Region 0 is the default and is what nearly every device uses. This parameter
 * exists so the alternative can be tested on a live device without rebuilding
 * and reflashing a module:
 *
 *     echo 1 > /sys/module/ufs_mtk_rpmb/parameters/region
 *
 * Changing it is harmless: a region the device does not implement answers with
 * a SCSI error, not with a write to the wrong place.
 */
static unsigned int region;
module_param(region, uint, 0644);
MODULE_PARM_DESC(region, "RPMB region for the SECURITY PROTOCOL command (0-3, default 0)");

static unsigned int rw_size = 1;
module_param(rw_size, uint, 0444);
MODULE_PARM_DESC(rw_size, "RPMB reliable write count (GEOMETRY bRPMB_ReadWriteSize; 1 = single frame)");
/*
 * MINDONE-RPMB-CAPACITY (P80/P45, 12.09, B15): was 0 ("unknown") - rpmb_descr.capacity never
 * reached any real value, so our rpmb_dev under-reported its own geometry regardless of the
 * teed "get dev info" outcome. B15's own TEE boot log gives the real number directly: "RPMB
 * SIZE: 0x1000000" (16 MiB) = 128 * 128 KiB, so 128 is a measured default, not a guess. Still
 * overridable via the module param for a different unit's geometry.
 */
static unsigned int capacity = 128;
module_param(capacity, uint, 0444);
MODULE_PARM_DESC(capacity, "RPMB capacity in 128 KiB units (GEOMETRY bRPMB_Size; default 128 = 16 MiB, per B15 TEE bootarg RPMB SIZE: 0x1000000)");

static struct ufs_hba *g_hba;
static struct scsi_device *g_sdev;
static struct rpmb_dev *rawdev_ufs_rpmb;
static DEFINE_MUTEX(rpmb_lock);

static int ufs_mtk_rpmb_sec(struct scsi_device *sdev, u8 *buf, unsigned int len, bool out)
{
	struct scsi_sense_hdr sshdr = {0};
	const struct scsi_exec_args args = { .sshdr = &sshdr };
	int reset_retries = SEC_PROTOCOL_RETRIES_ON_RESET;
	u8 cmd[SEC_PROTOCOL_CMD_SIZE];
	int ret;

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = out ? SECURITY_PROTOCOL_OUT : SECURITY_PROTOCOL_IN;
	cmd[1] = SEC_PROTOCOL_UFS;
	put_unaligned_be16(SEC_SPECIFIC_UFS_RPMB | ((region & 0x3) << 8), cmd + 2);
	cmd[4] = 0;				/* inc_512 = 0 */
	put_unaligned_be32(len, cmd + 6);	/* transfer / allocation length */
retry:
	ret = scsi_execute_cmd(sdev, cmd, out ? REQ_OP_DRV_OUT : REQ_OP_DRV_IN, buf, len,
			       SEC_PROTOCOL_TIMEOUT, SEC_PROTOCOL_RETRIES, &args);
	if (ret && scsi_sense_valid(&sshdr) && sshdr.sense_key == UNIT_ATTENTION)
		if (--reset_retries > 0)
			goto retry;
	if (ret) {
		dev_err(&sdev->sdev_gendev, "rpmb: security %s failed with err 0x%x\n",
			out ? "out" : "in", ret);
		if (scsi_sense_valid(&sshdr))
			scsi_print_sense_hdr(sdev, out ? "rpmb: security out" : "rpmb: security in", &sshdr);
	}
	return ret;
}

/* mainline ufshcd_rpmb_route_frames(): request via SECURITY PROTOCOL OUT, answer via SECURITY PROTOCOL IN */
static int ufs_mtk_rpmb_route_frames(struct device *dev, u8 *req, unsigned int req_len,
				     u8 *resp, unsigned int resp_len)
{
	struct scsi_device *sdev = g_sdev;
	int ret;

	if (!req || !resp || !req_len || !resp_len)
		return -EINVAL;
	if (!sdev || !scsi_device_online(sdev))
		return -ENODEV;
	ret = scsi_device_get(sdev);
	if (ret)
		return ret;
	mutex_lock(&rpmb_lock);
	scsi_autopm_get_device(sdev);		/* device resumed before RPMB access */
	ret = ufs_mtk_rpmb_sec(sdev, req, req_len, true);
	/*
	 * MINDONE-RPMB-WRITE (12.09, B16c): for JEDEC write requests (0x0001 program
	 * key, 0x0003 write data) the response is read ONLY after a separate "result read"
	 * request (0x0005) -- as in mainline's ufshcd_rpmb_route_frames(). Without it,
	 * SECURITY PROTOCOL IN returns an empty/stale frame. For reads (0x0002/0x0004) the
	 * response comes back immediately.
	 */
	if (!ret && req_len >= 512) {
		u16 type = get_unaligned_be16(req + 510);

		if (type == 0x0001 || type == 0x0003) {
			u8 *rr = kzalloc(512, GFP_KERNEL);

			if (!rr) {
				ret = -ENOMEM;
			} else {
				put_unaligned_be16(0x0005, rr + 510);
				ret = ufs_mtk_rpmb_sec(sdev, rr, 512, true);
				kfree(rr);
			}
		}
	}
	if (!ret)
		ret = ufs_mtk_rpmb_sec(sdev, resp, resp_len, false);
	scsi_autopm_put_device(sdev);
	mutex_unlock(&rpmb_lock);
	scsi_device_put(sdev);
	return ret;
}

/*
 * MINDONE-RPMB-XFER (/12.09): direct frame transport for rpmb_mtk.ko (teed
 * ioctl 10/11/12). Its local "linux/rpmb.h" is the older vendor API (rpmb_cmd_req is a
 * stub when CONFIG_RPMB is absent, F018/B16c: "end" after 27 us, response frame =
 * request frame, TEE: "Unexpected msg_type 0x0002 != 0x0200"), so we carry frames
 * through this export instead, bypassing the incompatible rpmb_dev structures.
 */
int ufs_mtk_rpmb_xfer(u8 *req, unsigned int req_len, u8 *resp, unsigned int resp_len)
{
	return ufs_mtk_rpmb_route_frames(NULL, req, req_len, resp, resp_len);
}
EXPORT_SYMBOL_GPL(ufs_mtk_rpmb_xfer);

static struct rpmb_descr ufs_mtk_rpmb_descr = {
	.type = RPMB_TYPE_UFS,
	.route_frames = ufs_mtk_rpmb_route_frames,
};

struct rpmb_dev *ufs_mtk_rpmb_get_raw_dev(void)
{
	return rawdev_ufs_rpmb;
}
EXPORT_SYMBOL_GPL(ufs_mtk_rpmb_get_raw_dev);

/*
 * MINDONE-RPMB-GEOMETRY (P80/P45, 12.09, B16): rpmb_mtk.ko's local "linux/rpmb.h" mirrors the
 * the older vendor rpmb_ops/rpmb_cmd_req API, not the real upstream rpmb_descr this module registers
 * against (rpmb.ko/kernel612-common) - the two "struct rpmb_dev" layouts do not agree, so
 * rpmb_mtk.c must not dereference ufs_mtk_rpmb_get_raw_dev()'s pointer's internals directly.
 * This plain-integer accessor sidesteps that: it hands back exactly the two values teed's
 * "get dev info" probe wants (JEDEC RPMB_SIZE_MULT in 128 KiB units, reliable write count),
 * straight from this module's own descriptor.
 */
void ufs_mtk_rpmb_get_geometry(u16 *out_capacity, u16 *out_reliable_wr_count)
{
	if (out_capacity)
		*out_capacity = ufs_mtk_rpmb_descr.capacity;
	if (out_reliable_wr_count)
		*out_reliable_wr_count = ufs_mtk_rpmb_descr.reliable_wr_count;
}
EXPORT_SYMBOL_GPL(ufs_mtk_rpmb_get_geometry);

static int __init ufs_mtk_rpmb_init(void)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct rpmb_dev *rdev;

	np = of_find_compatible_node(NULL, NULL, compat);
	if (!np) {
		pr_err("ufs_mtk_rpmb: no DT node with compatible %s\n", compat);
		return -ENODEV;
	}
	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return -ENODEV;
	g_hba = platform_get_drvdata(pdev);
	put_device(&pdev->dev);
	if (!g_hba || !g_hba->host) {
		pr_err("ufs_mtk_rpmb: UFS host not probed yet\n");
		return -EPROBE_DEFER;
	}
	g_sdev = scsi_device_lookup(g_hba->host, 0, 0, UFS_RPMB_SCSI_LUN);	/* takes a reference */
	if (!g_sdev) {
		pr_err("ufs_mtk_rpmb: RPMB W-LUN 0x%x not found\n", UFS_RPMB_SCSI_LUN);
		return -ENODEV;
	}
	ufs_mtk_rpmb_descr.reliable_wr_count = rw_size;
	ufs_mtk_rpmb_descr.capacity = capacity;
	rdev = rpmb_dev_register(g_hba->dev, &ufs_mtk_rpmb_descr);
	if (IS_ERR(rdev)) {
		pr_err("ufs_mtk_rpmb: rpmb_dev_register failed %ld\n", PTR_ERR(rdev));
		scsi_device_put(g_sdev);
		g_sdev = NULL;
		return PTR_ERR(rdev);
	}
	rawdev_ufs_rpmb = rdev;
	pr_info("ufs_mtk_rpmb: registered (rpmb lun %s, rw_size %u)\n", dev_name(&g_sdev->sdev_gendev), rw_size);
	return 0;
}

static void __exit ufs_mtk_rpmb_exit(void)
{
	if (rawdev_ufs_rpmb)
		rpmb_dev_unregister(rawdev_ufs_rpmb);
	if (g_sdev)
		scsi_device_put(g_sdev);
}
module_init(ufs_mtk_rpmb_init);
module_exit(ufs_mtk_rpmb_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MINDONE UFS RPMB glue for MediaTek (kernel 6.1)");
