/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mindone/compat.h — one source tree for two kernels (6.1 and 6.12).
 * Every API that changed between ACK android14-6.1 and android16-6.12 is wrapped
 * here behind LINUX_VERSION_CODE; module sources carry no version #ifdefs of
 * their own. Prefer forms that exist in both kernels over macros; add a
 * MINDONE_* macro only where the two kernels are genuinely incompatible.
 */
#ifndef __MINDONE_COMPAT_H__
#define __MINDONE_COMPAT_H__

#include <linux/version.h>
#include <linux/module.h>

/* class_create(): lost the owner argument in 6.4. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
#define MINDONE_CLASS_CREATE(name)	class_create(name)
#else
#define MINDONE_CLASS_CREATE(name)	class_create(THIS_MODULE, name)
#endif

/* DEFINE_SEMAPHORE(): mandatory initial count since 6.4. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
#define MINDONE_DEFINE_SEMAPHORE(name)	DEFINE_SEMAPHORE(name, 1)
#else
#define MINDONE_DEFINE_SEMAPHORE(name)	DEFINE_SEMAPHORE(name)
#endif

/*
 * i2c_driver probe: the single-argument callback is .probe_new before 6.6 and
 * .probe from 6.6 on (the two-argument .probe was removed). Use in initializers:
 *	MINDONE_I2C_PROBE(my_probe),
 * with `static int my_probe(struct i2c_client *client)`.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#define MINDONE_I2C_PROBE(fn)		.probe = (fn)
#else
#define MINDONE_I2C_PROBE(fn)		.probe_new = (fn)
#endif

/*
 * i2c_client_get_device_id(): appeared in 6.3 (commit 0d3bb2554b) together with the move
 * to single-argument probe -- before it, drivers received the id as a second argument.
 * We provide the same thing the kernel does internally: match the driver's id table
 * against the client. Needed by modules that have been switched to MINDONE_I2C_PROBE
 * but must still build under 6.1.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
#include <linux/i2c.h>
static inline const struct i2c_device_id *
i2c_client_get_device_id(const struct i2c_client *client)
{
	const struct i2c_driver *drv = to_i2c_driver(client->dev.driver);

	return i2c_match_id(drv->id_table, client);
}
#endif

/*
 * of_get_named_gpio_flags() was removed in 6.12 (only the flagless of_get_named_gpio
 * remains). MindOne drivers do not need the flags from the device description: ft3519
 * immediately overwrites irq_gpio_flags with IRQF_TRIGGER_FALLING|IRQF_ONESHOT
 * (focaltech_core.c:1001), and reset_gpio_flags is never read at all. So the wrapper
 * on newer kernels just zeroes the flags.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#include <linux/of_gpio.h>
static inline int mindone_of_get_named_gpio_flags(struct device_node *np, const char *name,
						  int index, void *flags)
{
	if (flags)
		*(u32 *)flags = 0;
	return of_get_named_gpio(np, name, index);
}
#else
#define mindone_of_get_named_gpio_flags(np, name, index, flags) \
	of_get_named_gpio_flags((np), (name), (index), (void *)(flags))
#endif

/*
 * GPIOF_DIR_IN disappeared in 6.12 along with the gpiolib legacy-flags cleanup. The value
 * was always 1 ("input"), and devm_gpio_request_one() still accepts it as before -- we
 * just keep the constant.
 */
#ifndef GPIOF_DIR_IN
#define MINDONE_GPIOF_DIR_IN	1
#else
#define MINDONE_GPIOF_DIR_IN	GPIOF_DIR_IN
#endif

/* <asm/unaligned.h> moved to <linux/unaligned.h> in 6.12. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#include <linux/unaligned.h>
#else
#include <asm/unaligned.h>
#endif

/*
 * MAX_ORDER was renamed MAX_PAGE_ORDER in 6.8 and its meaning changed by one:
 * the old MAX_ORDER was exclusive (highest order + 1), the new one is the highest
 * usable order. MINDONE_MAX_PAGE_ORDER always means "highest usable order".
 */
