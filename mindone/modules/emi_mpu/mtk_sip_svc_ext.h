/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk_sip_svc_ext.h -- MTK_SIP_KERNEL_* SMC-ID extension header.
 *
 * ACK 6.1.175 mainline ships include/linux/soc/mediatek/mtk_sip_svc.h with
 * ONLY MTK_SIP_KERNEL_IOMMU_CONTROL defined (upstream never carried the rest
 * of the vendor SMC ID table). Vendor .c files still #include
 * <linux/soc/mediatek/mtk_sip_svc.h> (angle-bracket) which resolves fine
 * (silently, no "file not found") to that minimal stub, then fail later with
 * "use of undeclared identifier MTK_SIP_KERNEL_*" wherever they reference an
 * ID the stub doesn't have.
 *
 * This file is the missing ID table, merged from two independent public
 * 6.1-era GPL kernel trees for other MT6789/MT6878-family devices that
 * both trace back to the same MTK ATF header; both independently confirm
 * the same ID numbering. Merged as the union of the two sets (the second
 * source adds EMIMPU_READ/WRITE/SET, TMEM, DAPC_MMUP_GET beyond the first).
 *
 * Include this as a companion right after the existing
 * `#include <linux/soc/mediatek/mtk_sip_svc.h>` line in each blocked .c
 * file (quote-form, resolves via the module's own -I$(src)). Every macro is
 * individually #ifndef-guarded so it is harmless regardless of whether a
 * future fuller stub defines some of these already.
 *
 * Real ATF/TF-A firmware handling these SMC calls is already flashed on the
 * device; this header only supplies the kernel-side ID constants needed to
 * compile against it -- it defines no behavior, just numbers.
 */
#ifndef __MTK_SIP_SVC_EXT_H
#define __MTK_SIP_SVC_EXT_H

#ifndef MTK_SIP_SMC_CONVENTION
#ifdef CONFIG_ARM64
#define MTK_SIP_SMC_CONVENTION          ARM_SMCCC_SMC_64
#else
#define MTK_SIP_SMC_CONVENTION          ARM_SMCCC_SMC_32
#endif
#endif

#ifndef MTK_SIP_SMC_CMD
#define MTK_SIP_SMC_CMD(fn_id) \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, MTK_SIP_SMC_CONVENTION, \
			   ARM_SMCCC_OWNER_SIP, fn_id)
#endif

/* VCOREFS */
#ifndef MTK_SIP_VCOREFS_CONTROL
#define MTK_SIP_VCOREFS_CONTROL		MTK_SIP_SMC_CMD(0x506)
#endif

/* EMI MPU */
#ifndef MTK_SIP_EMIMPU_CONTROL
#define MTK_SIP_EMIMPU_CONTROL		MTK_SIP_SMC_CMD(0x50B)
#endif
#ifndef MTK_SIP_KERNEL_EMIMPU_WRITE
#define MTK_SIP_KERNEL_EMIMPU_WRITE	MTK_SIP_SMC_CMD(0x260)
#endif
#ifndef MTK_SIP_KERNEL_EMIMPU_READ
#define MTK_SIP_KERNEL_EMIMPU_READ	MTK_SIP_SMC_CMD(0x261)
#endif
#ifndef MTK_SIP_KERNEL_EMIMPU_SET
#define MTK_SIP_KERNEL_EMIMPU_SET	MTK_SIP_SMC_CMD(0x262)
#endif

/* SDA / GIC dump */
#ifndef MTK_SIP_SDA_CONTROL
#define MTK_SIP_SDA_CONTROL		MTK_SIP_SMC_CMD(0x525)
#endif
#ifndef MTK_SIP_KERNEL_GIC_DUMP
#define MTK_SIP_KERNEL_GIC_DUMP		MTK_SIP_SMC_CMD(0x526)
#endif

/* Debug feature and ATF related */
#ifndef MTK_SIP_KERNEL_WDT
#define MTK_SIP_KERNEL_WDT		MTK_SIP_SMC_CMD(0x200)
#endif
#ifndef MTK_SIP_KERNEL_TIME_SYNC
#define MTK_SIP_KERNEL_TIME_SYNC	MTK_SIP_SMC_CMD(0x202)
#endif
#ifndef MTK_SIP_KERNEL_ATF_DEBUG
#define MTK_SIP_KERNEL_ATF_DEBUG	MTK_SIP_SMC_CMD(0x204)
#endif

/* CCCI debug feature */
#ifndef MTK_SIP_KERNEL_CCCI_GET_INFO
#define MTK_SIP_KERNEL_CCCI_GET_INFO	MTK_SIP_SMC_CMD(0x206)
#endif
#ifndef MTK_SIP_KERNEL_CCCI_CONTROL
#define MTK_SIP_KERNEL_CCCI_CONTROL	MTK_SIP_SMC_CMD(0x505)
#endif

