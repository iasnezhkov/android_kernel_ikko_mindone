# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import hashlib
import tempfile
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description='Check exact WEXT record admission, iteration and IE search conditions with host stubs')
parser.add_argument('--compiler', default='cc')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
base = root / 'mindone/modules/wlan_drv_gen4m_6789'
temporary = tempfile.TemporaryDirectory(prefix='mindone-wext-bounds-')
out = Path(temporary.name)
names = ('include/mindone_wsys_cmd.h', 'include/wlan_oid.h', 'os/linux/include/gl_wext.h', 'os/linux/gl_wext.c')
sources = {name: (base / name).read_text() for name in names}
code = '#include <assert.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <errno.h>\n#define PARAM_MAC_ADDR_LEN 6\n#define PARAM_MAX_LEN_RATES_EX 16\n#define ELEM_MAX_LEN_SSID 32\n#define OFFSET_OF offsetof\n'
for source, names in {'include/mindone_wsys_cmd.h': ('struct PARAM_SSID',), 'include/wlan_oid.h': ('enum ENUM_PARAM_OP_MODE', 'enum ENUM_PARAM_NETWORK_TYPE', 'struct PARAM_802_11_CONFIG_FH', 'struct PARAM_802_11_CONFIG', 'struct PARAM_BSSID_EX', 'struct PARAM_BSSID_LIST_EX'), 'os/linux/include/gl_wext.h': ('struct PARAM_FIXED_IEs',)}.items():
    for name in names:
        found = re.findall('^' + name + r' \{.*?^\};', sources[source], re.M | re.S)
        assert len(found) == 1, name
        code += found[0] + '\n'
