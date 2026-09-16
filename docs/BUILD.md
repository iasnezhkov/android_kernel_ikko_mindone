# Build

From a clean clone to a module set the device will load. Every command here is meant to be
copied as-is, except the clone URL in step 2, which is the only placeholder.

## What you need

| | |
|---|---|
| Host | Linux, x86_64 or aarch64. Verified on Ubuntu 24.04 |
| Disk | ~2 GB for the source tree, ~2.5 GB for the build output |
| RAM | The ThinLTO link of `vmlinux.o` is the peak. It was killed on a machine with 20 GB total but only ~7 GB free -- `make` reports `Error 137`, which is SIGKILL from the OOM killer, not a compile error. 🔴 **Check that your output directory is not on `tmpfs`**: a build tree in RAM competes with the linker for the same memory, and that was the cause here rather than the machine being small. If you still run out, cap the linker: `make ... LD="ld.lld --thinlto-jobs=4"`. |
| Time | Kernel: minutes to compile, but the ThinLTO link on its own takes tens of minutes. Full module set: ~1 minute to build, ~1 minute to verify |
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
| `../k612-out/Module.symvers` | the ABI every module is checked against |
| `../k612-out/include/config/kernel.release` | `6.12.92-4k+` — the vermagic modules must carry |

Compress the kernel before it goes into a boot image:

```sh
gzip -9 -n -c ../k612-out/arch/arm64/boot/Image > ../k612-out/Image.gz
```

🔴 `boot.img` must carry `Image.gz`, not a raw `Image`. MediaTek's bootloader refuses the
uncompressed one silently: no console output, no pstore, just a device that does not start. This
cost a full night to find.

## 3. Modules

The set the device actually boots, in the order it loads it:

```sh
export KERNEL_SRC=$PWD
export KOUT=$PWD/../k612-out
export MODULES_DIR=$PWD/mindone/modules
export TOOLCHAIN=/opt/toolchains/llvm-19.1.4-aarch64
export JOBS=$(nproc)

python3 mindone/modules/tools/scripts/build-all-modules.py \
    --out ../k6set \
    --load mindone/modules/modules.load
```

That is the command to use. `mindone/modules/modules.load` is the real list from the device:
290 modules, in load order. Building it from a clean clone gives **290 of 290, no build
failures, no gate failures** -- if you get anything else, something is wrong on your side, and
that is the point of publishing the list.

Order is not cosmetic: several modules hold devices that others depend on, and getting it wrong
usually looks like some unrelated device never probing.

`--load` also pulls in what the kernel builds in-tree. Seven of the 290 are not out-of-tree
modules at all -- `cfg80211`, `mac80211`, `rfkill`, `libarc4`, `zram`, `zsmalloc` and
`industrialio_triggered_buffer` come from the kernel output. The script finds them there and
copies them into the set under the names the list uses. Build with `--all` instead and those
four 802.11 modules are simply absent, which on a device looks like Wi-Fi never coming up.

For recovery there is a second, shorter list: `mindone/modules/modules.load.recovery`
(121 modules).

### Building everything instead

```sh
python3 mindone/modules/tools/scripts/build-all-modules.py --out ../k6set-all --all
```

`--all` builds every directory that has a Makefile, which is useful while working on a driver
that is not in the shipped set. Expect it to be noisy, and know why before you worry:

| What you will see | Why |
|---|---|
| ~11 build failures | directories outside the shipped set that do not compile against this kernel; none of them is in `modules.load` |
| ~8 gate failures for duplicate exports | pairs of mutually exclusive drivers, e.g. `tkcore` vs `mtk_tee_gpapi` (TEE clients) and `ccci_md_all` vs `ccci_auxadc`. Only one of each pair ships, so on the device there is no conflict; `--all` builds both into one directory and the gate correctly objects |
| no `modules.load` in the output | it is copied from the file you pass to `--load`; `--all` has no order to write |

`--only a.ko,b.ko` narrows either mode to specific modules -- it is a filter, so it needs
`--all` or `--load` alongside it:

```sh
python3 mindone/modules/tools/scripts/build-all-modules.py \
    --out ../k6set --all --only hf_manager.ko
```

`--prefer-intree` names drivers the kernel now builds in-tree, so the out-of-tree copy is
skipped.