/* DCM Security SMC call */
#ifndef MTK_SIP_KERNEL_DCM
#define MTK_SIP_KERNEL_DCM		MTK_SIP_SMC_CMD(0x230)
#endif

/* AMMS related SMC call */
#ifndef MTK_SIP_KERNEL_AMMS_GET_FREE_ADDR
#define MTK_SIP_KERNEL_AMMS_GET_FREE_ADDR	MTK_SIP_SMC_CMD(0x250)
#endif
#ifndef MTK_SIP_KERNEL_AMMS_GET_FREE_LENGTH
#define MTK_SIP_KERNEL_AMMS_GET_FREE_LENGTH	MTK_SIP_SMC_CMD(0x251)
#endif
#ifndef MTK_SIP_KERNEL_AMMS_GET_PENDING
#define MTK_SIP_KERNEL_AMMS_GET_PENDING	MTK_SIP_SMC_CMD(0x252)
#endif
#ifndef MTK_SIP_KERNEL_AMMS_ACK_PENDING
#define MTK_SIP_KERNEL_AMMS_ACK_PENDING	MTK_SIP_SMC_CMD(0x253)
#endif
#ifndef MTK_SIP_KERNEL_AMMS_GET_SEQ_ID
#define MTK_SIP_KERNEL_AMMS_GET_SEQ_ID		MTK_SIP_SMC_CMD(0x258)
#endif

/* Security related SMC call: DEVMPU */
#ifndef MTK_SIP_KERNEL_DEVMPU_VIO_GET
#define MTK_SIP_KERNEL_DEVMPU_VIO_GET	MTK_SIP_SMC_CMD(0x264)
#endif
#ifndef MTK_SIP_KERNEL_DEVMPU_PERM_GET
#define MTK_SIP_KERNEL_DEVMPU_PERM_GET	MTK_SIP_SMC_CMD(0x265)
#endif
#ifndef MTK_SIP_KERNEL_DEVMPU_VIO_CLR
#define MTK_SIP_KERNEL_DEVMPU_VIO_CLR	MTK_SIP_SMC_CMD(0x268)
#endif

/* TRNG */
#ifndef MTK_SIP_KERNEL_GET_RND
#define MTK_SIP_KERNEL_GET_RND		MTK_SIP_SMC_CMD(0x26A)
#endif

/* DEVAPC */
#ifndef MTK_SIP_KERNEL_DAPC_PERM_GET
#define MTK_SIP_KERNEL_DAPC_PERM_GET	MTK_SIP_SMC_CMD(0x26B)
#endif
#ifndef MTK_SIP_KERNEL_CLR_SRAMROM_VIO
#define MTK_SIP_KERNEL_CLR_SRAMROM_VIO	MTK_SIP_SMC_CMD(0x26C)
#endif
#ifndef MTK_SIP_KERNEL_DAPC_CAM_CONTROL
#define MTK_SIP_KERNEL_DAPC_CAM_CONTROL	MTK_SIP_SMC_CMD(0x52D)
#endif
#ifndef MTK_SIP_KERNEL_DAPC_MMUP_CONTROL
#define MTK_SIP_KERNEL_DAPC_MMUP_CONTROL	MTK_SIP_SMC_CMD(0x52E)
#endif
#ifndef MTK_SIP_KERNEL_DAPC_MMUP_GET
#define MTK_SIP_KERNEL_DAPC_MMUP_GET	MTK_SIP_SMC_CMD(0x531)
#endif
#ifndef MTK_SIP_KERNEL_DAPC_SUBSYS_GET
#define MTK_SIP_KERNEL_DAPC_SUBSYS_GET	MTK_SIP_SMC_CMD(0x531)
#endif

/* AUDIO / CMDQ / APUSYS / IMGSYS / AIE */
#ifndef MTK_SIP_AUDIO_CONTROL
#define MTK_SIP_AUDIO_CONTROL		MTK_SIP_SMC_CMD(0x517)
#endif
#ifndef MTK_SIP_CMDQ_CONTROL
#define MTK_SIP_CMDQ_CONTROL		MTK_SIP_SMC_CMD(0x518)
#endif
#ifndef MTK_SIP_APUSYS_MNOC_CONTROL
#define MTK_SIP_APUSYS_MNOC_CONTROL	MTK_SIP_SMC_CMD(0x519)
#endif
#ifndef MTK_SIP_IMGSYS_CONTROL
#define MTK_SIP_IMGSYS_CONTROL		MTK_SIP_SMC_CMD(0x532)
#endif
#ifndef MTK_SIP_AIE_CONTROL
#define MTK_SIP_AIE_CONTROL		MTK_SIP_SMC_CMD(0x53B)
#endif

/* MTK LPM / SSC */
#ifndef MTK_SIP_MTK_LPM_CONTROL
#define MTK_SIP_MTK_LPM_CONTROL	MTK_SIP_SMC_CMD(0x507)
#endif
#ifndef MTK_SIP_MTK_SSC_CONTROL
#define MTK_SIP_MTK_SSC_CONTROL	MTK_SIP_SMC_CMD(0x529)
#endif