#include <linux/mmzone.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
#define MINDONE_MAX_PAGE_ORDER		MAX_PAGE_ORDER
#else
#define MINDONE_MAX_PAGE_ORDER		(MAX_ORDER - 1)
#endif

/* dma_set_max_seg_size() returns void since 6.6; callers that checked the result use this. */
#include <linux/dma-mapping.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#define MINDONE_DMA_SET_MAX_SEG_SIZE(dev, size)	({ dma_set_max_seg_size((dev), (size)); 0; })
#else
#define MINDONE_DMA_SET_MAX_SEG_SIZE(dev, size)	dma_set_max_seg_size((dev), (size))
#endif

/* Tracepoint helper: __assign_str() lost its source argument in 6.10. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
#define MINDONE_ASSIGN_STR(dst, src)	__assign_str(dst)
#else
#define MINDONE_ASSIGN_STR(dst, src)	__assign_str(dst, src)
#endif

/* struct bus_type::dev_root became private in 6.4; use the accessor everywhere. */
#include <linux/device/bus.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
#define MINDONE_BUS_DEV_ROOT(bus)	bus_get_dev_root(bus)
#else
#define MINDONE_BUS_DEV_ROOT(bus)	((bus)->dev_root)
#endif

/* thermal_zone_device::devdata is private since 6.4. */
#include <linux/thermal.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
#define MINDONE_TZ_DEVDATA(tz)		thermal_zone_device_priv(tz)
#else
#define MINDONE_TZ_DEVDATA(tz)		((tz)->devdata)
#endif

/* sched_avg::util_est lost its struct wrapper in 6.9. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
#define MINDONE_UTIL_EST(sa)		((sa)->util_est)
#else
#define MINDONE_UTIL_EST(sa)		((sa)->util_est.enqueued)
#endif

/* Per-task estimated utilisation. The two kernels compute it differently, and the difference is
 * not cosmetic: 6.1 keeps a separate exponential average (ue.ewma) and returns the larger of the
 * two, while 6.9+ folds everything into one field. Copying either formula into a module by hand
 * would silently change scheduling behaviour on the other kernel, so both are kept here, each
 * mirroring _task_util_est() of its own kernel/sched/fair.c verbatim.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
#define MINDONE_TASK_UTIL_EST(p)						\
	(READ_ONCE((p)->se.avg.util_est) & ~UTIL_AVG_UNCHANGED)
#else
#define MINDONE_TASK_UTIL_EST(p)						\
	({ struct util_est __ue = READ_ONCE((p)->se.avg.util_est);		\
	   max(__ue.ewma, (__ue.enqueued & ~UTIL_AVG_UNCHANGED)); })
#endif

/* Walking every live dma_buf: the ACK helper kept the signature but changed its name between the
 * two kernels — dma_buf_get_each() in 6.1 (namespace MINIDUMP), get_dmabuf_debugfs_data() in 6.12
 * (namespace DMA_BUF). Both hold dmabuf_list_mutex across the callback, so the callback still must
 * not sleep. The upstream dma_buf_iter_begin()/next() pair of 6.12 is deliberately NOT used here:
 * it is not exported to modules, and it drops the lock between buffers, which is different
 * behaviour, not a drop-in replacement.
 * Callers must import both namespaces: MODULE_IMPORT_NS(MINIDUMP) and MODULE_IMPORT_NS(DMA_BUF).
 */
#include <linux/dma-buf.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define MINDONE_DMA_BUF_FOR_EACH(_cb, _priv)	get_dmabuf_debugfs_data((_cb), (_priv))
#else
#define MINDONE_DMA_BUF_FOR_EACH(_cb, _priv)	dma_buf_get_each((_cb), (_priv))
#endif

