"""Check only the native Settings code and ABI used by the localization module."""
from pathlib import Path
import struct

from common import plain, require, sha


class Image:
    def __init__(self, data):
        self.data = data
        require(0x100 <= len(data) <= 256_000_000 and data[:2] == b'MZ', 'Invalid Settings PE image')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        require(pe + 24 <= len(data) and data[pe:pe+4] == b'PE\0\0', 'Invalid Settings PE header')
        machine, count = struct.unpack_from('<HH', data, pe + 4)
        optional_size = struct.unpack_from('<H', data, pe + 20)[0]
        optional = pe + 24
        require(machine == 0x8664 and 0 < count <= 96 and optional_size >= 112 and
                optional + optional_size + count * 40 <= len(data), 'Settings requires a valid x64 image')
        require(struct.unpack_from('<H', data, optional)[0] == 0x20b, 'Settings requires PE32+')
        self.base = struct.unpack_from('<Q', data, optional + 24)[0]
        self.size, self.headers = struct.unpack_from('<II', data, optional + 56)
        require(0 < self.headers <= len(data) and self.headers <= self.size <= 512_000_000,
                'Invalid Settings image bounds')
        self.sections = []
        for index in range(count):
            offset = optional + optional_size + index * 40
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from('<IIII', data, offset + 8)
            require(raw_offset + raw_size <= len(data) and rva + max(virtual_size, raw_size) <= self.size,
                    'Invalid Settings section bounds')
            self.sections.append((rva, raw_size, raw_offset))

    def at(self, rva, size):
        require(isinstance(rva, int) and isinstance(size, int) and 0 <= rva < self.size and
                0 < size <= self.size - rva, 'Settings address outside image')
        if rva + size <= self.headers:
            return self.data[rva:rva + size]
        found = [(offset + rva - start) for start, length, offset in self.sections
                 if start <= rva and rva + size <= start + length]
        require(len(found) == 1, 'Missing or ambiguous Settings section')
        return self.data[found[0]:found[0]+size]


def check_bytes(data, profile):
    require(profile.get('schema') == 1 and profile.get('guards') and profile.get('pointers') and
            len(profile.get('literals', [])) == 4, 'Unknown Settings compatibility profile')
    image = Image(data)
    for guard in profile['guards']:
        require(sha(image.at(guard['rva'], guard['size'])) == guard['sha256'],
                'Settings code/ABI changed: ' + guard['name'])
    for pointer in profile['pointers']:
        require(struct.unpack('<Q', image.at(pointer['rva'], 8))[0] == image.base + pointer['target'],
                'Settings code/ABI changed: native control table')
    for literal in profile['literals']:
        expected = literal['text'].encode('ascii') + b'\0'
        require(image.at(literal['rva'], len(expected)) == expected,
                'Settings code/ABI changed: footer caption')
    return dict(compatible=True, guards=len(profile['guards']), client_release_pinned=False)


def check_core(path, profile):
    return check_bytes(plain(Path(path)).read_bytes(), profile)
