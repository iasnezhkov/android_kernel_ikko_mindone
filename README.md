<!-- SPDX-License-Identifier: GPL-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
<div align="center">

# Linux 6.18 · iKKO MindOne

### An unofficial forward-port — an independent research project

**Google android17-6.18 r12 GKI core · arm64 · 4 KiB pages · Helio G99 (MT6789 / MT8781)**

</div>

---

> An independent, unofficial device-support source release.

## 🎯 What this tree is for

The device shipped with Linux 5.10. This repository ports its device support to
Google's **`android17-6.18-2026-09_r12` GKI prebuilt**, CI **16754118**
(`6.18.32-android17-5`). The shipped core is Google's binary. Device code is supplied
as loadable vendor modules and device DTB/DTBO outputs, not additions to the GKI core.

```text
core             @gki_prebuilts//kernel_aarch64
base             android17-6.18-2026-09_r12, CI 16754118
mixed build      device DTB/DTBO and in-tree platform modules
Kleaf DDK        out-of-tree device drivers
external imports Google's abi_symbollist for the release
```

The companion ROM targets **LineageOS 24 / Android 17**, kernel **FCM 202604** and a
**64-bit-only** product. Its Android HAL interfaces are AIDL; some frontends still
use proprietary legacy backends and vendor-specific HIDL interfaces.

## 📦 What is in here

| Part | Source or contract |
|---|---|
| Core selection | `@gki_prebuilts//kernel_aarch64`; exact prebuilt images, DDK headers and module metadata must stay together |
| Device tree | `mindone/dts/`, reconstructed and adapted for this board |
| Configuration | `mindone/mindone.fragment` over `gki_defconfig` |
| Device modules | `mindone/modules/BUILD.bazel` DDK targets and the platform module outputs in `mindone/BUILD.bazel` |
| Normal boot lists | `mindone/vendor_boot.modules.load` (26) and `mindone/vendor_dlkm.modules.load` (232); combined `mindone/modules/modules.load` (258) |
| Recovery list | `mindone/vendor_boot.modules.load.recovery` (130), selected by `modules_recovery_list` |
| Version forks | `mindone/modules/include/mindone/compat-*.h` only; the same module sources also build against android16-6.12 |
| KMI contract | Google's symbol list for the release; no private allowlist for additional core imports |

Counts exclude empty and comment lines. The lists describe requested loading.
The companion ROM also stages Google's `system_dlkm` modules of the same release,
including USB-network and PPP modules. Keep that archive paired with the core and
vendor set.

Proprietary userspace, firmware and prebuilt release artifacts must be obtained
separately. They are not supplied as device-support source in this repository.

## 🚀 Build

Use an Android Common Kernel workspace pinned to the `manifest.xml` of CI **16754118**,
with this repository at `common`, checked out on **`android17-6.18`**. Configure the
workspace's Kleaf prebuilt extension for that CI, including its filegroup declaration,
DDK header archive, `vmlinux.symvers`, images and GKI module archives.
The manifest and extension configuration are in [docs/BUILD.md](docs/BUILD.md).

From the workspace root:

```sh
tools/bazel build //common/mindone:mindone //common/mindone:mindone_modules_install
tools/bazel build //common/mindone:mindone_images //common/mindone:mindone_dtbo
```

`mindone` produces device DTB/DTBO and declared platform modules over the prebuilt
base; `mindone_modules_install` stages device/base modules; `mindone_images` stages
ramdisk and vendor-DLKM outputs using the load lists. `mindone_dtbo` packages the
overlay. These targets do not rebuild the shipped GKI core.

Before ROM integration, gate the **complete compiled set** against the exact build
metadata: vermagic, `module_layout`, imported symbol CRCs, providers, duplicate
built-in drivers, Google's permitted external imports and, on 6.18, the export
flag table (`__kflagstab`): a protected core export must not be imported by an
unsigned module. See [docs/BUILD.md](docs/BUILD.md).

### Install and restore

Integrate the exact prebuilt `Image.gz`, Google's `system_dlkm` modules of the same
release, vendor modules, DTB/DTBO and load metadata into the companion device tree,
then build and sign its LineageOS package. Moving from 6.12 to 6.18 changes the GKI
release, so it takes a full ROM; a kernel-only update is possible again between
sets on the same release. Follow the device repository's flashing guide for signing,
Virtual A/B installation and restoration.

Keep a tested signed known-good package and stock partition-table/firmware backup.
Before snapshot merge, a failed candidate may fall back to the retained successful
slot. After merge, install the known-good package again; selecting the former slot
is not a restore. Never overwrite mounted `super`.

## 🔧 What is actually ours

The device tree and board integration are maintained here. MediaTek driver sources
have been adapted to ACK 6.18 and the GKI module contract; every version difference
lives in `include/mindone/compat-*.h`, so the same sources still build for 6.12.

Behaviour changes of 6.18 that compile cleanly but break at runtime were found on the
device and fixed in this tree:

- V4L2 ioctl handlers receive `NULL` instead of the file handle; the JPEG and video
  codec drivers now take the handle from the file.
- Local copies of kernel-internal headers had drifted: the rpmsg endpoint operations
  (SCP IPI) and the MT6397 chip structure shared between PMIC modules now match the
  running kernel's layout.
- `of_find_device_by_node(NULL)` returns `NULL`; camera drivers no longer require
  SMI larbs that this SoC does not have.
- The AFE pointer callback returns 0 for front ends without a memory interface.

Not ported to 6.18 yet: `task_turbo`, `mtk_heap_debug`, `musb_boost` and `pmsr`.

## 🖥 Hardware

The device has a single flip camera; Android exposes it as two camera IDs.
The panel driver reports its physical size (67.06 x 77.00 mm) and offers a
VFP-only 60 Hz mode beside the 96 Hz default.

### Wi-Fi firmware pairing

The RXFILTER implementation requires the reviewed
`WIFI_RAM_CODE_soc2_2_1d_1.bin`, SHA-256
`bfb11a2b31fdd22aadb603b186e9ae0ebc98a476a7f7adf0f4a572d520abbfec`.
Use the companion device repository's proprietary-file guide for extraction, firmware source
tooling and receipt checks. An arbitrary same-named firmware file is insufficient.

## 📚 Documentation

- [Build and module-set verification](docs/BUILD.md)
- [Driver provenance and load order](docs/MODULES.md)
- [Hardware details](docs/HARDWARE.md)
- [Porting to another board](docs/PORTING.md)

## 🕰 Where this came from

The forward-port progressed from **5.10 → 6.1 → 6.12 → 6.18**. `android17-6.18` is
the current line; `android16-6.12` stays available as the previous one, and the
earlier `android14-6.1` branch is historical.

## 🔎 Useful beyond this phone

The MT6789/MT8781 driver adaptations, modular platform support, reconstructed USB
controller integration, SCMI/SSPM setup and Kleaf DDK layout may help related boards.
DTB/DTBO, panel, calibration, firmware and donor compatibility remain board-specific.

---

See `COPYING`, `LICENSES/` and individual file headers.
Retain upstream MediaTek notices and applicable dual-license terms when redistributing
modified sources; this README does not replace file-specific licenses.
