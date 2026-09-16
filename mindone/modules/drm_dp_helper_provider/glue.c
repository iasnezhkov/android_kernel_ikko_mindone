// SPDX-License-Identifier: GPL-2.0
/* Minimal glue: drm_dp_helper.c is real upstream GPL-2.0 code (see its own
 * SPDX header) normally linked into a combined drm_display_helper.ko by the
 * Kconfig-driven build, never standalone, so it carries no MODULE_LICENSE()
 * of its own. Supplying it here (accurate to the real license, not
 * fabricated) so this can link as its own module and provide its real
 * EXPORT_SYMBOL()s to the closure universe. */
#include <linux/module.h>
MODULE_LICENSE("GPL");
