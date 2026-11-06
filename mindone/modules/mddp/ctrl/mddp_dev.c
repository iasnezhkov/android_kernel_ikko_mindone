/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 MediaTek Inc. */

#include <linux/fs.h>
#include <linux/compat.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/rtc.h>

#include "mddp_ctrl.h"
#include "mddp_debug.h"
#include "mddp_dev.h"
#include "mddp_if.h"
#include "mddp_sm.h"
#include "mddp_filter.h"
#include <linux/in6.h>

#define MDDP_DEV_NAME                   "mddp"

struct mddp_dev_rb_t {
	struct mddp_dev_rb_t   *next;
	struct mddp_dev_rb_t   *prev;

	uint32_t                rb_len;
	void                   *rb_data;
};

struct mddp_dev_rb_head_t {
	struct mddp_dev_rb_t           *next;
	struct mddp_dev_rb_t           *prev;

	uint32_t                        cnt;
	spinlock_t                      locker;
	wait_queue_head_t               read_wq;
};

#define MDDP_DEV_CLONE_COMM_HDR(_rsp, _req, _status) \
	do { \
		(_rsp)->mcode = (_req)->mcode; \
		(_rsp)->status = _status; \
		(_rsp)->app_type = (_req)->app_type; \
		(_rsp)->msg = (_req)->msg; \
	} while (0)

#define MDDP_SET_BUF_TERMIN(_buf, _len) \
	do { \
		_len = strlen(_buf); \
		if (_len > 1 && _buf[_len-1] == '\n') \
			_buf[_len-1] = '\0'; \
	} while (0)

#define MDDP_DSTATE_IS_VALID_ID(_id) (_id < MDDP_DSTATE_ID_NUM)
#define MDDP_DSTATE_IS_ACTIVATED() (mddp_dstate_activated_s)
#define MDDP_MD_LOG_IS_ACTIVATED() (mddp_md_log_activated_s)

static int32_t mddp_dev_open(struct inode *inode,
	struct file *file);
static int32_t mddp_dev_close(struct inode *inode,
	struct file *file);
static ssize_t mddp_dev_read(struct file *file,
	char *buf, size_t count, loff_t *ppos);
static ssize_t mddp_dev_write(struct file *file,
	const char __user *buf, size_t count, loff_t *ppos);
static long mddp_dev_ioctl(struct file *file,
	unsigned int cmd, unsigned long arg);
#if IS_ENABLED(CONFIG_COMPAT)
static long mddp_dev_compat_ioctl(struct file *filp,
	unsigned int cmd, unsigned long arg);
#endif
static unsigned int mddp_dev_poll(struct file *fp,
	struct poll_table_struct *poll);

static const struct file_operations mddp_dev_fops = {
	.owner          = THIS_MODULE,
	.open           = &mddp_dev_open,
	.read           = &mddp_dev_read,
	.write          = &mddp_dev_write,
	.release        = &mddp_dev_close,
	.unlocked_ioctl = &mddp_dev_ioctl,
#if IS_ENABLED(CONFIG_COMPAT)
	.compat_ioctl   = &mddp_dev_compat_ioctl,
#endif
	.poll           = &mddp_dev_poll,
};

static atomic_t mddp_dev_open_ref_cnt_s;
static atomic_t mddp_response_overflow = ATOMIC_INIT(0);
static struct mddp_dev_rb_head_t mddp_hidl_rb_head_s;
static DEFINE_MUTEX(mddp_dev_read_lock);
static DEFINE_MUTEX(mddp_dev_ioctl_lock);
static struct mddp_dev_rb_head_t mddp_dstate_rb_head_s;
static struct mddp_dev_rb_head_t mddp_md_log_rb_head_s;
uint32_t dstate_buffer_size;
uint32_t md_log_buffer_size;

#define MDDP_CMCMD_RSP_CNT (MDDP_CMCMD_RSP_END - MDDP_CMCMD_RSP_BEGIN)
static enum mddp_dev_evt_type_e
mddp_dev_rsp_status_mapping_s[MDDP_CMCMD_RSP_CNT][2] =  {

{MDDP_DEV_EVT_STOPPED_ERROR,        MDDP_DEV_EVT_SUPPORT_AVAILABLE},
{MDDP_DEV_EVT_NONE,                 MDDP_DEV_EVT_NONE},
{MDDP_DEV_EVT_STOPPED_ERROR,        MDDP_DEV_EVT_STARTED},
{MDDP_DEV_EVT_STOPPED_ERROR,        MDDP_DEV_EVT_STOPPED_UNSUPPORTED},
{MDDP_DEV_EVT_STOPPED_LIMIT_REACHED, MDDP_DEV_EVT_STOPPED_LIMIT_REACHED},
{MDDP_DEV_EVT_CONNECT_UPDATE,       MDDP_DEV_EVT_CONNECT_UPDATE},
{MDDP_DEV_EVT_WARNING_REACHED,      MDDP_DEV_EVT_WARNING_REACHED},
};
#undef MDDP_CMCMD_RSP_CNT

