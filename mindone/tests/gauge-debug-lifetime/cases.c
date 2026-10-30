// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
int main(void)
{
	assert(bat_get_debug_level() == BMLOG_DEBUG_LEVEL);
	registered = true;
	assert(bat_get_debug_level() == BMLOG_DEBUG_LEVEL);
	struct mtk_gauge gauge = { 0 };
	supply.gauge = &gauge;
	assert(bat_get_debug_level() == BMLOG_DEBUG_LEVEL);
	for (int level = 3; level <= 8; level++) {
		gauge.gm = malloc(sizeof(*gauge.gm));
		assert(gauge.gm);
		gauge.gm->log_level = level;
		registered = true;
		assert(bat_get_debug_level() == level);
		assert(supply.refs == 0);
		gauge.gm->log_level = level + 1;
		assert(bat_get_debug_level() == level + 1);
		registered = false;
		free(gauge.gm);
		gauge.gm = NULL;
		assert(bat_get_debug_level() == BMLOG_DEBUG_LEVEL);
		assert(supply.refs == 0);
	}
	puts("PASS fresh debug lookup across unregister/free/reprobe with balanced PSY references and current level");
	return 0;
}
