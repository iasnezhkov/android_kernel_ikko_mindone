<!-- SPDX-License-Identifier: GPL-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
# Build

From a clean clone to a module set the device will load. Every command here is meant to be
used in an ACK workspace; replace the clone URL and prebuilt artifact path below.

## Where this is, and how it got here

The kernel binary comes from Google's **`android17-6.18-2026-09_r15` prebuilt,
CI 16990439** (`6.18.32-android17-5`). This tree builds the device DTB/DTBO and modules through Kleaf DDK,
using `@gki_prebuilts//kernel_aarch64` as the base. It does not rebuild the shipped
`vmlinux` or `Image`.

`mindone/BUILD.bazel` defines 20 in-tree `module_outs`, all 20 in the normal boot
load lists. `mindone/modules/BUILD.bazel` defines 189 `ddk_module` targets, all
189 in those lists. Four further modules come from GKI: **213 listed modules**.

External imports must resolve through Google's `abi_symbollist` of the release.
There is no private mindone symbol list or `mindone_abi` target. The candidate gate
checks the complete compiled module set for Google list membership as well as ABI
compatibility.

The load order question has a measured answer too. On this device's tree, 16 nodes take pin states,
13 take EINT interrupts from the pin controller (the main PMIC among them) and 7 take GPIOs —
**and UFS is in none of the three**, so the boot device does not depend on the pin controller and
moving it into a module cannot cost the root filesystem. Everything else reaches it through the
PMIC and waits on `-EPROBE_DEFER`, which needs `pinctrl-mt6789.ko` early in
`vendor_boot.modules.load`.

## Building it with Kleaf

Android 16 dropped non-Bzlmod kernel builds, so Bazel is the only entry point Google supports. The
workspace is the `manifest.xml` published with CI 16990439, with `kernel/common` replaced by this
repository; pinning it keeps the build tools and toolchain at the revisions Google built the core with:

```sh
repo init --standalone-manifest -u file:///absolute/path/to/gki-prebuilt/manifest.xml \
          --depth=1 --no-tags
mkdir -p .repo/local_manifests
cat > .repo/local_manifests/mindone.xml <<'X'
<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <remove-project name="kernel/common" />
  <remote name="mindone" fetch="https://github.com/" />
  <project path="common" name="<owner>/<this-repo>" remote="mindone"
           revision="android17-6.18" clone-depth="1" />
</manifest>
X
repo sync -c -j8 --no-tags
```

Download the `kernel_aarch64` artifacts for CI **16990439** (pinned in `mindone/gki-base.json`),
including `kernel_aarch64_filegroup_decl.tar.gz`, `kernel_aarch64_ddk_headers_archive.tar.gz`,
`vmlinux.symvers`, the kernel images and the GKI module archives. You can fetch the exact pinned
set with the bundled helper:

```sh
python3 mindone/modules/tools/scripts/fetch-gki-prebuilt.py \
    --gki-base mindone/gki-base.json \
    --dest /absolute/path/to/gki-prebuilt
```

In `build/kernel/kleaf/bzlmod/bazel.MODULE.bazel`, declare the local artifact directory
(replace the absolute path):

```starlark
kernel_prebuilt_ext = use_extension(
    "//build/kernel/kleaf:kernel_prebuilt_ext.bzl",
    "kernel_prebuilt_ext",
)
kernel_prebuilt_ext.declare_kernel_prebuilts(
    name = "gki_prebuilts",
    local_artifact_path = "/absolute/path/to/gki-prebuilt",
    target = "kernel_aarch64",
)
use_repo(kernel_prebuilt_ext, "gki_prebuilts")
```

```sh
tools/bazel build //common/mindone:mindone //common/mindone:mindone_modules_install
```

| target | what it is |
|---|---|
| `//common/mindone:mindone` | mixed build on `@gki_prebuilts//kernel_aarch64`: `gki_defconfig` plus `mindone/mindone.fragment`, 20 in-tree module outputs and the device DTB/DTBO |
| `//common/mindone/modules:mindone_modules` | the out-of-tree drivers, one `ddk_module` each |
| `//common/mindone:mindone_modules_install` | device and base modules staged together |

Kleaf checks the configuration fragment and uses the prebuilt's DDK headers and symbol
metadata. Keep the kernel image, GKI modules and device modules tied to that exact base.
The old source-GKI BTF memory measurements do not describe this prebuilt build path.
The standalone `build.sh` and `mindone_defconfig` have been removed; there is no supported
`make` path for the device set.

## 1. Modules

The out-of-tree drivers are `ddk_module` targets, one per driver, all defined in
`mindone/modules/BUILD.bazel` and gathered under the `mindone_modules` group; there is no
supported standalone Makefile build path for this set. Build the whole set, staged with the in-tree platform drivers:

```sh
tools/bazel build //common/mindone:mindone_modules_install
```

One module on its own:

```sh
tools/bazel build //common/mindone/modules:hf_manager
```

