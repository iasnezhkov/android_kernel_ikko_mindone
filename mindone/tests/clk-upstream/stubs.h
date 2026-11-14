/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef CLK_UPSTREAM_STUBS_H
#define CLK_UPSTREAM_STUBS_H

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef uint64_t resource_size_t;
typedef int spinlock_t;

#define __iomem
#define __init
#define __exit
#define __initdata
#define __must_check
#define __acquire(x) ((void)0)
#define __release(x) ((void)0)
#define likely(x) (x)
#define unlikely(x) (x)
#define BIT(n) (1UL << (n))
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (sizeof(long) * 8 - 1 - (h))))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define READ_ONCE(x) (*(volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, v) (*(volatile __typeof__(x) *)&(x) = (v))
#define struct_size(p, member, n) (sizeof(*(p)) + sizeof((p)->member[0]) * (n))
#define do_div(n, base) ({ uint32_t __rem = (uint32_t)((n) % (base)); (n) = (n) / (base); __rem; })
#define MHZ_STUB 1000000
#define EXPORT_SYMBOL(s) extern int harness_unused_declaration
#define EXPORT_SYMBOL_GPL(s) extern int harness_unused_declaration
#define MODULE_LICENSE(s) extern int harness_unused_declaration
#define MODULE_DESCRIPTION(s) extern int harness_unused_declaration
#define MODULE_AUTHOR(s) extern int harness_unused_declaration
#define MODULE_DEVICE_TABLE(t, n) extern int harness_unused_declaration
#define MODULE_PARM_DESC(n, d) extern int harness_unused_declaration
#define THIS_MODULE NULL
#define GFP_KERNEL 0
#define MAX_ERRNO 4095
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
#define PRE_RATE_CHANGE BIT(0)
#define POST_RATE_CHANGE BIT(1)
#define ABORT_RATE_CHANGE BIT(2)
#define IORESOURCE_MEM 0x200

#define CLK_SET_RATE_GATE BIT(0)
#define CLK_SET_PARENT_GATE BIT(1)
#define CLK_SET_RATE_PARENT BIT(2)
#define CLK_IGNORE_UNUSED BIT(3)
#define CLK_GET_RATE_NOCACHE BIT(6)
#define CLK_SET_RATE_NO_REPARENT BIT(7)
#define CLK_GET_ACCURACY_NOCACHE BIT(8)
#define CLK_RECALC_NEW_RATES BIT(9)
#define CLK_SET_RATE_UNGATE BIT(10)
#define CLK_IS_CRITICAL BIT(11)
#define CLK_OPS_PARENT_ENABLE BIT(12)
#define CLK_DUTY_CYCLE_PARENT BIT(13)
#define CLK_GATE_SET_TO_DISABLE BIT(0)
#define CLK_GATE_HIWORD_MASK BIT(1)

static inline void *ERR_PTR(long error)
{
	return (void *)error;
}

static inline long PTR_ERR(const void *ptr)
{
	return (long)ptr;
}

static inline bool IS_ERR(const void *ptr)
{
	return (unsigned long)ptr >= (unsigned long)-MAX_ERRNO;
}

static inline bool IS_ERR_OR_NULL(const void *ptr)
{
	return !ptr || IS_ERR(ptr);
}

static inline void *ERR_CAST(const void *ptr)
{
	return (void *)ptr;
}

void harness_log(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define pr_err(...) harness_log(3, __VA_ARGS__)
#define pr_warn(...) harness_log(4, __VA_ARGS__)
#define pr_notice(...) harness_log(5, __VA_ARGS__)
#define pr_info(...) harness_log(6, __VA_ARGS__)
#define pr_debug(...) harness_log(7, __VA_ARGS__)
#define dev_err(dev, ...) harness_log(3, __VA_ARGS__)
#define dev_warn(dev, ...) harness_log(4, __VA_ARGS__)
#define dev_notice(dev, ...) harness_log(5, __VA_ARGS__)
#define dev_info(dev, ...) harness_log(6, __VA_ARGS__)
#define dev_err_probe(dev, err, ...) (harness_log(3, __VA_ARGS__), (err))
#define dev_err_ptr_probe(dev, err, ...) ERR_PTR(dev_err_probe(dev, err, __VA_ARGS__))

struct kernel_param;

struct kernel_param_ops {
	int (*set)(const char *val, const struct kernel_param *kp);
	int (*get)(char *buffer, const struct kernel_param *kp);
};

#define module_param_cb(name, ops, arg, perm) \
	const struct kernel_param_ops *const harness_param_##name = (ops)

int sysfs_emit(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int sysfs_emit_at(char *buf, int at, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

struct regmap;
struct clk;
struct clk_hw;
struct clk_core;
struct of_phandle_args;

struct device_node {
	const char *full_name;
	const char *compatible;
	int region;
};

struct device_driver_stub {
	const char *name;
	void *owner;
	const struct of_device_id *of_match_table;
};

struct device {
	struct device_node *of_node;
	struct device *parent;
	const void *match_data;
	void *driver_data;
	int pm_enabled;
	int pm_usage;
};

struct resource {
	resource_size_t start;
	resource_size_t end;
	unsigned long flags;
};

struct of_device_id {
	char name[32];
	char type[32];
	char compatible[128];
	const void *data;
};

struct platform_device_id {
	char name[20];
	unsigned long driver_data;
};

struct platform_device {
	const char *name;
	struct device dev;
	const struct platform_device_id *id_entry;
};

struct platform_driver {
	int (*probe)(struct platform_device *pdev);
	void (*remove)(struct platform_device *pdev);
	struct device_driver_stub driver;
};

void harness_add_driver(struct platform_driver *drv);
int platform_register_drivers(struct platform_driver * const *drivers, unsigned int count);
void platform_unregister_drivers(struct platform_driver * const *drivers, unsigned int count);

#define module_platform_driver(drv)						\
	static void __attribute__((constructor)) harness_ctor_##drv(void)	\
	{									\
		harness_add_driver(&drv);					\
	}
#define module_init(fn)								\
	static void __attribute__((constructor)) harness_init_##fn(void)	\
	{									\
		fn();								\
	}
#define module_exit(fn)								\
	static void (*const harness_exit_##fn)(void) __attribute__((unused)) = fn

#define platform_get_device_id(pdev) ((pdev)->id_entry)

static inline void platform_set_drvdata(struct platform_device *pdev, void *data)
{
	pdev->dev.driver_data = data;
}

static inline void *platform_get_drvdata(const struct platform_device *pdev)
{
	return pdev->dev.driver_data;
}

static inline const void *of_device_get_match_data(const struct device *dev)
{
	return dev->match_data;
}

static inline const void *device_get_match_data(const struct device *dev)
{
	return dev->match_data;
}

struct clk_rate_request {
	struct clk_core *core;
	unsigned long rate;
	unsigned long min_rate;
	unsigned long max_rate;
	unsigned long best_parent_rate;
	struct clk_hw *best_parent_hw;
};

struct clk_ops {
	int (*prepare)(struct clk_hw *hw);
	void (*unprepare)(struct clk_hw *hw);
	int (*is_prepared)(struct clk_hw *hw);
	void (*unprepare_unused)(struct clk_hw *hw);
	int (*enable)(struct clk_hw *hw);
	void (*disable)(struct clk_hw *hw);
	int (*is_enabled)(struct clk_hw *hw);
	void (*disable_unused)(struct clk_hw *hw);
	unsigned long (*recalc_rate)(struct clk_hw *hw, unsigned long parent_rate);
	long (*round_rate)(struct clk_hw *hw, unsigned long rate, unsigned long *parent_rate);
	int (*determine_rate)(struct clk_hw *hw, struct clk_rate_request *req);
	int (*set_parent)(struct clk_hw *hw, u8 index);
	u8 (*get_parent)(struct clk_hw *hw);
	int (*set_rate)(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate);
};

struct clk_parent_data {
	const struct clk_hw *hw;
	const char *fw_name;
	const char *name;
	int index;
};

struct clk_init_data {
	const char *name;
	const struct clk_ops *ops;
	const char * const *parent_names;
	const struct clk_parent_data *parent_data;
	const struct clk_hw **parent_hws;
	u8 num_parents;
	unsigned long flags;
};

struct clk_hw {
	struct clk_core *core;
	struct clk *clk;
	const struct clk_init_data *init;
};

struct clk {
	struct clk_hw *hw;
	int index;
};

struct clk_onecell_data {
	struct clk **clks;
	unsigned int clk_num;
};

struct clk_hw_onecell_data {
	unsigned int num;
	struct clk_hw *hws[];
};

struct clk_div_table {
	unsigned int val;
	unsigned int div;
};

struct clk_mux {
	struct clk_hw hw;
	void __iomem *reg;
	const u32 *table;
	u32 mask;
	u8 shift;
	u8 flags;
	spinlock_t *lock;
};

struct clk_gate {
	struct clk_hw hw;
	void __iomem *reg;
	u8 bit_idx;
	u8 flags;
	spinlock_t *lock;
};

struct clk_divider {
	struct clk_hw hw;
	void __iomem *reg;
	u8 shift;
	u8 width;
	u16 flags;
	const struct clk_div_table *table;
	spinlock_t *lock;
};

struct clk_composite {
	struct clk_hw hw;
	struct clk_ops ops;
	struct clk_hw *mux_hw;
	struct clk_hw *rate_hw;
	struct clk_hw *gate_hw;
	const struct clk_ops *mux_ops;
	const struct clk_ops *rate_ops;
	const struct clk_ops *gate_ops;
};

#define to_clk_composite(_hw) container_of(_hw, struct clk_composite, hw)
#define to_clk_mux(_hw) container_of(_hw, struct clk_mux, hw)
#define to_clk_gate(_hw) container_of(_hw, struct clk_gate, hw)
#define to_clk_divider(_hw) container_of(_hw, struct clk_divider, hw)

extern const struct clk_ops clk_mux_ops;
extern const struct clk_ops clk_gate_ops;
extern const struct clk_ops clk_divider_ops;

struct notifier_block {
	int (*notifier_call)(struct notifier_block *nb, unsigned long action, void *data);
	int priority;
};

struct clk_notifier_data {
	struct clk *clk;
	unsigned long old_rate;
	unsigned long new_rate;
};

static inline int notifier_from_errno(int err)
{
	return err ? 0x8000 | -err : NOTIFY_OK;
}

static inline int devm_clk_notifier_register(struct device *dev, struct clk *clk, struct notifier_block *nb)
{
	(void)dev;
	(void)clk;
	(void)nb;
	return 0;
}

#define ATOMIC_NOTIFIER_HEAD(name) int name

static inline int atomic_notifier_chain_register(int *head, struct notifier_block *nb)
{
	(void)head;
	(void)nb;
	return 0;
}

static inline int atomic_notifier_chain_unregister(int *head, struct notifier_block *nb)
{
	(void)head;
	(void)nb;
	return 0;
}

static inline int atomic_notifier_call_chain(int *head, unsigned long val, void *data)
{
	(void)head;
	(void)val;
	(void)data;
	return 0;
}

#define DEFINE_MUTEX(name) int name
#define DEFINE_SPINLOCK(name) spinlock_t name
#define spin_lock_irqsave(lock, flags) ((void)(lock), (flags) = 0)
#define spin_unlock_irqrestore(lock, flags) ((void)(lock), (void)(flags))

static inline void mutex_lock(int *lock)
{
	(*lock)++;
}

static inline void mutex_unlock(int *lock)
{
	(*lock)--;
}

void harness_udelay(unsigned long us);
#define udelay(us) harness_udelay(us)

static inline unsigned long long sched_clock(void)
{
	return 0;
}

static inline void *kzalloc(size_t size, int flags)
{
	(void)flags;
	return calloc(1, size);
}

static inline void *kcalloc(size_t n, size_t size, int flags)
{
	(void)flags;
	return calloc(n, size);
}

static inline void kfree(const void *ptr)
{
	free((void *)ptr);
}

void *devm_kzalloc(struct device *dev, size_t size, int flags);

u32 harness_readl(const volatile void *addr);
void harness_writel(u32 val, volatile void *addr);
void *harness_iomap(struct device_node *np);
void harness_iounmap(volatile void *addr);

#define readl(a) harness_readl(a)
#define writel(v, a) harness_writel((v), (a))

static inline void iounmap(volatile void *addr)
{
	harness_iounmap(addr);
}

int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val);
int regmap_write(struct regmap *map, unsigned int reg, unsigned int val);
int regmap_update_bits(struct regmap *map, unsigned int reg, unsigned int mask, unsigned int val);

static inline int regmap_set_bits(struct regmap *map, unsigned int reg, unsigned int bits)
{
	return regmap_update_bits(map, reg, bits, bits);
}

static inline int regmap_clear_bits(struct regmap *map, unsigned int reg, unsigned int bits)
{
	return regmap_update_bits(map, reg, bits, 0);
}

#define regmap_read_poll_timeout_atomic(map, addr, val, cond, delay_us, timeout_us)	\
	({ int __ret = regmap_read((map), (addr), &(val)); __ret ? __ret : ((cond) ? 0 : -ETIMEDOUT); })

struct regmap *syscon_node_to_regmap(struct device_node *np);
struct regmap *device_node_to_regmap(struct device_node *np);
struct regmap *syscon_regmap_lookup_by_phandle(struct device_node *np, const char *property);

int of_device_is_compatible(const struct device_node *np, const char *compatible);
struct device_node *of_find_compatible_node(struct device_node *from, const char *type,
					    const char *compatible);
struct device_node *of_parse_phandle(const struct device_node *np, const char *name, int index);

static inline void *of_iomap(struct device_node *np, int index)
{
	(void)index;
	return harness_iomap(np);
}

static inline void of_node_put(struct device_node *np)
{
	(void)np;
}

struct resource *platform_get_resource(struct platform_device *pdev, unsigned int type, unsigned int num);
void *devm_ioremap_resource(struct device *dev, struct resource *res);
void *devm_platform_ioremap_resource(struct platform_device *pdev, unsigned int index);

int devm_pm_runtime_enable(struct device *dev);
int pm_runtime_resume_and_get(struct device *dev);
int pm_runtime_put(struct device *dev);

struct clk *clk_register(struct device *dev, struct clk_hw *hw);
struct clk *clk_register_fixed_rate(struct device *dev, const char *name, const char *parent,
				    unsigned long flags, unsigned long rate);
struct clk *clk_register_fixed_factor(struct device *dev, const char *name, const char *parent,
				      unsigned long flags, unsigned int mult, unsigned int div);
struct clk *clk_register_divider(struct device *dev, const char *name, const char *parent,
				 unsigned long flags, void __iomem *reg, u8 shift, u8 width,
				 u8 divider_flags, spinlock_t *lock);
struct clk *clk_register_composite(struct device *dev, const char *name,
				   const char * const *parent_names, int num_parents,
				   struct clk_hw *mux_hw, const struct clk_ops *mux_ops,
				   struct clk_hw *rate_hw, const struct clk_ops *rate_ops,
				   struct clk_hw *gate_hw, const struct clk_ops *gate_ops,
				   unsigned long flags);
struct clk *of_clk_src_onecell_get(struct of_phandle_args *args, void *data);
int of_clk_add_provider(struct device_node *np,
			struct clk *(*get)(struct of_phandle_args *args, void *data), void *data);

int clk_hw_register(struct device *dev, struct clk_hw *hw);
void clk_hw_unregister(struct clk_hw *hw);
struct clk_hw *clk_hw_register_fixed_rate(struct device *dev, const char *name, const char *parent,
					  unsigned long flags, unsigned long rate);
void clk_hw_unregister_fixed_rate(struct clk_hw *hw);
struct clk_hw *clk_hw_register_fixed_factor(struct device *dev, const char *name, const char *parent,
					    unsigned long flags, unsigned int mult, unsigned int div);
void clk_hw_unregister_fixed_factor(struct clk_hw *hw);
struct clk_hw *clk_hw_register_divider(struct device *dev, const char *name, const char *parent,
				       unsigned long flags, void __iomem *reg, u8 shift, u8 width,
				       u8 divider_flags, spinlock_t *lock);
void clk_hw_unregister_divider(struct clk_hw *hw);
struct clk_hw *clk_hw_register_composite(struct device *dev, const char *name,
					 const char * const *parent_names, int num_parents,
					 struct clk_hw *mux_hw, const struct clk_ops *mux_ops,
					 struct clk_hw *rate_hw, const struct clk_ops *rate_ops,
					 struct clk_hw *gate_hw, const struct clk_ops *gate_ops,
					 unsigned long flags);
void clk_hw_unregister_composite(struct clk_hw *hw);
struct clk_hw *of_clk_hw_onecell_get(struct of_phandle_args *args, void *data);
int of_clk_add_hw_provider(struct device_node *np,
			   struct clk_hw *(*get)(struct of_phandle_args *args, void *data), void *data);
void of_clk_del_provider(struct device_node *np);

const char *clk_hw_get_name(const struct clk_hw *hw);
struct clk_hw *__clk_get_hw(struct clk *clk);
int __clk_mux_determine_rate(struct clk_hw *hw, struct clk_rate_request *req);
int clk_mux_determine_rate_flags(struct clk_hw *hw, struct clk_rate_request *req, unsigned long flags);

struct mtk_clk_rst_desc;
int mtk_register_reset_controller_with_dev(struct device *dev, const struct mtk_clk_rst_desc *desc);

struct reset_controller_dev {
	int nr_resets;
};

static inline int ffs_stub(int x)
{
	return __builtin_ffs(x);
}
#define ffs(x) ffs_stub(x)

#endif
