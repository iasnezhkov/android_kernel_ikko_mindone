/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MALI_KBASE_HW_ACCESS_H_
#define _MALI_KBASE_HW_ACCESS_H_

#include <linux/version_compat_defs.h>

#define KBASE_REGMAP_PERM_READ (1U << 0)
#define KBASE_REGMAP_PERM_WRITE (1U << 1)
#define KBASE_REGMAP_WIDTH_32_BIT (1U << 2)
#define KBASE_REGMAP_WIDTH_64_BIT (1U << 3)

#define KBASE_REG_READ(kbdev, reg_enum)                                             \
	(kbase_reg_is_size64(kbdev, reg_enum) ? kbase_reg_read64(kbdev, reg_enum) : \
						      kbase_reg_read32(kbdev, reg_enum))

#define KBASE_REG_WRITE(kbdev, reg_enum, value)                                             \
	(kbase_reg_is_size64(kbdev, reg_enum) ? kbase_reg_write64(kbdev, reg_enum, value) : \
						      kbase_reg_write32(kbdev, reg_enum, value))

u32 kbase_reg_read32(struct kbase_device *kbdev, u32 reg_enum);

u64 kbase_reg_read64(struct kbase_device *kbdev, u32 reg_enum);

u64 kbase_reg_read64_coherent(struct kbase_device *kbdev, u32 reg_enum);

void kbase_reg_write32(struct kbase_device *kbdev, u32 reg_enum, u32 value);

void kbase_reg_write64(struct kbase_device *kbdev, u32 reg_enum, u64 value);

bool kbase_reg_is_size64(struct kbase_device *kbdev, u32 reg_enum);

bool kbase_reg_is_size32(struct kbase_device *kbdev, u32 reg_enum);

bool kbase_reg_is_valid(struct kbase_device *kbdev, u32 reg_enum);

bool kbase_reg_is_accessible(struct kbase_device *kbdev, u32 reg_enum, u32 flags);

bool kbase_reg_is_powered_access_allowed(struct kbase_device *kbdev, u32 reg_enum);

bool kbase_reg_is_init(struct kbase_device *kbdev);

int kbase_reg_get_offset(struct kbase_device *kbdev, u32 reg_enum, u32 *offset);

int kbase_reg_get_enum(struct kbase_device *kbdev, u32 offset, u32 *reg_enum);

#ifdef CONFIG_MALI_DEBUG

const char *kbase_reg_get_enum_string(u32 reg_enum);
#endif

u64 kbase_reg_get_gpu_id(struct kbase_device *kbdev);

int kbase_regmap_init(struct kbase_device *kbdev);

u32 kbase_regmap_backend_init(struct kbase_device *kbdev);

void kbase_regmap_term(struct kbase_device *kbdev);

#define kbase_reg_poll32_timeout(kbdev, reg_enum, val, cond, delay_us, timeout_us,       \
				 delay_before_read)                                      \
	mali_read_poll_timeout_atomic(kbase_reg_read32, val, cond, delay_us, timeout_us, \
				      delay_before_read, kbdev, reg_enum)

#define kbase_reg_poll64_timeout(kbdev, reg_enum, val, cond, delay_us, timeout_us,       \
				 delay_before_read)                                      \
	mali_read_poll_timeout_atomic(kbase_reg_read64, val, cond, delay_us, timeout_us, \
				      delay_before_read, kbdev, reg_enum)
#endif
