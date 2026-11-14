/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include "stubs.h"

int ack_request(const u32 *snk, int nsnk, u32 op_mw, const u32 *src, int nsrc, struct request *out);
int fork_request(const u32 *snk, int nsnk, const u32 *src, int nsrc, struct request *out);

#define FIXED(mv, ma, flags) ((u32)(flags) | (((u32)(mv) / 50) << 10) | ((u32)(ma) / 10))
#define VAR(min_mv, max_mv, ma) ((2u << 30) | (((u32)(max_mv) / 50) << 20) | (((u32)(min_mv) / 50) << 10) | ((u32)(ma) / 10))
#define BATT(min_mv, max_mv, mw) ((1u << 30) | (((u32)(max_mv) / 50) << 20) | (((u32)(min_mv) / 50) << 10) | ((u32)(mw) / 250))
#define PPS(min_mv, max_mv, ma) ((3u << 30) | (((u32)(max_mv) / 100) << 17) | (((u32)(min_mv) / 100) << 8) | ((u32)(ma) / 50))

static u32 lcg = 12345;

static u32 next(u32 n)
{
	lcg = lcg * 1103515245u + 12345u;
	return (lcg >> 8) % n;
}

static int make_source(u32 *src)
{
	static const int five[] = { 500, 900, 1500, 2000, 2400, 3000, 3250, 5000 };
	static const int high_mv[] = { 9000, 12000, 15000, 20000 };
	static const int high_ma[] = { 1000, 1500, 1670, 2000, 2220, 2250, 3000, 5000 };
	int n = 0, i;

	src[n++] = FIXED(5000, five[next(8)], next(2) ? (1u << 29) | (1u << 26) : 0);
	for (i = 0; i < 4 && n < 6; i++)
		if (next(3))
			src[n++] = FIXED(high_mv[i], high_ma[next(8)], 0);
	if (!next(4) && n < 7)
		src[n++] = PPS(3300, 11000, 3000);
	if (!next(12) && n < 7)
		src[n++] = VAR(5000, 12000, 1500 + 500 * next(4));
	if (!next(12) && n < 7)
		src[n++] = BATT(5000, 9000, 10000 + 2500 * next(4));
	return n;
}

static int same(const struct request *a, const struct request *b)
{
	return a->pos == b->pos && a->mv == b->mv && a->ma == b->ma && a->max_ma == b->max_ma &&
	       a->mismatch == b->mismatch;
}

static void show(const char *label, const u32 *src, int n, const struct request *a, const struct request *f)
{
	int i;

	printf("%s:", label);
	for (i = 0; i < n; i++)
		printf(" %08x", src[i]);
	printf(" | tcpm pos %d %d mV %d mA max %d mm %d | fork pos %d %d mV %d mA max %d mm %d\n", a->pos, a->mv, a->ma,
	       a->max_ma, a->mismatch, f->pos, f->mv, f->ma, f->max_ma, f->mismatch);
}

int run(const u32 *dt_snk, int dt_nsnk, u32 op_mw, const u32 *fork_snk, int fork_nsnk, int rounds, int mode,
	int *kinds, int *five_volt)
{
	u32 src[8];
	int round, n, bad = 0;
	struct request a, f;

	memset(kinds, 0, 4 * sizeof(int));
	*five_volt = 0;
	for (round = 0; round < rounds; round++) {
		n = make_source(src);
		memset(&a, 0, sizeof(a));
		memset(&f, 0, sizeof(f));
		if (ack_request(dt_snk, dt_nsnk, op_mw, src, n, &a) || fork_request(fork_snk, fork_nsnk, src, n, &f)) {
			show("no request", src, n, &a, &f);
			return -1;
		}
		if (!a.usb_comm) {
			show("tcpm without USB communication bit", src, n, &a, &f);
			return -1;
		}
		if (a.mv == 5000)
			(*five_volt)++;
		if (same(&a, &f)) {
			kinds[0]++;
			continue;
		}
		if (mode == 1 && (src[0] & 0x3ff) * 10 > 3000 && f.mv > 5000 && a.mv == 5000) {
			kinds[1]++;
			continue;
		}
		if (mode == 1 && a.pos == f.pos && a.mv == 5000 && a.ma == f.ma && a.mismatch && !f.mismatch &&
		    a.ma * 5 < 18000) {
			kinds[2]++;
			continue;
		}
		if (mode == 2 && a.mv == 5000 && f.mv > 5000) {
			kinds[3]++;
			continue;
		}
		if (bad++ < 10)
			show("difference", src, n, &a, &f);
	}
	return bad;
}

int main(int argc, char **argv)
{
	static const u32 fork_sink[] = { 0x0001912c | (1u << 29) | (1u << 25), 0x0002d0c8 };
	static const u32 five_only[] = { 0x0001912c | (1u << 29) | (1u << 25) };
	extern u32 dt_caps0[], dt_caps1[];
	extern int dt_caps0_n, dt_caps1_n;
	extern u32 dt_caps0_op_mw, dt_caps1_op_mw;
	int bad, kinds[4], five;

	if (argc != 2)
		return 2;
	if (!strcmp(argv[1], "caps0")) {
		bad = run(dt_caps0, dt_caps0_n, dt_caps0_op_mw, five_only, 1, 200000, 0, kinds, &five);
		printf("caps-0 against the fork policy with its 5 V sink PDO: %d of 200000 requests identical, %d at 5 V\n",
		       kinds[0], five);
		if (bad || kinds[0] != 200000 || five != 200000)
			return 1;
	} else if (!strcmp(argv[1], "caps1")) {
		bad = run(dt_caps1, dt_caps1_n, dt_caps1_op_mw, fork_sink, 2, 200000, 1, kinds, &five);
		printf("caps-1 against the fork policy with both sink PDOs: %d identical, %d 5 V because the 5 V PDO "
		       "offers more than 3 A, %d same contract with the mismatch bit at 5 V\n", kinds[0], kinds[1], kinds[2]);
		if (bad || !kinds[0])
			return 1;
	} else if (!strcmp(argv[1], "caps0-vs-fork")) {
		bad = run(dt_caps0, dt_caps0_n, dt_caps0_op_mw, fork_sink, 2, 200000, 2, kinds, &five);
		printf("caps-0 against the fork policy with both sink PDOs: %d identical, %d at 5 V where the fork takes 9 V\n",
		       kinds[0], kinds[3]);
		if (bad || !kinds[3])
			return 1;
	} else {
		return 2;
	}
	printf("PASS %s\n", argv[1]);
	return 0;
}
