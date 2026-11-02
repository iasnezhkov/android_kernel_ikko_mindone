<!-- SPDX-License-Identifier: GPL-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
# Drivers

`mindone/modules/` declares 280 `ddk_module` targets in
`mindone/modules/BUILD.bazel`; the `mindone_modules` group selects 253.
The normal boot load lists contain 262 modules:
253 DDK outputs, five device in-tree outputs (`8250_mtk`, `cfg80211`,
`drm_display_helper`, `drm_dma_helper`, `reset-ti-syscon`) and four GKI modules
(`libarc4`, `rfkill`, `zram`, `zsmalloc`). Eight in-tree outputs are declared in
`mindone/BUILD.bazel`; three are not in those lists. The base is Google's
`android16-6.12-2026-09_r10` prebuilt, CI 16736897.

Mali kbase is **r54p1**: the `mali_kbase_mt6789` target builds `mali_kbase_r54/`.
CPU capacity information comes from `mtk_cpu_capacity`; `sugov_ext` is removed.
Pair this set with a ROM that selects the standard `schedutil` governor.

MediaTek SoCs put almost everything outside the kernel proper: display, camera, audio DSP,
Wi-Fi/BT, sensors, thermal, charging, the cellular modem interface. None of it is upstream, and
iKKO published nothing. These trees were ported from MediaTek's own published sources for the
MT6789 family and corrected where they were wrong for this board.

## Inventory

`mindone/modules/MODULES.tsv` lists every tracked module:
kernel module name, `.ko` filename, which boot stage ships it (first-stage ramdisk /
`vendor_dlkm` / recovery) and the build subdirectory. It is generated — regenerate it from a
build pass rather than editing by hand.

The four unused AF wrappers `subaf`, `sub2af`, `main2af` and `main3af` retain
DDK definitions but are removed from `mindone_modules` and the combined and vendor-DLKM load lists.
`mainaf` remains selected for the main camera actuator. In the companion ROM,
`libarc4` moves from `vendor_dlkm` to `system_dlkm`; its vendor list consequently
contains 235 entries, while this kernel's vendor request list contains 236.

## Load order

`mindone/vendor_boot.modules.load` (26 modules, the
first-stage ramdisk set) and `mindone/vendor_dlkm.modules.load`
(236, the rest) request 262 modules, with no overlap between the
two files. Both are wired directly into the `mindone_images` `kernel_images` target in
`mindone/BUILD.bazel`, which selects their requested staging lists. For recovery
there is a third, shorter list:
`mindone/vendor_boot.modules.load.recovery`
(131). Counts exclude empty and comment lines. The combined
`mindone/modules/modules.load` has 262 entries and the same set as the two normal boot
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