/* pwm_apply_state() was renamed pwm_apply_might_sleep() in 6.8. */
#include <linux/pwm.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
#define MINDONE_PWM_APPLY_STATE(pwm, state)	pwm_apply_might_sleep((pwm), (state))
#else
#define MINDONE_PWM_APPLY_STATE(pwm, state)	pwm_apply_state((pwm), (state))
#endif

/*
 * Sysctl tables: the nested .child model and register_sysctl_table() are gone since
 * 6.11, and 6.12 rejects the empty sentinel entry. Keep the sentinel in the source
 * (6.1 needs it) and register the leaf table under an explicit path.
 */
#include <linux/sysctl.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
#define MINDONE_REGISTER_SYSCTL(path, table)	register_sysctl_sz((path), (table), ARRAY_SIZE(table) - 1)
#else
#define MINDONE_REGISTER_SYSCTL(path, table)	register_sysctl((path), (table))
#endif

/* dma_heap_ops::allocate flag arguments narrowed to u32/u64 in 6.10. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
typedef u32 mindone_heap_fd_flags_t;
typedef u64 mindone_heap_flags_t;
#else
typedef unsigned long mindone_heap_fd_flags_t;
typedef unsigned long mindone_heap_flags_t;
#endif

/*
 * Energy model: the performance-state table is RCU-managed since 6.9 and read
 * through em_perf_state_from_pd(); callers on 6.9+ should hold rcu_read_lock().
 */
#include <linux/energy_model.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
#define MINDONE_EM_TABLE(pd)		em_perf_state_from_pd(pd)
#else
#define MINDONE_EM_TABLE(pd)		((pd)->table)
#endif

/* pwm_chip embeds its device since 6.9; the old chip->dev pointer is the parent. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
#define MINDONE_PWMCHIP_PARENT(chip)	pwmchip_parent(chip)
#else
#define MINDONE_PWMCHIP_PARENT(chip)	((chip)->dev)
#endif

/* power_supply_desc::usb_types is a bitmask since 6.12 (array + count before). */
#include <linux/power_supply.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define MINDONE_PSY_USB_TYPES(array, mask)	.usb_types = (mask)
#else
#define MINDONE_PSY_USB_TYPES(array, mask)	.usb_types = (array), .num_usb_types = ARRAY_SIZE(array)
#endif

/* vb2_queue::num_buffers went private in 6.8. */
#include <media/videobuf2-core.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
#define MINDONE_VB2_NUM_BUFFERS(q)	vb2_get_num_buffers(q)
#else
#define MINDONE_VB2_NUM_BUFFERS(q)	((q)->num_buffers)
#endif

/* SCMI protocol handle: set_priv() takes the negotiated protocol version since 6.6. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#define MINDONE_SCMI_SET_PRIV(ph, priv, version)	(ph)->set_priv((ph), (priv), (version))
#else
#define MINDONE_SCMI_SET_PRIV(ph, priv, version)	(ph)->set_priv((ph), (priv))
#endif

/* pwm_ops lost its owner field in 6.7 (the core takes the module from pwm_chip). */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
#define MINDONE_PWM_OPS_OWNER
#else
#define MINDONE_PWM_OPS_OWNER		.owner = THIS_MODULE,
#endif

/*
 * generic_pm_domain::opp_to_performance_state is gone in 6.12: the core derives the
 * performance state from the OPP table (required-opps / opp-level) itself.
 */
#include <linux/pm_domain.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define MINDONE_GENPD_OPP_TO_PSTATE(genpd, fn)	do { } while (0)
#else
#define MINDONE_GENPD_OPP_TO_PSTATE(genpd, fn)	((genpd)->opp_to_performance_state = (fn))
#endif

/*
 * Unsigned file offsets: FMODE_UNSIGNED_OFFSET (set in open) became the static
 * file_operations flag FOP_UNSIGNED_OFFSET in 6.12.
 */
