#ifndef MINDONE_RXFILTER_CONTROL_H
#define MINDONE_RXFILTER_CONTROL_H

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>

enum rxfilter_operation {
	RXFILTER_START,
	RXFILTER_STOP,
	RXFILTER_ADD4,
	RXFILTER_ADD6,
	RXFILTER_REMOVE4,
	RXFILTER_REMOVE6,
};

enum rxfilter_phase {
	RXFILTER_IDLE,
	RXFILTER_PROBE,
	RXFILTER_WRITE,
	RXFILTER_READBACK,
	RXFILTER_VERIFY,
};

enum rxfilter_result {
	RXFILTER_IGNORED,
	RXFILTER_NEEDS_WRITE,
	RXFILTER_CONFIRMED,
};

struct rxfilter_nonce {
	u32 value;
};

struct rxfilter_control {
	u64 epoch;
	u64 pending_epoch;
	u32 pending_nonce;
	u8 desired[4];
	u8 applied[4];
	u8 observed_controls[4][3];
	u8 valid;
	u8 capable;
	u8 bss;
	u8 sequence;
	u8 expected;
	u8 phase;
};

static inline int rxfilter_is_command(const char *command, unsigned int length)
{
	return command && length >= 8 && !memcmp(command, "RXFILTER", 8);
}

static inline int rxfilter_parse(const char *command, unsigned int length)
{
	static const char * const names[] = {
		"RXFILTER-START", "RXFILTER-STOP", "RXFILTER-ADD 2",
		"RXFILTER-ADD 3", "RXFILTER-REMOVE 2", "RXFILTER-REMOVE 3",
	};
	unsigned int i, size;

	if (!command)
		return -EOPNOTSUPP;
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		size = strlen(names[i]);
		if (length > size && command[size] == '\0' &&
		    !memcmp(command, names[i], size))
			return i;
	}
	return -EOPNOTSUPP;
}

static inline void rxfilter_init(struct rxfilter_control *control)
{
	unsigned int i;

	memset(control, 0, sizeof(*control));
	control->epoch = 1;
	for (i = 0; i < 4; i++)
		control->desired[i] = 3;
}

static inline int rxfilter_set_desired(struct rxfilter_control *control,
		unsigned int bss, int operation)
{
	u8 next;

	if (bss >= 4)
		return -EINVAL;
	if (operation < RXFILTER_START || operation > RXFILTER_REMOVE6)
		return -EOPNOTSUPP;
	if (control->phase != RXFILTER_IDLE)
		return -EBUSY;
	next = control->desired[bss];
	switch (operation) {
	case RXFILTER_START:
		next |= 4;
		break;
	case RXFILTER_STOP:
		next &= 3;
		break;
	case RXFILTER_ADD4:
		next |= 1;
		break;
	case RXFILTER_ADD6:
		next |= 2;
		break;
	case RXFILTER_REMOVE4:
		next &= ~1;
		break;
	case RXFILTER_REMOVE6:
		next &= ~2;
		break;
	}
	control->desired[bss] = next;
	return 0;
}

static inline int rxfilter_query_begin(struct rxfilter_control *control,
		struct rxfilter_nonce *nonce, unsigned int bss, u8 sequence,
		u8 request[12])
{
	u32 value;
	unsigned int i;

	if (bss >= 4 || !control->epoch)
		return -EINVAL;
	if (control->phase != RXFILTER_IDLE &&
	    control->phase != RXFILTER_READBACK)
		return -EBUSY;
	if (control->phase == RXFILTER_READBACK && control->bss != bss)
		return -EINVAL;
	if (nonce->value == (u32)~0U)
		return -EOVERFLOW;
	value = ++nonce->value;
	memset(request, 0, 12);
	request[4] = bss;
	request[5] = 'R';
	request[6] = 'X';
	request[7] = 1;
	for (i = 0; i < 4; i++)
		request[8 + i] = value >> (i * 8);
	control->pending_nonce = value;
	control->pending_epoch = control->epoch;
	control->sequence = sequence;
	control->bss = bss;
	control->expected = control->desired[bss] & 4 ?
		control->desired[bss] : 0;
	control->phase = control->phase == RXFILTER_IDLE ?
		RXFILTER_PROBE : RXFILTER_VERIFY;
	return 0;
}

static inline int rxfilter_write_queued(struct rxfilter_control *control)
{
	if (control->phase != RXFILTER_WRITE ||
	    control->pending_epoch != control->epoch ||
	    !(control->capable & (1U << control->bss)))
		return -EINVAL;
	control->valid &= ~(1U << control->bss);
	control->phase = RXFILTER_READBACK;
	return 0;
}

static inline int rxfilter_event(struct rxfilter_control *control,
		u8 eid, u8 sequence, const u8 *payload, unsigned int length)
{
	u32 nonce;
	u8 policy, expected_status, awake, asleep;
	unsigned int i;

	if ((control->phase != RXFILTER_PROBE &&
	     control->phase != RXFILTER_VERIFY) ||
	    control->pending_epoch != control->epoch ||
	    eid != 0xfd || sequence != control->sequence || !payload ||
	    length != 12 || payload[1] != 0xc1 || payload[2] != 1 ||
	    payload[3] || payload[11])
		return RXFILTER_IGNORED;
	nonce = 0;
	for (i = 0; i < 4; i++)
		nonce |= (u32)payload[4 + i] << (i * 8);
	if (nonce != control->pending_nonce)
		return RXFILTER_IGNORED;
	policy = payload[0] & 7;
	expected_status = 0xa0 | (control->bss << 3) | policy;
	if (payload[0] != expected_status || (policy && policy < 4)) {
		control->capable &= ~(1U << control->bss);
		control->valid &= ~(1U << control->bss);
		control->phase = RXFILTER_IDLE;
		return -EOPNOTSUPP;
	}
	awake = payload[8] >> 4;
	asleep = payload[9] >> 4;
	if (!(payload[8] & 15) || (payload[8] & 15) > 3 ||
	    !(payload[9] & 15) || (payload[9] & 15) > 3 ||
	    (policy == 0 ? (!awake || awake > 5 || !asleep || asleep > 5) :
	     (awake != policy + 4 || asleep != awake ||
	      (payload[8] & 15) != 3 || (payload[9] & 15) != 3)))
		return RXFILTER_IGNORED;
	memcpy(control->observed_controls[control->bss], payload + 8, 3);
	control->capable |= 1U << control->bss;
	control->applied[control->bss] = policy;
	control->valid |= 1U << control->bss;
	if (policy == control->expected) {
		control->phase = RXFILTER_IDLE;
		return RXFILTER_CONFIRMED;
	}
	if (control->phase == RXFILTER_PROBE) {
		control->phase = RXFILTER_WRITE;
		return RXFILTER_NEEDS_WRITE;
	}
	control->phase = RXFILTER_IDLE;
	return -EIO;
}

static inline void rxfilter_cancel(struct rxfilter_control *control)
{
	control->phase = RXFILTER_IDLE;
	control->pending_nonce = 0;
}

static inline void rxfilter_external_write(struct rxfilter_control *control,
		unsigned int bss)
{
	if (bss >= 4)
		return;
	control->valid &= ~(1U << bss);
	if (control->phase != RXFILTER_IDLE && control->bss == bss)
		rxfilter_cancel(control);
}

static inline void rxfilter_reset(struct rxfilter_control *control)
{
	control->epoch++;
	control->valid = 0;
	control->capable = 0;
	rxfilter_cancel(control);
}

#endif
