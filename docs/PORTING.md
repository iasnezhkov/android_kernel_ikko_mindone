<!-- SPDX-License-Identifier: GPL-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
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
(`modules.load` in its `vendor_boot` ramdisk, and the equivalent list for `vendor_dlkm`): that is
the exact set the vendor considered necessary, in the order they considered correct. Reproducing
that list is a better goal than reproducing any particular source tree.
`mindone/vendor_boot.modules.load` and
`mindone/vendor_dlkm.modules.load` here are the adapted GKI/DDK lists for
this board, with unused vendor modules removed and GKI providers accounted for.

Then check your build against it by name. "No build failures" says nothing on its own: a set can
build clean and still be missing modules the device actually loads — check the output against
the load list by name, not just by exit code.

🔴 Watch for modules whose Makefile builds the module name out of a variable:

    WLAN_CHIP_ID := 6789
    MODULE_NAME  := wlan_drv_gen4m_$(WLAN_CHIP_ID)
    obj-m        += $(MODULE_NAME).o

MediaTek writes all six connectivity drivers this way — Wi-Fi, BT, GPS, FM, WMT. Any tooling
that scans Makefiles for `obj-m` without expanding variables will not see them, will not report
them as failures either, and will hand you a set that looks complete and has no Wi-Fi in it.
That happened here twice.

Not everything in the load list is out-of-tree. Here, five listed modules are
`module_outs` of the device Kleaf `kernel_build`: `8250_mtk`, `cfg80211`,
`drm_display_helper`, `drm_dma_helper` and `reset-ti-syscon`. Four more
(`rfkill`, `libarc4`, `zram`, `zsmalloc`) come from the GKI prebuilt. Clocks,
pinctrl and UFS are now DDK targets. Check actual providers and dependencies rather
than assuming every module comes from the out-of-tree group.

## 3. Build modules against the exact GKI prebuilt

Use Kleaf DDK with the matching GKI headers and symbol metadata. Here the base is
`@gki_prebuilts//kernel_aarch64`, Google's `android16-6.12-2026-09_r10`, CI 16736897.
`modpost` resolves imports at build time; the candidate gates then check ABI CRCs,
providers, built-in collisions and Google symbol-list membership for the complete set.
Do not add private kernel exports or a private symbol list to make an import pass.

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

Every change here has a commit message saying what the vendor original did, why it was wrong for
this board, and what the evidence was. That is what makes the tree reviewable by someone who
does not have the hardware — and what makes it possible to re-derive a decision a year later
instead of re-discovering it.
