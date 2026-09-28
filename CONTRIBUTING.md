# Contributing

This is a small, independent tree. Reports and patches are welcome; please make them easy to
act on.

## Before reporting a problem

State which of these you have, because the answer changes completely:

- the **same device** (iKKO MindOne), or
- a **different MT6789/MT8781 device** — most of this tree is SoC-level and should transfer, but
  the device tree under `arch/arm64/boot/dts/mediatek/mindone.dts` will not.

Then include:

| | |
|---|---|
| Kernel release | `uname -r` on the device, or `include/config/kernel.release` from your build |
| Toolchain | `clang --version` — mismatches here cause most "modules will not load" reports |
| What you ran | the exact commands, not a description of them |
| What happened | the actual output, trimmed but not paraphrased |

For a module that will not load, `dmesg` around the `insmod` and the output of
`strings <module>.ko | grep vermagic` answer most of the question by themselves.

## Patches

- One change per commit, with a message that says **why**, not what — the diff already says what.
  Where the code differs from the vendor original, the message says what the original did and
  what evidence made us change it. That is the part that makes a change reviewable by someone
  without the hardware.
- Keep comments in the code itself short and rare. The history is where a change is explained,
  not a comment block next to it.
- English only, in code and in commit messages.
- No binaries, no firmware, no absolute paths from your machine. `mindone/modules/tools/scripts/tree-lint.py`
  and `mindone/modules/tools/scripts/extpathcheck.py` check this; run them before sending.

## Before you send

```sh
python3 mindone/modules/tools/scripts/tree-lint.py
python3 mindone/modules/tools/scripts/extpathcheck.py
```

If your change touches a module, build it and let `kocheck.py` verify it against the kernel you
built it for — a module that builds but carries the wrong `module_layout` will silently refuse to
load on a device, and that is not something a reviewer can see in a diff.

## What this project will not take

- Prebuilt binaries of any kind, including `.ko` files.
- Code copied from a vendor tree without a note saying where it came from and under what license.
- Attestation or integrity workarounds.
