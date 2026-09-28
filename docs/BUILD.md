# Build

From a clean clone to a module set the device will load. Every command here is meant to be
copied as-is, except the clone URL in step 2, which is the only placeholder.

## Where this is, and how it got here

This is the arrangement Google intends for a device kernel, and it is what the device runs
today, not a direction it is moving toward: **the kernel binary comes from GKI, and this tree
builds only the drivers on top of it.** The standalone build this document used to walk
through — this tree compiling its own `vmlinux` — no longer produces anything that matches the
device: `mindone/modules` moved to a single Bazel `BUILD.bazel` (one `ddk_module` target per
driver) and dropped the per-directory `Makefile`s that build required. `module_layout
0x35c04eb7` and the `6.12.92-4k+` vermagic it used to produce belong to that retired path.

Measured on the running device (28.09), built by Kleaf: `module_layout 0xf6779cbd`, 296 modules
loaded, and the `vmlinux` has no KMI difference from Google's own `gki/aarch64/abi.stg` for this
base. As of the `android16-6.12-2026-09` base, the split below is in place on the Kleaf side:

| | |
|---|---|
| ✅ done | `//common:kernel_aarch64` (the actual `vmlinux`/`Image`) builds from stock `gki_defconfig` plus only Google's own `tv_gki.fragment` — zero diff against `origin/android16-6.12-2026-09` on both files |
| ✅ done | `//common/mindone:mindone` (`mindone/BUILD.bazel`) is a real mixed build: `base_kernel = "//common:kernel_aarch64"`, its `outs` are only the two device DTB/DTBO blobs, no `vmlinux` — the MediaTek platform code (clk-mt6789, pinctrl-mt6789, ufs-mediatek, the DRM display/GEM-DMA helpers, the dma-buf deferred-free/page-pool helpers, 8250_mtk, reset-ti-syscon, cfg80211/mac80211) is 18 `module_outs` of that target, gated by `mindone.fragment`, not by anything in `gki_defconfig` |
| ✅ measured 2026-09-27 | every symbol in `gki/aarch64/symbols/mindone_gki_modules` (776 documented vmlinux/in-tree-module imports) resolves against `gki/aarch64/abi.stg` — Google's own ABI representation for this base, also untouched by this tree — 0 missing |
| ✅ done | the five vmlinux-patch exports this doc used to describe (`_text`, `put_task_stack`, `log_buf_addr_get`, `log_buf_len_get`, `cpuidle_driver_state_disabled`) are gone (`219dda371`, "back to origin/android16-6.12"); the four diagnostic paths that needed them were moved instead: thermal now rides the stock, unconditional `trace_android_vh_update_thermal_trip_flag` call site through a new hook module, `mrdump` looks its symbols up through kallsyms, `log_store` uses `kmsg_dump`, and `aee_aed` is not currently compiled at all |
| ✅ done | the clock-gate operations, the DRM display/GEM-DMA helpers and the dma-buf page-pool helpers that `gki_defconfig` does not compile are exactly the drivers now built as `mindone` module_outs (previous paragraph) — the gap this section used to call "22 symbols with no provider" is closed by that, not by patching `gki_defconfig` |
| ❌ still open | Google has not published a signed prebuilt for `android16-6.12-2026-09` yet (`git ls-remote --tags` against `kernel/common` still shows nothing under that name as of 2026-09-27); `//common:kernel_aarch64` is still built by us from source, not downloaded. Switching `base_kernel` to the downloaded prebuilt is a small change on the Kleaf workspace side (declaring the `@gki_prebuilts` repository); it is prepared but not applied |

None of the counts this section used to carry (1809 symbols, 43 defconfig options, a 1362 s
reference build) describe the current tree — they predate the mixed-build split above and a fresh
measurement needs a build this repository's device-free passes do not run. Treat them as
historical unless re-measured.

