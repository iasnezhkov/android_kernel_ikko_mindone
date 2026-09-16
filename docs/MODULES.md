# Drivers

`mindone/modules/` holds 339 buildable module directories and 5 638 source files. The device's
shipped set is 290 modules: 283 built from here, plus 7 the kernel builds in-tree (`cfg80211`,
`mac80211`, `rfkill`, `libarc4`, `zram`, `zsmalloc`, `industrialio_triggered_buffer`).

MediaTek SoCs put almost everything outside the kernel proper: display, camera, audio DSP,
Wi-Fi/BT, sensors, thermal, charging, the cellular modem interface. None of it is upstream, and
iKKO published nothing. These trees were ported from MediaTek's own published sources for the
MT6789 family and corrected where they were wrong for this board.

## Inventory

[`../mindone/modules/MODULES.tsv`](../mindone/modules/MODULES.tsv) lists every tracked module:
kernel module name, `.ko` filename, which boot stage ships it (first-stage ramdisk /
`vendor_dlkm` / recovery) and the build subdirectory. It is generated — regenerate it from a
build pass rather than editing by hand.

Trees outside the shipped 290 are superseded, recovery-only, or still under evaluation. The TSV
is the current answer; a count in prose would go stale.

## Load order

[`../mindone/modules/modules.load`](../mindone/modules/modules.load) is the real list: 290 modules in
the order this device loads them, with [`modules.load.recovery`](../mindone/modules/modules.load.recovery)
(121) for recovery. Pass it to `build-all-modules.py --load`; that is what produces a set the
device can boot, and it is the same list the LineageOS device tree installs as
`modules.load.ramdisk`.

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
| `kocheck.py` | vermagic, `module_layout` CRC, per-symbol CRC and provider, against the kernel's `Module.symvers` |
| `tree-lint.py` | source hygiene: no foreign device names or paths in published code |
| `extpathcheck.py` | no module Makefile points outside this repository (current debt: 0) |

The set build runs all three; a module that fails any of them is not counted as built.
