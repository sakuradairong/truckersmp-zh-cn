"""Build bounded relocation contracts from an explicitly reviewed native baseline.

Only build tooling uses Capstone/pefile. The installer and loaded DLL run the
same C++ checker over these generated rules. No client code is executed here.
"""
from __future__ import annotations
import bisect
import collections
import json
import re
import struct
from pathlib import Path
import capstone
import pefile
from common import ROOT, read_json, require, sha


class Exporter:
    def __init__(self, data, manifest):
        self.pe = pefile.PE(data=data)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.functions = sorted((e.struct.BeginAddress, e.struct.EndAddress)
                                for e in self.pe.DIRECTORY_ENTRY_EXCEPTION)
        self.starts = [a for a, _ in self.functions]
        self.decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.decoder.detail = True
        self.manifest = manifest
        self.relocations = {e.rva for block in getattr(self.pe, 'DIRECTORY_ENTRY_BASERELOC', [])
                            for e in block.entries if e.type == 10}
        self.imports = {}
        for entry in getattr(self.pe, 'DIRECTORY_ENTRY_IMPORT', []):
            for imp in entry.imports:
                self.imports[imp.address-self.base] = entry.dll.decode('ascii').lower()+'!'+(
                    imp.name.decode('ascii') if imp.name else '#'+str(imp.ordinal))
        self.contracts = {}

    def section(self, at, size=1):
        matches = [s for s in self.pe.sections if s.VirtualAddress <= at and
                   at+size <= s.VirtualAddress+max(s.Misc_VirtualSize, s.SizeOfRawData)]
        require(len(matches) == 1, 'Relocation target has no unique section')
        s = matches[0]
        return 1 if s.Characteristics & 0x20000000 else 3 if s.Characteristics & 0x80000000 else 2

    def raw(self, at, size):
        data = self.pe.get_data(at, size)
        require(len(data) == size, 'Truncated relocation baseline')
        return data

    def function_end(self, at):
        i = bisect.bisect_right(self.starts, at)-1
        return self.functions[i][1] if i >= 0 and at < self.functions[i][1] else at+32

    def code(self, at, size):
        raw = self.raw(at, size+15)
        instructions = []
        inline = set()
        image_regs = set()
        special = self.manifest['image_relative_tables']
        table_sizes = iter(special['sizes'] if at == special['function'] else ())
        fields = []
        for ins in self.decoder.disasm(raw, at):
            if ins.address >= at+size or (inline and ins.address >= min(inline)):
                break
            instructions.append(ins)
            off = ins.address-at
            for op in ins.operands:
                if op.type != capstone.CS_OP_MEM:
                    continue
                if op.mem.base == capstone.x86.X86_REG_RIP:
                    fields.append((off+ins.disp_offset, 4, 1, off+ins.size, ins.address+ins.size+op.mem.disp))
                elif ((op.mem.base in image_regs and at <= op.mem.disp < at+size) or
                      (special['function'] <= at < self.function_end(special['function']) and
                       op.mem.disp in self.contracts and self.contracts[op.mem.disp].get('zeros') is not None) or
                      (at == special['function'] and 0xe00000 <= op.mem.disp < 0xe10000)):
                    target = op.mem.disp
                    fields.append((off+ins.disp_offset, 4, 2, 0, target))
                    if at <= target < at+size:
                        inline.add(target)
                    elif target not in self.contracts:
                        count = next(table_sizes)//4
                        entries = struct.unpack('<'+'I'*count, self.raw(target, count*4))
                        require(all(v == 0 or at <= v < at+size for v in entries), 'Unknown CRT jump-table target')
                        self.contracts[target] = dict(kind=2, size=count*4, raw=self.raw(target, count*4).hex(),
                            mask=('00000000'*count), refs=[dict(off=i*4, width=4, mode=2, next=0, target=v)
                                for i, v in enumerate(entries) if v], alignment=target % 8,
                            zeros=[i*4 for i, v in enumerate(entries) if not v])
            if (ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP)) and ins.imm_size:
                fields.append((off+ins.imm_offset, ins.imm_size, 1, off+ins.size, ins.operands[0].imm))
            # Track a locally established image base for inline jump-table RVAs.
            _, written = ins.regs_access()
            image_regs.difference_update(written)
            if ins.mnemonic == 'lea' and len(ins.operands) == 2:
                op = ins.operands[1]
                if op.type == capstone.CS_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP and ins.address+ins.size+op.mem.disp == 0:
                    image_regs.add(ins.operands[0].reg)
        require(instructions, 'Empty native function')
        end = instructions[-1].address+instructions[-1].size
        require(end >= at+size or (inline and end == min(inline)), 'Incomplete native function '+hex(at))
        if not inline:
            size = end-at
        raw = raw[:size]
        mask = bytearray(b'\xff'*size)
        refs = []
        if inline:
            boundaries = sorted(inline)+[at+size]
            for first, last in zip(boundaries, boundaries[1:]):
                # An index byte table contains no executable RVAs; leave it exact.
                values = [struct.unpack('<I', self.raw(p, 4))[0] for p in range(first, last-3, 4)]
                if values and all(v == 0 or at <= v < at+size for v in values):
                    fields += [(first-at+i*4, 4, 2, 0, v) for i, v in enumerate(values) if v]
        for offset, width, mode, following, target in fields:
            require(0 <= offset and offset+width <= size, 'Reference crosses code contract')
            value = int.from_bytes(raw[offset:offset+width], 'little', signed=mode == 1)
            require((at+following+value if mode == 1 else value) == target, 'Instruction reference encoding mismatch')
            mask[offset:offset+width] = b'\0'*width
            refs.append(dict(off=offset, width=width, mode=mode, next=following, target=target))
        # Only explicitly reviewed TLS immediate operands may move. Object-field
        # offsets and ordinary immediates remain exact. The consumer checks TLS
        # bounds and consistent identity across all references to each slot.
        for field in self.manifest.get('tls_operands', {}).get(str(at), []):
            offset, extent = field['off'], field['extent']
            ins = next((i for i in instructions if i.address-at+i.imm_offset == offset and i.imm_size == 4), None)
            require(ins is not None and ins.mnemonic == 'mov', 'Unreviewed TLS operand')
            tls = self.pe.DIRECTORY_ENTRY_TLS.struct
            index = tls.AddressOfIndex-self.base
            require(any(r['target'] == index for r in refs) and b'\x65\x48\x8b\x04\x25\x58\x00\x00\x00' in raw,
                    'TLS operand lacks reviewed TLS lookup')
            value = int.from_bytes(raw[offset:offset+4], 'little')
            require(value+extent <= tls.EndAddressOfRawData-tls.StartAddressOfRawData+tls.SizeOfZeroFill, 'TLS operand bounds')
            mask[offset:offset+4] = b'\0'*4
            refs.append(dict(off=offset, width=4, mode=3, next=extent, target=0xf0000000+value, terminal_kind=5))
        return dict(kind=1, size=size, raw=raw.hex(), mask=mask.hex(), refs=refs, alignment=0)

    def readonly(self, at):
        # A fixed window may end in the middle of a pointer. The loader rewrites
        # all eight bytes during ASLR; include the whole pointer and validate its
        # target instead of accidentally pinning its low bytes to the disk image.
        end = at+self.manifest.get('readonly_sizes', {}).get(str(at), 16)
        for pos in sorted(self.relocations):
            if pos < end and pos+8 > at:
                require(pos >= at, 'Read-only dependency starts inside a relocated pointer')
                end = max(end, pos+8)
        size = end-at
        require(self.section(at, size) == 2, 'Read-only dependency crosses a section boundary')
        raw = self.raw(at, size)
        mask = bytearray(b'\xff'*size)
        refs = []
        for pos in sorted(self.relocations):
            if at <= pos and pos+8 <= end:
                target = struct.unpack('<Q', self.raw(pos, 8))[0]-self.base
                mask[pos-at:pos-at+8] = b'\0'*8
                refs.append(dict(off=pos-at, width=8, mode=0, next=0, target=target))
        # Literal pooling may move a string from +0 to +4. Byte equality and
        # reference identity prove it; its old residue modulo eight does not.
        return dict(kind=2, size=size, raw=raw.hex(), mask=mask.hex(), refs=refs, alignment=0, alignment_modulus=1)

    def contract(self, at, depth=0):
        if at in self.contracts:
            return
        require(len(self.contracts) < 6000, 'Dependency contract budget exceeded')
        if at == 0:
            self.contracts[at] = dict(kind=0, size=0, refs=[])
            return
        if at in self.imports:
            self.contracts[at] = dict(kind=4, size=8, name=self.imports[at], refs=[])
            return
        kind = self.section(at)
        if kind == 1:
            # Direct dependencies: full unwind extent; deeper library boundaries:
            # independently checked entry block. These are ABI guards, not a proof
            # of transitive application behaviour.
            size = self.function_end(at)-at if depth == 0 else min(32, self.function_end(at)-at)
            node = self.code(at, size)
            node['coverage'] = 'function' if depth == 0 else 'entry'
        elif kind == 2:
            node = self.readonly(at)
        else:
            # Runtime globals cannot be compared with their pre-initialization
            # values. Require a writable non-code cell and its reference identity.
            node = dict(kind=3, size=8, refs=[], alignment=0, alignment_modulus=1)
        self.contracts[at] = node
        for ref in node['refs']:
            if ref.get('terminal_kind') == 5:
                continue
            if depth < 1:
                self.contract(ref['target'], depth+1)
            else:
                target = ref['target']
                ref['terminal_kind'] = 0 if not target else 4 if target in self.imports else self.section(target)
                if target in self.imports:
                    ref['terminal_import'] = self.imports[target]