The load order question has a measured answer too. On this device's tree, 16 nodes take pin states,
13 take EINT interrupts from the pin controller (the main PMIC among them) and 7 take GPIOs —
**and UFS is in none of the three**, so the boot device does not depend on the pin controller and
moving it into a module cannot cost the root filesystem. Everything else reaches it through the
PMIC and waits on `-EPROBE_DEFER`, which needs `pinctrl-mt6789.ko` early in
`vendor_boot.modules.load`.

## Building it with Kleaf

Android 16 dropped non-Bzlmod kernel builds, so Bazel is the only entry point Google supports. The
workspace is the ACK manifest with `kernel/common` replaced by this repository:

```sh
repo init -u https://android.googlesource.com/kernel/manifest -b common-android16-6.12 \
          --depth=1 --no-tags
mkdir -p .repo/local_manifests
cat > .repo/local_manifests/mindone.xml <<'X'
<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <remove-project name="kernel/common" />
  <remote name="mindone" fetch="https://github.com/" />
  <project path="common" name="<owner>/<this-repo>" remote="mindone"
           revision="android16-6.12" clone-depth="1" />
</manifest>
X
repo sync -c -j8 --no-tags
tools/bazel build //common/mindone:mindone //common/mindone:mindone_modules_install
```

| target | what it is |
|---|---|
| `//common/mindone:mindone` | mixed build on `base_kernel = //common:kernel_aarch64`: `gki_defconfig` plus `mindone/mindone.fragment`, no `vmlinux` in its `outs`, 18 device-specific in-tree modules plus the device DTB/DTBO — the ~109 standard GKI in-tree modules come from `kernel_aarch64` itself, not from this target |
| `//common/mindone/modules:mindone_modules` | the out-of-tree drivers, one `ddk_module` each |
| `//common/mindone:mindone_modules_install` | both of the above, staged together |
| `//common/mindone:mindone_abi` | the KMI symbol list targets |

