<div align="center">

# Linux 6.12 · iKKO MindOne

### An unofficial forward-port to 6.12, done by hand — an independent research project

[![Kernel](https://img.shields.io/badge/Linux-6.12.92-A42E2B?style=flat-square&logo=linux&logoColor=white)](#-build)
[![ACK](https://img.shields.io/badge/base-android16--6.12--2026--09-3DDC84?style=flat-square&logo=android&logoColor=white)](https://source.android.com/docs/core/architecture/kernel/android-common)
[![SoC](https://img.shields.io/badge/Helio%20G99-MT6789%20%2F%20MT8781-0071C5?style=flat-square)](#-hardware)
[![Modules](https://img.shields.io/badge/modules-296-success?style=flat-square)](docs/MODULES.md)
[![License](https://img.shields.io/badge/license-GPL--2.0-blue?style=flat-square)](COPYING)

**arm64 · 4 KiB pages · `6.12.92-android16-6-<stamp>-4k`**

</div>

---

> ### ⚠️ Experimental, research project, still moving
> An independent, unofficial bring-up — not a vendor release, and built with no vendor support,
> assistance, or collaboration of any kind. It runs as a daily driver, but things change
> between commits, some hardware is unfinished, and nothing here is certified by anyone.
> Read [Status](#-status) before depending on it.

## 🎯 What this tree is for

The device shipped with a vendor kernel that stopped at Linux 5.10 (the `android12-5.10` line, on Android 15).
This tree carries it forward to a current, actively developed kernel line.

The kernel binary is **Google's**. This repository is the device half: the tree that describes the
hardware, the configuration fragment that turns on what this board needs, and the 296 drivers it
loads on top of a Generic Kernel Image.

```
kernel           //common:kernel_aarch64 — Google's GKI, unmodified gki_defconfig
this repository  device tree + mindone.fragment + 278 out-of-tree drivers + 18 in-tree platform drivers
contract         gki/aarch64/symbols/mindone — the symbols those drivers need from GKI
module_layout    0xf6779cbd
```

> **Status.** The Kleaf GKI mixed build below is what runs on the device today, not merely the
> direction: `kernel_aarch64` builds from an unmodified `gki_defconfig` + Google's own fragment,
> untouched by this tree, and the MediaTek platform drivers are either `module_outs` of a
> separate `mindone` kernel_build on top of it or `ddk_module` targets in `mindone/modules/` —
> never inside `kernel_aarch64` itself. Measured on the device (28.09): `module_layout
> 0xf6779cbd`, 296 modules loaded, and the running `vmlinux` has no KMI difference from Google's
> own ABI file for this base (0 of 776 documented imports missing). The standalone `make` path
> this document used to describe no longer builds a module set that matches the device — what
> that means for local iteration is in **[docs/BUILD.md](docs/BUILD.md)**.

No confidential vendor material went into this, and there was no vendor support or
collaboration of any kind. The base is Google's Android Common Kernel. The device tree was
**reconstructed by hand from the stock DTB** extracted off a retail unit — a compiled binary, not
source, decoded and rebuilt from scratch as `.dts`/`.dtsi`. The out-of-tree drivers started from
**MediaTek's own published open-source kernel sources** for this SoC family — the GPL sources
MediaTek is required to release for its chips, not anything confidential or device-specific — then
were fixed and adapted board by board; **"What is actually ours"** below says how much of that had
to change.

## 📦 What is in here

| | |
|---|---|
| **Kernel** | Google ACK `android16-6.12-2026-09` (`6.12.92`), consumed as `//common:kernel_aarch64` — this repository builds only the drivers on top of it |
| **Device tree** | `mindone.dts` + 18 `mt6789-*.dtsi`, rebuilt from the stock DTB, **zero `dtc` warnings** |
| **Config** | `mindone/mindone.fragment` on top of `gki_defconfig`, checked on every build by Kleaf's `check_defconfig`. `arch/arm64/configs/mindone_defconfig` remains for the legacy kernel-only `make` path, which no longer has a matching module build |
| **Drivers** | `mindone/modules/` — 278 `ddk_module` targets, 3 257 C sources and headers |
| **KMI** | `gki/aarch64/symbols/mindone` — what those drivers need from the kernel, in the format Google's own tooling writes |
| **Load order** | `mindone/vendor_boot.modules.load` + `mindone/vendor_dlkm.modules.load` — the real 296, in the order the device loads them |

🚫 No blobs, no firmware, no bootloader, no prebuilt binaries — **verified by file content, not
by extension.** Proprietary userspace is extracted by each user from their own device.

## 🚀 Build

Android 16 dropped non-Bzlmod kernel builds, so **Kleaf**, the Bazel build Google ships with the
ACK, is the supported entry point. The workspace is the ACK repo manifest with `kernel/common`
replaced by this repository:

```sh
tools/bazel build //common/mindone:mindone_modules_install
```

A plain `make` build of the kernel image alone still works, without a full Kleaf workspace — see
**[docs/BUILD.md](docs/BUILD.md)** for what it is still useful for and, more importantly, what it
no longer builds:

```sh
TOOLCHAIN=/opt/toolchains/llvm-19.1.4-aarch64 ./build.sh ../k612-out
```

> 🔴 **The clang version matters.** clang decides symbol CRCs and `module_layout`. Build the
> kernel with one major version and the modules with another, and every module refuses to load
> without saying why — which is one reason the Kleaf path above, where both come from the same
> build, is the one that ships.

> 🔴 **Memory.** Generating BTF (`pahole`) peaks near **14.4 GiB**. Under that ceiling the build
> dies with a bare `Killed` and `FAILED: load BTF from vmlinux: Invalid argument`, naming nothing.
> In a container or VM, check the limit that applies to the build, not the machine's total.

Modules, ABI checking, and the path to a flashable image → **[docs/BUILD.md](docs/BUILD.md)**.

## 🔧 What is actually ours

MediaTek keeps almost everything outside the kernel proper, and no device-specific material was
available to start from. So most of this repository is work, not configuration.

<table>
<tr><td width="30%"><b>most</b><br><sub>of 278 driver directories</sub></td>
<td>carry local fixes: vendor code that was wrong for this board, targeted another SoC revision,
or did not compile against a modern kernel. What changed and why is in the commit history for
each driver.</td></tr>
<tr><td><b>2 modules</b><br><sub>written from scratch</sub></td>
<td>for problems no vendor code solved — see the table below.</td></tr>
<tr><td><b>1 driver</b><br><sub>reconstructed</sub></td>
<td><code>musb_hdrc_recon</code>, a rebuilt MediaTek musb (built today as the `musb_hdrc` target).</td></tr>
</table>

<details open>
<summary><b>The two written from scratch</b></summary>

| | |
|---|---|
| `mindone_thermal` | thermal zones the stock tables never described |
| `mindone_ufs_screen` | storage behaviour specific to this board |

</details>

**🔌 The USB one is worth calling out.** `musb_hdrc_recon` fixes a defect that made the device
unable to sleep *at all*: on cable unplug the gadget teardown runs before the disconnect work, so
the driver's own release path was unreachable and the "USB suspend lock" stayed held forever.

**⬆️ Upstream work pulled back.** UFS MediaTek clock-scaling and Vcore binding from **v6.17** are
merged into this 6.12 tree. A systematic diff of the MediaTek clock drivers against **v6.18** was
done and closed with **no delta needed** — a negative result worth stating, because it means the
clock layer here is already current.

## 🖥 Hardware

| ✅ Works | |
|---|---|
| **Boot** | ~21 s to `boot_completed` |
| **Audio** | speaker, headset, Bluetooth, microphone, in-call |
| **Cellular** | calls, SMS, mobile data |
| **Wireless** | Wi-Fi, Bluetooth, NFC |
| **Camera** | both logical cameras of the flip module; the main IMX766 sensor's full 4096×3072 mode, brought up faster through burst I2C writes and chunked EEPROM calibration reads |
| **Video** | hardware decode and encode through the stateful V4L2 driver (`mtk_vcodec`), behind an open Codec2 HAL |
| **Graphics** | MDP |
| **Security** | fingerprint |
| **System** | USB (adb/MTP), charging, suspend/resume, 96 Hz display |
| **Thermal** | a kernel-owned skin-temperature zone at 46 °C, bound to CPU/GPU cooling in `mindone_thermal` |

| 🚧 Unfinished | |
|---|---|
| **RPMB** | hardware-backed key storage fails its MAC check; storage falls back safely, boot unaffected |
| **60 Hz panel mode** | exists on a branch, held back: the panel reports no physical size, so the second mode reaches apps at dpi 0 |
| **High-speed capture** | 120 fps not reached |
| **HDR** | not implemented |
| **vSIM** | parked deliberately, not attempted |

**🔁 Reproducible.** Two builds from the same tree and toolchain now produce a **byte-identical
`Image`**. `build.sh` pins the build user, host and `SOURCE_DATE_EPOCH`; before that the account
name and the build moment were compiled in, and two builds differed by ~0.7 %. Verified on one
machine into one output path — a different absolute build path is not separately tested.

## 📊 Status

This is the kernel the device runs every day under LineageOS 23.2 (Android 16) — not a tree that
merely compiles. But it is an independent bring-up by one person: expect rough edges, treat
anything unusual as probably known, and keep a way to restore the stock firmware.

Current focus is testing and optimisation rather than new features. Kernel **6.18** and
**Android 17** are where this goes next; both have been looked at, neither is committed to.

### What has and has not been tested

| | |
|---|---|
| **Daily use** | Yes — this is the author's phone, running this kernel every day |
| **VTS / CTS** | **No.** Neither has been run. No compatibility evidence exists, and none is claimed |
| **Any certification** | None, by anyone |
| **Reproducible builds** | Byte-identical `Image` on one machine into one output path. A different absolute build path is untested |
| **Module ABI** | Checked numerically on every build (`module_layout`, per-symbol CRC) — see [docs/BUILD.md](docs/BUILD.md) |

## 📚 Documentation

| | |
|---|---|
| **[docs/BUILD.md](docs/BUILD.md)** | Clean clone → kernel → modules → verified set |
| **[docs/MODULES.md](docs/MODULES.md)** | The driver trees: provenance, load order, gates |
| **[docs/HARDWARE.md](docs/HARDWARE.md)** | The device, and the behaviours that are invisible until they bite |
| **[docs/PORTING.md](docs/PORTING.md)** | Reusing this on another MT6789/MT8781 board |

## 🕰 Where this came from

A forward-port. The device shipped on `android12-5.10`; the work went **5.10 → 6.1 → 6.12**, and
each step had to boot before the next could start.

This repository carries two branches. **`android16-6.12`** is the one to build against --
that is where the drivers, the device tree, and all ongoing work live. **`android14-6.1`** is
the earlier step: the first forward-port of this device off its original vendor kernel, where
the device tree and most of the driver work were first proven on real hardware. It is kept as
history, unmaintained -- see its own README for what that means. The drivers are not duplicated
there: `mindone/modules/` lives only on `android16-6.12` and builds against either kernel.

## 🔎 Useful beyond this phone

Most of the work is SoC-level, not board-level, so it transfers to any **MT6789 / MT8781
(Helio G99)** bring-up on a modern kernel:

- **6.12 on a platform that shipped with 5.10** — the full path (`android12-5.10` →
  `android14-6.1` → `android16-6.12-2026-09`), including the API drift that actually hurts:
  scheduler, cpufreq and devfreq changes the MediaTek performance and DVFS helpers depend on.
- **278 out-of-tree MediaTek modules** (275 loaded at boot, plus 17 more MediaTek platform
  drivers Kleaf compiles in-tree and 4 stock GKI modules — 296 in total) building against a
  current ACK — display, camera, audio DSP, Wi-Fi/BT, sensors, thermal, charging, UFS, USB, the
  CCCI modem interface, co-processors (`sspm_v3`, `mcupm`, `vcp`, `adsp`), memory and bus
  protection, and the GPU stack.
- **A device tree reconstructed from a stock DTB** to zero `dtc` warnings — 18 `mt6789-*.dtsi`
  covering clocks, pinctrl, regulators, display, camera, audio, thermal, reserved memory.
- **SCMI/SSPM on MediaTek**: upstream `arm_scmi` needs `arm,scmi-shmem` on the shared-memory
  nodes, which stock trees do not carry; the vendor `tinysys` protocol (id `0x80`) needs a
  `vendor_id` on kernels ≥ 6.9.
- **A reconstructed `musb` gadget/host driver**, including the unplug path that otherwise holds a
  wakeup source and stops the device suspending entirely.

<details>
<summary><sub>Search terms</sub></summary>
<sub>

MT6789, MT8781, MT8781V-CA, Helio G99, MediaTek, mtk, android16-6.12, Android Common Kernel, ACK,
LineageOS 23, Android 16, out-of-tree kernel modules, vendor modules, GKI, `mtk-cmdq`,
`wlan_drv_gen4m`, `eccci`, `ccci_mdinit`, `hf_manager`, `soc_temp_lvts`, `ufs-mediatek`, `sspm`,
`mcupm`, `tinysys`, `device_apc_mt6789`, `imgsensor`, `mt6366`, `snd_soc_mt6789_afe`, `musb_hdrc`,
gen4m, MOLY modem firmware, `md1img`.

</sub>
</details>

---

<div align="center">
<sub>

**GPL-2.0**, as upstream Linux — see [COPYING](COPYING) and [LICENSES/](LICENSES).
The reconstructed device tree and everything in `mindone/modules/` are under the same terms.
Files carrying an upstream MediaTek dual GPL-2.0/BSD header keep it unchanged.

</sub>
</div>
