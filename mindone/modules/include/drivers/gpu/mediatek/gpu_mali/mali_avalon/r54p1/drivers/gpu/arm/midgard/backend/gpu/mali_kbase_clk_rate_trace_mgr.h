/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_CLK_RATE_TRACE_MGR_
#define _KBASE_CLK_RATE_TRACE_MGR_

#define KBASE_CLOCK_DOMAIN_TOP (0)

#define KBASE_CLOCK_DOMAIN_SHADER_CORES (1)

struct kbase_clk_data {
	struct kbase_clk_rate_trace_manager *clk_rtm;
	void *gpu_clk_handle;
	void *plat_private;
	struct notifier_block clk_rate_change_nb;
	unsigned long clock_val;
	u8 index;
};

int kbase_clk_rate_trace_manager_init(struct kbase_device *kbdev);

void kbase_clk_rate_trace_manager_term(struct kbase_device *kbdev);

void kbase_clk_rate_trace_manager_gpu_active(struct kbase_device *kbdev);

void kbase_clk_rate_trace_manager_gpu_idle(struct kbase_device *kbdev);

static inline void
kbase_clk_rate_trace_manager_subscribe_no_lock(struct kbase_clk_rate_trace_manager *clk_rtm,
					       struct kbase_clk_rate_listener *listener)
{
	lockdep_assert_held(&clk_rtm->lock);
	list_add(&listener->node, &clk_rtm->listeners);
}

static inline void
kbase_clk_rate_trace_manager_subscribe(struct kbase_clk_rate_trace_manager *clk_rtm,
				       struct kbase_clk_rate_listener *listener)
{
	unsigned long flags;

	spin_lock_irqsave(&clk_rtm->lock, flags);
	kbase_clk_rate_trace_manager_subscribe_no_lock(clk_rtm, listener);
	spin_unlock_irqrestore(&clk_rtm->lock, flags);
}

static inline void
kbase_clk_rate_trace_manager_unsubscribe(struct kbase_clk_rate_trace_manager *clk_rtm,
					 struct kbase_clk_rate_listener *listener)
{
	unsigned long flags;

	spin_lock_irqsave(&clk_rtm->lock, flags);
	list_del(&listener->node);
	spin_unlock_irqrestore(&clk_rtm->lock, flags);
}

void kbase_clk_rate_trace_manager_notify_all(struct kbase_clk_rate_trace_manager *clk_rtm,
					     u32 clock_index, unsigned long new_rate);

#endif
