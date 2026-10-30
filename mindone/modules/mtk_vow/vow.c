// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include "mtk-afe-external.h"
#include "scp.h"
#include "vow.h"

static DEFINE_MUTEX(vow_lock);
static DEFINE_SPINLOCK(vow_rx_lock);
static DECLARE_WAIT_QUEUE_HEAD(vow_wait);
static DECLARE_WAIT_QUEUE_HEAD(voice_wait);
static DECLARE_WAIT_QUEUE_HEAD(engine_wait);
static struct wakeup_source *vow_wake;
static struct vow_speaker_model_t models[MAX_VOW_SPEAKER_MODEL];
static struct vow_ipi_combined_info_t notification;
static unsigned int google_version;
static char google_arch[VOW_ENGINE_INFO_LENGTH_BYTE];
static char alexa_version[VOW_ENGINE_INFO_LENGTH_BYTE];
static u8 voice_buffer[VOW_VBUF_LENGTH];
static unsigned int voice_bytes;
static unsigned int event_sequence;
static unsigned int event_seen;
static unsigned long features;
static unsigned int users;
static unsigned int bargein_irq;
static bool available;
static bool initialized;
static bool enabled;
static bool recording;
static bool bargein;
static bool scp_stopped;
static bool voice_reader;
static void vow_scp_stop_work(struct work_struct *work);
static DECLARE_WORK(vow_stop_work, vow_scp_stop_work);
static void *vow_memory;
static unsigned int vow_memory_size;

static void vow_feature(unsigned int id, bool enable)
{
	if (enable && !(features & BIT(id))) {
		scp_register_feature(id);
		features |= BIT(id);
	} else if (!enable && (features & BIT(id))) {
		scp_deregister_feature(id);
		features &= ~BIT(id);
	}
}

static int vow_vendor(int uuid)
{
	switch (uuid) {
	case VENDOR_ID_MTK:
		return VOW_VENDOR_M_FEATURE_ID;
	case VENDOR_ID_AMAZON:
		return VOW_VENDOR_A_FEATURE_ID;
	case VENDOR_ID_OTHERS:
		return VOW_VENDOR_G_FEATURE_ID;
	default:
		return -EINVAL;
	}
}

static void vow_update_vendors(void)
{
	unsigned long wanted = 0;
	int i, id;

	for (i = 0; i < ARRAY_SIZE(models); i++) {
		id = vow_vendor(models[i].uuid);
		if (models[i].enabled && id >= 0)
			wanted |= BIT(id);
	}
	vow_feature(VOW_VENDOR_M_FEATURE_ID, wanted & BIT(VOW_VENDOR_M_FEATURE_ID));
	vow_feature(VOW_VENDOR_A_FEATURE_ID, wanted & BIT(VOW_VENDOR_A_FEATURE_ID));
	vow_feature(VOW_VENDOR_G_FEATURE_ID, wanted & BIT(VOW_VENDOR_G_FEATURE_ID));
}

static int vow_send(unsigned int id, unsigned int count, u32 *data)
{
	return vow_ipi_send(id, count, data, VOW_IPI_BYPASS_ACK) ? 0 : -EIO;
}

static int vow_flag(unsigned int id, unsigned int value)
{
	u32 payload[] = { id, value };

	return vow_send(IPIMSG_VOW_SET_FLAG, ARRAY_SIZE(payload), payload);
}

static void vow_receive(unsigned int id, void *data)
{
	struct vow_ipi_combined_info_t *info = data;
	unsigned long flags;
	unsigned int offset, size;

	spin_lock_irqsave(&vow_rx_lock, flags);
	switch (id) {
	case IPIMSG_VOW_COMBINED_INFO:
		if ((info->ipi_type_flag & RECOG_OK_IDX_MASK) && READ_ONCE(enabled)) {
			notification = *info;
			event_sequence++;
			__pm_wakeup_event(vow_wake, 1000);
			wake_up_interruptible(&vow_wait);
		}
		if ((info->ipi_type_flag & DEBUG_DUMP_IDX_MASK) && recording &&
		    vow_memory) {
			offset = info->voice_buf_offset;
			size = info->voice_length;
			if (offset <= VOW_VOICEDATA_SIZE &&
			    size <= VOW_VOICEDATA_SIZE - offset &&
			    size <= sizeof(voice_buffer) - voice_bytes) {
				memcpy(voice_buffer + voice_bytes,
				       vow_memory + VOW_VOICEDATA_OFFSET + offset, size);
				voice_bytes += size;
				wake_up_interruptible(&voice_wait);
			}
		}
		break;
	case IPIMSG_VOW_GOOGLE_ENGINE_VER:
		google_version = *(u32 *)data;
		wake_up(&engine_wait);
		break;
	case IPIMSG_VOW_GOOGLE_ARCH:
		memcpy(google_arch, data, sizeof(google_arch));
		wake_up(&engine_wait);
		break;
	case IPIMSG_VOW_ALEXA_ENGINE_VER:
		memcpy(alexa_version, data, sizeof(alexa_version));
		wake_up(&engine_wait);
		break;
	}
	spin_unlock_irqrestore(&vow_rx_lock, flags);
}

