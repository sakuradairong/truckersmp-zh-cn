"""Generate independent player-filter ABI guards and isolated native fixtures."""
import json
from pathlib import Path

from common import ROOT, read_json, require, sha
from settings_compatibility import Image


def prepare(data, output):
    profile = read_json(ROOT / 'profiles/player-filters-abi.json')
    require(profile['schema'] == 1, 'Unknown player-filter ABI profile')
    image = Image(data)
    guards = []
    for row in profile['guards']:
        raw = image.at(row['rva'], row['size'])
        require(sha(raw) == row['sha256'], 'Player-filter code/ABI changed: ' + row['name'])
        guards.append(dict(row, bytes=raw.hex()))
    lines = ['// Generated from independently reviewed player-list operations.', '#pragma once',
             'namespace playerfiltersabi {',
             'struct Guard {unsigned rva;const unsigned char* bytes;unsigned size;};']
    for n, row in enumerate(guards):
        lines.append('inline constexpr unsigned char kGuard%d[]={%s};' %
                     (n, ','.join('0x%02x' % b for b in bytes.fromhex(row['bytes']))))
    lines.append('inline constexpr Guard kGuards[]={')
    lines += ['{0x%x,kGuard%d,sizeof kGuard%d},' % (row['rva'], n, n) for n, row in enumerate(guards)]
    lines += ['};', '}']
    output.mkdir(parents=True, exist_ok=True)
    (output / 'player_filters_profile.h').write_text('\n'.join(lines) + '\n', encoding='utf-8')

    # Rebase every external relative call to owned test stubs. Never load TMP.
    import capstone
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    fixtures = ['#pragma once', 'struct PlayerFixtureCall {unsigned offset,target;};']
    for name, start, size in [('MoveString', 0x81c50, 0x89), ('EraseRows', 0x1d3010, 0x187)]:
        raw = image.at(start, size)
        fixtures.append('inline constexpr unsigned char kPlayer%s[]={%s};' %
                        (name, ','.join('0x%02x' % b for b in raw)))
        calls = []
        for ins in decoder.disasm(raw, start):
            if ins.mnemonic == 'call':
                target = int(ins.op_str, 0)
                require(target in (0x54720, 0x966c88, 0x81c50, 0x82a90), 'Unexpected player fixture call')
                calls.append('{0x%x,0x%x},' % (ins.address - start, target))
        fixtures += ['inline constexpr PlayerFixtureCall kPlayer%sCalls[]={' % name, *calls, '};']
    (output / 'native_player_fixture.h').write_text('\n'.join(fixtures) + '\n', encoding='utf-8')
    return dict(schema=1, guards=len(guards), hooks=5, reviewed_rows_bytes=0x98,
                source_lists_unchanged=True, live_validation='not_performed')