`mindone/modules/MODULES.tsv` is a human-readable inventory (module name, `.ko` name, which boot
stage ships it, source directory) -- read it to decide what belongs in your set, but do not pass
it to `--load`; that flag wants the plain list, not the table.

You get `../k6set/` with stripped `.ko` files, `modules.load` (copied from the list you passed),
`build-manifest.txt` (kernel release, `module_layout`, toolchain, per-module sha256) and
`build-summary.json`.

One module on its own:

```sh
export PATH=/opt/toolchains/llvm-19.1.4-aarch64/bin:$PATH
export ARCH=arm64 LLVM=1 LLVM_IAS=1 CROSS_COMPILE=aarch64-linux-gnu- CC=clang
make -C $PWD O=$PWD/../k612-out M=$PWD/mindone/modules/hf_manager modules
```

`O=` is not optional here. The kernel was built out of tree, so its configuration lives in the
output directory; without it make stops with
`include/config/auto.conf: No such file or directory`. The `.ko` lands next to the module's
sources.

## 4. Check before you flash

The set build gates every module through `kocheck.py` automatically. If you built one by hand,
check it yourself:

```sh
grep -w module_layout ../k612-out/Module.symvers   # the kernel's ABI CRC
strings hf_manager.ko | grep vermagic               # must equal kernel.release
```

Three conditions, in order:

1. **vermagic equals the target's `uname -r`.** Anything else will not load.
2. **No unresolved symbols.** `modpost` reports these at build time; a warning here is a failure,
   not a nuisance.
3. **No collision with a built-in driver.** If the kernel already claims the device, the module
   either refuses to bind or breaks another driver's binding.

## 5. Into a flashable image

The kernel alone does not boot the phone. Hand the output to the LineageOS device tree:

```sh
# inside your LineageOS source tree
cp ../k612-out/Image.gz                                   device/ikko/mindone/kernel/
cp ../k612-out/arch/arm64/boot/dts/mediatek/mindone.dtb   device/ikko/mindone/kernel/dtb/
cp ../k6set/*.ko                                          device/ikko/mindone/kernel/modules/
cp ../k6set/modules.load                                  device/ikko/mindone/kernel/modules.load.ramdisk
cp mindone/modules/modules.load.recovery                  device/ikko/mindone/kernel/modules.load.recovery
```

🔴 **Empty `device/ikko/mindone/kernel/dtb/` before that copy.** The device tree's
`BOARD_PREBUILT_DTBIMAGE_DIR` concatenates *every* `*.dtb` in that directory into the blob the
bootloader loads. A leftover file from an earlier build -- and the directory is a build product,
so it is `.gitignore`d and survives a clean checkout -- turns the image into two device trees
back to back. Nothing warns you at build time; the phone simply does not boot, with no adb and a
preloader window every ~33 s. `rm -f device/ikko/mindone/kernel/dtb/*.dtb` first. (The device
tree now fails the build on this, but only if you are on a version that has that check.)

Then build the ROM as described in that repository's README. The kernel command line lives in
its `BoardConfig.mk`, not here.

Flashing itself is out of scope for this repository: this device shares one `super` region
between both slots, so a wrong write can leave neither slot bootable. Read the device tree's
flashing notes first.

## If something goes wrong

| Symptom | Cause |
|---|---|
| Modules build, then nothing loads on the device | clang version mismatch between kernel and modules. Compare `module_layout` in `Module.symvers` with what the running kernel reports |
| Device does not start, no console, no pstore | raw `Image` in `boot.img` instead of `Image.gz` |
| `modpost` warns about undefined symbols | a module is being built against the wrong `KOUT`, or its dependency is not in the set |
| The device boots but has no Wi-Fi | the set was built with `--all`. The 802.11 stack (`cfg80211`, `mac80211`, `rfkill`, `libarc4`) is built by the kernel, not out of tree, and only `--load` copies it into the set. Check `ls ../k6set/cfg80211.ko` |
| A driver never probes, and an unrelated device stops working | load order. Check the module's position in `modules.load` |
| Build stops at `LD vmlinux.o` with `Error 137` | Not a compile error -- the OOM killer stopped the ThinLTO link. Most often the output directory is on `tmpfs`; move it to a real disk, or use `LD="ld.lld --thinlto-jobs=4"` |
| Build fails immediately on a fresh clone | `KERNEL_SRC` and `KOUT` point at different kernels — the build script refuses this on purpose |