static int vow_initialize(void)
{
	dma_addr_t phys;
	u32 address;
	int retry, ret;

	if (scp_stopped)
		return -EPIPE;
	if (initialized)
		return 0;
	for (retry = 0; retry < 100; retry++) {
		if (vow_check_scp_status())
			break;
		if (msleep_interruptible(20))
			return -ERESTARTSYS;
	}
	if (!vow_check_scp_status())
		return -ETIMEDOUT;
	vow_memory_size = scp_get_reserve_mem_size(VOW_MEM_ID);
	phys = scp_get_reserve_mem_phys(VOW_MEM_ID);
	if (!phys || upper_32_bits(phys) || upper_32_bits(phys + vow_memory_size - 1) ||
	    vow_memory_size < VOW_CUSTOM_MODEL_OFFSET + VOW_MAX_CUST_MODEL_SIZE)
		return -ENOSPC;
	vow_memory = (void *)scp_get_reserve_mem_virt(VOW_MEM_ID);
	if (!vow_memory)
		return -ENODEV;
	address = phys + VOW_VOICEDATA_OFFSET;
	ret = vow_send(IPIMSG_VOW_APREGDATA_ADDR, 1, &address);
	if (ret)
		return ret;
	ret = vow_flag(VOW_FLAG_PRE_LEARN, 1);
	if (ret)
		return ret;
	vow_send(IPIMSG_VOW_GET_GOOGLE_ENGINE_VER, 0, NULL);
	vow_send(IPIMSG_VOW_GET_GOOGLE_ARCH, 0, NULL);
	vow_send(IPIMSG_VOW_GET_ALEXA_ENGINE_VER, 0, NULL);
	initialized = true;
	scp_stopped = false;
	return 0;
}

static int vow_model_message(unsigned int slot, bool clear)
{
	u32 payload[] = { clear ? VOW_MODEL_CLEAR : VOW_MODEL_SPEAKER,
		models[slot].uuid, slot, models[slot].model_size, models[slot].keyword };

	return vow_send(IPIMSG_VOW_SET_MODEL, ARRAY_SIZE(payload), payload);
}

static int vow_model_status(unsigned int slot, bool start)
{
	u32 payload[] = { models[slot].keyword, models[slot].confidence_lv };
	int ret;

	ret = vow_send(start ? IPIMSG_VOW_MODEL_START : IPIMSG_VOW_MODEL_STOP,
		       ARRAY_SIZE(payload), payload);
	if (!ret) {
		models[slot].enabled = start;
		vow_update_vendors();
	}
	return ret;
}

static void vow_stop(void)
{
	unsigned long flags;
	int i;
	u32 irq = bargein_irq;

	for (i = 0; i < ARRAY_SIZE(models); i++) {
		if (models[i].enabled)
			vow_model_status(i, false);
		if (models[i].flag)
			vow_model_message(i, true);
		memset(&models[i], 0, sizeof(models[i]));
	}
	if (bargein)
		vow_send(IPIMSG_VOW_SET_BARGEIN_OFF, 1, &irq);
	if (initialized) {
		vow_flag(VOW_FLAG_DEBUG, 0);
		vow_send(IPIMSG_VOW_DISABLE, 0, NULL);
	}
	for (i = 0; i < BITS_PER_LONG; i++)
		vow_feature(i, false);
	spin_lock_irqsave(&vow_rx_lock, flags);
	recording = false;
	enabled = false;
	voice_bytes = 0;
	event_sequence++;
	spin_unlock_irqrestore(&vow_rx_lock, flags);
	bargein = false;
	initialized = false;
	__pm_relax(vow_wake);
	wake_up_interruptible(&voice_wait);
	wake_up_interruptible(&vow_wait);
}

