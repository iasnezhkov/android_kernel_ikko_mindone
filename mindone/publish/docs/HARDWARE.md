<!-- SPDX-License-Identifier: GPL-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
# Hardware

## SoC

MediaTek Helio G99, `MT6789` / `MT8781V-CA`. arm64, 4 KiB pages, kernel release
`6.18.32-android17-5-<stamp>-4k`. The device shipped with a vendor kernel that stopped at
Linux 5.10 (the `android12-5.10` line, on Android 15); this tree is 6.18.

## Subsystems

The 6.18 line pairs with the LineageOS 24 device tree.

**Camera.** One IMX766 in a flip module, exposed as camera IDs 0 and 1 for its two
positions. Both IDs use the same sensor power sequence, calibration EEPROM, DW9800W
actuator and 0x5A30 OIS MCU (`ois_mindone` on I2C4 at 100 kHz), and only one may hold
the sensor at a time; a power-on of one ID waits up to 500 ms for the other to release
the shared supplies. The full 4096×3072 mode is brought up through burst I2C writes and
chunked EEPROM calibration reads, and the `imx766_4cell` parameter selects whether the
camera HAL sees Bayer or quad-Bayer output.

**USB-C.** The RT1715 port controller at `0x4e` on i2c-6 signals on EINT13, level low.
It is a TCPM `usb-c-connector` that sinks 5 V by default, and the charger's OTG boost
supplies VBUS when the port is a source.

**Audio codec.** The MT6366 codec uses the control names of upstream `mt6358.c`, and a
larger playback volume value is a louder gain.

**Wi-Fi.** The IPv6-only allocation bug is fixed; address/mask pairs use the correct IPv4
stride, and IPv6 changes while suspended schedule offload refresh. The framework alone
owns firmware suspend policy; no per-cycle PM notifier rewrites it. Independent IPv4/IPv6 multicast RXFILTER policy
is supported during suspend with the matching reviewed firmware. APF v4 is enabled with a 2048-byte program limit. See the
[firmware pairing requirements](../README.md#wi-fi-firmware-pairing) and the
companion device repository's proprietary-file guide.

**Video.** Hardware decode and encode through the stateful V4L2 driver (`mtk_vcodec`), behind an
open Codec2 HAL. The MT6789 driver advertises HEVC Main/Main10 and VP9 Profile 0
through read-only profile controls. The JPEG encoder powers its engine per encode job
through runtime PM.

**Flash and autofocus.** The stock GPIO flashlight is selected; LM3643 does not answer
on i2c-6 and is not built. GPIO157 stays with fingerprint reset. `mainaf` sets the
DW9800W actuator address to `0x0c` before initialization, checks one-byte
register-address writes and serves both `/dev/MAINAF` and `/dev/SUBAF`, so both camera
IDs drive the one actuator.

**Battery sampling.** The PTIM v1 command returns a coherent sample with acquisition
status to the requesting socket without taking over the stock GM30 daemon registration.
The companion ROM ships a shadow gauge consumer alongside the production fuel gauge.

**Graphics.** Mali r54p1 and MDP. The donor composer contract includes hardware vsync,
idle kicks, seven atomic properties and MDP-only capability reporting; unsupported
MML functions and colour matrices are refused.

**Thermal.** Device-tree zones: CPU and GPU passive trips and a 46 °C skin trip on the board
NTC, cooling through cpufreq and the `gpufreq` cooling device of the GPU frequency
driver; the skin zone limits the GPU as well.

**Display.** The panel driver reports its physical size (67.06 x 77.00 mm) and
offers a VFP-only 60 Hz mode beside the 96 Hz default.

## Things that cost real work

Notes for anyone on similar hardware; each is a trap that is invisible until it bites.

**The device tree had to be rebuilt.** The stock DTB was extracted from the device's own
`vendor_boot` and rebuilt to source, down to zero `dtc` warnings. 18 `mt6789-*.dtsi` files.

**Early reads from carve-out memory kill the boot silently.** Before the MMU is up, a read from
a reserved region is `Device-nGnRnE`: writes post and vanish, reads must return data, so they
raise an external abort and the device stops with no message at all. Writes are safe, reads are
not — which inverts the usual debugging instinct.

**`boot.img` needs a gzipped kernel.** A raw `Image` is refused by the bootloader without a
word: no console, no pstore, no slot change.

**Virtual A/B uses shared logical-partition storage.** Deliver the paired `boot`,
`vendor_boot`, `vendor_dlkm`, `system_dlkm` and DTBO in a full signed ROM as described
by the companion device repository's flashing guide. Updates through `update_engine`
can target the inactive slot while snapshots preserve the running system.
Direct writes to `super` are not an equivalent update path. After snapshot merge, the former
slot is not necessarily an independent rollback even when both boot slots still exist. Check
slot-success metadata and snapshot state before updating, and keep the kernel and module set
from the same verified build together.

**`clk_summary` in debugfs resets the device.** Walking the clock tree reads CG registers of
powered-down domains, which trips the bus access controller. Read the clock list from the device
tree or the driver instead.

**SCMI needs a compatible the stock DTB lacks.** The upstream `arm_scmi` driver wants
`arm,scmi-shmem` on the shared-memory nodes; the stock tree has no such string. Added in
`mt6789.dtsi`. The vendor `tinysys` protocol needed one further change on the module side.
