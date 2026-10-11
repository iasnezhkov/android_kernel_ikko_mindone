# Hardware

## SoC

MediaTek Helio G99, `MT6789` / `MT8781V-CA`. arm64, 4 KiB pages, kernel release
`6.12.92-android16-6-<stamp>-4k`. The device shipped with a vendor kernel that stopped at
Linux 5.10 (the `android12-5.10` line, on Android 15); this tree is 6.12.

## What works

Boots to `boot_completed` in ~21 s. Sound (speaker, headset, Bluetooth, microphone), voice calls
and mobile data, Wi-Fi, Bluetooth, NFC, camera, fingerprint, USB, charging, suspend/resume.

**Camera.** Both logical cameras of the flip module; the main IMX766 sensor's full 4096×3072
mode, brought up faster through burst I2C writes and chunked EEPROM calibration reads.

**Video.** Hardware decode and encode through the stateful V4L2 driver (`mtk_vcodec`), behind an
open Codec2 HAL.

**Graphics.** MDP.

**Thermal.** A kernel-owned skin-temperature zone at 46 °C, bound to CPU/GPU cooling in
`mindone_thermal`.

## What does not

| | |
|---|---|
| **Wi-Fi address offload** | `kalSetNetAddress()` underallocates its IPv6 address records. With no IPv4 address and one IPv6 address it allocates 29 bytes but writes 30. Retained KFENCE reports and the installed driver binary confirm the mismatch; their original boot date is unknown. The current source snapshot does not fix it. |
| **RPMB** | Hardware-backed key storage is unfinished — the replay-protected block fails its MAC check. Storage falls back safely; boot and normal use are unaffected. |
| **60 Hz panel mode** | Exists on a branch, held back: the panel reports no physical size, so the second mode reaches apps at dpi 0. |
| **High-speed capture** | 120 fps not reached. |
| **HDR** | Not implemented. |

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

**Virtual A/B uses shared logical-partition storage.** Signed partial OTAs through
`update_engine` can target the inactive slot while snapshots preserve the running system.
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
