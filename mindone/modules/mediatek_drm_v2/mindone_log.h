#ifndef MINDONE_LOG_H
#define MINDONE_LOG_H
extern int mindone_log;
#define MINDONE_PR(fmt, ...) \
	do { if (mindone_log) pr_info(fmt, ##__VA_ARGS__); } while (0)
#endif
