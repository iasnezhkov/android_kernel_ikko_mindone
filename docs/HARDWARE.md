# Hardware

## SoC

MediaTek Helio G99, `MT6789` / `MT8781V-CA`. arm64, 4 KiB pages, kernel release `6.12.92-4k+`.

## What works

Sound (speaker, headset, Bluetooth, microphone), voice calls and mobile data, Wi-Fi, Bluetooth,
camera, fingerprint, NFC, USB, charging, thermal management, suspend/resume. The device runs
this kernel daily under LineageOS 23.2 (Android 16).

## What does not

| | |
|---|---|
| **RPMB** | Hardware-backed key storage is unfinished — the replay-protected block fails its MAC check. Storage falls back safely; boot and normal use are unaffected. |

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

**Both slots share one `super` region.** All six logical partitions of both slots start at the
same offsets. There is no "install to the spare slot and try it" on this device, and after a ROM
install the other slot does not boot at all.

**`clk_summary` in debugfs resets the device.** Walking the clock tree reads CG registers of
powered-down domains, which trips the bus access controller. Read the clock list from the device
tree or the driver instead.

**SCMI needs a compatible the stock DTB lacks.** The upstream `arm_scmi` driver wants
`arm,scmi-shmem` on the shared-memory nodes; the stock tree has no such string. Added in
`mt6789.dtsi`. The vendor `tinysys` protocol needed one further change on the module side.