🔴 **Memory.** Generating BTF (`pahole`) is the tallest single step, and the build as a whole has
been measured peaking at **15.3 GiB** (`memory.peak` of the container's cgroup). Below that ceiling
`pahole` is killed with a bare `Killed` and `FAILED: load BTF from vmlinux: Invalid argument`,
naming nothing. Give it **18 GiB** rather than sixteen: at sixteen the headroom is under a
gigabyte. In a container or a VM, check the limit that applies to the build rather than the total
the machine reports, and read `memory.events` afterwards - `oom_kill` counts what the log does not
say.

The configuration is `gki_defconfig` plus a fragment rather than a defconfig of its own, so Kleaf's
`check_defconfig` verifies on every build that the result still says what the fragment claims. That
check is what first caught an option this tree believed was off and was not.

`kernel.release` differs between the two paths: `6.12.92-4k+` from `make`,
`6.12.92-android16-6-<stamp>-4k` from Kleaf, which appends its own localversion and page-size
suffix — a tree built without `--config=stamp` gets `maybe-dirty` in place of `<stamp>`. The
device reports the Kleaf form; `6.12.92-4k+` belongs only to the retired standalone build below.

## Building it with make (kernel image only, kept for local iteration)

This no longer builds anything that ships. It still compiles a bare `vmlinux`/`Image` from this
repository and a clang — useful for iterating on core kernel code without a full Kleaf
workspace — but it cannot build a matching module set: the per-module `Makefile`s
`mindone/modules` used to have are gone, replaced by the single `mindone/modules/BUILD.bazel`
Kleaf builds from. Do not compare its `module_layout` against the device; it is a different
build with a different module set (in fact, none).

## What you need

| | |
|---|---|
| Host | Linux, x86_64 or aarch64. Verified on Ubuntu 24.04 |
| Disk | ~2 GB for the source tree, ~2.5 GB for the build output |
| RAM | The ThinLTO link of `vmlinux.o` is the peak. It was killed on a machine with 20 GB total but only ~7 GB free -- `make` reports `Error 137`, which is SIGKILL from the OOM killer, not a compile error. 🔴 **Check that your output directory is not on `tmpfs`**: a build tree in RAM competes with the linker for the same memory, and that was the cause here rather than the machine being small. If you still run out, cap the linker: `make ... LD="ld.lld --thinlto-jobs=4"`. |
| Time | Kernel image only: minutes to compile, but the ThinLTO link on its own takes tens of minutes. This path no longer builds the module set — see [Building it with Kleaf](#building-it-with-kleaf) |
| Toolchain | clang **19.1.4**, the kernel.org prebuilt (below) |

Host packages on a Debian/Ubuntu system:

```sh
sudo apt update
sudo apt install -y build-essential bc bison flex libssl-dev libelf-dev \
                    python3 git rsync zstd cpio kmod
```

`pahole` (1.31 or newer) is needed only if you want module BTF; without it the build still
works.

## 1. Toolchain

```sh
curl -LO https://mirrors.edge.kernel.org/pub/tools/llvm/files/llvm-19.1.4-aarch64.tar.gz
sudo mkdir -p /opt/toolchains
sudo tar -C /opt/toolchains -xf llvm-19.1.4-aarch64.tar.gz
```

Use the `-x86_64` tarball instead if your host is x86_64. Either way the clang **version** must
be 19.1.4.

🔴 This is not a style preference. clang decides symbol CRCs and `module_layout`. Build the
kernel with one version and the modules with another, and every module fails to load — with no
error message that says why.

## 2. Kernel

```sh
git clone <this repo> mindone-kernel
cd mindone-kernel
TOOLCHAIN=/opt/toolchains/llvm-19.1.4-aarch64 ./build.sh ../k612-out
```

What `build.sh` runs, if you would rather do it by hand:

```sh
export PATH=/opt/toolchains/llvm-19.1.4-aarch64/bin:$PATH
export ARCH=arm64 LLVM=1 LLVM_IAS=1 CROSS_COMPILE=aarch64-linux-gnu-
make O=../k612-out mindone_defconfig
make O=../k612-out -j"$(nproc)" Image modules dtbs
```

You now have:

| File | What it is |
|---|---|
| `../k612-out/arch/arm64/boot/Image` | the kernel, ~42 MB |
| `../k612-out/arch/arm64/boot/dts/mediatek/mindone.dtb` | the device tree blob, ~180 KB |
| `../k612-out/Module.symvers` | this build's own symbol CRCs — not what any shipped module is checked against; that is Kleaf's `mindone_abi` target |
| `../k612-out/include/config/kernel.release` | `6.12.92-4k+` — the vermagic modules must carry |

Compress the kernel before it goes into a boot image:

```sh
gzip -9 -n -c ../k612-out/arch/arm64/boot/Image > ../k612-out/Image.gz
```

🔴 `boot.img` must carry `Image.gz`, not a raw `Image`. MediaTek's bootloader refuses the
uncompressed one silently: no console output, no pstore, just a device that does not start. This
cost a full night to find.

## 3. Modules

The out-of-tree drivers are `ddk_module` targets, one per driver, all defined in
`mindone/modules/BUILD.bazel` and gathered under the `mindone_modules` group; there is no
per-module `Makefile` any more. Build the whole set, staged with the in-tree platform drivers:

```sh
tools/bazel build //common/mindone:mindone_modules_install
```

One module on its own:

```sh
tools/bazel build //common/mindone/modules:hf_manager
```

The set the device actually boots, in the order it loads it, is two files, not one:
[`mindone/vendor_boot.modules.load`](../mindone/vendor_boot.modules.load) (29 modules, the
first-stage set that goes in the ramdisk) and
[`mindone/vendor_dlkm.modules.load`](../mindone/vendor_dlkm.modules.load) (267, the rest) —
296 modules with no overlap between the two files. Both are wired directly into the
`mindone_images` `kernel_images` target in `mindone/BUILD.bazel`, so a Kleaf build stages them
in that order by construction; order is not cosmetic; several modules hold devices that others
depend on, and getting it wrong usually looks like some unrelated device never probing.

Of the 296: 275 come from the 278 `ddk_module` targets in `mindone/modules/BUILD.bazel` (three
more are defined there but not shipped — `mindone_thermal_hook.ko`, `mtk_scp_vow.ko`,
`mtk_vow.ko`). 17 more are `module_outs` of the `mindone` `kernel_build` in `mindone/BUILD.bazel`
— MediaTek platform code (`clk-mt6789`, `pinctrl-mt6789`, `ufs-mediatek`, the DRM/dma-buf
helpers, `8250_mtk`, `cfg80211`, `mac80211`, …) compiled in-tree against `gki_defconfig` plus
`mindone.fragment`, gated by the fragment rather than by anything in stock `gki_defconfig`
(`phy-mtk-ufs.ko`, the 18th `module_outs` entry, is defined but not currently in either load
list). The last 4 — `libarc4`, `rfkill`, `zram`, `zsmalloc` — are unmodified `kernel_aarch64`
in-tree modules, unrelated to this repository's fragment.

For recovery there is a third, shorter list:
[`mindone/vendor_boot.modules.load.recovery`](../mindone/vendor_boot.modules.load.recovery)
(148 modules).

`mindone/modules/MODULES.tsv` is a human-readable inventory (module name, `.ko` name, boot
stage, source directory), regenerated from a build pass. It predates the split above (its own
header says regenerated 2026-09-15, before the mixed-build cleanup) — treat its per-module
detail as a starting point to check, not as the current split; the counts in this document are
the ones measured against today's `BUILD.bazel` files and load lists.

## 4. Check before you flash

Three conditions, in order, whatever built the module:

1. **vermagic equals the target's `uname -r`.** Anything else will not load.
2. **No unresolved symbols.** `modpost` reports these at build time; a warning here is a failure,
   not a nuisance.
3. **No collision with a built-in driver.** If the kernel already claims the device, the module
   either refuses to bind or breaks another driver's binding.

For the Kleaf build, the `mindone_abi` target checks the KMI symbol list
(`gki/aarch64/symbols/mindone`) against the kernel's own ABI representation
(`gki/aarch64/abi.stg`) on every build — that is the same check condition (1) is asking for,
done ahead of time against every symbol the drivers import, not just one module's vermagic.

## 5. Into a flashable image

`mindone_images` in `mindone/BUILD.bazel` (`build_initramfs = True`, `build_vendor_dlkm = True`)
builds the ramdisk and `vendor_dlkm` images directly from the `mindone` kernel build and the
`mindone_modules` set, staged in the order `vendor_boot.modules.load` /
`vendor_dlkm.modules.load` give:

```sh
tools/bazel build //common/mindone:mindone_images
```

Then build the ROM as described in that repository's README. The kernel command line lives in
its `BoardConfig.mk`, not here.

Flashing itself is out of scope for this repository: this device shares one `super` region
between both slots, so a wrong write can leave neither slot bootable. Read the device tree's
flashing notes first.

## If something goes wrong

| Symptom | Cause |
|---|---|
| Modules build, then nothing loads on the device | a build that did not go through Kleaf, or an ABI check that was skipped. Compare `module_layout` against what the running kernel reports |
| Device does not start, no console, no pstore | raw `Image` in `boot.img` instead of `Image.gz` (only relevant to the legacy kernel-only `make` build; Kleaf's `mindone_images` handles this itself) |
| `modpost` warns about undefined symbols | a driver imports something outside `gki/aarch64/symbols/mindone` — add it there or drop the import |
| The device boots but has no Wi-Fi | `cfg80211`/`mac80211` (in-tree `module_outs`) or `rfkill`/`libarc4` (stock `kernel_aarch64` modules) missing from the image — check they are in `vendor_dlkm.modules.load` |
| A driver never probes, and an unrelated device stops working | load order. Check the module's position in `vendor_boot.modules.load` / `vendor_dlkm.modules.load` |
| Build stops at `LD vmlinux.o` with `Error 137` | Not a compile error -- the OOM killer stopped the ThinLTO link. Most often the output directory is on `tmpfs`; move it to a real disk, or use `LD="ld.lld --thinlto-jobs=4"` |
