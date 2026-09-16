# Linux kernel 6.1 for iKKO MindOne — unsupported

**MediaTek Helio G99 · MT6789 / MT8781V-CA · arm64**

> **Not maintained. No support.** This was the step that made the 6.12 tree possible and it did
> its job. It still builds and it still boots, but nothing here is debugged or fixed any more.
> Switch to branch **`android16-6.12`** — that is the tree everything goes into.

## Why it is kept

It is the only forward-port of this device from its original `android12-5.10` vendor kernel to a
modern ACK branch, and it is where the device tree and most of the driver work were first proven
to run on real hardware. Deleting it would erase the evidence for decisions the 6.12 tree still
depends on.

## What it is

Google ACK `android14-6.1` (`6.1.175`), hand forward-ported from the device's original
`android12-5.10` branch, plus ~30 commits: the MindOne device tree and the kernel-side changes
the out-of-tree drivers need.

Drivers are **not** on this branch. They live on branch `android16-6.12`, under
`mindone/modules/`, and build against either kernel — `include/mindone/compat.h` there absorbs
the differences. Check that branch out alongside this one, or build the modules from it with
`KOUT` pointing at this kernel's output.

## Build

```sh
TOOLCHAIN=/opt/toolchains/llvm-19.1.4-aarch64 ./build.sh ../k61-out
```

clang **19.1.4**, kernel.org prebuilt, Linux host. For modules and ABI checking, follow
`docs/BUILD.md` on branch `android16-6.12`, with `KOUT` pointing at `../k61-out`.

🔴 The kernel and the module set must be built with the same clang. A mismatch changes
`module_layout` and every module silently refuses to load.

## Status

It booted and ran as the device's daily kernel before 6.12 replaced it. Since then it has
received nothing: no fixes, no security backports, no testing. Treat any problem you find here
as expected rather than as a bug to report.

## License

GPL-2.0, as upstream Linux — see `COPYING` and `LICENSES/`.
