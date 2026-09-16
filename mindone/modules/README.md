# Out-of-tree drivers

339 driver trees, 5 638 sources, 290 modules in the device's shipped set.

This directory is part of the kernel repository it builds against — kernel and drivers are one
tree, not two. Build instructions, ABI checking and the inventory are in the kernel's
documentation:

- [`docs/BUILD.md`](../../docs/BUILD.md) — building the set and single modules
- [`docs/MODULES.md`](../../docs/MODULES.md) — inventory, load order, provenance, gates
- [`MODULES.tsv`](MODULES.tsv) — the generated per-module table

## License

GPL-2.0. Files that arrived with MediaTek's dual GPL-2.0/BSD header keep it unchanged. Files
carrying a non-redistributable vendor notice were removed rather than published; the tree is
checked to contain none.
