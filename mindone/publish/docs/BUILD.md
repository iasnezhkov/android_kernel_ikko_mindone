<!-- SPDX-License-Identifier: GPL-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
# Build

From a clean clone to a module set the device will load. Every command here is meant to be
used in an ACK workspace; replace the clone URL and prebuilt artifact path below.

## Where this is, and how it got here

The kernel binary comes from Google's **`android16-6.12-2026-09_r10` prebuilt,
CI 16736897**. This tree builds the device DTB/DTBO and modules through Kleaf DDK,
using `@gki_prebuilts//kernel_aarch64` as the base. It does not rebuild the shipped
`vmlinux` or `Image`.

`mindone/BUILD.bazel` defines eight in-tree `module_outs`; five are in the normal boot
load lists. `mindone/modules/BUILD.bazel` defines 280 `ddk_module` targets, of which
253 are in those lists. Four further modules come from GKI: **262 listed modules**.

External imports must resolve through Google's `gki/aarch64/symbols/*` lists.
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
workspace is the ACK manifest with `kernel/common` replaced by this repository:

```sh
repo init -u https://android.googlesource.com/kernel/manifest -b common-android16-6.12 \
          --depth=1 --no-tags
mkdir -p .repo/local_manifests
cat > .repo/local_manifests/mindone.xml <<'X'
<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <remove-project name="kernel/common" />
  <remote name="mindone" fetch="https://github.com/" />
  <project path="common" name="<owner>/<this-repo>" remote="mindone"
           revision="android16-6.12" clone-depth="1" />
</manifest>
X
repo sync -c -j8 --no-tags
```

Download the `kernel_aarch64` artifacts for CI **16736897**, including
`kernel_aarch64_filegroup_decl.tar.gz`, `kernel_aarch64_ddk_headers_archive.tar.gz`,
`vmlinux.symvers`, the kernel images and the GKI module archives. In
`build/kernel/kleaf/bzlmod/bazel.MODULE.bazel`, declare the local artifact directory
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
| `//common/mindone:mindone` | mixed build on `@gki_prebuilts//kernel_aarch64`: `gki_defconfig` plus `mindone/mindone.fragment`, eight in-tree module outputs and the device DTB/DTBO |
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
`mindone/modules/modules.load` contains 262 entries; its set equals the disjoint union
of the two normal boot lists wired into `mindone/BUILD.bazel`:
`mindone/vendor_boot.modules.load` (26 modules, the
first-stage set that goes in the ramdisk) and
`mindone/vendor_dlkm.modules.load` (236, the rest) —
262 modules with no overlap between the two files. Both are wired directly into the
`mindone_images` `kernel_images` target in `mindone/BUILD.bazel`, so a Kleaf build stages them
using the requested lists; order is not cosmetic; several modules hold devices that others
depend on, and getting it wrong usually looks like some unrelated device never probing.

Of the 262 listed modules, 253 come from the 280 DDK targets. Five are in-tree
`mindone` outputs: `8250_mtk`, `cfg80211`, `drm_display_helper`, `drm_dma_helper`,
and `reset-ti-syscon`. The other three declared in-tree outputs — `mac80211`,
`industrialio-triggered-buffer` and `kfifo_buf` — are not in the normal load lists.
The last four listed modules — `libarc4`, `rfkill`, `zram`, `zsmalloc` — come from GKI.
Clocks, pinctrl, UFS and the dma-buf helpers are now DDK targets.

The build selects recovery through `modules_recovery_list` in `mindone/BUILD.bazel`:
there is a third, shorter list:
`mindone/vendor_boot.modules.load.recovery`
(131 modules, counted by the same non-empty, non-comment rule).
These counts describe requested staging entries.

`mindone/modules/MODULES.tsv` is a human-readable inventory (module name, `.ko` name, boot
stage, source directory), regenerated from a build pass.

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
