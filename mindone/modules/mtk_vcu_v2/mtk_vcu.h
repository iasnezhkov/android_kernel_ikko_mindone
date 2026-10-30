/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016 MediaTek Inc. */

#ifndef MTK_VCU_H
#define MTK_VCU_H

#include <aee.h>
#include <linux/fdtable.h>
#include <linux/platform_device.h>
#include <media/videobuf2-v4l2.h>

#define CMDQ_SEC_SUPPORT

#if IS_ENABLED(CONFIG_MTK_AEE_FEATURE)
#define vcu_aee_print(string, args...) do {\
	char vcu_name[100];\
	int ret;\
	ret = snprintf(vcu_name, 100, "[VCU] "string, ##args); \
	if (ret > 0)\
		aee_kernel_warning_api(__FILE__, __LINE__, \
			DB_OPT_MMPROFILE_BUFFER | DB_OPT_NE_JBT_TRACES, \
			vcu_name, "[VCU] error:"string, ##args); \
	pr_info("[VCU] error:"string, ##args);  \
	} while (0)
#else
#define vcu_aee_print(string, args...) \
		pr_info("[VCU] error:"string, ##args)

#endif

extern int mtk_vcodec_vcp;

typedef int (*ipi_handler_t)(void *data,
							 unsigned int len,
							 void *priv);

enum ipi_id {
	IPI_VCU_INIT = 0,
	IPI_VDEC_COMMON,
	IPI_VDEC_RESOURCE,
	IPI_VENC_COMMON,
	IPI_MDP,
	IPI_MDP_1,
	IPI_MDP_2,
	IPI_MDP_3,
	IPI_CAMERA,
	IPI_MAX = 20,
};

enum vcu_codec_ipi_type {
	VCU_VDEC = 0,
	VCU_VENC,
	VCU_RESOURCE,
	VCU_CODEC_MAX
};

struct vcu_v4l2_callback_func {
	void (*enc_prepare)(void *ctx_prepare,
		unsigned int core_id, unsigned long *flags);
	void (*enc_unprepare)(void *ctx_unprepare,
		unsigned int core_id, unsigned long *flags);
	void (*enc_pmqos_gce_begin)(void *ctx_begin,
		unsigned int core_id, int job_cnt);
	void (*enc_pmqos_gce_end)(void *ctx_end,
		unsigned int core_id, int job_cnt);
	void (*gce_timeout_dump)(void *ctx);
	void (*vdec_realease_lock)(void *ctx);
	int (*enc_lock)(void *ctx_lock, int core_id, bool sec);
	void (*enc_unlock)(void *ctx_unlock, int core_id);
};

struct vcu_v4l2_func {
	struct platform_device *(*vcu_get_plat_device)(struct platform_device *pdev);
	int (*vcu_load_firmware)(struct platform_device *pdev);
	int (*vcu_compare_version)(struct platform_device *pdev,
				const char *expected_version);
	void (*vcu_get_task)(struct task_struct **task, int reset);
	void (*vcu_put_task)(void);
	int (*vcu_set_v4l2_callback)(struct platform_device *pdev,
		struct vcu_v4l2_callback_func *call_back);
	int (*vcu_get_ctx_ipi_binding_lock)(struct platform_device *pdev,
		struct mutex **mutex, unsigned long type);
	int (*vcu_set_codec_ctx)(struct platform_device *pdev,
			 void *codec_ctx, struct vb2_buffer *src_vb,
			 struct vb2_buffer *dst_vb, unsigned long type);
	int (*vcu_clear_codec_ctx)(struct platform_device *pdev,
			 void *codec_ctx, unsigned long type);
	void *(*vcu_mapping_dm_addr)(struct platform_device *pdev,
				  uintptr_t dtcm_dmem_addr, size_t len);
	int (*vcu_ipi_register)(struct platform_device *pdev,
				 enum ipi_id id, ipi_handler_t handler,
				 const char *name, void *priv);
	int (*vcu_ipi_send)(struct platform_device *pdev,
			 enum ipi_id id, void *buf,
			 unsigned int len, void *priv);
	int (*vcu_set_log)(const char *val);
	int (*vcu_get_log)(char *val, unsigned int val_len);
	void (*vcu_get_gce_lock)(struct platform_device *pdev, unsigned long codec_type);
	void (*vcu_put_gce_lock)(struct platform_device *pdev, unsigned long codec_type);
};
extern struct vcu_v4l2_func vcu_func;

int vcu_ipi_register(struct platform_device *pdev, enum ipi_id id,
	ipi_handler_t handler, const char *name, void *priv);

int vcu_ipi_send(struct platform_device *pdev,
				 enum ipi_id id, void *buf,
				 unsigned int len, void *priv);

struct platform_device *vcu_get_plat_device(struct platform_device *pdev);

int vcu_load_firmware(struct platform_device *pdev);

int vcu_compare_version(struct platform_device *pdev,
						const char *expected_version);

void *vcu_mapping_dm_addr(struct platform_device *pdev,
						  uintptr_t dtcm_dmem_addr, size_t len);

void vcu_get_task(struct task_struct **task, int reset);
void vcu_put_task(void);
int vcu_set_v4l2_callback(struct platform_device *pdev,
	struct vcu_v4l2_callback_func *call_back);
int vcu_get_ctx_ipi_binding_lock(struct platform_device *pdev,
	struct mutex **mutex, unsigned long type);
int vcu_set_codec_ctx(struct platform_device *pdev,
		 void *codec_ctx, struct vb2_buffer *src_vb,
		 struct vb2_buffer *dst_vb, unsigned long type);
int vcu_clear_codec_ctx(struct platform_device *pdev,
		 void *codec_ctx, unsigned long type);
void vcu_get_gce_lock(struct platform_device *pdev, unsigned long codec_type);
void vcu_put_gce_lock(struct platform_device *pdev, unsigned long codec_type);

extern void venc_encode_prepare(void *ctx_prepare,
		unsigned int core_id, unsigned long *flags);
extern void venc_encode_unprepare(void *ctx_prepare,
		unsigned int core_id, unsigned long *flags);
extern int venc_lock(void *ctx_lock, int core_id, bool sec);
extern void venc_unlock(void *ctx_unlock, int core_id);
extern void venc_encode_pmqos_gce_begin(void *ctx_begin,
		unsigned int core_id, int job_cnt);
extern void venc_encode_pmqos_gce_end(void *ctx_end,
		unsigned int core_id, int job_cnt);
extern void vdec_check_release_lock(void *ctx_check);
extern void mtk_vcodec_gce_timeout_dump(void *ctx);
int vcu_set_log(const char *val);
int vcu_get_log(char *val, unsigned int val_len);

#endif
