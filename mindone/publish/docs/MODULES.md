<!-- SPDX-License-Identifier: GPL-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
# Drivers

`mindone/modules/` declares 195 `ddk_module` targets in
`mindone/modules/BUILD.bazel`; the `mindone_modules` group selects 191.
The normal boot load lists contain 214 modules: 190 of those DDK outputs,
20 device in-tree outputs and four GKI modules (`libarc4`, `rfkill`, `zram`,
`zsmalloc`). 23 in-tree outputs are declared in `mindone/BUILD.bazel`; three
of them (`mac80211`, `industrialio-triggered-buffer`, `kfifo_buf`) are not in those
lists. The base is Google's `android17-6.18-2026-09_r13` prebuilt, CI 16831611.

Mali kbase is **r54p1** in the M28 and later sets; earlier sets used r32p1.
The `mali_kbase_mt6789` target now builds `mali_kbase_r54/`.
CPU capacity information comes from `mtk_cpu_capacity`; `sugov_ext` is removed.
Pair this set with a ROM that selects the standard `schedutil` governor.

MediaTek SoCs put almost everything outside the kernel proper: display, camera, audio DSP,
Wi-Fi/BT, sensors, thermal, charging, the cellular modem interface. Most of it has no upstream
driver for MT6789, and iKKO published nothing. These trees were ported from MediaTek's own
published sources for the MT6789 family and corrected where they were wrong for this board.
Where the Android Common Kernel has the driver, it replaces the vendor copy: the clk-mtk
core, the power-domain, PMIC-wrapper and pin-controller drivers and the TCPM Type-C stack
come from the r13 sources, with MT6789 data added where r13 lacks it.

## Inventory

`mindone/modules/MODULES.tsv` is the module inventory: module name, `.ko` file, source
(`ddk`, `in-tree` or `gki`), whether `mindone_modules` selects it, the load lists that name it
(`vendor_boot`, `vendor_dlkm`, `recovery`) and its source directories under `mindone/modules/`.
`python3 mindone/tests/modules-tsv-check.py --write` regenerates it from `BUILD.bazel` and the
load lists; without `--write` the check fails when the table is stale.

The four unused AF wrappers `subaf`, `sub2af`, `main2af` and `main3af` retain
DDK definitions but are removed from `mindone_modules` and the combined and vendor-DLKM load lists.
`mainaf` remains selected for the main camera actuator. `ois_aw86006`, the driver of the
camera module's OIS controller, is selected but named in no load list. In the companion ROM,
`libarc4` moves from `vendor_dlkm` to `system_dlkm`; its vendor list consequently
contains 192 entries, while this kernel's vendor request list contains 193.

## Load order

`mindone/vendor_boot.modules.load` (21 modules, the
first-stage ramdisk set) and `mindone/vendor_dlkm.modules.load`
(193, the rest) request 214 modules, with no overlap between the
two files. Both are wired directly into the `mindone_images` `kernel_images` target in
`mindone/BUILD.bazel`, which selects their requested staging lists. For recovery
there is a third, shorter list:
`mindone/vendor_boot.modules.load.recovery`
(109). Counts exclude empty and comment lines. The combined
`mindone/modules/modules.load` has 214 entries and the same set as the two normal boot
lists. Recovery uses `modules_recovery_list` in `mindone/BUILD.bazel`. These are
requested staging lists.

The order matters. Several modules hold devices other modules depend on, and
getting it wrong rarely looks like a module failure — it looks like some unrelated device's
probe being deferred forever.

## Provenance

No single upstream per module. Most combine an ACK driver skeleton with device-specific glue
worked out against this hardware. Where a file was adapted from a specific vendor source, the
origin is stated in a banner at the top of that file.

Files that arrived with MediaTek's dual GPL-2.0/BSD header keep it unchanged.
The gen4m driver uses our GPL-2.0 header `mindone_wsys_cmd.h` with only the interface
definitions it needs, replacing `wsys_cmd_handler_fw.h`.

## Comments

Do not add explanatory comments to code. Keep license headers and inherited upstream
comments; put reasoning and evidence in commit messages or documentation.

## Gates

| Tool | Checks |
|---|---|
| Google symbol-list gate (candidate tooling) | compiled external imports against Google's `gki/aarch64/symbols/*` lists, after resolving module-to-module exports; no private mindone list |
| `modcheck` (candidate tooling) | ABI CRCs, symbol providers, built-in collisions and load-order checks against the exact set's kernel metadata |
| `kocheck.py` | vermagic, `module_layout` CRC, per-symbol CRC and provider against a `Module.symvers`, for local verification outside Kleaf |
| `tree-lint.py` | source hygiene: no foreign device names or paths in published code, no Cyrillic, comment length |
| `extpathcheck.py` | no driver gains a new source path outside this repository (current debt: 0) |
