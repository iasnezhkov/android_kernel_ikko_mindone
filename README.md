<div align="center">

# Linux 6.12 · iKKO MindOne

### An unofficial forward-port to 6.12, done by hand — an independent research project

[![Kernel](https://img.shields.io/badge/Linux-6.12.92-A42E2B?style=flat-square&logo=linux&logoColor=white)](#-build)
[![ACK](https://img.shields.io/badge/base-android16--6.12--lts-3DDC84?style=flat-square&logo=android&logoColor=white)](https://source.android.com/docs/core/architecture/kernel/android-common)
[![SoC](https://img.shields.io/badge/Helio%20G99-MT6789%20%2F%20MT8781-0071C5?style=flat-square)](#-hardware)
[![Modules](https://img.shields.io/badge/modules-290%20%2F%20290-success?style=flat-square)](docs/MODULES.md)
[![License](https://img.shields.io/badge/license-GPL--2.0-blue?style=flat-square)](COPYING)

**arm64 · 4 KiB pages · `6.12.92-4k+`**

</div>

---

> ### ⚠️ Experimental, research project, still moving
> An independent, unofficial bring-up — not a vendor release, and built with no vendor support,
> assistance, or collaboration of any kind. It runs as a daily driver, but things change
> between commits, some hardware is unfinished, and nothing here is certified by anyone.
> Read [Status](#-status) before depending on it.

## 🎯 What two commands give you

From a clean clone: the `Image`, the DTB, and the **290 modules the device actually boots on**.

```
module_layout    0x35c04eb7
kernel.release   6.12.92-4k+
modules built    290 / 290 — nothing failing the ABI gate
```

Those numbers are what a correct build produces. If yours differ, something is wrong, and
**[docs/BUILD.md](docs/BUILD.md)** says what.

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
| **Kernel** | Google ACK `android16-6.12-lts` (`6.12.92`) + ~60 commits of device support |
| **Device tree** | `mindone.dts` + 18 `mt6789-*.dtsi`, rebuilt from the stock DTB, **zero `dtc` warnings** |
| **Config** | `arch/arm64/configs/mindone_defconfig`, 752 enabled options |
| **Drivers** | `mindone/modules/` — 339 buildable directories, 5 638 source files |
| **Load order** | `mindone/modules/modules.load` — the real 290, in the order the device loads them |

🚫 No blobs, no firmware, no bootloader, no prebuilt binaries — **verified by file content, not
by extension.** Proprietary userspace is extracted by each user from their own device.

## 🚀 Build

```sh
TOOLCHAIN=/opt/toolchains/llvm-19.1.4-aarch64 ./build.sh ../k612-out
```

> 🔴 **The clang version is load-bearing.** clang **19.1.4** (the kernel.org prebuilt). A
> different one changes symbol CRCs and `module_layout`, and every module then refuses to load
> without saying why.

Modules, ABI checking, and the path to a flashable image → **[docs/BUILD.md](docs/BUILD.md)**.

## 🔧 What is actually ours

MediaTek keeps almost everything outside the kernel proper, and no device-specific material was
available to start from. So most of this repository is work, not configuration.

<table>
<tr><td width="30%"><b>172 of 339</b><br><sub>module directories</sub></td>
<td>carry our own fixes — <b>317 files</b>, marked <code>MINDONE</code> where changed. Vendor code
that was wrong for this board, targeted another SoC revision, or did not compile against a modern
kernel. Every change says what the original did and why it had to go.</td></tr>
<tr><td><b>10 modules</b><br><sub>written from scratch</sub></td>
<td>for problems no vendor code solved — see the table below.</td></tr>
<tr><td><b>1 driver</b><br><sub>reconstructed</sub></td>
<td><code>musb_hdrc_recon</code>, a rebuilt MediaTek musb.</td></tr>
</table>

<details open>
<summary><b>The ten written from scratch</b></summary>

| | |
|---|---|
| `mindone_thermal` | thermal zones the stock tables never described |
| `mindone_pmic_guard` | re-arms PMIC interrupt enables after resume; a lost `pwrap` write used to leave the power key dead until reboot |
| `mindone_ufs_screen`, `mindone_rfldo` | storage and RF regulator behaviour specific to this board |
| `mindone_panicdump`, `mindone_ctlfail`, `mindone_ptydbg` | bring-up instrumentation that kept paying off, so it stayed |
| `mindone_mpuperm`, `mindone_ntty_restore`, `mindone_usblock_shim` | memory-protection, line-discipline and USB wakelock workarounds |

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
| **Audio** | speaker, headset, Bluetooth, microphone, in-call |
| **Cellular** | calls, SMS, mobile data |
| **Wireless** | Wi-Fi, Bluetooth, NFC |
| **Camera** | both logical cameras of the flip module |
| **Security** | fingerprint |
| **System** | USB (adb/MTP), charging, thermal, suspend/resume, 96 Hz display |

| 🚧 Unfinished | |
|---|---|
| **RPMB** | hardware-backed key storage fails its MAC check; storage falls back safely, boot unaffected |
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
  `android14-6.1` → `android16-6.12-lts`), including the API drift that actually hurts:
  scheduler, cpufreq and devfreq changes the MediaTek performance and DVFS helpers depend on.
- **~340 out-of-tree MediaTek modules** (290 loaded at boot) building against a current ACK — display, camera, audio
  DSP, Wi-Fi/BT, sensors, thermal, charging, UFS, USB, the CCCI modem interface, co-processors
  (`sspm_v3`, `mcupm`, `vcp`, `adsp`), memory and bus protection, and the GPU stack.
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
