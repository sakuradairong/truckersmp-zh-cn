"""Generate a native-only profile after checking the required Settings code and ABI."""
from __future__ import annotations
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
import capstone
import pefile
from capstone.x86_const import X86_REG_RDX, X86_REG_RIP

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-settings-zh'
CORE = None
HOOKS = [('window', 0x13d110), ('page', 0x13de20), ('label', 0x13e130),
         ('combo', 0x13e860), ('cstring', 0xf0310), ('string', 0xef4f0),
         ('convert', 0xf0210), ('show', 0x13da10), ('freight-update', 0xfc300),
         ('freight-destroy', 0xfc260),
         ('freight-combo-item', 0x3304f0), ('freight-autocomplete-item', 0x331700)]


def require(test, message):
    if not test:
        raise ValueError(message)


def cpp(s):
    return json.dumps(s, ensure_ascii=False)


def main():
    global CORE, OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=OUT)
    args = parser.parse_args()
    CORE, OUT = args.core.absolute(), args.output.absolute()
    from common import plain, read_json
    from settings_compatibility import check_bytes
    plain(CORE); plain(OUT, missing=True)
    require(not OUT.is_relative_to(CORE.parent), 'Build output cannot be inside the TMP installation')
    data = CORE.read_bytes()
    check_bytes(data, read_json(ROOT / 'profiles/settings-abi.json'))
    p = pefile.PE(data=data)
    base = p.OPTIONAL_HEADER.ImageBase
    m = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    m.detail = True
    catalog_bytes = (ROOT / 'profiles/settings-zh-cn.json').read_bytes()
    catalog = json.loads(catalog_bytes)
    require(catalog['schema'] == 1 and len(catalog['pages']) == 5, 'Unknown Settings catalog schema')
    expected = collections.Counter()
    wanted = dict(catalog['buttons'])
    for pair in catalog['buttons']:
        expected[pair[0]] += 1
    for page in catalog['pages']:
        for row in page['rows']:
            for key in ('label', 'tooltip'):
                if key in row:
                    en, zh = row[key]
                    require(wanted.get(en, zh) == zh, 'Ambiguous translation')
                    wanted[en] = zh
                    expected[en] += 1

    def instructions(start, end):
        return list(m.disasm(p.get_data(start, end-start), base+start))

    def rip_rdx(i):
        return (i.mnemonic == 'lea' and len(i.operands) == 2 and
                i.operands[0].reg == X86_REG_RDX and
                i.operands[1].type == capstone.CS_OP_MEM and
                i.operands[1].mem.base == X86_REG_RIP)

    evidence = []
    for start, end in [(0x13a200, 0x13cb20), (0x150e80, 0x153a00), (0x13d110, 0x13d8a1)]:
        ins = instructions(start, end)
        for n, i in enumerate(ins):
            if i.mnemonic != 'call' or i.op_str != hex(base+0xf0310):
                continue
            lea = next((j for j in reversed(ins[max(0, n-8):n]) if rip_rdx(j)), None)
            if lea is None:
                continue
            rva = lea.address+lea.size+lea.operands[1].mem.disp-base
            en = p.get_data(rva, 400).split(b'\0')[0].decode('ascii', errors='replace')
            if en in wanted:
                evidence.append(dict(english=en, source_rva=rva, return_rva=i.address+i.size-base))
    # Four boolean descriptors share a helper that forwards a static char*.
    for site in (0x13a6d3, 0x13b709, 0x13b74b, 0x13c9bb):
        i = instructions(site, site+7)[0]
        require(rip_rdx(i), 'Boolean label evidence changed')
        rva = site+i.size+i.operands[1].mem.disp
        en = p.get_data(rva, 100).split(b'\0')[0].decode('ascii')
        require(en in wanted, 'Unknown boolean label')
        evidence.append(dict(english=en, source_rva=rva, return_rva=0x151104))
    require(collections.Counter(x['english'] for x in evidence) == expected,
            'Native label/tooltip evidence is incomplete')

    checks = []
    def guard(name, rva, size):
        checks.append(dict(name=name, rva=rva, bytes=p.get_data(rva, size).hex()))
    for name, rva in HOOKS:
        guard(name, rva, 32)
    guard('native-wide-assign', 0x81fe0, 0x144)
    guard('native-text-destroy', 0xef630, 0x1f)
    guard('native-label-set', 0x336bc0, 32)
    guard('native-text-font-set', 0x3374c0, 0x71)
    guard('native-font-layout', 0x163b40, 0x5b)
    guard('native-font-cache-layout', 0x126cc0, 0x2b3)
    guard('native-font-measure', 0x127100, 0x116)
    guard('freight-control-layout', 0xfb220, 0xff5)
    guard('freight-combo-item-layout', 0x3304f0, 0x2bd)
    guard('freight-autocomplete-item-layout', 0x331700, 0x3b0)
    guard('native-name-getter', 0x15d5d0, 8)
    guard('native-children-getter', 0x15eb50, 0x18)
    guard('native-tooltip-getter', 0x15fd20, 8)
    guard('window-title-setter', 0x32f2b0, 0x4e)
    guard('page-title-call', 0x13de55, 5)
    guard('combo-convert-call', 0x13e99b, 5)
    guard('index-selection-read', 0x13eb90, 0x2b)
    guard('index-selection-write', 0x13ebc0, 0x59)
    guard('integer-combo-vtable', 0xdd8330, 0)  # ASLR-dependent pointers checked by target RVA at runtime.
    checks = [g for g in checks if g['bytes']]
    for site in evidence:
        guard('caption-call', site['return_rva']-5, 5)
    # Guards deliberately contain no base relocations; byte comparison remains valid under ASLR.
    relocations = {e.rva for block in getattr(p, 'DIRECTORY_ENTRY_BASERELOC', []) for e in block.entries if e.type}
    for g in checks:
        require(not any(g['rva'] <= r < g['rva']+len(bytes.fromhex(g['bytes'])) for r in relocations),
                'ASLR relocation inside code guard')

    lines = ['// Generated after checking the Settings code and ABI; no client release whitelist.',
             '#pragma once', 'namespace settingszh {',
             f'inline constexpr unsigned kImageSize = 0x{p.OPTIONAL_HEADER.SizeOfImage:x};',
             'inline constexpr unsigned kComboVtable = 0xdd8330;',
             'inline constexpr unsigned kLabelVtable = 0xdf2450;',
             'inline constexpr unsigned kTextVtable = 0xdf2ab8;',
             'inline constexpr unsigned kFreightVtable = 0xdccab0;',
             'inline constexpr wchar_t kSettingsFont[] = L"TmpSettingsZH.ttf";',
             'inline constexpr wchar_t kSettingsBoldFont[] = L"TmpSettingsZH-Semibold.ttf";',
             'inline constexpr wchar_t kFreightFont[] = L"TmpFreightZH.ttf";',
             'inline constexpr wchar_t kFreightBoldFont[] = L"TmpFreightZH-Semibold.ttf";',
             'struct Pair { const char* en; const wchar_t* zh; };',
             'struct Row { unsigned page; Pair label; Pair tooltip; };',
             'struct Literal { unsigned caller, source; Pair text; };',
             'struct Options { unsigned page; const char* label; const Pair* pairs; unsigned count; };',
             'struct Guard { unsigned rva; const unsigned char* bytes; unsigned size; };',
             'inline constexpr Pair kPages[] = {']
    for page in catalog['pages']:
        lines.append(f'    {{{cpp(page["title"][0])}, L{cpp(page["title"][1])}}},')
    lines += ['};', f'inline constexpr Pair kWindow = {{{cpp(catalog["window"][0])}, L{cpp(catalog["window"][1])}}};',
              'inline constexpr Row kRows[] = {']
    for index, page in enumerate(catalog['pages']):
        for row in page['rows']:
            label, tip = row['label'], row.get('tooltip')
            tip_text = f'{{{cpp(tip[0])}, L{cpp(tip[1])}}}' if tip else '{nullptr, nullptr}'
            lines.append(f'    {{{index}, {{{cpp(label[0])}, L{cpp(label[1])}}}, {tip_text}}},')
    lines += ['};', 'inline constexpr Literal kLiterals[] = {']
    for en, zh in catalog['buttons']:
        matches = [e for e in evidence if e['english'] == en]
        require(len(matches) == 1, 'Footer call site is ambiguous')
        e = matches[0]
        lines.append(f'    {{0x{e["return_rva"]:x}, 0x{e["source_rva"]:x}, {{{cpp(en)}, L{cpp(zh)}}}}},')
    lines += ['};']
    lines.append('inline constexpr const char* kFooterNames[] = {"settings_cancel","settings_defaults","settings_apply","settings_ok"};')
    groups = []
    for index, page in enumerate(catalog['pages']):
        for row in page['rows']:
            if not row.get('options'):
                continue
            name = 'kOptions'+str(len(groups))
            groups.append((index, row, name))
            lines.append(f'inline constexpr Pair {name}[] = {{')
            for en, zh in row['options']:
                lines.append(f'    {{{cpp(en)}, L{cpp(zh)}}},')
            lines.append('};')
    lines.append('inline constexpr Options kOptions[] = {')
    for index, row, name in groups:
        lines.append(f'    {{{index}, {cpp(row["label"][0])}, {name}, {len(row["options"])}}},')
    lines += ['};']
    for n, g in enumerate(checks):
        bs = ','.join(f'0x{x:02x}' for x in bytes.fromhex(g['bytes']))
        lines.append(f'inline constexpr unsigned char kGuard{n}[] = {{{bs}}};')
    lines.append('inline constexpr Guard kGuards[] = {')
    for n, g in enumerate(checks):
        lines.append(f'    {{0x{g["rva"]:x}, kGuard{n}, sizeof kGuard{n}}},')
    lines += ['};', '}']
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'settings_profile.h').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    report = dict(schema=1, core_sha256=hashlib.sha256(data).hexdigest(), catalog_sha256=hashlib.sha256(catalog_bytes).hexdigest(),
                  hook_rvas=dict(HOOKS), static_caption_occurrences=len(evidence),
                  labels=sum(len(pg['rows']) for pg in catalog['pages']),
                  tooltips=sum('tooltip' in row for pg in catalog['pages'] for row in pg['rows']),
                  option_groups=len(groups), option_occurrences=sum(len(r['options']) for _, r, _ in groups),
                  source_evidence=evidence, runtime_guards=checks,
                  limits='Static ABI review and offline tests do not establish in-game loading or rendering.')
    (OUT / 'native-profile.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')

    # Isolated test fixture: exact reviewed assign instructions, with every external call
    # redirected by the test to its own allocator/copy/error stubs. No TMP DLL is loaded.
    start, end = 0x81fe0, 0x82125
    code = p.get_data(start, end-start)
    calls = []
    for i in instructions(start, end):
        if i.mnemonic == 'call':
            target = int(i.op_str, 0)-base
            require(target in (0x9bb3f0, 0x82680, 0x54720, 0x82590, 0x82620, 0x966c88), 'Unknown assign call')
            calls.append((i.address-base-start, target))
    fixture = ['#pragma once', 'inline constexpr unsigned char kAssignCode[] = {'+
               ','.join(f'0x{x:02x}' for x in code)+'};',
               'struct FixtureCall { unsigned offset, target; };',
               'inline constexpr FixtureCall kAssignCalls[] = {']
    fixture += [f'{{0x{o:x}, 0x{t:x}}},' for o,t in calls]
    fixture += ['};']
    (OUT / 'native_assign_fixture.h').write_text('\n'.join(fixture)+'\n', encoding='utf-8')
    fixture = ['#pragma once']
    for name, start, end in [('SetFont', 0x3374c0, 0x337531), ('Measure', 0x127100, 0x127216)]:
        for i in instructions(start, end):
            require(not any(op.type == capstone.CS_OP_MEM and op.mem.base == X86_REG_RIP for op in i.operands),
                    'Unexpected external font fixture data')
            if i.mnemonic == 'call' and i.operands[0].type == capstone.CS_OP_IMM:
                require(start <= i.operands[0].imm-base < end, 'Unexpected external font fixture call')
        fixture.append('inline constexpr unsigned char k'+name+'Code[] = {'+
                       ','.join(f'0x{x:02x}' for x in p.get_data(start,end-start))+'};')
    (OUT / 'native_font_fixture.h').write_text('\n'.join(fixture)+'\n', encoding='utf-8')
    print(json.dumps({k:report[k] for k in ('labels','tooltips','static_caption_occurrences','option_groups','option_occurrences')}, ensure_ascii=False))


if __name__ == '__main__':
    main()
