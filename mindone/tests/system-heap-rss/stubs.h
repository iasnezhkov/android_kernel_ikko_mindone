/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef unsigned long long u64;
typedef long ssize_t;
typedef long loff_t;

#define __user
#define GFP_KERNEL 0
#define PIDTYPE_PID 0

struct mutex { int locked; };
#define DEFINE_MUTEX(name) struct mutex name = { 0 }
static inline void mutex_lock(struct mutex *m) { assert(!m->locked); m->locked = 1; }
static inline void mutex_unlock(struct mutex *m) { assert(m->locked); m->locked = 0; }

static inline u64 div_u64(u64 dividend, u32 divisor) { return dividend / divisor; }
static inline void *kcalloc(size_t n, size_t sz, int gfp) { (void)gfp; return calloc(n, sz); }
static inline void kfree(const void *p) { free((void *)p); }

static inline bool strstarts(const char *str, const char *prefix)
{
	return strncmp(str, prefix, strlen(prefix)) == 0;
}

static inline char *strim(char *s)
{
	size_t len;

	while (*s && isspace((unsigned char)*s))
		s++;
	len = strlen(s);
	while (len > 0 && isspace((unsigned char)s[len - 1]))
		s[--len] = '\0';
	return s;
}

static inline int kstrtoint(const char *s, unsigned int base, int *res)
{
	char *end = NULL;
	long v;

	if (!s || !*s)
		return -EINVAL;
	errno = 0;
	v = strtol(s, &end, (int)base);
	if (errno || !end || *end != '\0')
		return -EINVAL;
	*res = (int)v;
	return 0;
}

static inline unsigned long copy_from_user(void *to, const void *from, unsigned long n)
{
	memcpy(to, from, n);
	return 0;
}

struct dma_buf {
	size_t size;
};

struct file {
	int is_dmabuf;
	void *private_data;
	long count;
};

static inline int is_dma_buf_file(struct file *f) { return f->is_dmabuf; }
static inline long file_count(struct file *f) { return f->count; }

struct files_struct {
	struct file *fds[16];
	unsigned int max_fds;
};

struct task_struct {
	int pid;
	int locked;
	int refs;
	struct files_struct *files;
};

struct pid {
	struct task_struct *task;
	int refs;
};

struct inode { int dummy; };
struct proc_dir_entry {
	char name[32];
	unsigned int mode;
	unsigned int uid;
	unsigned int gid;
};

struct seq_file {
	char buf[2048];
	size_t len;
};

static int seq_printf(struct seq_file *s, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(s->buf + s->len, sizeof(s->buf) - s->len, fmt, ap);
	va_end(ap);
	if (n > 0)
		s->len += (size_t)n;
	return 0;
}

struct proc_ops {
	int (*proc_open)(struct inode *, struct file *);
	ssize_t (*proc_read)(struct file *, char *, size_t, loff_t *);
	ssize_t (*proc_write)(struct file *, const char *, size_t, loff_t *);
	loff_t (*proc_lseek)(struct file *, loff_t, int);
	int (*proc_release)(struct inode *, struct file *);
};

static int single_open(struct file *f, int (*show)(struct seq_file *, void *), void *d)
{
	(void)f; (void)show; (void)d;
	return 0;
}
static ssize_t seq_read(struct file *f, char *b, size_t c, loff_t *p) { (void)f; (void)b; (void)c; (void)p; return 0; }
static loff_t seq_lseek(struct file *f, loff_t o, int w) { (void)f; (void)o; (void)w; return 0; }
static int single_release(struct inode *i, struct file *f) { (void)i; (void)f; return 0; }
static void *pde_data(const struct inode *i) { (void)i; return NULL; }

static int init_user_ns;
typedef unsigned int kuid_t;
typedef unsigned int kgid_t;
static inline kuid_t make_kuid(void *ns, unsigned int v) { (void)ns; return v; }
static inline kgid_t make_kgid(void *ns, unsigned int v) { (void)ns; return v; }

static struct proc_dir_entry fake_root;
static struct proc_dir_entry fake_entry;
static int proc_removed;

static struct proc_dir_entry *proc_mkdir(const char *name, struct proc_dir_entry *parent)
{
	(void)parent;
	snprintf(fake_root.name, sizeof(fake_root.name), "%s", name);
	return &fake_root;
}

static struct proc_dir_entry *proc_create_data(const char *name, unsigned int mode,
					       struct proc_dir_entry *parent,
					       const struct proc_ops *ops, void *data)
{
	(void)parent; (void)ops; (void)data;
	snprintf(fake_entry.name, sizeof(fake_entry.name), "%s", name);
	fake_entry.mode = mode;
	return &fake_entry;
}

static void proc_set_user(struct proc_dir_entry *de, kuid_t uid, kgid_t gid)
{
	de->uid = uid;
	de->gid = gid;
}

static void proc_remove(struct proc_dir_entry *de)
{
	(void)de;
	proc_removed++;
}

static struct task_struct *sim_tasks[4];
static struct pid sim_pids[4];
static int sim_task_count;

static struct pid *find_get_pid(int nr)
{
	for (int i = 0; i < sim_task_count; i++) {
		if (sim_tasks[i]->pid == nr) {
			sim_pids[i].task = sim_tasks[i];
			sim_pids[i].refs++;
			return &sim_pids[i];
		}
	}
	return NULL;
}

static struct task_struct *get_pid_task(struct pid *pid, int type)
{
	(void)type;
	if (!pid || !pid->task)
		return NULL;
	pid->task->refs++;
	return pid->task;
}

static void put_pid(struct pid *pid)
{
	if (!pid)
		return;
	assert(pid->refs > 0);
	pid->refs--;
}

static void put_task_struct(struct task_struct *t)
{
	assert(t->refs > 0);
	t->refs--;
}

static void task_lock(struct task_struct *t)
{
	assert(!t->locked);
	t->locked = 1;
}

static void task_unlock(struct task_struct *t)
{
	assert(t->locked);
	t->locked = 0;
}

static int iterate_fd(struct files_struct *files, unsigned int n,
		      int (*f)(const void *, struct file *, unsigned int),
		      const void *p)
{
	int res = 0;

	if (!files)
		return 0;
	for (; n < files->max_fds; n++) {
		if (!files->fds[n])
			continue;
		res = f(p, files->fds[n], n);
		if (res)
			break;
	}
	return res;
}