#include <linux/fs.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define MINDONE_FOP_UNSIGNED_OFFSET		.fop_flags = FOP_UNSIGNED_OFFSET,
#define MINDONE_SET_FMODE_UNSIGNED_OFFSET(filp)	do { } while (0)
#else
#define MINDONE_FOP_UNSIGNED_OFFSET
#define MINDONE_SET_FMODE_UNSIGNED_OFFSET(filp)	((filp)->f_mode |= FMODE_UNSIGNED_OFFSET)
#endif

/*
 * pwm_chip lives inside the driver's private struct before 6.9 and is allocated by
 * the PWM core (private data behind it) from 6.9 on. Drivers declare the member with
 * MINDONE_PWMCHIP_MEMBER, allocate with MINDONE_PWMCHIP_ALLOC and reach the private
 * struct with MINDONE_PWMCHIP_PRIV; pwmchip_add()/pwmchip_remove() take the chip pointer.
 */
#include <linux/err.h>
#include <linux/slab.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
#define MINDONE_PWMCHIP_MEMBER
#define MINDONE_PWMCHIP_ALLOC(_dev, _npwm, _type)	devm_pwmchip_alloc((_dev), (_npwm), sizeof(_type))
#define MINDONE_PWMCHIP_PRIV(_chip, _type)	((_type *)pwmchip_get_drvdata(_chip))
#define MINDONE_PWMCHIP_INIT(_chip, _dev)	do { } while (0)
#else
#define MINDONE_PWMCHIP_MEMBER			struct pwm_chip chip;
#define MINDONE_PWMCHIP_ALLOC(_dev, _npwm, _type)	({ _type *__p = devm_kzalloc((_dev), sizeof(_type), GFP_KERNEL); \
						   __p ? ({ __p->chip.npwm = (_npwm); &__p->chip; }) : ERR_PTR(-ENOMEM); })
#define MINDONE_PWMCHIP_PRIV(_chip, _type)	container_of((_chip), _type, chip)
#define MINDONE_PWMCHIP_INIT(_chip, _dev)	do { (_chip)->dev = (_dev); (_chip)->base = -1; } while (0)
#endif

/* get_user_pages()/pin_user_pages() lost the vmas argument in 6.5. */
#include <linux/mm.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
#define MINDONE_PIN_USER_PAGES(start, n, flags, pages)	pin_user_pages((start), (n), (flags), (pages))
#define MINDONE_GET_USER_PAGES(start, n, flags, pages)	get_user_pages((start), (n), (flags), (pages))
#define MINDONE_GET_USER_PAGES_REMOTE(mm, start, n, flags, pages, locked) 	get_user_pages_remote((mm), (start), (n), (flags), (pages), (locked))
#else
#define MINDONE_PIN_USER_PAGES(start, n, flags, pages)	pin_user_pages((start), (n), (flags), (pages), NULL)
#define MINDONE_GET_USER_PAGES(start, n, flags, pages)	get_user_pages((start), (n), (flags), (pages), NULL)
#define MINDONE_GET_USER_PAGES_REMOTE(mm, start, n, flags, pages, locked) 	get_user_pages_remote((mm), (start), (n), (flags), (pages), NULL, (locked))
#endif

/*
 * IOMMU domain lifecycle (6.1 vs 6.12): iommu_domain_ops::detach_dev is gone and a
 * driver instead exposes an identity domain whose attach_dev undoes the translation
 * (iommu_ops::identity_domain, 6.7+); iommu_fwspec::ops went private in 6.11.
 */
#include <linux/iommu.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
#define MINDONE_IOMMU_DETACH_DEV(fn)
#define MINDONE_IOMMU_IDENTITY_DOMAIN(ptr)	.identity_domain = (ptr),
#else
#define MINDONE_IOMMU_DETACH_DEV(fn)		.detach_dev = (fn),
#define MINDONE_IOMMU_IDENTITY_DOMAIN(ptr)
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
#define MINDONE_FWSPEC_IS_OURS(_fwspec, _ops)	((_fwspec) != NULL)
#else
#define MINDONE_FWSPEC_IS_OURS(_fwspec, _ops)	((_fwspec) && (_fwspec)->ops == (_ops))
#endif