uint32_t mddp_debug_log_class_s = MDDP_LC_ALL;
uint32_t mddp_debug_log_level_s = MDDP_LL_DEFAULT;
static bool mddp_dstate_activated_s;
static bool mddp_md_log_activated_s;
bool mddp_connection_base_activated_s;

static struct mddp_dev_rb_t *mddp_query_dstate(
		struct mddp_dev_rb_head_t *list, uint32_t seq);
static struct mddp_dev_rb_t *mddp_dequeue_dstate(
		struct mddp_dev_rb_head_t *list);
static void mddp_clear_dstate(struct mddp_dev_rb_head_t *list);

static ssize_t
debug_log_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE,
			"debug_log_class(%x), debug_log_level(%x)\n",
			mddp_debug_log_class_s, mddp_debug_log_level_s);
}

static ssize_t
debug_log_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	uint32_t                lv;
	uint32_t                class;
	unsigned long           value;

	if (!kstrtoul(buf, 0, &value)) {
		class = (value & MDDP_DEBUG_LOG_CLASS_MASK) >> 4;
		if (MDDP_IS_VALID_LOG_CLASS(class))
			mddp_debug_log_class_s = class;

		lv = value & MDDP_DEBUG_LOG_LV_MASK;
		if (MDDP_IS_VALID_LOG_LEVEL(lv))
			mddp_debug_log_level_s = lv;
	}
	return count;
}
static DEVICE_ATTR_RW(debug_log);

static ssize_t
version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "MDDP version(%d)\n",
				 __MDDP_VERSION__);
}
static DEVICE_ATTR_RO(version);

static ssize_t
state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mddp_app_t          *app;
	uint32_t                    type;
	uint8_t                     idx;
	uint32_t                    ret_num = 0;
	uint32_t                    seq = 0;
	struct mddp_dev_rb_head_t  *list = &mddp_dstate_rb_head_s;
	struct mddp_dev_rb_t       *entry;

	for (idx = 0; idx < MDDP_MOD_CNT; idx++) {
		type = mddp_sm_module_list_s[idx];
		app = mddp_get_app_inst(type);
		ret_num += scnprintf(buf + ret_num, PAGE_SIZE - ret_num,
					"type(%d), state(%d)\n",
					app->type, app->state);
		ret_num += scnprintf(buf + ret_num, PAGE_SIZE - ret_num,
				"drv_reg(%d), feature(%d)\n",
				app->drv_reg, (app->feature & 0x7));
		ret_num += scnprintf(buf + ret_num, PAGE_SIZE - ret_num,
				"abnormal(%x), reset_cnt(%d)\n",
				app->abnormal_flags, app->reset_cnt);
		if (app->feature & MDDP_FEATURE_NEW_INFO) {
			ret_num += scnprintf(buf + ret_num, PAGE_SIZE - ret_num,
					"MD(%d.%d)\n",
					app->mddp_feat.major_version, app->mddp_feat.minor_version);
			ret_num += scnprintf(buf + ret_num, PAGE_SIZE - ret_num,
					"common_feat(%x), wfc_feat(%x), wh_feat(%x)\n",
					app->mddp_feat.common, app->mddp_feat.wfc,
					app->mddp_feat.wh);
		}

		if (ret_num <= 0)
			return scnprintf(buf, PAGE_SIZE,
				"%s: Failed to fill-in data!\n", __func__);
	}

	spin_lock(&list->locker);
	entry = mddp_query_dstate(list, seq);
	while (entry) {
		if (entry->rb_data) {
			ret_num += scnprintf(buf + ret_num, PAGE_SIZE - ret_num,
					"%s\n",
					((struct mddp_dstate_t *)entry->rb_data)->str);
		}
		seq += 1;
		entry = mddp_query_dstate(list, seq);
	}
	spin_unlock(&list->locker);

	return ret_num;
}

static ssize_t
state_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	unsigned long           value;

	if (!kstrtoul(buf, 0, &value)) {
		if (value == MDDP_DETAILED_STATE_ENABLE) {
			mddp_dstate_activated_s = true;
			mddp_enqueue_dstate(MDDP_DSTATE_ID_START);
		} else  if (value == MDDP_DETAILED_STATE_DISABLE) {
			mddp_enqueue_dstate(MDDP_DSTATE_ID_STOP);
			mddp_dstate_activated_s = false;
		}
	}

	return count;
}
static DEVICE_ATTR_RW(state);

static ssize_t
wh_statistic_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mddp_app_t      *app;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	if (app->sysfs_callback)
		return app->sysfs_callback(app,
				MDDP_SYSFS_CMD_STATISTIC_READ, buf, 0);

	return scnprintf(buf, PAGE_SIZE,
				"Cannot change WH mode, mddp-wh config(%d)\n",
				app->is_config);
}
static DEVICE_ATTR_RO(wh_statistic);

#ifdef MDDP_EM_SUPPORT
#define EM_CMD_BUF_SZ 512
static uint8_t em_cmd_buf[EM_CMD_BUF_SZ];
static int32_t em_cmd_app = -1;
static int32_t em_cmd_status;