helper = re.findall(r'^static int wext_get_variable_ies\(.*?^\}', sources['os/linux/gl_wext.c'], re.M | re.S)
assert len(helper) == 1
loop = re.findall(r'\tfor \(i = 0; i < prList->u4NumberOfItems; \+\+i\) \{\n.*?(?=\t\t/\* BSSID \*/)', sources['os/linux/gl_wext.c'], re.S)
assert len(loop) == 1
advance = re.findall(r'\t\tprBss = \(struct PARAM_BSSID_EX \*\) \(\(char \*\)prBss \+\s*prBss->u4Length\);', sources['os/linux/gl_wext.c'])
assert len(advance) == 1
conditions = re.findall(r'\t\tif \(pucVariableIEs && wextSrchDesired.*?\)\) \{', sources['os/linux/gl_wext.c'], re.S)
assert len(conditions) == 4
code += '''static uint8_t *allocation_end;
static unsigned calls;
static int search(uint8_t *ptr, int32_t len) {
assert(ptr);
assert(len >= 0);
assert(ptr + len <= allocation_end);
calls++;
return 0;
}
static int wextSrchDesiredWPAIE(uint8_t *ptr, int32_t len, uint8_t id, uint8_t **result) { (void)id; (void)result; return search(ptr,len); }
static int wextSrchDesiredWPSIE(uint8_t *ptr, int32_t len, uint8_t id, uint8_t **result) { (void)id; (void)result; return search(ptr,len); }
static int wextSrchDesiredWAPIIE(uint8_t *ptr, int32_t len, uint8_t **result) { (void)result; return search(ptr,len); }
struct result { unsigned exported, searches; int ret; };
'''
cases = '''static struct PARAM_BSSID_LIST_EX *make_list(unsigned first_ie, unsigned second_ie, uint32_t *size) {
uint32_t offset = offsetof(struct PARAM_BSSID_EX, aucIEs);
*size = offsetof(struct PARAM_BSSID_LIST_EX,arBssid) + 2*offset + first_ie + second_ie;
struct PARAM_BSSID_LIST_EX *list = calloc(1,*size);
assert(list);
list->u4NumberOfItems = 2;
struct PARAM_BSSID_EX *first = &list->arBssid[0];
first->u4Length = offset + first_ie;
first->u4IELength = first_ie;
struct PARAM_BSSID_EX *second = (void *)((uint8_t *)first + first->u4Length);
second->u4Length = offset + second_ie;
second->u4IELength = second_ie;
return list;
}
int main(void) {
uint32_t size;
struct PARAM_BSSID_LIST_EX *list = make_list(0,16,&size);
struct result r = scan_slices(list,size);
assert(r.exported == 2 && r.searches == 4 && r.ret == 0);
free(list);
list = make_list(12,16,&size);
r = scan_slices(list,size);
assert(r.exported == 2 && r.searches == 8 && r.ret == 0);
free(list);
for (unsigned len=1;len<12;len++) {
list = make_list(0,16,&size);
struct PARAM_BSSID_EX *first = &list->arBssid[0];
struct PARAM_BSSID_EX *second = (void *)((uint8_t *)first + first->u4Length);
second->u4IELength = len;
r = scan_slices(list,size);
assert(r.exported == 1 && r.searches == 0 && r.ret == -EINVAL);
free(list);
}
list = make_list(0,16,&size);
struct PARAM_BSSID_EX *first = &list->arBssid[0];
struct PARAM_BSSID_EX *second = (void *)((uint8_t *)first + first->u4Length);
second->u4Length++;
r = scan_slices(list,size);
assert(r.exported == 1 && r.searches == 0 && r.ret == -EINVAL);
free(list);
list = make_list(0,16,&size);
list->u4NumberOfItems = 3;
r = scan_slices(list,size);
assert(r.exported == 2 && r.searches == 4 && r.ret == -EINVAL);
free(list);
puts("PASS: exact admission/advance + 4 exact guarded IE search call conditions; zeroIE->valid, fixedIE->valid, 11 shortIE, record overrun, one-past record");
}
'''
results = []
for name in ('corrected','reject_zero_ie','unguard_wpa','unguard_wps','unguard_rsn','unguard_wapi','ignore_helper_error'):
    current_helper = helper[0]
    current_conditions = conditions.copy()
    if name == 'reject_zero_ie':
        old = 'if (!prBss->u4IELength)\n\t\treturn 0;'
        assert old in current_helper
        current_helper = current_helper.replace(old,'if (!prBss->u4IELength)\n\t\treturn -EINVAL;')
    if name.startswith('unguard_'):
        i = ('unguard_wpa','unguard_wps','unguard_rsn','unguard_wapi').index(name)
        current_conditions[i] = current_conditions[i].replace('pucVariableIEs && ','')
    unit = code + current_helper + '\nstatic struct result scan_slices(struct PARAM_BSSID_LIST_EX *prList, uint32_t u4AllocBufLen) { uint32_t i; int ret = 0; struct result r = {0}; uint8_t *pucVariableIEs; uint8_t *prDesiredIE = NULL; struct PARAM_BSSID_EX *prBss = &prList->arBssid[0]; allocation_end = (uint8_t *)prList + u4AllocBufLen; calls = 0;\n'
    current_loop = loop[0]
    if name == 'ignore_helper_error':
        assert 'ret = wext_get_variable_ies' in current_loop
        current_loop = current_loop.replace('ret = wext_get_variable_ies', 'wext_get_variable_ies')
    unit += current_loop + '\nr.exported++;\n'
    for condition in current_conditions:
        unit += condition + ' assert(0); }\n'
    unit += advance[0] + '\n} r.ret = ret; r.searches = calls; return r; }\n' + cases
    path = out / ('wext_slices_' + name)
    path.with_suffix('.c').write_text(unit)
    built = subprocess.run([args.compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-fstrict-flex-arrays=3','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(path.with_suffix('.c')),'-o',str(path)],capture_output=True,text=True,timeout=40)
    assert built.returncode == 0, built.stderr
    run = subprocess.run([str(path)],capture_output=True,text=True,timeout=15)
    expected = 'ptr' if name.startswith('unguard_') else ('r.exported == 2' if name == 'reject_zero_ie' else 'r.exported == 1')
    passed = run.returncode == 0 if name == 'corrected' else run.returncode != 0 and expected in run.stderr
    results.append(dict(name=name,passed=passed,returncode=run.returncode,stdout=run.stdout,stderr=run.stderr))
result = dict(source_sha256={name:hashlib.sha256(text.encode()).hexdigest() for name,text in sources.items()}, fragments_sha256=dict(helper=hashlib.sha256(helper[0].encode()).hexdigest(),admission=hashlib.sha256(loop[0].encode()).hexdigest(),advance=hashlib.sha256(advance[0].encode()).hexdigest(),conditions=[hashlib.sha256(value.encode()).hexdigest() for value in conditions]),limitation='Exact helper, admission/advance slices and four guarded search call conditions; synthetic observations substitute event output, IE parser stubs substitute searches. No full WEXT or firmware scenario.',results=results)
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
temporary.cleanup()
print(json.dumps(result,indent=2))
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