static int vow_open(struct inode *inode, struct file *file)
{
	mutex_lock(&vow_lock);
	if (!available) {
		mutex_unlock(&vow_lock);
		return -ENODEV;
	}
	users++;
	mutex_unlock(&vow_lock);
	return 0;
}

static int vow_release(struct inode *inode, struct file *file)
{
	mutex_lock(&vow_lock);
	if (!--users)
		vow_stop();
	mutex_unlock(&vow_lock);
	return 0;
}

static int vow_find_model(long handle)
{
	int i;

	if (handle < INT_MIN || handle > INT_MAX)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(models); i++)
		if (models[i].flag && models[i].id == handle)
			return i;
	return -ENOENT;
}

static int vow_upload(unsigned long arg)
{
	struct vow_model_info_t model;
	int slot, ret;

	if (copy_from_user(&model, (void __user *)arg, sizeof(model)))
		return -EFAULT;
	if (model.uuid < INT_MIN || model.uuid > INT_MAX || model.id < INT_MIN || model.id > INT_MAX || model.keyword < 0 ||
	    model.keyword > U16_MAX || model.size < VOW_MODEL_SIZE_THRES ||
	    model.size > VOW_MODEL_SIZE || vow_vendor(model.uuid) < 0)
		return -EINVAL;
	if (vow_find_model(model.id) >= 0)
		return -EEXIST;
	for (slot = 0; slot < ARRAY_SIZE(models); slot++)
		if (!models[slot].flag)
			break;
	if (slot == ARRAY_SIZE(models))
		return -ENOSPC;
	if (copy_from_user(vow_memory + slot * VOW_MODEL_SIZE,
			   (void __user *)model.addr, model.size))
		return -EFAULT;
	models[slot].id = model.id;
	models[slot].keyword = model.keyword;
	models[slot].uuid = model.uuid;
	models[slot].model_size = model.size;
	ret = vow_model_message(slot, false);
	if (!ret)
		models[slot].flag = 1;
	return ret;
}