#define EM_CMD_RESET() (em_cmd_app = -1)

static ssize_t
em_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "status:%d, cmd_buf:%s\n",
			em_cmd_status, em_cmd_buf);
}

static ssize_t
em_test_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct mddp_app_t      *app;
	const char             *delim = " ";
	char                   *token;
	char                   *strsep_buf_p;
	unsigned int            str_len;

	str_len = strlen(buf);
	snprintf(em_cmd_buf, EM_CMD_BUF_SZ, "%.*s",
			(int)min(count, sizeof(em_cmd_buf) - 1), buf);
	strsep_buf_p = em_cmd_buf;
	MDDP_SET_BUF_TERMIN(em_cmd_buf, str_len);

	token = strsep(&strsep_buf_p, delim);
	if (token == NULL) {
		em_cmd_status = -EINVAL;
		goto input_param_error;
	}

	if (kstrtoint(token, 10, &em_cmd_app))
		return -EINVAL;

	if (em_cmd_app != MDDP_APP_TYPE_WH) {
		em_cmd_status = -EPERM;
		goto not_support_error;
	}

	app = mddp_get_app_inst(em_cmd_app);
	if (app->sysfs_callback) {

		snprintf(em_cmd_buf, EM_CMD_BUF_SZ, "%.*s",
				(int)min(count, sizeof(em_cmd_buf) - 1), buf);
		MDDP_SET_BUF_TERMIN(em_cmd_buf, str_len);
		em_cmd_status = app->sysfs_callback(app,
						MDDP_SYSFS_EM_CMD_TEST_WRITE,
						em_cmd_buf, strlen(em_cmd_buf));
		return em_cmd_status < 0 ? em_cmd_status : count;
	}

	em_cmd_status = -ERANGE;
	snprintf(em_cmd_buf, EM_CMD_BUF_SZ, "%.*s",
			(int)min(count, sizeof(em_cmd_buf) - 1), buf);
	return count;

input_param_error:
not_support_error:
	EM_CMD_RESET();
	snprintf(em_cmd_buf, EM_CMD_BUF_SZ, "%.*s",
			(int)min(count, sizeof(em_cmd_buf) - 1), buf);
	return count;
}
static DEVICE_ATTR_RW(em_test);
#endif

static ssize_t
md_log_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	uint32_t                    ret_num = 0;
	uint32_t                    seq = 0;
	struct mddp_dev_rb_head_t  *list = &mddp_md_log_rb_head_s;
	struct mddp_dev_rb_t       *entry;

	spin_lock(&list->locker);
	entry = mddp_query_dstate(list, seq);
	while (entry) {
		if (entry->rb_data) {
			ret_num += scnprintf(buf + ret_num, PAGE_SIZE - ret_num,
					"%s\n",	((struct mddp_md_log_t *)entry->rb_data)->str);
		}

		seq += 1;
		entry = mddp_query_dstate(list, seq);
	}
	spin_unlock(&list->locker);

	return ret_num;
}

static ssize_t
md_log_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	unsigned long           value;

	if (!kstrtoul(buf, 0, &value)) {
		if (value == MDDP_MD_LOG_ENABLE) {
			mddp_md_log_activated_s = true;
			mddp_enqueue_md_log(MDDP_MD_LOG_ID_START);
		} else  if (value == MDDP_MD_LOG_DISABLE) {
			mddp_enqueue_md_log(MDDP_MD_LOG_ID_STOP);
			mddp_md_log_activated_s = false;
		}
	}

	return count;
}
static DEVICE_ATTR_RW(md_log);

static ssize_t
mddp_method_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE,"MDDP is %s base now\n", mddp_connection_base_activated_s? "Connection":"Packet");
}

static ssize_t
mddp_method_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	unsigned long value;

	if (!kstrtoul(buf, 0, &value)) {
		if (value == MDDP_CONNECTION_BASE_ENABLE)
			mddp_connection_base_activated_s = true;
		else if (value == MDDP_CONNECTION_BASE_DISABLE)
			mddp_connection_base_activated_s = false;
	}

	return count;
}
static DEVICE_ATTR_RW(mddp_method);

static struct attribute *mddp_attrs[] = {
	&dev_attr_version.attr,
	&dev_attr_state.attr,
	&dev_attr_wh_statistic.attr,
	&dev_attr_debug_log.attr,
	&dev_attr_md_log.attr,
#ifdef MDDP_EM_SUPPORT
	&dev_attr_em_test.attr,
#endif
	&dev_attr_mddp_method.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mddp);

static inline void __mddp_dev_insert(struct mddp_dev_rb_t *new,
		struct mddp_dev_rb_t *prev,
		struct mddp_dev_rb_t *next,
		struct mddp_dev_rb_head_t *list)
{
	new->next = next;
	new->prev = prev;
	next->prev = prev->next = new;
	list->cnt++;
}

