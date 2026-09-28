# Drivers

`mindone/modules/` holds 278 `ddk_module` targets (3,257 `.c`/`.h` files) in a single
`mindone/modules/BUILD.bazel` — there is no per-module `Makefile` any more. The device's shipped
set is 296 modules, split across two files: 275 come from those 278 targets (three more are
defined but not shipped: `mindone_thermal_hook.ko`, `mtk_scp_vow.ko`, `mtk_vow.ko`); 17 are
MediaTek platform drivers Kleaf compiles in-tree as `module_outs` of the `mindone` `kernel_build`
in `mindone/BUILD.bazel` (`clk-mt6789`, `pinctrl-mt6789`, `ufs-mediatek`, the DRM/dma-buf
helpers, `8250_mtk`, `cfg80211`, `mac80211`, …; an 18th, `phy-mtk-ufs.ko`, is defined there but
not currently shipped); the last 4 (`libarc4`, `rfkill`, `zram`, `zsmalloc`) are unmodified
`kernel_aarch64` in-tree modules. This split is measured directly against today's `BUILD.bazel`
files and load lists — see the Inventory section below for why a count in prose otherwise goes
stale.

MediaTek SoCs put almost everything outside the kernel proper: display, camera, audio DSP,
Wi-Fi/BT, sensors, thermal, charging, the cellular modem interface. None of it is upstream, and
iKKO published nothing. These trees were ported from MediaTek's own published sources for the
MT6789 family and corrected where they were wrong for this board.

## Inventory

[`../mindone/modules/MODULES.tsv`](../mindone/modules/MODULES.tsv) lists every tracked module:
kernel module name, `.ko` filename, which boot stage ships it (first-stage ramdisk /
`vendor_dlkm` / recovery) and the build subdirectory. It is generated — regenerate it from a
build pass rather than editing by hand. It predates the 2026-09-22..24 GKI-mixed-build cleanup
(its own header says regenerated 2026-09-15), so treat its per-module detail as a starting point
to check rather than the current split; the split above is measured against today's tree.

Trees outside the shipped 296 are superseded, recovery-only, or still under evaluation.

## Load order

[`../mindone/vendor_boot.modules.load`](../mindone/vendor_boot.modules.load) (29 modules, the
first-stage ramdisk set) and [`../mindone/vendor_dlkm.modules.load`](../mindone/vendor_dlkm.modules.load)
(267, the rest) are the real list the device loads, in order — 296 modules, no overlap between the
two files. Both are wired directly into the `mindone_images` `kernel_images` target in
`mindone/BUILD.bazel`, so a Kleaf build stages them in that order by construction. For recovery
there is a third, shorter list:
[`../mindone/vendor_boot.modules.load.recovery`](../mindone/vendor_boot.modules.load.recovery)
(148).

The order matters. Several modules hold devices other modules depend on, and
getting it wrong rarely looks like a module failure — it looks like some unrelated device's
probe being deferred forever.

## Provenance

No single upstream per module. Most combine an ACK driver skeleton with device-specific glue
worked out against this hardware. Where a file was adapted from a specific vendor source, the
origin is stated in a banner at the top of that file.

Files that arrived with MediaTek's dual GPL-2.0/BSD header keep it unchanged. Files carrying a
non-redistributable vendor notice were removed rather than published: the tree is checked to
contain none.

## Comments

Comments explain why the code differs from the vendor original, because that difference is
usually the whole point of the file. English only.

## Gates

| Tool | Checks |
|---|---|
| `mindone_abi` (Kleaf target) | every symbol in `gki/aarch64/symbols/mindone` against `gki/aarch64/abi.stg` on every build — this is what gates the shipped set today |
| `kocheck.py` | the same class of check (vermagic, `module_layout` CRC, per-symbol CRC and provider) against a `Module.symvers`, for local verification outside Kleaf |
| `tree-lint.py` | source hygiene: no foreign device names or paths in published code, no Cyrillic, comment length |
| `extpathcheck.py` | no driver gains a new source path outside this repository (current debt: 0) |