/* MMSRAM / APUSYS / SCP DVFS */
#ifndef MTK_SIP_MMSRAM_CONTROL
#define MTK_SIP_MMSRAM_CONTROL		MTK_SIP_SMC_CMD(0x51D)
#endif
#ifndef MTK_SIP_APUSYS_CONTROL
#define MTK_SIP_APUSYS_CONTROL		MTK_SIP_SMC_CMD(0x51E)
#endif
#ifndef MTK_SIP_SCP_DVFS_CONTROL
#define MTK_SIP_SCP_DVFS_CONTROL	MTK_SIP_SMC_CMD(0x232)
#endif

/* IOMMU related SMC call (note: ACK stub already has MTK_SIP_KERNEL_IOMMU_CONTROL
 * at the same 0x514 -- this is the vendor's alternate name for the same ID) */
#ifndef MTK_IOMMU_SECURE_CONTROL
#define MTK_IOMMU_SECURE_CONTROL	MTK_SIP_SMC_CMD(0x514)
#endif

/* TMEM */
#ifndef MTK_SIP_TMEM_CONTROL
#define MTK_SIP_TMEM_CONTROL		MTK_SIP_SMC_CMD(0x524)
#endif

/* USB / CCU */
#ifndef MTK_SIP_KERNEL_USB_CONTROL
#define MTK_SIP_KERNEL_USB_CONTROL	MTK_SIP_SMC_CMD(0x527)
#endif
#ifndef MTK_SIP_KERNEL_CCU_CONTROL
#define MTK_SIP_KERNEL_CCU_CONTROL	MTK_SIP_SMC_CMD(0x52A)
#endif

/* ADSP */
#ifndef MTK_SIP_KERNEL_ADSP_CONTROL
#define MTK_SIP_KERNEL_ADSP_CONTROL	MTK_SIP_SMC_CMD(0x52B)
#endif

/* SCP / VCP tinysys */
#ifndef MTK_SIP_TINYSYS_SCP_CONTROL
#define MTK_SIP_TINYSYS_SCP_CONTROL	MTK_SIP_SMC_CMD(0x528)
#endif
#ifndef MTK_SIP_TINYSYS_VCP_CONTROL
#define MTK_SIP_TINYSYS_VCP_CONTROL	MTK_SIP_SMC_CMD(0x52C)
#endif

/* PCIe */
#ifndef MTK_SIP_KERNEL_PCIE_CONTROL
#define MTK_SIP_KERNEL_PCIE_CONTROL	MTK_SIP_SMC_CMD(0x52F)
#endif

/* GPUEB */
#ifndef MTK_SIP_KERNEL_GPUEB_CONTROL
#define MTK_SIP_KERNEL_GPUEB_CONTROL	MTK_SIP_SMC_CMD(0x530)
#endif

/* CONNSYS combo (WLAN/BT/GPS/FM) -- IDs only, no connac source exists (hard limit) */
#ifndef MTK_SIP_KERNEL_CONNSYS_CONTROL
#define MTK_SIP_KERNEL_CONNSYS_CONTROL	MTK_SIP_SMC_CMD(0x534)
#endif
#ifndef MTK_SIP_KERNEL_WLAN_CONTROL
#define MTK_SIP_KERNEL_WLAN_CONTROL	MTK_SIP_SMC_CMD(0x535)
#endif
#ifndef MTK_SIP_KERNEL_BT_CONTROL
#define MTK_SIP_KERNEL_BT_CONTROL	MTK_SIP_SMC_CMD(0x536)
#endif
#ifndef MTK_SIP_KERNEL_GPS_CONTROL
#define MTK_SIP_KERNEL_GPS_CONTROL	MTK_SIP_SMC_CMD(0x537)
#endif
#ifndef MTK_SIP_KERNEL_FM_CONTROL
#define MTK_SIP_KERNEL_FM_CONTROL	MTK_SIP_SMC_CMD(0x538)
#endif

#ifndef MTK_SIP_KERNEL_RGU_CONTROL
#define MTK_SIP_KERNEL_RGU_CONTROL	MTK_SIP_SMC_CMD(0x53A)
#endif
#ifndef MTK_SIP_TINYSYS_SSPM_CONTROL
#define MTK_SIP_TINYSYS_SSPM_CONTROL	MTK_SIP_SMC_CMD(0x53C)
#endif
#ifndef MTK_SIP_KERNEL_ISE_CONTROL
#define MTK_SIP_KERNEL_ISE_CONTROL	MTK_SIP_SMC_CMD(0x53D)
#endif
#ifndef MTK_SIP_KERNEL_SLBC_CONTROL
#define MTK_SIP_KERNEL_SLBC_CONTROL	MTK_SIP_SMC_CMD(0x53E)
#endif

#endif /* __MTK_SIP_SVC_EXT_H */