static inline void __mddp_dev_rb_unlink(struct mddp_dev_rb_t *entry,
		struct mddp_dev_rb_head_t *list)
{
	struct mddp_dev_rb_t           *next;
	struct mddp_dev_rb_t           *prev;

	list->cnt--;
	next = entry->next;
	prev = entry->prev;
	entry->next = entry->prev = NULL;
	next->prev = prev;
	prev->next = next;
}

static void mddp_dev_rb_enqueue_tail(struct mddp_dev_rb_head_t *list,
		struct mddp_dev_rb_t *new)
{
	unsigned long flags;

	spin_lock(&list->locker);
	if (list == &mddp_hidl_rb_head_s && list->cnt >= 1024) {
		kfree(new->rb_data);
		kfree(new);
		atomic_set(&mddp_response_overflow, 1);
	} else {
		__mddp_dev_insert(new, list->prev, (struct mddp_dev_rb_t *)list, list);
	}
	spin_unlock(&list->locker);

	spin_lock_irqsave(&list->read_wq.lock, flags);
	wake_up_all_locked(&list->read_wq);
	spin_unlock_irqrestore(&list->read_wq.lock, flags);
}

static struct mddp_dev_rb_t *mddp_dev_rb_peek(
		struct mddp_dev_rb_head_t *list)
{
	struct mddp_dev_rb_t *entry;

	entry = list->next;

	if (entry == (struct mddp_dev_rb_t *)list)
		entry = NULL;

	return entry;
}

static struct mddp_dev_rb_t *mddp_dev_rb_query(
		struct mddp_dev_rb_head_t *list, uint32_t seq)
{
	struct mddp_dev_rb_t     *entry = NULL;
	uint32_t                  cnt = 0;

	entry = mddp_dev_rb_peek(list);
	while (entry) {
		if (seq == cnt)
			break;

		entry = entry->next;
		if (entry == (struct mddp_dev_rb_t *)list) {
			entry = NULL;
			break;
		}

		cnt += 1;
	}

	return entry;
}

static struct mddp_dev_rb_t *mddp_dev_rb_dequeue(
		struct mddp_dev_rb_head_t *list)
{
	struct mddp_dev_rb_t     *entry = NULL;

	entry = mddp_dev_rb_peek(list);
	if (entry)
		__mddp_dev_rb_unlink(entry, list);

	return entry;
}

void mddp_dev_purge_quota_notifications(void)
{
	struct mddp_dev_rb_head_t *list = &mddp_hidl_rb_head_s;
	struct mddp_dev_rb_t *entry, *next;
	struct mddp_dev_rsp_common_t *rsp;

	mutex_lock(&mddp_dev_read_lock);
	spin_lock(&list->locker);
	for (entry = list->next; entry != (struct mddp_dev_rb_t *)list;
	     entry = next) {
		next = entry->next;
		rsp = entry->rb_data;
		if (rsp->msg != MDDP_CMCMD_LIMIT_IND &&
		    rsp->msg != MDDP_CMCMD_WARNING_IND)
			continue;
		__mddp_dev_rb_unlink(entry, list);
		kfree(entry->rb_data);
		kfree(entry);
	}
	spin_unlock(&list->locker);
	mutex_unlock(&mddp_dev_read_lock);
}

static bool mddp_dev_rb_queue_empty(struct mddp_dev_rb_head_t *list)
{
	return READ_ONCE(list->next) == (struct mddp_dev_rb_t *)list;
}

static struct mddp_dev_rb_t *mddp_query_dstate(
		struct mddp_dev_rb_head_t *list, uint32_t seq)
{
	return mddp_dev_rb_query(list, seq);
}

static struct mddp_dev_rb_t *mddp_dequeue_dstate(
		struct mddp_dev_rb_head_t *list)
{
	return mddp_dev_rb_dequeue(list);
}

static void mddp_clear_dstate(
		struct mddp_dev_rb_head_t *list)
{
	struct mddp_dev_rb_t           *entry;

	spin_lock(&list->locker);
	entry = mddp_dequeue_dstate(list);
	while (entry) {
		if (list != &mddp_hidl_rb_head_s) {
			kfree(((struct mddp_md_log_t *)entry->rb_data)->str);
			((struct mddp_md_log_t *)entry->rb_data)->str = NULL;
		}
		kfree(entry->rb_data);
		entry->rb_data = NULL;
		kfree(entry);
		entry = NULL;

		entry = mddp_dequeue_dstate(list);
	}
	spin_unlock(&list->locker);
}

void mddp_dev_list_init(struct mddp_dev_rb_head_t *list)
{
	spin_lock_init(&list->locker);
	list->cnt = 0;
	list->prev = list->next = (struct mddp_dev_rb_t *)list;

	init_waitqueue_head(&list->read_wq);
}

struct miscdevice mddp_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MDDP_DEV_NAME,
	.fops = &mddp_dev_fops,
	.groups = mddp_groups,
};

void mddp_dev_prepare(void)
{
	atomic_set(&mddp_dev_open_ref_cnt_s, 0);
	mddp_dev_list_init(&mddp_hidl_rb_head_s);
	mddp_dev_list_init(&mddp_dstate_rb_head_s);
	mddp_dev_list_init(&mddp_md_log_rb_head_s);
	mddp_dstate_activated_s = false;
}