/*
 * Shrinkers: a static struct registered with register_shrinker() before 6.7, an object
 * from shrinker_alloc() registered with shrinker_register() since. MINDONE_SHRINKER_DEFINE
 * declares the shrinker plus <var>_register(name)/<var>_unregister() for the module.
 */
#include <linux/shrinker.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
#define MINDONE_SHRINKER_DEFINE(_var, _count, _scan, _seeks)				\
	static struct shrinker *_var;							\
	static inline int _var##_register(const char *name)				\
	{										\
		_var = shrinker_alloc(0, "%s", name);					\
		if (!_var)								\
			return -ENOMEM;							\
		_var->count_objects = (_count);						\
		_var->scan_objects = (_scan);						\
		_var->seeks = (_seeks);							\
		shrinker_register(_var);						\
		return 0;								\
	}										\
	static inline void _var##_unregister(void) { shrinker_free(_var); }
#else
#define MINDONE_SHRINKER_DEFINE(_var, _count, _scan, _seeks)				\
	static struct shrinker _var = {							\
		.count_objects = (_count), .scan_objects = (_scan), .seeks = (_seeks),	\
	};										\
	static inline int _var##_register(const char *name)				\
	{ return register_shrinker(&_var, "%s", name); }				\
	static inline void _var##_unregister(void) { unregister_shrinker(&_var); }
#endif

/* Legacy gpio_set_debounce() is gone in 6.12; the descriptor form exists in both kernels. */
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define MINDONE_GPIO_SET_DEBOUNCE(_gpio, _us)	gpiod_set_debounce(gpio_to_desc(_gpio), (_us))
#else
#define MINDONE_GPIO_SET_DEBOUNCE(_gpio, _us)	gpio_set_debounce((_gpio), (_us))
#endif

/* __skb_frag_set_page() was replaced by skb_frag_fill_page_desc() in 6.5 (offset/size set separately by callers). */
#include <linux/skbuff.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
#define MINDONE_SKB_FRAG_SET_PAGE(_frag, _page)	skb_frag_fill_page_desc((_frag), (_page), 0, 0)
#else
#define MINDONE_SKB_FRAG_SET_PAGE(_frag, _page)	__skb_frag_set_page((_frag), (_page))
#endif

