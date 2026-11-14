<!-- SPDX-License-Identifier: GPL-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
<div align="center">

# Linux 6.18 · iKKO MindOne

### An unofficial forward-port — an independent research project

**Google android17-6.18 r13 GKI core · arm64 · 4 KiB pages · Helio G99 (MT6789 / MT8781)**

</div>

---

> An independent, unofficial device-support source release.

## 🎯 What this tree is for

The device shipped with Linux 5.10. This repository ports its device support to
Google's **`android17-6.18-2026-09_r13` GKI prebuilt**, CI **16831611**
(`6.18.32-android17-5`). The shipped core is Google's binary. Device code is supplied
as loadable vendor modules and device DTB/DTBO outputs, not additions to the GKI core.

```text
core             @gki_prebuilts//kernel_aarch64
base             android17-6.18-2026-09_r13, CI 16831611
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
| Normal boot lists | `mindone/vendor_boot.modules.load` (21) and `mindone/vendor_dlkm.modules.load` (193); combined `mindone/modules/modules.load` (214) |
| Recovery list | `mindone/vendor_boot.modules.load.recovery` (109), selected by `modules_recovery_list` |
| Version forks | `mindone/modules/include/mindone/compat-*.h` only |
| KMI contract | Google's symbol list for the release; no private allowlist for additional core imports |

Counts exclude empty and comment lines. The lists describe requested loading.
The companion ROM also stages Google's `system_dlkm` modules of the same release,
including USB-network and PPP modules. Keep that archive paired with the core and
vendor set.

Proprietary userspace, firmware and prebuilt release artifacts must be obtained
separately. They are not supplied as device-support source in this repository.

## 🚀 Build

Use an Android Common Kernel workspace pinned to the `manifest.xml` of CI **16831611**,
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
have been adapted to ACK 6.18 and the GKI module contract; every kernel-version
difference lives in `include/mindone/compat-*.h`.

Where the Android Common Kernel already has a driver for this SoC, it replaces the
vendor copy: `arm_dsu_pmu`, `mediatek-cpufreq-hw`, `thermal-generic-adc`, `reboot-mode`,
`syscon-reboot-mode`, `phy-generic`, `mtk-uart-apdma`, `mtk-sd` with `cqhci` and
`mmc_hsq`, `mtk_wdt` and the pin-controller libraries `pinctrl-paris`,
`pinctrl-mtk-common-v2` and `mtk-eint` are built from this tree as in-tree modules.
`i2c-dev` is added the same way, because the GKI core does not provide `/dev/i2c-*`.

Other ACK drivers lack MT6789 support in r13 or are not built as GKI modules. They are
carried as DDK modules copied from the r13 sources, with what this board needs added:

- **Clocks.** `clk-mtk-ack` is the clk-mtk core of r13 as one module. The MT6789 clock
  modules are data drivers over it and keep their module names, compatibles, clock names
  and dt-bindings IDs. The core differs from r13 in two places the MT6789 hardware needs:
  a mux is updated on every enable, and PLL rate changes go through the FHCTL hook.
- **Power domains.** `mtk-pm-domains` describes the fifteen MT6789 domains with their
  bus-protection steps, and waits 100 µs after the power acknowledgement before it
  releases clocks and isolation, as MediaTek's MT6789 sequence does.
- **PMIC wrapper.** `mtk-pmic-wrap` gains the MT6789 register map and capabilities.
- **Pin controller.** `pinctrl-mt6789` is only the SoC data, on the ACK libraries above.
- **USB Type-C.** The RT1715 port controller runs on the kernel's TCPM through
  `tcpci_rt1711h`, as a `usb-c-connector` that sinks 5 V by default. The driver
  dispatches its own alerts, because r13 does not export `tcpci_irq()`, and turns its
  VBUS supply off at shutdown. `extcon_mtk_usb` provides the role and mode switch, the
  charger's PD adapter is backed by the TCPM power supply, and the headset detector
  takes Type-C audio accessories from `extcon_mtk_usb`.

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

Board-specific corrections:

- **Camera.** The flip camera is one IMX766 with one DW9800W actuator and one
  calibration EEPROM, reached through two sensor indices. Both indices power the sensor
  the same way and only one may hold its supplies at a time; camera 1 reads the same
  EEPROM and drives the same actuator through `/dev/SUBAF`. The IMX766 driver writes the
  mode tables it declares, reports binning, phase-detection and gain ranges as the tuning
  expects, keeps frame-length bookkeeping consistent with the sensor, uses fewer I2C
  transactions per exposure update, enables the temperature sensor and no longer
  advertises stagger HDR or seamless mode switching. The flip position follows the Hall
  sensor level rather than single edges. A power-on of one index waits up to 500 ms for
  the other index to release the shared supplies. The `imx766_4cell` parameter selects
  the sensor format the camera HAL sees (Bayer by default, or quad Bayer for software or
  on-sensor remosaic); phase-detection pixels go out on data type 0x31, matching the
  virtual-channel description, and the 4:3 modes report the 27.8 fps they run at.
- **I2C.** `i2c_mt65xx` can run the transfers to up to three 7-bit targets at their own
  rate, from 10 kHz to 1 MHz (Fm+), slower or faster than the DT bus speed, set by a
  client driver or the `slow_target` parameter. No target is set by default.
- **Audio codec.** The MT6366 codec offers the controls of upstream `mt6358.c`
  (`Headphone Volume`, one stereo `PGA Volume`), and its playback volumes count like
  upstream: a larger value is a louder gain.
- **JPEG encoder.** `mtk_jpeg` holds one runtime-PM reference per encode job and drops it
  when the job ends; system sleep waits for the running job and suspends the engine.
- **GPU thermal.** Mali r54 has no devfreq on this board, so `mtk_gpufreq_mt6789`
  registers a `gpufreq` cooling device and the GPU and skin thermal zones map to it.
  MediaTek's dormant devfreq governor and cooling code are removed from r54.
- **Charging.** A USB host's current budget is used only on a real Type-C port with a
  host present, and a source without a host never puts the charger into HiZ. CPU and
  modem power throttling register with the low-battery, battery over-current and
  battery-percent detectors.

Not built, because nothing on this device uses them: the audio DSP chain, the video
companion processor, MML, MMProfile, SLBC, the dummy Trustonic driver, the C2K USB
bypass, `cpuqos_v3`, `musb_boost`, `flashlights-lm3643` and a set of MediaTek debug,
logging and stub drivers for blocks this SoC does not have; their sources are removed.
`mtk_heap_debug` is not built either; its headers are still shared with other modules.
The vendor forks that the ACK drivers above replace, the PMIC wrapper, the SCP power
domains and the Richtek Type-C stack, are no longer built; their directories remain in
the tree. The forks of the clock core and the pin controller are removed.

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
