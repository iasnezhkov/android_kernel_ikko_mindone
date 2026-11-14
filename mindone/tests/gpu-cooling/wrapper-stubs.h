/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
typedef struct { int held; } raw_spinlock_t;
static inline void raw_spin_lock_irqsave(raw_spinlock_t *lock, unsigned long flags) { (void)flags; assert(!lock->held); lock->held = 1; }
static inline void raw_spin_unlock_irqrestore(raw_spinlock_t *lock, unsigned long flags) { (void)flags; assert(lock->held); lock->held = 0; }
static int knob_ipi_error;
static int gpufreq_ipi_to_gpueb(struct gpufreq_ipi_data data) { (void)data; event("ipi"); return knob_ipi_error; }
static void gpufreq_init_external_callback(void) {}
void gpufreq_debug_init(unsigned int dual_buck, unsigned int gpueb_support) { (void)dual_buck; (void)gpueb_support; event("gpuppm_fp"); }