/* iommu_setup_dma_ops() is private since 6.9: the core sets up DMA ops for the device itself. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
#define MINDONE_IOMMU_SETUP_DMA_OPS(_dev, _base, _size)	do { } while (0)
#else
#define MINDONE_IOMMU_SETUP_DMA_OPS(_dev, _base, _size)	iommu_setup_dma_ops((_dev), (_base), (_size))
#endif

/* The android_vh_ipv6_gen_linklocal_addr vendor hook does not exist in ACK 6.12. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define MINDONE_REGISTER_IPV6_LLA_HOOK(_fn, _data)	(0)
#define MINDONE_UNREGISTER_IPV6_LLA_HOOK(_fn, _data)	(0)
#else
#define MINDONE_REGISTER_IPV6_LLA_HOOK(_fn, _data)	register_trace_android_vh_ipv6_gen_linklocal_addr((_fn), (_data))
#define MINDONE_UNREGISTER_IPV6_LLA_HOOK(_fn, _data)	unregister_trace_android_vh_ipv6_gen_linklocal_addr((_fn), (_data))
#endif

/* mrdump (AEE minidump) is not ported to 6.12: its extra-dump hooks become no-ops there. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define MINDONE_MRDUMP_MINI_ADD_EXTRA_FILE(_va, _pa, _size, _name)	(-1)
#define MINDONE_MRDUMP_SET_EXTRA_DUMP(_id, _fn)				do { } while (0)
#else
#define MINDONE_MRDUMP_MINI_ADD_EXTRA_FILE(_va, _pa, _size, _name)	mrdump_mini_add_extra_file((_va), (_pa), (_size), (_name))
#define MINDONE_MRDUMP_SET_EXTRA_DUMP(_id, _fn)				mrdump_set_extra_dump((_id), (_fn))
#endif

/*
 * Shrinkers embedded in driver structs: a struct member registered in place before 6.7,
 * a pointer from shrinker_alloc() since. MINDONE_SHRINKER_MEMBER declares the member,
 * MINDONE_SHRINKER_SETUP fills and registers it, MINDONE_SHRINKER_PRIV recovers the
 * owner inside count/scan callbacks, MINDONE_SHRINKER_TEARDOWN unregisters.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
#define MINDONE_SHRINKER_MEMBER(_name)		struct shrinker *_name
#define MINDONE_SHRINKER_SETUP(_sh, _count, _scan, _seeks, _str, _priv)		\
	({ struct shrinker *__s = shrinker_alloc(0, "%s", (_str)); int __r = -ENOMEM;	\
	   if (__s) { __s->count_objects = (_count); __s->scan_objects = (_scan);	\
		      __s->seeks = (_seeks); __s->batch = 0; __s->private_data = (_priv);	\
		      (_sh) = __s; shrinker_register(__s); __r = 0; }			\
	   __r; })
#define MINDONE_SHRINKER_PRIV(_s, _type, _member)	((_type *)(_s)->private_data)
#define MINDONE_SHRINKER_TEARDOWN(_sh)		shrinker_free(_sh)
#else
#define MINDONE_SHRINKER_MEMBER(_name)		struct shrinker _name
#define MINDONE_SHRINKER_SETUP(_sh, _count, _scan, _seeks, _str, _priv)		\
	({ (_sh).count_objects = (_count); (_sh).scan_objects = (_scan);		\
	   (_sh).seeks = (_seeks); (_sh).batch = 0;					\
	   register_shrinker(&(_sh), "%s", (_str)); })
#define MINDONE_SHRINKER_PRIV(_s, _type, _member)	container_of((_s), _type, _member)
#define MINDONE_SHRINKER_TEARDOWN(_sh)		unregister_shrinker(&(_sh))
#endif

/* follow_pfn() was removed in 6.8; 6.12 walks the mapping with follow_pfnmap_start()/end(). */
static inline int mindone_follow_pfn(struct vm_area_struct *vma, unsigned long address, unsigned long *pfn)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	struct follow_pfnmap_args args = { .vma = vma, .address = address };
	int ret = follow_pfnmap_start(&args);

	if (ret)
		return ret;
	*pfn = args.pfn;
	follow_pfnmap_end(&args);
	return 0;
#else
	return follow_pfn(vma, address, pfn);
#endif
}

/* Scheduler helpers that stopped being visible to modules: capacity_orig_of (6.7), cpu_util_cfs. */
#include <linux/sched.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
#define MINDONE_CAPACITY_ORIG_OF(_cpu)		arch_scale_cpu_capacity(_cpu)
/* cpu_util_cfs() is not exported by GKI 6.12: same formula from the rq (util_avg vs util_est, capped). */
#define MINDONE_CPU_UTIL_CFS(_rq, _cpu)		\
	min_t(unsigned long, max_t(unsigned long, READ_ONCE((_rq)->cfs.avg.util_avg),		\
				   READ_ONCE(MINDONE_UTIL_EST(&(_rq)->cfs.avg))),			\
	      arch_scale_cpu_capacity(_cpu))
#else
#define MINDONE_CAPACITY_ORIG_OF(_cpu)		capacity_orig_of(_cpu)
/* 6.1: cpu_util_cfs(int cpu). Passing the rq pointer here (as the 5.10 API took) silently truncated
 * it to a CPU index and tripped the UBSAN bounds trap on the first governor hook call (F3558). */
