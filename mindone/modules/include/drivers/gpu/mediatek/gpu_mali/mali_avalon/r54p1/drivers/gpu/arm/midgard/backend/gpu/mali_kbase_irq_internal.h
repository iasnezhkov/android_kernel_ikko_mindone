/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_IRQ_INTERNAL_H_
#define _KBASE_IRQ_INTERNAL_H_

#define JOB_IRQ_TAG 0
#define MMU_IRQ_TAG 1
#define GPU_IRQ_TAG 2

int kbase_install_interrupts(struct kbase_device *kbdev);

void kbase_release_interrupts(struct kbase_device *kbdev);

void kbase_synchronize_irqs(struct kbase_device *kbdev);

#ifdef CONFIG_MALI_DEBUG
#if IS_ENABLED(CONFIG_MALI_REAL_HW)

int kbase_validate_interrupts(struct kbase_device *const kbdev);
#endif
#endif

irq_handler_t kbase_get_interrupt_handler(struct kbase_device *kbdev, u32 irq_tag);

int kbase_set_custom_irq_handler(struct kbase_device *kbdev, irq_handler_t custom_handler,
				 u32 irq_tag);

#endif
