/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
static int parse_memtrack(const char *out, int target_pid, long long *pss_kb, long long *rss_kb)
{
	const char *p = out;
	int found = 0;

	while (*p) {
		const char *nl = strchr(p, '\n');
		size_t n = nl ? (size_t)(nl - p) : strlen(p);
		char line[256];
		unsigned int pid = 0;
		long long pss = 0, rss = 0;

		if (n >= sizeof(line))
			n = sizeof(line) - 1;
		memcpy(line, p, n);
		line[n] = '\0';
		if (n >= 3 && strncmp(line, "---", 3) == 0)
			break;
		if (sscanf(line, "%u %lld %lld", &pid, &pss, &rss) == 3 &&
		    (int)pid == target_pid) {
			*pss_kb = pss;
			*rss_kb = rss;
			found = 1;
			break;
		}
		if (!nl)
			break;
		p = nl + 1;
	}
	return found;
}

int main(void)
{
	struct dma_buf buf1 = { .size = 4096 * 1024 };
	struct dma_buf buf2 = { .size = 2048 * 1024 };
	struct dma_buf fake_non_dma = { .size = 8192 * 1024 };
	struct file f_non_dma = { .is_dmabuf = 0, .private_data = &fake_non_dma, .count = 1 };
	struct file f_dma1 = { .is_dmabuf = 1, .private_data = &buf1, .count = 2 };
	struct file f_dma1_dup = { .is_dmabuf = 1, .private_data = &buf1, .count = 2 };
	struct file f_dma2 = { .is_dmabuf = 1, .private_data = &buf2, .count = 1 };
	struct files_struct files1 = {
		.fds = { &f_non_dma, &f_dma1, &f_dma1_dup, &f_dma2 },
		.max_fds = 4,
	};
	struct task_struct t1 = { .pid = 1234, .refs = 1, .files = &files1 };
	struct task_struct t_empty = { .pid = 5678, .refs = 1, .files = NULL };
	struct seq_file seq = {};
	loff_t pos = 0;
	long long pss_kb = -1, rss_kb = -1;

	sim_tasks[0] = &t1;
	sim_tasks[1] = &t_empty;
	sim_task_count = 2;

	assert(system_heap_procfs_init() == 0);
	assert(strcmp(fake_root.name, "dma_heap") == 0);
	assert(strcmp(fake_entry.name, "rss_pid") == 0);
	assert(fake_entry.mode == 0660);
	assert(fake_entry.uid == 0 && fake_entry.gid == 1000);

	assert(dma_heap_rss_pid_write(NULL, "bad\n", 4, &pos) == -EINVAL);
	assert(dma_heap_rss_pid_write(NULL, "pid:0\n", 6, &pos) == -EINVAL);
	assert(dma_heap_rss_pid_write(NULL, "pid:-5\n", 7, &pos) == -EINVAL);
	assert(dma_heap_rss_pid_write(NULL, "pid:abc\n", 8, &pos) == -EINVAL);

	assert(dma_heap_rss_pid_write(NULL, "pid:1234\n", 9, &pos) == 9);
	assert(dma_heap_rss_pid_show(&seq, NULL) == 0);
	assert(strstr(seq.buf, "PID     PSS(KB)   RSS(KB)\n") != NULL);
	assert(strstr(seq.buf, "-----EGL memtrack data end\n") != NULL);
	assert(parse_memtrack(seq.buf, 1234, &pss_kb, &rss_kb) == 1);
	assert(rss_kb == 6144);
	assert(pss_kb == 4096);
	assert(t1.refs == 1 && !t1.locked && sim_pids[0].refs == 0);

	memset(&seq, 0, sizeof(seq));
	assert(dma_heap_rss_pid_show(&seq, NULL) == 0);
	assert(parse_memtrack(seq.buf, 1234, &pss_kb, &rss_kb) == 0);

	memset(&seq, 0, sizeof(seq));
	assert(dma_heap_rss_pid_write(NULL, "pid:5678\n", 9, &pos) == 9);
	assert(dma_heap_rss_pid_show(&seq, NULL) == 0);
	assert(parse_memtrack(seq.buf, 5678, &pss_kb, &rss_kb) == 1);
	assert(rss_kb == 0 && pss_kb == 0);
	assert(t_empty.refs == 1 && !t_empty.locked && sim_pids[1].refs == 0);

	memset(&seq, 0, sizeof(seq));
	assert(dma_heap_rss_pid_write(NULL, "pid:9999\n", 9, &pos) == 9);
	assert(dma_heap_rss_pid_show(&seq, NULL) == 0);
	assert(parse_memtrack(seq.buf, 9999, &pss_kb, &rss_kb) == 0);

	printf("PASS system-heap-rss\n");
	return 0;
}