#define MINDONE_CPU_UTIL_CFS(_rq, _cpu)		cpu_util_cfs(_cpu)
#endif


/*
 * DRM node open: 6.12 refuses the open unless the driver declares the flag in
 * the new file_operations::fop_flags (drm_file.c:312 returns -EINVAL). In 6.1
 * the kernel set FMODE_UNSIGNED_OFFSET itself (drm_file.c:359) and the
 * fop_flags field does not exist at all, so this must expand to nothing there.
 * Symptom without it: /dev/dri/card0 cannot be opened, the composer HAL gets
 * zero CRTCs and dies on a null deref -> black screen with a live adb.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define MINDONE_FOPS_UNSIGNED_OFFSET	.fop_flags = FOP_UNSIGNED_OFFSET,
#else
#define MINDONE_FOPS_UNSIGNED_OFFSET
#endif

/*
 * nvmem providers: since 6.9 the core registers "fixed" cells declared as DT
 * child nodes only when the provider asks for it (nvmem/core.c:1035). Without
 * it the provider probes fine but every consumer gets -ENOENT on a cell lookup
 * by name. Symptom: mtk-soc-temp-lvts and mtk-pmic-temp fail to probe, and the
 * SoC/PMIC thermal zones (soc_max among them) never appear.
 */
#include <linux/nvmem-provider.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
#define MINDONE_NVMEM_LEGACY_OF_CELLS(_cfg)	((_cfg).add_legacy_fixed_of_cells = true)
#else
#define MINDONE_NVMEM_LEGACY_OF_CELLS(_cfg)	do { } while (0)
#endif

/*
 * cpufreq_driver::exit lost its return value in 6.11: `int (*exit)()` in 6.1, `void (*exit)()`
 * in 6.12. Both cores discard whatever it returns anyway — cpufreq.c calls it as a bare
 * statement, not through an assignment, on both kernels — so the return value was never
 * actually observed. MINDONE_CPUFREQ_EXIT_RETTYPE/_RETURN let one function body build with the
 * right type on each kernel without changing behaviour.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
#define MINDONE_CPUFREQ_EXIT_RETTYPE		void
#define MINDONE_CPUFREQ_EXIT_RETURN(val)	return
#else
#define MINDONE_CPUFREQ_EXIT_RETTYPE		int
#define MINDONE_CPUFREQ_EXIT_RETURN(val)	return (val)
#endif

/*
 * get_file_rcu(): 6.7 changed it from a macro taking the file pointer into a
 * function taking the ADDRESS of the pointer and returning struct file *.
 * Passing the pointer where the address is expected compiles with a warning and
 * makes the kernel read the first eight bytes of struct file as an address.
 * Symptom: reading the module's /proc file panics the kernel.
 */
/* 6.7+ get_file_rcu() is no longer a single atomic_long_inc_not_zero(): it LOOPS until the
 * reference is taken or the pointer changes. Called on a dying file (f_count == 0) whose release
 * is blocked behind a lock the caller holds, it spins forever. That is exactly the rss_pid walk
 * of mtk_heap_debug: get_dmabuf_debugfs_data() holds dmabuf_list_mutex, dma_buf_file_release()
 * of a buffer being freed waits for that mutex, and the walker spins on the buffer's dead file
 * at 100 % kernel time (memtrack HAL hung, gralloc blocked in __dma_buf_list_add, every UI
 * process ANRs; 13.09 06:17, F4281). Keep the 6.1 semantics on every kernel: one attempt, false
 * when the file is already dying. f_count is atomic_long_t through 6.12.
 */
#define MINDONE_GET_FILE_RCU(_fp)	(atomic_long_inc_not_zero(&(_fp)->f_count) != 0)

#endif /* __MINDONE_COMPAT_H__ */
