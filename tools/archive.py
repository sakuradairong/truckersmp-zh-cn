"""Bounded reader/writer for the reviewed TMP resource container.

Archive payloads are supplied by the user; none are bundled in this project.
The opaque table field is preserved without assigning it an unproven meaning.
"""
import struct
import zlib
from common import require

ENTRY = struct.Struct('<QQIIII')


def parse(data, count=None):
    require(32 <= len(data) <= 2_000_000, 'Archive size outside bounds')
    require(data[:12] == b'TMP#\x01\x00\x00\x00CITY', 'Unsupported resource format')
    actual_count, table_offset = struct.unpack_from('<IQ', data, 12)
    require(table_offset == 32 and 0 < actual_count <= 4096 and
            (count is None or actual_count == count), 'Unexpected resource table')
    count = actual_count
    end = 32 + count * ENTRY.size
    require(end <= len(data), 'Truncated resource table')
    rows, previous, total = [], -1, 0
    for index in range(count):
        h, offset, flags, opaque, size, packed_size = ENTRY.unpack_from(data, 32 + index * ENTRY.size)
        require(h > previous and flags == 14, 'Unordered hashes or unsupported flags')
        previous = h
        require(0 < size <= 200_000 and 0 < packed_size <= 200_000, 'Entry size outside bounds')
        require(end <= offset and offset + packed_size <= len(data), 'Payload outside archive')
        packed = data[offset:offset + packed_size]
        decoder = zlib.decompressobj()
        raw = decoder.decompress(packed, size + 1)
        require(len(raw) == size and decoder.eof and not decoder.unused_data
                and not decoder.unconsumed_tail, 'Zlib size/checksum mismatch')
        total += size
        require(total <= 2_000_000, 'Total decompressed size outside bounds')
        rows.append(dict(index=index, path_hash=h, offset=offset, flags=flags, opaque=opaque,
                         size=size, packed_size=packed_size, raw=raw, packed=packed))
    cursor = end
    for row in sorted(rows, key=lambda item: item['offset']):
        require(row['offset'] == cursor, 'Payload overlap or gap')
        cursor += row['packed_size']
    require(cursor == len(data), 'Unexpected archive trailer')
    return rows


def repack(data, count, replacements):
    rows = parse(data, count)
    require(set(replacements) <= set(range(count)), 'Invalid replacement index')
    result = bytearray(data[:32 + count * ENTRY.size])
    for row in rows:
        raw = replacements.get(row['index'], row['raw'])
        packed = zlib.compress(raw, 9) if row['index'] in replacements else row['packed']
        offset = len(result)
        result.extend(packed)
        ENTRY.pack_into(result, 32 + row['index'] * ENTRY.size, row['path_hash'], offset,
                        row['flags'], row['opaque'], len(raw), len(packed))
    verify(data, bytes(result), count, set(replacements))
    return bytes(result)


def verify(source, candidate, count, allowed):
    before, after = parse(source, count), parse(candidate, count)
    require(source[:32] == candidate[:32], 'Archive header changed')
    changed = set()
    for old, new in zip(before, after):
        require(all(old[k] == new[k] for k in ('index', 'path_hash', 'flags', 'opaque')),
                'Resource metadata changed')
        if old['raw'] != new['raw']:
            changed.add(old['index'])
        else:
            require(old['packed'] == new['packed'], 'Unrelated compressed payload changed')
    require(changed == set(allowed), 'Unexpected resource change scope')
    return before, after
