# Porting this to another MT6789 device

The method, in the order that worked. Most of the cost is in the first two steps; the rest
follows from them.

## 1. Take the device tree from the device, not from a similar one

Extract the stock DTB from the unit's own `vendor_boot`, decompile it, and rebuild it to source
until `dtc` is silent. A DTB from another board of the same SoC will boot far enough to look
right and then fail somewhere unrelated — GPIO numbering, regulator ranges and panel timings
differ per board even when the chip does not.

## 2. Decide what is a module and what is not

MediaTek keeps almost everything out of tree. Start from the device's shipped module list
(`modules.load` in its `vendor_boot` ramdisk): that is the exact set the vendor considered
necessary, in the order they considered correct. Reproducing that list is a better goal than
reproducing any particular source tree. `mindone/modules/modules.load` here is that list for this
device, if you want to see the shape of one.

Then check your build against it by name. "Built 328 of 339" says nothing: two of this device's
modules were missing from a set that reported no failures at all.

🔴 Watch for modules whose Makefile builds the module name out of a variable:

    WLAN_CHIP_ID := 6789
    MODULE_NAME  := wlan_drv_gen4m_$(WLAN_CHIP_ID)
    obj-m        += $(MODULE_NAME).o

MediaTek writes all six connectivity drivers this way — Wi-Fi, BT, GPS, FM, WMT. Any tooling
that scans Makefiles for `obj-m` without expanding variables will not see them, will not report
them as failures either, and will hand you a set that looks complete and has no Wi-Fi in it.
That happened here twice.

Seven more of the 290 are not out-of-tree modules at all: `cfg80211`, `mac80211`, `rfkill`,
`libarc4`, `zram`, `zsmalloc` and `industrialio_triggered_buffer` come from the kernel build. A
set assembled only from out-of-tree output is missing the whole 802.11 stack, which on the device
looks like Wi-Fi never coming up rather than like a missing file.

## 3. Build modules against your own kernel, never against a binary

`modpost` resolves every symbol against `Module.symvers` at build time. That is the only step
that will tell you a module cannot work, and it happens before you flash anything. Check three
things per module: vermagic equals the target's `uname -r`, no unresolved symbols, no collision
with a driver the kernel already builds in.

## 4. Expect the ABI, not the code, to be the wall

Porting a driver across kernel versions is mostly mechanical. What is not mechanical is the
scheduler/cpufreq/devfreq API drift: the MediaTek performance and DVFS helpers were the last
group to come across and needed real rework, not renaming.

## 5. Instrument before the MMU, carefully

Early bring-up markers must be **writes only**. A read from a reserved region before the MMU is
up raises an external abort and the device stops with no output. A marker that writes a word to a
known-safe physical address survives; the same marker with a read does not, and it looks
identical to a hang in unrelated code.

## 6. Verify on the device, not in an emulator

QEMU filters build mistakes and nothing else. For anything involving real physical addresses its
verdict means nothing in either direction, and it cannot test `vendor_boot` images at all.

## 7. Keep the changes explainable

Every change here carries a comment saying what the vendor original did, why it was wrong for
this board, and what the evidence was. That is what makes the tree reviewable by someone who
does not have the hardware — and what makes it possible to re-derive a decision a year later
instead of re-discovering it.
