import json, sys, os
d = json.load(open(sys.argv[1]))
_dp = os.path.join(os.path.dirname(sys.argv[1]), 'deps.json')
mdeps = json.load(open(_dp)) if os.path.exists(_dp) else {}
o = []
o.append('load(\n    "//build/kernel/kleaf:kernel.bzl",\n    "ddk_headers",\n    "ddk_module",\n    "kernel_module_group",\n)\n\n')
o.append('package(default_visibility = ["//visibility:public"])\n\n')
o.append('_MINDONE_COPTS = ["-Wno-error"]\n\n')
o.append('ddk_headers(\n    name = "mindone_headers",\n    hdrs = glob(["include/**/*.h"]),\n)\n\n')
o.append('ddk_headers(\n    name = "mindone_dt_bindings",\n    hdrs = glob(["mmqos_mt6789/dt-bindings/**/*.h"]),\n    includes = ["mmqos_mt6789"],\n)\n\n')
_DTB = ("iommu_debug", "iommu_gz", "mtk_mm_heap", "mtk_smi")
names = sorted(d)
for n in names:
    m = d[n]
    inc = [p for p in m['includes'] if not p.startswith('KERNEL:')]
    copts = [c for c in m['copts'] if c.startswith('-W')]
    defs = m['defines']
    o.append('ddk_module(\n')
    o.append('    name = "%s",\n' % n)
    o.append('    srcs = [\n%s\n    ],\n' % '\n'.join('        "%s",' % s for s in m['srcs']))
    o.append('    out = "%s.ko",\n' % n)
    o.append('    hdrs = glob(["%s/**/*.h"]),\n' % m['dir'])
    o.append('    kernel_build = "//common/mindone:mindone",\n')
    if inc:
        o.append('    includes = [\n%s\n    ],\n' % '\n'.join('        "%s",' % p for p in inc))
    if defs:
        o.append('    local_defines = [\n%s\n    ],\n' % '\n'.join('        "%s",' % x.replace('"', '\\"') for x in defs))
    if copts:
        o.append('    copts = _MINDONE_COPTS + [\n%s\n    ],\n' % '\n'.join('        "%s",' % c for c in copts))
    else:
        o.append('    copts = _MINDONE_COPTS,\n')
    def _norm(x): return x.replace('-', '_')
    _byn = {_norm(k): k for k in d}
    _raw = mdeps.get(n) or mdeps.get(_norm(n)) or []
    extra = []
    if n in _DTB:
        extra.append("mindone_dt_bindings")
    for x in _raw:
        t = x if x in d else _byn.get(_norm(x))
        if t and t != n and t not in extra:
            extra.append(t)
    if extra:
        o.append('    deps = [\n        ":mindone_headers",\n%s\n    ],\n)\n\n' % '\n'.join('        ":%s",' % x for x in extra))
    else:
        o.append('    deps = [":mindone_headers"],\n)\n\n')
o.append('kernel_module_group(\n    name = "mindone_modules",\n    srcs = [\n%s\n    ],\n)\n' % '\n'.join('        ":%s",' % n for n in names))
open(sys.argv[2], 'w').write(''.join(o))
print('modules:', len(names))
