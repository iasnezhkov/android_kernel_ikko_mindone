/* MINDONE: gate for the diagnostic markers of this driver.
 * They printed several lines per plane check, i.e. per frame, and that alone filled
 * the kernel log ring in about a minute, destroying all early-boot evidence.
 * Default is 0 (silent). Enable at runtime:
 *   echo 1 > /sys/module/mediatek_drm/parameters/mindone_log
 */
#ifndef MINDONE_LOG_H
#define MINDONE_LOG_H
extern int mindone_log;
#define MINDONE_PR(fmt, ...) \
	do { if (mindone_log) pr_info(fmt, ##__VA_ARGS__); } while (0)
#endif