int32_t mddp_dev_init(void)
{
	return misc_register(&mddp_dev);
}

void mddp_dev_unpublish(void)
{
	misc_deregister(&mddp_dev);
}

void mddp_dev_uninit(void)
{
	dstate_buffer_size = 0;
	md_log_buffer_size = 0;
	mddp_clear_dstate(&mddp_hidl_rb_head_s);
	mddp_clear_dstate(&mddp_dstate_rb_head_s);
	mddp_clear_dstate(&mddp_md_log_rb_head_s);
}

void mddp_dev_response(enum mddp_app_type_e type,
		enum mddp_ctrl_msg_e msg, bool is_success,
		uint8_t *data, uint32_t data_len)
{
	struct mddp_dev_rb_head_t      *list = &mddp_hidl_rb_head_s;
	struct mddp_dev_rb_t           *entry;
	struct mddp_dev_rsp_common_t   *dev_rsp;
	uint16_t                        status;
	uint32_t                        rsp_idx;

	if (msg < MDDP_CMCMD_RSP_BEGIN || msg >= MDDP_CMCMD_RSP_END) {
		MDDP_C_LOG(MDDP_LL_NOTICE,
				"%s: invalid rsp msg(%d) in type(%d)!\n",
				__func__, msg, type);
		return;
	}

	rsp_idx = (msg - MDDP_CMCMD_RSP_BEGIN);
	status = mddp_dev_rsp_status_mapping_s[rsp_idx][is_success];

	if (status == MDDP_DEV_EVT_NONE) {

		MDDP_C_LOG(MDDP_LL_NOTICE,
				"%s: No RSP, type(%d), msg(%d), is_success(%d).\n",
				__func__, type, msg, is_success);
		return;
	}

	dev_rsp = kmalloc(sizeof(struct mddp_dev_rsp_common_t) + data_len,
				GFP_ATOMIC);
	if (unlikely(!dev_rsp))
		return;

	dev_rsp->mcode = MDDP_CTRL_MSG_MCODE;
	dev_rsp->status = status;
	dev_rsp->app_type = type;
	dev_rsp->msg = msg;
	dev_rsp->data_len = data_len;
	if (data_len > 0)
		memcpy(dev_rsp->data, data, data_len);

	entry = kmalloc(sizeof(struct mddp_dev_rb_t), GFP_ATOMIC);
	if (unlikely(!entry)) {
		kfree(dev_rsp);
		return;
	}

	entry->rb_len = sizeof(struct mddp_dev_rsp_common_t) + data_len;
	entry->rb_data = dev_rsp;
	mddp_dev_rb_enqueue_tail(list, entry);
}

#define MDDP_CURR_TIME_STR_SZ 32
#define MDDP_DSTATE_MAX_BUF_SZ 4000

void mddp_enqueue_dstate(enum mddp_dstate_id_e id, ...)
{
	struct mddp_dev_rb_head_t  *list = &mddp_dstate_rb_head_s;
	struct mddp_dev_rb_t       *entry;
	struct mddp_dstate_t       *dstat;
	struct rtc_time             rt;
	struct timespec64           ts;
	char                        curr_time_str[MDDP_CURR_TIME_STR_SZ];
	va_list                     ap;
	int                         ip;
	int                         port;
	unsigned long long          rx;
	unsigned long long          tx;

	if (!MDDP_DSTATE_IS_VALID_ID(id) || !MDDP_DSTATE_IS_ACTIVATED())
		return;

	dstat = kzalloc(sizeof(struct mddp_dstate_t), GFP_ATOMIC);
	if (unlikely(!dstat))
		return;

	dstat->str = kzalloc(MDDP_DSTATE_STR_SZ, GFP_ATOMIC);
	if (unlikely(!dstat->str)) {
		kfree(dstat);
		return;
	}

	entry = kzalloc(sizeof(struct mddp_dev_rb_t), GFP_ATOMIC);
	if (unlikely(!entry)) {
		kfree(dstat->str);
		kfree(dstat);
		return;
	}

	ktime_get_real_ts64(&ts);
	rtc_time64_to_tm(ts.tv_sec, &rt);
	snprintf(curr_time_str, MDDP_CURR_TIME_STR_SZ,
			"%d%02d%02d %02d:%02d:%02d.%09ld UTC",
			rt.tm_year + 1900, rt.tm_mon + 1, rt.tm_mday,
			rt.tm_hour, rt.tm_min, rt.tm_sec, ts.tv_nsec);

	dstat->id = id;
	va_start(ap, id);
	switch (id) {
	case MDDP_DSTATE_ID_START:
		dstate_buffer_size = 0;
		mddp_clear_dstate(list);
		snprintf(dstat->str, MDDP_DSTATE_STR_SZ,
				mddp_dstate_temp_s[id].str, curr_time_str);
		break;
	case MDDP_DSTATE_ID_STOP:
	case MDDP_DSTATE_ID_SUSPEND_TAG:
	case MDDP_DSTATE_ID_RESUME_TAG:
		snprintf(dstat->str, MDDP_DSTATE_STR_SZ,
				mddp_dstate_temp_s[id].str, curr_time_str);
		break;
	case MDDP_DSTATE_ID_NEW_TAG:
		ip = va_arg(ap, int);
		port = va_arg(ap, int);
		snprintf(dstat->str, MDDP_DSTATE_STR_SZ,
				mddp_dstate_temp_s[id].str, curr_time_str,
				ip, port);
		break;
	case MDDP_DSTATE_ID_GET_OFFLOAD_STATS:
		rx = va_arg(ap, unsigned long long);
		tx = va_arg(ap, unsigned long long);
		snprintf(dstat->str, MDDP_DSTATE_STR_SZ,
				mddp_dstate_temp_s[id].str, curr_time_str,
				rx, tx);
		break;
	default:
		break;
	}

	va_end(ap);
	entry->rb_len = MDDP_DSTATE_STR_SZ;
	entry->rb_data = dstat;
	mddp_dev_rb_enqueue_tail(list, entry);
	dstate_buffer_size += entry->rb_len;
	while (dstate_buffer_size > MDDP_DSTATE_MAX_BUF_SZ) {
		spin_lock(&list->locker);
		entry = mddp_dev_rb_dequeue(list);
		if (entry) {
			dstate_buffer_size -= entry->rb_len;
			kfree(((struct mddp_dstate_t *)entry->rb_data)->str);
			((struct mddp_dstate_t *)entry->rb_data)->str = NULL;
			kfree(entry->rb_data);
			entry->rb_data = NULL;
			kfree(entry);
			entry = NULL;
		}
		spin_unlock(&list->locker);
	}
}

