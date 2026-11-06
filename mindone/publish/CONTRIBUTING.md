<!-- SPDX-License-Identifier: GPL-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
# Contributing

This is a small, independent tree. Reports and patches are welcome; please make them easy to
act on.

## Before reporting a problem

State which of these you have, because the answer changes completely:

- the **same device** (iKKO MindOne), or
- a **different MT6789/MT8781 device** — most of this tree is SoC-level and should transfer, but
  the device tree under `mindone/dts/` will not.

Then include:

| | |
|---|---|
| Kernel release | `uname -r` on the device, or `include/config/kernel.release` from your build |
| Build identity | exact GKI prebuilt, matching DDK headers, symbol metadata and module set; include `clang --version` |
| What you ran | the exact commands, not a description of them |
| What happened | the actual output, trimmed but not paraphrased |

For a module that will not load, include `dmesg` around the `insmod`, its vermagic
and the complete-set ABI/provider and Google symbol-list gate reports.

## Patches

- One change per commit, with a message that says **why**, not what — the diff already says what.
  Where the code differs from the vendor original, the message says what the original did and
  what evidence made us change it. That is the part that makes a change reviewable by someone
  without the hardware.
- Do not add explanatory code comments. Retain license headers and inherited upstream
  comments; explain changes in commit messages or documentation.
- English only, in code and in commit messages.
- No binaries, no firmware, no absolute paths from your machine. `mindone/modules/tools/scripts/tree-lint.py`
  and `mindone/modules/tools/scripts/extpathcheck.py` check this; run them before sending.

## Before you send

```sh
python3 mindone/modules/tools/scripts/tree-lint.py
python3 mindone/modules/tools/scripts/extpathcheck.py
```

If your change touches a module, build it against the exact GKI prebuilt and gate the
complete set for symbol CRCs, providers, built-in collisions and Google symbol-list membership.
`kocheck.py` is a local ABI check; follow [Build](docs/BUILD.md) for the complete gate.

## What this project will not take

- Prebuilt binaries of any kind, including `.ko` files.
- Code copied from a vendor tree without a note saying where it came from and under what license.
- Attestation or integrity workarounds.
