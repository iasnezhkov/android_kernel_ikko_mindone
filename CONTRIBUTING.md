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
- Keep the existing comment style: where our code differs from the vendor original, the comment
  explains what the original did and what evidence made us change it. That is the part that
  makes a change reviewable by someone without the hardware.
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

## Markers you will see in comments

Two kinds of reference appear throughout the code:

- `F1234` — an entry in the project's engineering log, where a finding was recorded with the
  measurement that established it.
- `SOMETHING-IN-CAPS` — a longer internal note on one subsystem.

Neither is in this repository: the log is a working record, largely of dead ends, and publishing
it would add volume rather than information. The markers are kept because they make a claim
traceable if you ask about it, and because the comment next to them is written to stand on its
own. If a comment ever fails to, that is a defect worth reporting.

## What this project will not take

- Prebuilt binaries of any kind, including `.ko` files.
- Code copied from a vendor tree without a note saying where it came from and under what license.
- Attestation or integrity workarounds.