void mddp_enqueue_md_log(enum mddp_md_log_id_e id, ...)
{
	struct mddp_dev_rb_head_t  *list = &mddp_md_log_rb_head_s;
	struct mddp_dev_rb_t       *entry;
	struct mddp_md_log_t       *log;
	struct rtc_time             rt;
	struct timespec64           ts;
	char                        curr_time_str[MDDP_CURR_TIME_STR_SZ];
	va_list                     ap;
	char						*mdfpm_log_str;
	uint32_t					strSize = 0;

	if (!MDDP_MD_LOG_IS_ACTIVATED())
		return;

	log = kzalloc(sizeof(struct mddp_md_log_t), GFP_ATOMIC);
	if (unlikely(!log))
		return;

	entry = kzalloc(sizeof(struct mddp_dev_rb_t), GFP_ATOMIC);
	if (unlikely(!entry)) {
		kfree(log);
		return;
	}

	ktime_get_real_ts64(&ts);
	rtc_time64_to_tm(ts.tv_sec, &rt);
	snprintf(curr_time_str, MDDP_CURR_TIME_STR_SZ,
			"%d%02d%02d %02d:%02d:%02d.%09ld UTC",
			rt.tm_year + 1900, rt.tm_mon + 1, rt.tm_mday,
			rt.tm_hour, rt.tm_min, rt.tm_sec, ts.tv_nsec);

	log->id = id;
	va_start(ap, id);

	switch (id) {
	case MDDP_MD_LOG_ID_START:
		log->str = kzalloc(MD_LOG_START_SZ, GFP_ATOMIC);
		if (unlikely(!(log->str))) {
			va_end(ap);
			kfree(log);
			kfree(entry);
			return;
		}
		md_log_buffer_size = 0;
		mddp_clear_dstate(list);
		snprintf(log->str, MDDP_DSTATE_STR_SZ,
				mddp_md_log_temp_s[id].str, curr_time_str);
		break;
	case MDDP_MD_LOG_ID_STOP:
		log->str = kzalloc(MD_LOG_START_SZ, GFP_ATOMIC);
		if (unlikely(!(log->str))) {
			va_end(ap);
			kfree(log);
			kfree(entry);
			return;
		}
		snprintf(log->str, MDDP_DSTATE_STR_SZ,
				mddp_md_log_temp_s[id].str, curr_time_str);
		break;
	case MDDP_MD_LOG_ID_GET_LOG:
		strSize = va_arg(ap, uint32_t);
		log->str = kzalloc(strSize + MDDP_CURR_TIME_STR_SZ + 3, GFP_ATOMIC);
		if (unlikely(!(log->str))) {
			va_end(ap);
			kfree(log);
			kfree(entry);
			return;
		}
		mdfpm_log_str = va_arg(ap, char*);
		snprintf(log->str, strSize + MDDP_CURR_TIME_STR_SZ + 3,
				mddp_md_log_temp_s[id].str,	curr_time_str, mdfpm_log_str);
		break;
	default:
		break;
	}

	va_end(ap);
	entry->rb_len = strSize + MDDP_CURR_TIME_STR_SZ + 3;
	entry->rb_data = log;
	mddp_dev_rb_enqueue_tail(list, entry);
	md_log_buffer_size += entry->rb_len;
	while (md_log_buffer_size > MDDP_DSTATE_MAX_BUF_SZ) {
		spin_lock(&list->locker);
		entry = mddp_dev_rb_dequeue(list);
		if (entry) {
			md_log_buffer_size -= entry->rb_len;
			kfree(((struct mddp_md_log_t *)entry->rb_data)->str);
			((struct mddp_md_log_t *)entry->rb_data)->str = NULL;
			kfree(entry->rb_data);
			entry->rb_data = NULL;
			kfree(entry);
			entry = NULL;
		}
		spin_unlock(&list->locker);
	}
}

