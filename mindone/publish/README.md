<!-- SPDX-License-Identifier: GPL-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
<div align="center">

# Linux 6.12 · iKKO MindOne

### An unofficial forward-port — an independent research project

**Google android16-6.12 r10 GKI core · arm64 · 4 KiB pages · Helio G99 (MT6789 / MT8781)**

</div>

---

> An independent, unofficial device-support source release.

## 🎯 What this tree is for

The device shipped with Linux 5.10. This repository ports its device support to
Google's **`android16-6.12-2026-09_r10` GKI prebuilt**, CI **16736897**.
The shipped core is Google's binary. Device code is supplied as loadable vendor
modules and device DTB/DTBO outputs, not additions to the GKI core.

```text
core             @gki_prebuilts//kernel_aarch64
base             android16-6.12-2026-09_r10, CI 16736897
mixed build      device DTB/DTBO and in-tree platform modules
Kleaf DDK        out-of-tree device drivers
external imports Google's gki/aarch64/symbols/* lists
```

The companion ROM targets **LineageOS 24.0 / Android 17** on its default `lineage-24.0`
branch and LineageOS 23.2 / Android 16 on `lineage-23.2`, with **FCM 202504** and a
**64-bit-only** product. Its Android HAL interfaces are AIDL; some frontends still
use proprietary legacy backends and vendor-specific HIDL interfaces.

## 📦 What is in here

| Part | Source or contract |
|---|---|
| Core selection | `@gki_prebuilts//kernel_aarch64`; exact prebuilt images, DDK headers and module metadata must stay together |
| Device tree | `mindone/dts/`, reconstructed and adapted for this board |
| Configuration | `mindone/mindone.fragment` over `gki_defconfig` |
| Device modules | `mindone/modules/BUILD.bazel` DDK targets and the platform module outputs in `mindone/BUILD.bazel` |
| Normal boot lists | `mindone/vendor_boot.modules.load` (26) and `mindone/vendor_dlkm.modules.load` (236); combined `mindone/modules/modules.load` (262) |
| Recovery list | `mindone/vendor_boot.modules.load.recovery` (131), selected by `modules_recovery_list` |
| KMI contract | Google's symbol lists; no private allowlist for additional core imports |

Counts exclude empty and comment lines. The lists describe requested loading.
The companion ROM also stages selected Google GKI modules in `system_dlkm`, including USB-network and
PPP modules. Keep that archive paired with the core and vendor set.

Proprietary userspace, firmware and prebuilt release artifacts must be obtained
separately. They are not supplied as device-support source in this repository.

## 🚀 Build

Use an Android Common Kernel workspace for `common-android16-6.12`, with this
repository at `common`, checked out on **`android16-6.12`**. Configure the workspace's
Kleaf prebuilt extension for **CI 16736897**, including its filegroup declaration,
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
overlay. These targets do not rebuild the shipped GKI core. The retired standalone
`build.sh` / `mindone_defconfig` route is not the supported device build path.

Before ROM integration, gate the **complete compiled set** against the exact build
metadata: vermagic, `module_layout`, imported symbol CRCs, providers, duplicate
built-in drivers and Google's permitted external imports. See [docs/BUILD.md](docs/BUILD.md).

### Install and restore

Integrate the exact prebuilt `Image.gz`, matching GKI modules, vendor modules,
DTB/DTBO and load metadata into the companion device tree, then build and sign
its LineageOS package. Follow that repository's flashing guide for signing,
Virtual A/B installation and restoration.

Deliver the paired `boot`, `vendor_boot`, `vendor_dlkm`, `system_dlkm` and DTBO
with donor userspace inside a full signed ROM. The package also carries the stock firmware
images the device repository extracts; the two TEE partitions differ in size, so a TEE
image must fit the smaller one. A kernel-only partial update cannot carry this set.

Keep a tested signed known-good package and stock partition-table/firmware backup.
Before snapshot merge, a failed candidate may fall back to the retained successful
slot. After merge, install the known-good package again; selecting the former slot
is not a restore. Direct Linux-host fastboot recovery requires a complete matched
image set and current partition geometry. Never overwrite mounted `super`.

## 🔧 What is actually ours