Count non-empty, non-comment lines to measure the requested module lists.
`mindone/modules/modules.load` contains 213 entries; its set equals the disjoint union
of the two normal boot lists wired into `mindone/BUILD.bazel`:
`mindone/vendor_boot.modules.load` (21 modules, the
first-stage set that goes in the ramdisk) and
`mindone/vendor_dlkm.modules.load` (192, the rest) —
213 modules with no overlap between the two files. Both are wired directly into the
`mindone_images` `kernel_images` target in `mindone/BUILD.bazel`, so a Kleaf build stages them
using the requested lists; order is not cosmetic; several modules hold devices that others
depend on, and getting it wrong usually looks like some unrelated device never probing.

Of the 213 listed modules, all 189 come from the 189 DDK targets. Twenty are in-tree
`mindone` outputs: `8250_mtk`, `arm_dsu_pmu`, `cfg80211`, `cqhci`, `drm_display_helper`,
`drm_dma_helper`, `i2c-dev`, `mediatek-cpufreq-hw`, `mmc_hsq`, `mtk-eint`, `mtk-sd`,
`mtk-uart-apdma`, `mtk_wdt`, `phy-generic`, `pinctrl-mtk-common-v2`, `pinctrl-paris`,
`reboot-mode`, `reset-ti-syscon`, `syscon-reboot-mode` and `thermal-generic-adc`.
The last four listed modules — `libarc4`, `rfkill`, `zram`, `zsmalloc` — come from GKI.
The clocks (the ACK 6.18 clk-mtk core as `clk-mtk-ack` and the MT6789 clock data drivers),
the power domains, the PMIC wrapper, the LVTS thermal driver (`lvts_thermal`), the
`pinctrl-mt6789` data driver, Type-C (`tcpci_rt1711h`), UFS and the dma-buf helpers
are DDK targets.

The build selects recovery through `modules_recovery_list` in `mindone/BUILD.bazel`:
there is a third, shorter list:
`mindone/vendor_boot.modules.load.recovery`
(107 modules, counted by the same non-empty, non-comment rule).
These counts describe requested staging entries.

`mindone/modules/MODULES.tsv` is the module inventory: module name, `.ko` file, source
(`ddk`, `in-tree` or `gki`), whether `mindone_modules` selects it, the load lists that name it
(`vendor_boot`, `vendor_dlkm`, `recovery`) and its source directories under `mindone/modules/`.
`python3 mindone/tests/modules-tsv-check.py --write` regenerates it from `BUILD.bazel` and the
load lists; without `--write` the check fails when the table is stale.

## 2. Check before you flash

Three conditions, in order, whatever built the module:

1. **vermagic equals the target's `uname -r`.** Anything else will not load.
2. **No unresolved symbols.** `modpost` reports these at build time; a warning here is a failure,
   not a nuisance.
3. **No collision with a built-in driver.** If the kernel already claims the device, the module
   either refuses to bind or breaks another driver's binding.

The candidate gate checks `module_layout` and imported symbol CRCs against the exact
set's kernel metadata, checks providers and built-in collisions, and runs the Google
symbol-list gate over the complete compiled module set. Module-to-module exports are
resolved within the set; remaining imports must be on Google's lists. A symbol appearing
in `abi.stg` alone is not permission to import it. Drop or replace nonlisted imports;
do not create a private mindone allowlist.

## 3. Into a flashable image

`mindone_images` in `mindone/BUILD.bazel` (`build_initramfs = True`, `build_vendor_dlkm = True`)
builds the ramdisk and `vendor_dlkm` images directly from the `mindone` kernel build and the
`mindone_modules` set, staged in the order `vendor_boot.modules.load` /
`vendor_dlkm.modules.load` give:

```sh
tools/bazel build //common/mindone:mindone_images
```

Then build the ROM as described in that repository's README. The kernel command line lives in
its `BoardConfig.mk`, not here.

Publishing a GitHub Release on this repository runs `.github/workflows/release.yml`, which
fetches the pinned GKI prebuilts from `mindone/gki-base.json`, builds `mindone.dtb`,
`dtbo.img` and the complete Kleaf DDK module set, checks vermagic and load-list completeness,
and attaches `Image.gz`, `mindone.dtb`, `dtbo.img`, `mindone-kernel-<tag>.tar.gz`,
`build-manifest.json` and `SHA256SUMS` to the release.

Flashing itself is out of scope for this repository: this device shares one `super` region
between both slots, so a wrong write can leave neither slot bootable. Read the device tree's
flashing notes first.

## If something goes wrong

| Symptom | Cause |
|---|---|
| Modules build, then nothing loads on the device | a build that did not go through Kleaf, or an ABI check that was skipped. Compare `module_layout` against what the running kernel reports |
| Device does not start, no console, no pstore | raw `Image` in `boot.img` instead of the prebuilt `Image.gz` |
| `modpost` warns about undefined symbols | missing provider or mismatched prebuilt metadata; external kernel imports must also pass Google's symbol-list gate — drop or replace nonlisted imports |
| The device boots but has no Wi-Fi | `cfg80211` (device in-tree output) or `rfkill`/`libarc4` (GKI modules) missing from the image — check `vendor_dlkm.modules.load` and dependencies |
| A driver never probes, and an unrelated device stops working | load order. Check the module's position in `vendor_boot.modules.load` / `vendor_dlkm.modules.load` |