static long vow_voice_data(unsigned long arg)
{
	struct vow_model_info_t info;
	unsigned long flags;
	u8 *buffer;
	u32 size;
	long ret;

	if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
		return -EFAULT;
	if (!info.addr || !info.return_size_addr || info.size != VOW_VBUF_LENGTH)
		return -EINVAL;
	buffer = kmalloc(VOW_VBUF_LENGTH, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	mutex_lock(&vow_lock);
	if (voice_reader) {
		ret = -EBUSY;
		goto unlock;
	}
	voice_reader = true;
	mutex_unlock(&vow_lock);
	ret = wait_event_interruptible_timeout(voice_wait,
		READ_ONCE(voice_bytes) >= VOW_VOICE_RECORD_THRESHOLD ||
		!READ_ONCE(recording) || !READ_ONCE(enabled) || READ_ONCE(scp_stopped), msecs_to_jiffies(5000));
	mutex_lock(&vow_lock);
	voice_reader = false;
	if (scp_stopped) {
		ret = -EPIPE;
		goto unlock;
	}
	if (ret <= 0) {
		ret = ret ? ret : -ETIMEDOUT;
		goto unlock;
	}
	spin_lock_irqsave(&vow_rx_lock, flags);
	size = voice_bytes;
	memcpy(buffer, voice_buffer, size);
	voice_bytes = 0;
	spin_unlock_irqrestore(&vow_rx_lock, flags);
	ret = copy_to_user((void __user *)info.addr, buffer, size) ||
		copy_to_user((void __user *)info.return_size_addr, &size, sizeof(size)) ?
		-EFAULT : 0;
unlock:
	mutex_unlock(&vow_lock);
	kfree(buffer);
	return ret;
}

static long vow_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vow_model_start_t start;
	struct vow_model_info_t voice;
	struct vow_engine_info_t engine;
	unsigned long flags;
	u32 value, length = VOW_ENGINE_INFO_LENGTH_BYTE;
	char engine_data[VOW_ENGINE_INFO_LENGTH_BYTE];
	int slot, ret = 0;

	if (cmd == VOW_READ_VOICE_DATA)
		return vow_voice_data(arg);
	if (cmd == VOW_SET_CONTROL && arg == VOWControlCmd_Init)
		cancel_work_sync(&vow_stop_work);
	mutex_lock(&vow_lock);
	if (cmd == VOW_SET_CONTROL && arg == VOWControlCmd_Reset) {
		vow_stop();
		scp_stopped = false;
		goto unlock;
	}
	if (cmd == VOW_SET_CONTROL && arg == VOWControlCmd_Init && scp_stopped) {
		vow_stop();
		scp_stopped = false;
	}
	ret = vow_initialize();
	if (ret)
		goto unlock;
	switch (cmd) {
	case VOW_SET_CONTROL:
		switch (arg) {
		case VOWControlCmd_Init:
			break;
		case VOWControlCmd_EnableDebug:
			ret = vow_flag(VOW_FLAG_DEBUG, 1);
			if (!ret) {
				WRITE_ONCE(recording, true);
				vow_feature(VOW_DUMP_FEATURE_ID, true);
				__pm_stay_awake(vow_wake);
			}
			break;
		case VOWControlCmd_DisableDebug:
			ret = vow_flag(VOW_FLAG_DEBUG, 0);
			WRITE_ONCE(recording, false);
			vow_feature(VOW_DUMP_FEATURE_ID, false);
			__pm_relax(vow_wake);
			wake_up_interruptible(&voice_wait);
			break;
		case VOWControlCmd_EnableSeamlessRecord:
			ret = vow_flag(VOW_FLAG_SEAMLESS, 1);
			break;
		case VOWControlCmd_Mic_Single:
		case VOWControlCmd_Mic_Dual:
			ret = vow_flag(VOW_FLAG_DUAL_MIC_SWITCH,
				      arg == VOWControlCmd_Mic_Dual ? VOW_ENABLE_DUAL_MIC :
				      VOW_ENABLE_SINGLE_MAIN_MIC);
			break;
		default:
			ret = -EOPNOTSUPP;
		}
		break;
	case VOW_SET_APREG_INFO:
		if (copy_from_user(&voice, (void __user *)arg, sizeof(voice)))
			ret = -EFAULT;
		else if (!voice.addr || !voice.return_size_addr || voice.size != VOW_VBUF_LENGTH)
			ret = -EINVAL;
		break;
	case VOW_SET_DSP_AEC_PARAMETER:
		if (copy_from_user(&engine, (void __user *)arg, sizeof(engine)) ||
		    copy_from_user(&value, (void __user *)engine.return_size_addr, sizeof(value))) {
			ret = -EFAULT;
			break;
		}
		if (!value || value > VOW_MAX_CUST_MODEL_SIZE) {
			ret = -EINVAL;
			break;
		}
		if (copy_from_user(vow_memory + VOW_CUSTOM_MODEL_OFFSET,
				   (void __user *)engine.data_addr, value))
			ret = -EFAULT;
		else
			ret = vow_send(IPIMSG_VOW_SET_CUSTOM_MODEL, 1, &value);
		break;
	case VOW_SET_SPEAKER_MODEL:
		ret = vow_upload(arg);
		break;
	case VOW_CLR_SPEAKER_MODEL:
		slot = vow_find_model((int)arg);
		if (slot < 0) {
			ret = slot;
			break;
		}
		if (models[slot].enabled)
			ret = vow_model_status(slot, false);
		if (!ret)
			ret = vow_model_message(slot, true);
		if (!ret)
			memset(&models[slot], 0, sizeof(models[slot]));
		break;
	case VOW_MODEL_START:
	case VOW_MODEL_STOP:
		if (copy_from_user(&start, (void __user *)arg, sizeof(start))) {
			ret = -EFAULT;
			break;
		}
		slot = vow_find_model(start.handle);
		if (slot < 0) {
			ret = slot;
			break;
		}
		if (start.confidence_level < 0 || start.confidence_level > U32_MAX) {
			ret = -EINVAL;
			break;
		}
		models[slot].confidence_lv = start.confidence_level;
		models[slot].rx_inform_addr = start.dsp_inform_addr;
		models[slot].rx_inform_size_addr = start.dsp_inform_size_addr;
		ret = vow_model_status(slot, cmd == VOW_MODEL_START);
		break;
	case VOW_RECOG_ENABLE:
		if (arg >= VOW_MTKIF_MAX) {
			ret = -EINVAL;
			break;
		}
		vow_feature(VOW_FEATURE_ID, true);
		ret = vow_flag(VOW_FLAG_MTKIF_TYPE, arg);
		if (!ret)
			ret = vow_send(IPIMSG_VOW_ENABLE, 0, NULL);
		if (!ret)
			WRITE_ONCE(enabled, true);
		else
			vow_feature(VOW_FEATURE_ID, false);
		break;
	case VOW_RECOG_DISABLE:
		ret = vow_send(IPIMSG_VOW_DISABLE, 0, NULL);
		if (!ret)
			WRITE_ONCE(enabled, false);
		WRITE_ONCE(recording, false);
		vow_feature(VOW_DUMP_FEATURE_ID, false);
		__pm_relax(vow_wake);
		vow_feature(VOW_FEATURE_ID, false);
		wake_up_interruptible(&voice_wait);
		fallthrough;
	case VOW_CHECK_STATUS:
		spin_lock_irqsave(&vow_rx_lock, flags);
		event_sequence++;
		spin_unlock_irqrestore(&vow_rx_lock, flags);
		wake_up_interruptible(&vow_wait);
		break;
	case VOW_BARGEIN_ON:
	case VOW_BARGEIN_OFF:
		if (arg >= VOW_BARGEIN_IRQ_MAX_NUM) {
			ret = -EINVAL;
			break;
		}
		value = arg;
		ret = vow_send(cmd == VOW_BARGEIN_ON ? IPIMSG_VOW_SET_BARGEIN_ON :
			       IPIMSG_VOW_SET_BARGEIN_OFF, 1, &value);
		if (!ret) {
			bargein = cmd == VOW_BARGEIN_ON;
			bargein_irq = arg;
			vow_feature(VOW_BARGEIN_FEATURE_ID, bargein);
		}
		break;
	case VOW_GET_GOOGLE_ENGINE_VER:
		wait_event_timeout(engine_wait, READ_ONCE(google_version), msecs_to_jiffies(200));
		value = READ_ONCE(google_version);
		ret = value ? (copy_to_user((void __user *)arg, &value, sizeof(value)) ?
			       -EFAULT : 0) : -EAGAIN;
		break;
	case VOW_GET_GOOGLE_ARCH:
	case VOW_GET_ALEXA_ENGINE_VER:
		if (copy_from_user(&engine, (void __user *)arg, sizeof(engine))) {
			ret = -EFAULT;
			break;
		}
		spin_lock_irqsave(&vow_rx_lock, flags);
		memcpy(engine_data, cmd == VOW_GET_GOOGLE_ARCH ? google_arch :
		       alexa_version, sizeof(engine_data));
		spin_unlock_irqrestore(&vow_rx_lock, flags);
		if (copy_to_user((void __user *)engine.data_addr, engine_data, length) ||
		    copy_to_user((void __user *)engine.return_size_addr, &length, sizeof(length)))
			ret = -EFAULT;
		break;
	default:
		ret = -ENOTTY;
	}
unlock:
	mutex_unlock(&vow_lock);
	return ret;
}