The device tree and board integration are maintained here. MediaTek driver sources
have been adapted to ACK 6.12 and the GKI module contract. The reconstructed USB
controller support and board-specific UFS behavior are part of that work.

Recent source work includes:

- Graphics: Mali r54p1, display idle handling and the donor composer contract: hardware
  vsync, idle kicks, atomic properties, MDP-only capabilities and mode/debug ioctls.
  CCORR composes calibration and Night Light and rejects unsupported matrix domains.
- Media: VCU v2 integration, decoder negotiation before VSI initialization and the
  encoder layout for the Lenovo ARM64 userspace peer. The MT6789 decoder advertises
  read-only HEVC Main/Main10 and VP9 Profile 0 menus; VP9 Profile 2 is not advertised.
- Connectivity: gen4m firmware pairing and APF support, framework-owned suspend policy,
  suspended IPv6 address refresh and corrected IPv4 address/mask pairs; MDDP
  connection-mode support and USB/Type-C teardown and transition handling.
- Board support: cache-parity reporting, voice-wakeup modules and AW862xx nominal
  resonance configuration. The stock GPIO flashlight is selected again; no LM3643
  answered on i2c-6. LM3643 remains non-shipping, and GPIO157 belongs to fingerprint reset.
- Camera autofocus: `mainaf` retains the DW9800W backend and sets its 7-bit I2C
  address to `0x0c` before the first transfer; a register-address write expects one
  byte. The unused `subaf`, `sub2af`, `main2af` and `main3af` wrappers are no longer
  selected or loaded; their DDK definitions remain buildable.
- Storage and USB host: while the UFS clock is scaled up to 192 MHz a helper bound to the
  UFS host holds a 650 mV Vcore floor through the DVFSRC regulator. MUSB QMU host transfers
  flush the TX FIFO correctly, mark every ISO frame on errors, program the high-bandwidth
  multiplier on TX and return a full GPD ring to usbcore instead of stranding the URB.
- Power and safety: panel, charger and UFS controllers use C and standard kernel APIs;
  DT zones and Mali devfreq replace the private thermal adapter. Charger USB-current
  policy, a read-only current-command node and driver hardening remain. The status-carrying
  PTIM sample command validates its request and replies to the requesting netlink
  socket, allowing the companion shadow gauge alongside GM30 without registering
  as the stock daemon. Stock commands retain their registered-daemon PID check.

These source changes require matching userspace.

## 🖥 Hardware

The set drives the cellular modem, Wi-Fi, Bluetooth, NFC, audio, the cameras, hardware
video, fingerprint, USB host and gadget, charging, suspend/resume and the 96 Hz display.

The panel driver reports its physical size (67.06 x 77.00 mm) and offers a
VFP-only 60 Hz mode beside the 96 Hz default.

### Wi-Fi firmware pairing

The RXFILTER implementation requires the reviewed
`WIFI_RAM_CODE_soc2_2_1d_1.bin`, SHA-256
`bfb11a2b31fdd22aadb603b186e9ae0ebc98a476a7f7adf0f4a572d520abbfec`.
Use the companion device repository's proprietary-file guide for extraction, firmware source
tooling and receipt checks. An arbitrary same-named firmware file is insufficient.

With that firmware, suspend-time multicast filtering supports allow, block and restore
policies alongside unicast controls. APF v4 is enabled with a 2048-byte program limit.

## 📚 Documentation

- [Build and module-set verification](docs/BUILD.md)
- [Driver provenance and load order](docs/MODULES.md)
- [Hardware details](docs/HARDWARE.md)
- [Porting to another board](docs/PORTING.md)

## 🕰 Where this came from

The forward-port progressed from **5.10 → 6.1 → 6.12**. Use `android16-6.12` for
current device support. The earlier `android14-6.1` branch is historical.

## 🔎 Useful beyond this phone

The MT6789/MT8781 driver adaptations, modular platform support, reconstructed USB
controller integration, SCMI/SSPM setup and Kleaf DDK layout may help related boards.
DTB/DTBO, panel, calibration, firmware and donor compatibility remain board-specific.

---

See `COPYING`, `LICENSES/` and individual file headers.
Retain upstream MediaTek notices and applicable dual-license terms when redistributing
modified sources; this README does not replace file-specific licenses.