static int32_t mddp_dev_open(struct inode *inode, struct file *file)
{
	MDDP_C_LOG(MDDP_LL_INFO, "%s: IOCTL dev open.\n", __func__);

	if (atomic_cmpxchg(&mddp_dev_open_ref_cnt_s, 0, 1))
		return -EBUSY;

	return 0;
}

static int32_t mddp_dev_close(struct inode *inode, struct file *file)
{
	MDDP_C_LOG(MDDP_LL_INFO, "%s: IOCTL dev close.\n", __func__);

	mutex_lock(&mddp_dev_ioctl_lock);
	mddp_on_disable(MDDP_APP_TYPE_ALL);
	mutex_lock(&mddp_dev_read_lock);
	mddp_clear_dstate(&mddp_hidl_rb_head_s);
	atomic_set(&mddp_response_overflow, 0);
	mutex_unlock(&mddp_dev_read_lock);
	atomic_set(&mddp_dev_open_ref_cnt_s, 0);
	mutex_unlock(&mddp_dev_ioctl_lock);

	return 0;
}

static ssize_t mddp_dev_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	int32_t                         ret = 0;
	uint32_t                        len = 0;
	struct mddp_dev_rb_head_t      *list = &mddp_hidl_rb_head_s;
	struct mddp_dev_rb_t           *entry;

retry:
	if (atomic_xchg(&mddp_response_overflow, 0))
		return -EOVERFLOW;
	if (mddp_dev_rb_queue_empty(list)) {
		if (!(file->f_flags & O_NONBLOCK)) {
			spin_lock_irq(&list->read_wq.lock);
			ret = wait_event_interruptible_locked_irq(
				list->read_wq,
				!mddp_dev_rb_queue_empty(list));
			spin_unlock_irq(&list->read_wq.lock);

			if (ret == -ERESTARTSYS) {
				ret = -EINTR;
				goto exit;
			}
		} else {
			ret = -EAGAIN;
			goto exit;
		}
	}

	mutex_lock(&mddp_dev_read_lock);
	MDDP_C_LOG(MDDP_LL_DEBUG,
			"%s: IOCTL dev read, count(%zu).\n", __func__, count);
	spin_lock(&list->locker);
	entry = mddp_dev_rb_peek(list);
	spin_unlock(&list->locker);

	if (!entry) {
		mutex_unlock(&mddp_dev_read_lock);
		goto retry;
	}
	len = entry->rb_len;

	if (count >= entry->rb_len) {
		if (copy_to_user(buf, entry->rb_data, entry->rb_len)) {
			MDDP_C_LOG(MDDP_LL_WARN, "%s: copy_to_user fail!\n",
					__func__);
			ret = -EFAULT;
		}

		spin_lock(&list->locker);
		entry = mddp_dev_rb_dequeue(list);
		if (entry == NULL) {
			MDDP_C_LOG(MDDP_LL_WARN,
					"%s: unexpected dequeue fail!\n",
					__func__);
			ret = -EFAULT;
			spin_unlock(&list->locker);
			goto out_unlock;
		}

		kfree(entry->rb_data);
		entry->rb_data = NULL;
		kfree(entry);
		entry = NULL;
		spin_unlock(&list->locker);
	} else {
		ret = -ENOBUFS;
		goto out_unlock;
	}

out_unlock:
	mutex_unlock(&mddp_dev_read_lock);
exit:
	if (ret)
		return ret;
	return len;
}

static ssize_t mddp_dev_write(struct file *file,
		const char __user *buf,
		size_t count,
		loff_t *ppos)
{

	MDDP_C_LOG(MDDP_LL_DEBUG, "%s: Receive\n", __func__);

	return count;
}