static ssize_t vow_read(struct file *file, char __user *buffer, size_t count,
			loff_t *offset)
{
	struct vow_eint_data_struct_t event = { .size = sizeof(event) };
	struct vow_ipi_combined_info_t info;
	unsigned long flags;
	u32 extra;
	int ret, i;

	if (count < sizeof(event))
		return -EINVAL;
	ret = wait_event_interruptible(vow_wait,
		READ_ONCE(event_sequence) != READ_ONCE(event_seen) || READ_ONCE(scp_stopped));
	if (ret)
		return ret;
	mutex_lock(&vow_lock);
	if (scp_stopped) {
		ret = -EPIPE;
		goto unlock;
	}
	spin_lock_irqsave(&vow_rx_lock, flags);
	info = notification;
	event_seen = event_sequence;
	notification.ipi_type_flag = 0;
	spin_unlock_irqrestore(&vow_rx_lock, flags);
	event.eint_status = enabled ? VOW_EINT_RETRY : VOW_EINT_DISABLE;
	if (enabled && (info.ipi_type_flag & RECOG_OK_IDX_MASK)) {
		for (i = 0; i < ARRAY_SIZE(models); i++) {
			if (!models[i].enabled || models[i].keyword != info.recog_ok_keywordid)
				continue;
			event.id = models[i].id;
			event.eint_status = VOW_EINT_PASS;
			event.data[0] = info.confidence_lv;
			extra = min_t(u32, info.extra_data_len, VOW_EXTRA_DATA_SIZE);
			if (extra && models[i].rx_inform_addr && models[i].rx_inform_size_addr &&
			    (copy_to_user((void __user *)models[i].rx_inform_addr,
					  vow_memory + VOW_EXTRA_DATA_OFFSET, extra) ||
			     copy_to_user((void __user *)models[i].rx_inform_size_addr,
					  &extra, sizeof(extra)))) {
				ret = -EFAULT;
				goto unlock;
			}
			break;
		}
	}
	ret = copy_to_user(buffer, &event, sizeof(event)) ? -EFAULT : sizeof(event);
unlock:
	mutex_unlock(&vow_lock);
	return ret;
}