def prepare(data, output, native_report):
    manifest = read_json(ROOT/'profiles/native-addresses.json')
    require(sha(data) == manifest['baseline_sha256'], 'Build relocation rules from the reviewed baseline only')
    ex = Exporter(data, manifest)
    nodes = []
    for row in manifest['nodes']:
        node = ex.code(row['rva'], row['size'])
        nodes.append(dict(node, old=row['rva']))
    tables = []
    for row in manifest['vtables']:
        refs = [dict(slot=s, target=struct.unpack('<Q', ex.raw(row['rva']+s, 8))[0]-ex.base) for s in row['slots']]
        tables.append(dict(old=row['rva'], refs=refs))
    guards = []
    for name in ('settings-abi.json', 'player-filters-abi.json'):
        for guard in read_json(ROOT/'profiles'/name)['guards']:
            group = 4 if name.startswith('player') else 2 if guard['name'].startswith('freight') else 3 if guard['name'].startswith('native-font') else 1
            guards.append(dict(rva=guard['rva'], size=guard['size'], name=guard['name'], group=group))
    roots = {n['old']: n for n in nodes}
    def owned(at):
        return at in {t['old'] for t in tables} or any(n['old'] <= at < n['old']+n['size'] for n in nodes)
    for node in nodes:
        for ref in node['refs']:
            if not owned(ref['target']):
                ex.contract(ref['target'])
    for table in tables:
        for ref in table['refs']:
            if not owned(ref['target']):
                ex.contract(ref['target'])
    settings_source = (ROOT/'src/settings_zh.cpp').read_text(encoding='utf-8')
    player_source = (ROOT/'src/player_filters.h').read_text(encoding='utf-8')
    required_groups = {
        '1': sorted({int(x, 16) for x in re.findall(r'Rva\((0x[0-9a-fA-F]+)\)', settings_source)} |
                    {v for k, v in native_report['hook_rvas'].items() if not k.startswith('freight')} |
                    {0xdd8340, 0xdf2438, 0xdf2aa0}),
        '2': sorted({v for k, v in native_report['hook_rvas'].items() if k.startswith('freight')} |
                    {0x82020, 0x336f20, 0x337820, 0xef690, 0xdccac0, 0xdf2aa0}),
        '4': sorted({int(x, 16) for x in re.findall(r'Rva\((0x[0-9a-fA-F]+)\)', player_source)} |
                    {0x1cdd90, 0x1cb680, 0x1d6860, 0x1cdaf0, 0x1cf890})}
    result = dict(schema=2, abi=1, nodes=nodes, tables=tables, contracts={str(k): v for k, v in sorted(ex.contracts.items())},
                  aliases=manifest['aliases'], guards=guards, required=manifest['required'],
                  required_groups=required_groups,
                  literals=[dict(source=x['source_rva'], caller=x['return_rva']) for x in native_report['source_evidence']],
                  baseline_sha256=sha(data), limits=dict(candidates=4096, milliseconds=15000))
    variant_path = ROOT/'profiles/native-0794-relocation.json'
    review = read_json(ROOT/'profiles/native-0794-review.json')
    require(sha(variant_path.read_bytes()) == review['profile_sha256'], 'Reviewed variant digest differs')
    variant = read_json(variant_path)
    require(variant['canonical_baseline_sha256'] == sha(data), 'Variant canonical baseline differs')
    result['variants'] = [variant]
    encoded = json.dumps(result, separators=(',', ':'))
    output.mkdir(parents=True, exist_ok=True)
    (output/'relocation-profile.json').write_text(encoded+'\n', encoding='utf-8')
    parts = ['R"TMPPROFILE('+encoded[i:i+16000]+')TMPPROFILE"' for i in range(0, len(encoded), 16000)]
    (output/'relocation_profile.h').write_text('#pragma once\n#include <string>\nnamespace tmpcompat {\ninline constexpr const char* kProfileDigest="'+sha(encoded.encode())+'";\ninline constexpr const char* kProfileParts[]={'+',\n'.join(parts)+'};\n'
        'inline const std::string& Profile(){static const std::string text=[](){std::string s;for(auto p:kProfileParts)s+=p;return s;}();return text;}\n}\n', encoding='utf-8')
    return dict(schema=2, abi=1, profile_sha256=sha(encoded.encode()), nodes=len(nodes), contracts=len(ex.contracts),
                dependency_coverage=dict(collections.Counter(n.get('coverage', str(n['kind'])) for n in ex.contracts.values())))