static long mddp_dev_request(unsigned int cmd, void __user *arg)
{
	static const uint32_t sizes[] = {
		0, 0, sizeof(struct mddp_dev_req_act_t), 0, 0,
		sizeof(struct mddp_dev_req_set_data_limit_t),
		sizeof(struct mddp_dev_req_set_ct_value_t),
		sizeof(struct mddp_dev_req_set_warning_and_data_limit_t),
		sizeof(struct mddp_dev_ipv4_conntrack_event_t),
		sizeof(struct mddp_dev_ipv4_conntrack_event_t),
		sizeof(struct mddp_dev_ipv6_conntrack_event_t),
		sizeof(struct mddp_dev_ipv6_conntrack_event_t),
	};
	struct mddp_dev_req_common_t request;
	uint8_t output[sizeof(struct mddp_dev_rsp_common_t) + MDDP_MAX_GET_BUF_SZ] __aligned(8) = {0};
	struct mddp_dev_rsp_common_t *reply = (void *)output;
	struct mddp_dev_req_act_t *act = (void *)reply->data;
	uint32_t length = MDDP_MAX_GET_BUF_SZ;
	uint32_t app;
	long ret;

	if (cmd != MDDP_CMCMD_DEV_REQ)
		return -ENOTTY;
	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;
	if (request.mcode != MDDP_CTRL_MSG_MCODE ||
	    request.version != MDDP_CTRL_MSG_VERSION ||
	    (unsigned int)request.msg >= ARRAY_SIZE(sizes) ||
	    request.data_len != sizes[request.msg])
		return -EINVAL;
	app = request.msg == MDDP_CMCMD_ACT_REQ || request.msg == MDDP_CMCMD_DEACT_REQ ?
		MDDP_APP_TYPE_WH : MDDP_APP_TYPE_ALL;
	if (request.app_type != app)
		return -EOPNOTSUPP;
	if (request.data_len && copy_from_user(reply->data,
			(const uint8_t __user *)arg + sizeof(request), request.data_len))
		return -EFAULT;
	if ((request.msg == MDDP_CMCMD_ACT_REQ ||
	     request.msg == MDDP_CMCMD_SET_DATA_LIMIT_REQ ||
	     request.msg == MDDP_CMCMD_SET_WARNING_AND_DATA_LIMIT_REQ) &&
	    !memchr(reply->data, 0, IFNAMSIZ))
		return -EINVAL;

	switch (request.msg) {
	case MDDP_CMCMD_ENABLE_REQ:
		return mddp_on_enable(app);
	case MDDP_CMCMD_DISABLE_REQ:
		return mddp_on_disable(app);
	case MDDP_CMCMD_ACT_REQ:
		if (!memchr(act->dl_dev_name, 0, IFNAMSIZ))
			return -EINVAL;
		return mddp_on_activate(app, act->ul_dev_name, act->dl_dev_name);
	case MDDP_CMCMD_DEACT_REQ:
		return mddp_on_deactivate(app);
	case MDDP_CMCMD_GET_OFFLOAD_STATS_REQ:
		ret = mddp_on_get_offload_stats(app, reply->data, &length);
		if (ret)
			return ret;
		if (length > MDDP_MAX_GET_BUF_SZ)
			return -EOVERFLOW;
		MDDP_DEV_CLONE_COMM_HDR(reply, &request, 0);
		reply->data_len = length;
		return copy_to_user(arg, reply, sizeof(*reply) + length) ? -EFAULT : 0;
	case MDDP_CMCMD_SET_DATA_LIMIT_REQ:
		return mddp_on_set_data_limit(app, reply->data, request.data_len);
	case MDDP_CMCMD_SET_CT_VALUE_REQ:
		return mddp_on_set_ct_value(app, reply->data, request.data_len);
	case MDDP_CMCMD_SET_WARNING_AND_DATA_LIMIT_REQ:
		return mddp_on_set_warning_and_data_limit(app, reply->data, request.data_len);
	case MDDP_CMCMD_IPV4_NEW_CONNTRACK:
	case MDDP_CMCMD_IPV4_DST_CONNTRACK:
		if (!READ_ONCE(mddp_connection_base_activated_s))
			return -EOPNOTSUPP;
		return mddp_f_send_v4_conntrack_info((void *)reply->data, request.msg);
	case MDDP_CMCMD_IPV6_NEW_CONNTRACK:
	case MDDP_CMCMD_IPV6_DST_CONNTRACK:
		if (!READ_ONCE(mddp_connection_base_activated_s))
			return -EOPNOTSUPP;
		return mddp_f_send_v6_conntrack_info((void *)reply->data, request.msg);
	default:
		return -EINVAL;
	}
}

static long mddp_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret;

	if (mutex_lock_interruptible(&mddp_dev_ioctl_lock))
		return -ERESTARTSYS;
	ret = mddp_dev_request(cmd, (void __user *)arg);
	mutex_unlock(&mddp_dev_ioctl_lock);
	return ret;
}

#if IS_ENABLED(CONFIG_COMPAT)
static long mddp_dev_compat_ioctl(struct file *filp,
		unsigned int cmd,
		unsigned long arg)
{
	return mddp_dev_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static unsigned int mddp_dev_poll(struct file *fp, struct poll_table_struct *poll)
{
	struct mddp_dev_rb_head_t *list = &mddp_hidl_rb_head_s;
	unsigned int mask = 0;

	poll_wait(fp, &list->read_wq, poll);
	spin_lock(&list->locker);
	if (!mddp_dev_rb_queue_empty(list))
		mask = EPOLLIN | EPOLLRDNORM;
	if (atomic_read(&mddp_response_overflow))
		mask |= EPOLLERR;
	spin_unlock(&list->locker);
	return mask;
}