static void vow_scp_stop_work(struct work_struct *work)
{
	mutex_lock(&vow_lock);
	vow_stop();
	mutex_unlock(&vow_lock);
}

static int vow_scp_event(struct notifier_block *nb, unsigned long event, void *data)
{
	if (event == SCP_EVENT_STOP) {
		WRITE_ONCE(scp_stopped, true);
		schedule_work(&vow_stop_work);
		wake_up_interruptible(&voice_wait);
		wake_up_interruptible(&vow_wait);
	}
	return NOTIFY_DONE;
}

static struct notifier_block vow_scp_notifier = {
	.notifier_call = vow_scp_event,
};

static int vow_send_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct vow_sound_soc_ipi_send_info *info = data;

	if (event != NOTIFIER_VOW_IPI_SEND)
		return NOTIFY_DONE;
	if (!info)
		return NOTIFY_BAD;
	return vow_ipi_send(info->msg_id, info->payload_len, info->payload,
			    info->need_ack) ? NOTIFY_STOP : NOTIFY_BAD;
}

static struct notifier_block vow_send_notifier = {
	.notifier_call = vow_send_event,
};

static const struct file_operations vow_fops = {
	.owner = THIS_MODULE,
	.open = vow_open,
	.release = vow_release,
	.read = vow_read,
	.unlocked_ioctl = vow_ioctl,
};

static struct miscdevice vow_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = VOW_DEVNAME,
	.fops = &vow_fops,
	.mode = 0600,
};

static int __init vow_init(void)
{
	int ret;

	static_assert(VOW_SET_CONTROL == 0x40045603);
	static_assert(VOW_SET_SPEAKER_MODEL == 0x40045604);
	static_assert(VOW_CLR_SPEAKER_MODEL == 0x40045605);
	static_assert(VOW_CHECK_STATUS == 0x4004560c);
	static_assert(VOW_MODEL_START == 0x4004560f);
	static_assert(VOW_MODEL_STOP == 0x40045610);
	static_assert(VOW_GET_GOOGLE_ENGINE_VER == 0x40045612);
	static_assert(VOWControlCmd_Reset == 6);
	static_assert(offsetof(struct vow_model_info_t, keyword) == 8);
	static_assert(offsetof(struct vow_model_info_t, addr) == 16);
	static_assert(offsetof(struct vow_model_info_t, size) == 24);
	static_assert(offsetof(struct vow_model_info_t, return_size_addr) == 32);
	static_assert(offsetof(struct vow_model_info_t, uuid) == 40);
	static_assert(offsetof(struct vow_model_info_t, data) == 48);
	static_assert(offsetof(struct vow_model_start_t, confidence_level) == 8);
	static_assert(offsetof(struct vow_model_start_t, dsp_inform_addr) == 16);
	static_assert(offsetof(struct vow_model_start_t, dsp_inform_size_addr) == 24);
	static_assert(sizeof(struct vow_model_info_t) == 56);
	static_assert(sizeof(struct vow_model_start_t) == 32);
	static_assert(sizeof(struct vow_engine_info_t) == 16);
	static_assert(sizeof(struct vow_eint_data_struct_t) == 16);
	static_assert(sizeof(struct vow_ipi_send_info) == 36);
	static_assert(sizeof(struct vow_ipi_receive_info) == 104);
	vow_wake = wakeup_source_register(NULL, "vow");
	if (!vow_wake)
		return -ENOMEM;
	ret = vow_ipi_register(vow_receive);
	if (ret)
		goto unregister_wake;
	ret = misc_register(&vow_device);
	if (ret)
		goto unregister_ipi;
	ret = register_vow_ipi_send_notifier(&vow_send_notifier);
	if (ret)
		goto deregister_misc;
	scp_A_register_notify(&vow_scp_notifier);
	mutex_lock(&vow_lock);
	available = true;
	mutex_unlock(&vow_lock);
	return 0;

deregister_misc:
	misc_deregister(&vow_device);
unregister_ipi:
	vow_ipi_unregister();
	cancel_work_sync(&vow_stop_work);
unregister_wake:
	wakeup_source_unregister(vow_wake);
	return ret;
}

module_init(vow_init);
MODULE_AUTHOR("MediaTek Inc.");
MODULE_DESCRIPTION("MediaTek VoW legacy sound trigger");
MODULE_LICENSE("GPL v2");
