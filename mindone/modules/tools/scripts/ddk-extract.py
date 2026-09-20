import os, glob, json, shlex, collections
ROOT = os.path.expanduser('~/opt/kernel/mindone/modules')
KSRC = os.path.expanduser('~/opt/kernel')
os.chdir(ROOT)
mods = {}
for d in sorted(os.listdir('.')):
    if not os.path.isdir(d):
        continue
    for m in glob.glob(os.path.join(d, '*.mod')):
        name = os.path.basename(m)[:-4]
        objs = [os.path.basename(l.strip())[:-2] for l in open(m) if l.strip().endswith('.o')]
        toks_per_obj = []
        for o in objs:
            c = os.path.join(d, '.' + o + '.o.cmd')
            if not os.path.exists(c):
                continue
            first = open(c, errors='replace').readline()
            if ':=' not in first:
                continue
            t = shlex.split(first.split(':=', 1)[1])
            if ';' in t:
                t = t[:t.index(';')]
            if not t or os.path.basename(t[0]) != 'clang':
                continue
            toks_per_obj.append(t)
        mods[name] = {'dir': d, 'objs': objs, 'toks': toks_per_obj}
cnt = collections.Counter(); n = 0
for m in mods.values():
    for t in m['toks']:
        n += 1
        for x in set(t):
            cnt[x] += 1
base = {x for x, c in cnt.items() if c >= n * 0.95}
print('baseline warning/suppression tokens:', sorted(x for x in base if x.startswith('-W')))
out = {}
for name, m in mods.items():
    inc, defs, copts, other = [], [], [], []
    for t in m['toks']:
        i = 0
        while i < len(t):
            x = t[i]
            if x in base or x.startswith('-Wp,-MMD') or x.endswith('.c') or x in ('-c', '-o') or x.endswith('.o'):
                i += 1; continue
            if x.startswith('-I'):
                p = x[2:]
                p = os.path.relpath(p, ROOT) if p.startswith(ROOT) else ('KERNEL:' + os.path.relpath(p, KSRC) if p.startswith(KSRC) else p)
                if p not in inc: inc.append(p)
            elif x.startswith('-D'):
                v = x[2:]
                if v == '':
                    i += 1
                    v = t[i] if i < len(t) else ''
                if not v or v.startswith('KBUILD_') or v.startswith('__KBUILD_'):
                    i += 1; continue
                if v not in defs: defs.append(v)
            elif x == '-include':
                i += 1
                if i < len(t):
                    p = t[i]
                    p = os.path.relpath(p, ROOT) if p.startswith(ROOT) else p
                    if ('include', p) not in other: other.append(('include', p))
            elif x.startswith('-W') or x.startswith('-f') or x.startswith('-m'):
                if x not in copts: copts.append(x)
            else:
                if x not in [o for o in other]: other.append(x)
            i += 1
    srcs = []
    for o in m['objs']:
        hit = None
        for ext in ('.c', '.S'):
            cand = os.path.join(m['dir'], o + ext)
            if os.path.exists(cand):
                hit = cand; break
        if hit is None:
            for ext in ('.c', '.S'):
                g2 = glob.glob(os.path.join(m['dir'], '**', o + ext), recursive=True)
                if g2:
                    hit = g2[0]; break
        if hit:
            srcs.append(hit)
    out[name] = {'dir': m['dir'], 'srcs': srcs, 'objs': m['objs'], 'includes': inc, 'defines': defs, 'copts': copts, 'other': other}
json.dump(out, open('/tmp/mindone-ddk.json', 'w'), indent=1)
miss = [k for k, v in out.items() if len(v['srcs']) != len(v['objs'])]
print('modules', len(out), 'baseline tokens', len(base))
print('modules whose sources were not all found:', len(miss), miss[:6])
ic = collections.Counter()
for v in out.values():
    for p in v['includes']: ic[p] += 1
print('distinct private include dirs:', len(ic))
for p, c in ic.most_common(10): print('  %4d %s' % (c, p))
oc = collections.Counter()
for v in out.values():
    for x in v['other']:
        oc[x if isinstance(x, str) else x[0] + ':' + x[1]] += 1
print('other tokens:', oc.most_common(8))
