"""Reproduce the reviewed 0.7.9.4 variant; never learn rules from an unknown build.

All consumer entry points retain their 0.7.9.3 canonical IDs. Their full
instruction patterns must match before new dependency contracts are exported.
The mapping input is research evidence, not a runtime compatibility decision.
"""
import argparse
import copy
import json
from pathlib import Path
from common import read_json, require, sha, ROOT
from prepare_relocation import Exporter

OLD = '5dcf4e19edb5ff2efd040714beed16a59fd1e662bf39770c65b049da591090df'
NEW = '4916d448b9ea3f8cdec0a82ae092e9b58c7e46038a361b0acaa90fd1f4510cd1'


def prepare(baseline, updated, mapping_path, output, report_path):
    a, b = baseline.read_bytes(), updated.read_bytes()
    require(sha(a) == OLD and sha(b) == NEW, 'Only the two reviewed client builds are accepted')
    profile = read_json(ROOT/'build/native-settings-zh/relocation-profile.json')
    profile.pop('variants', None)
    manifest = read_json(ROOT/'profiles/native-addresses.json')
    old, new = Exporter(a, manifest), Exporter(b, copy.deepcopy(manifest))
    evidence = read_json(mapping_path)
    mapping = {int(k): v for k, v in evidence.get('addresses', evidence.get('canonical_mapping', {})).items()}
    require(mapping, 'Missing reviewed address mapping')
    # Two identical Lua registration instruction patterns exist. These literals
    # distinguish PlayerPanel from Chat; the latter is explicitly not accepted.
    mapping[0x1cf890] = 0x1cf400
    for n in profile['nodes']:
        at = mapping[n['old']]
        raw, mask = bytes.fromhex(n['raw']), bytes.fromhex(n['mask'])
        actual = new.raw(at, n['size'])
        require(all((x&m) == (y&m) for x, y, m in zip(raw, actual, mask)), 'Consumer ABI changed: '+hex(n['old']))
        for r in n['refs']:
            value = int.from_bytes(new.raw(at+r['off'], r['width']), 'little', signed=r['mode'] == 1)
            target = at+r['next']+value if r['mode'] == 1 else value-new.base if r['mode'] == 0 else value
            require(r['target'] not in mapping or mapping[r['target']] == target, 'Conflicting canonical edge')
            mapping[r['target']] = target
    panel = b'PlayerPanel = PlayerPanel or {}\0'
    require(new.raw(mapping[0xdde920], len(panel)) == panel, 'Wrong Lua panel')
    reverse = {v:k for k,v in mapping.items()}
    require(len(reverse) == len(mapping), 'Canonical identities unexpectedly merged')
    def canonical(at):
        if at == 0 or at >= 0xf0000000:
            return at
        return reverse.get(at, at+0x10000000)
    new.manifest['image_relative_tables']['function'] = mapping[manifest['image_relative_tables']['function']]
    # These two TLS helpers are shared by the speed and localization paths.
    new.manifest['tls_operands'] = {
        str(mapping[0x322c50]): [dict(off=38, extent=4), dict(off=46, extent=24)],
        str(mapping[0x2b8e90]): [dict(off=56, extent=4), dict(off=65, extent=24)],
    }
    nodes = [dict(new.code(mapping[n['old']], n['size']), old=n['old']) for n in profile['nodes']]
    tables = []
    for t in profile['tables']:
        refs = []
        for r in t['refs']:
            target = int.from_bytes(new.raw(mapping[t['old']]+r['slot'], 8), 'little')-new.base
            require(mapping.get(r['target']) == target, 'Vtable relation changed')
            refs.append(dict(slot=r['slot'], target=target))
        tables.append(dict(old=t['old'], refs=refs))
    def owned(at):
        return any(mapping[n['old']] <= at < mapping[n['old']]+n['size'] for n in nodes) or at in [mapping[t['old']] for t in tables]
    for node in nodes+tables:
        for ref in node['refs']:
            if not owned(ref['target']):
                new.contract(ref['target'])
    # Reviewed literal boundaries are stored, not guessed by the runtime. Only
    # identical text at corresponding old/new addresses is eligible for trimming.
    literals = []
    for at, node in new.contracts.items():
        if node['kind'] != 2 or node['refs'] or at not in reverse:
            continue
        previous = reverse[at]
        left, right = old.raw(previous, 256), new.raw(at, 256)
        for width, minimum in [(2, 2), (1, 3)]:
            end = 0
            while end < 254:
                value = int.from_bytes(left[end:end+width], 'little')
                if not 32 <= value < 127:
                    break
                end += width
            if end >= minimum*width and left[end:end+width] == bytes(width) and left[:end+width] == right[:end+width]:
                size = end+width
                # No pointer may overlap a literal contract.
                if any(p < at+size and p+8 > at for p in new.relocations):
                    break
                node.update(size=size, raw=right[:size].hex(), mask='ff'*size)
                literals.append(dict(old=previous, rva=at, size=size, text=left[:end].decode('utf-16le' if width == 2 else 'ascii')))
                break
    for node in nodes+tables+list(new.contracts.values()):
        for ref in node['refs']:
            ref['target'] = canonical(ref['target'])
    result = copy.deepcopy(profile)
    result.update(nodes=nodes, tables=tables,
        contracts={str(canonical(k)):v for k,v in sorted(new.contracts.items())},
        baseline_sha256=NEW, canonical_baseline_sha256=OLD)
    output.write_text(json.dumps(result, separators=(',', ':'))+'\n', encoding='utf-8')
    report_path.write_text(json.dumps(dict(baseline_sha256=OLD, updated_sha256=NEW,
        profile_sha256=sha(output.read_bytes()), unchanged_consumer_functions=len(nodes),
        canonical_mapping={str(k):v for k,v in sorted(mapping.items())},
        literal_boundaries=literals, dependency_contracts=len(new.contracts)), indent=2)+'\n', encoding='utf-8')
    print('Unchanged consumer functions:', len(nodes), 'strict new contracts:', len(new.contracts), 'reviewed literals:', len(literals))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for arg in ['baseline', 'updated', 'mapping', 'output', 'report']:
        p.add_argument(arg, type=Path)
    args = p.parse_args()
    prepare(args.baseline, args.updated, args.mapping, args.output, args.report)
