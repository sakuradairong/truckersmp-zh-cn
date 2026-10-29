"""Local TrueType cmap transformations. No font data is bundled."""
import hashlib
import struct
MAGIC_SUM = 0xB1B0AFBA

def require(condition, message):
    if not condition:
        raise ValueError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def checksum(data):
    padded = data + b"\0" * (-len(data) % 4)
    return sum(struct.unpack(">" + "I" * (len(padded) // 4), padded)) & 0xFFFFFFFF

def tables_from(data):
    require(data[:4] == b"\0\1\0\0", "Expected a TrueType SFNT")
    count = struct.unpack_from(">H", data, 4)[0]
    require(0 < count < 100 and 12 + count * 16 <= len(data), "Invalid SFNT directory")
    tables = {}
    for index in range(count):
        tag, _, offset, size = struct.unpack_from(">4sIII", data, 12 + index * 16)
        require(tag not in tables and offset + size <= len(data), "Invalid SFNT table")
        tables[tag] = data[offset:offset + size]
    return tables

def read_cmap4(data, offset):
    fmt, length, language, seg2 = struct.unpack_from(">HHHH", data, offset)
    require(fmt == 4 and seg2 > 0 and seg2 % 2 == 0, "Expected a BMP format-4 cmap")
    require(offset + length <= len(data), "Truncated cmap")
    count = seg2 // 2
    ends = offset + 14
    starts = ends + 2 * count + 2
    deltas = starts + 2 * count
    ranges = deltas + 2 * count
    require(ranges + 2 * count <= offset + length, "Truncated cmap arrays")
    result = {}
    previous = -1
    for index in range(count):
        end = struct.unpack_from(">H", data, ends + 2 * index)[0]
        start = struct.unpack_from(">H", data, starts + 2 * index)[0]
        delta = struct.unpack_from(">h", data, deltas + 2 * index)[0]
        range_word = ranges + 2 * index
        relative = struct.unpack_from(">H", data, range_word)[0]
        require(previous < start <= end, "Overlapping cmap segments")
        previous = end
        for cp in range(start, min(end, 65534) + 1):
            if relative:
                address = range_word + relative + (cp - start) * 2
                require(address + 2 <= offset + length, "cmap glyph outside subtable")
                glyph = struct.unpack_from(">H", data, address)[0]
                glyph = (glyph + delta) & 65535 if glyph else 0
            else:
                glyph = (cp + delta) & 65535
            if glyph:
                result[cp] = glyph
    return result, language

def unicode_cmaps(data):
    version, count = struct.unpack_from(">HH", data)
    require(version == 0 and count > 0, "Invalid cmap directory")
    result = []
    for index in range(count):
        platform, encoding, offset = struct.unpack_from(">HHI", data, 4 + 8 * index)
        require(platform == 0 or (platform == 3 and encoding in (1, 10)),
                "This proof only supports this font's Unicode cmap tables")
        mapping, language = read_cmap4(data, offset)
        result.append((platform, encoding, mapping, language))
    return result

def cmap4_from(mapping, language):
    groups = []
    for cp, glyph in sorted(mapping.items()):
        require(0 <= cp < 65535 and 0 < glyph < 65536, "Invalid BMP mapping")
        if groups and cp == groups[-1][-1][0] + 1:
            groups[-1].append((cp, glyph))
        else:
            groups.append([(cp, glyph)])
    groups.append([(65535, 0)])
    count = len(groups)
    ends, starts, deltas, ranges, glyphs = [], [], [], [], []
    for index, group in enumerate(groups):
        starts.append(group[0][0])
        ends.append(group[-1][0])
        possible = {(glyph - cp) & 65535 for cp, glyph in group}
        if len(possible) == 1:
            deltas.append(next(iter(possible)))
            ranges.append(0)
        else:
            deltas.append(0)
            ranges.append(2 * (count - index + len(glyphs)))
            glyphs.extend(glyph for _, glyph in group)
    length = 16 + 8 * count + 2 * len(glyphs)
    require(length <= 65535 and max(ranges) <= 65535, "cmap exceeds format-4 limits")
    power = 1 << (count.bit_length() - 1)
    header = struct.pack(">7H", 4, length, language, count * 2,
                         power * 2, power.bit_length() - 1, count * 2 - power * 2)
    array = lambda values: struct.pack(">" + "H" * len(values), *values)
    return header + array(ends) + b"\0\0" + array(starts) + array(deltas) + array(ranges) + array(glyphs)

def remapped_cmap(source, replacements):
    records = unicode_cmaps(source)
    directory = bytearray(struct.pack(">HH", 0, len(records)))
    payload = bytearray()
    for platform, encoding, mapping, language in records:
        mapping.update(replacements)
        subtable = cmap4_from(mapping, language)
        directory.extend(struct.pack(">HHI", platform, encoding, 4 + 8 * len(records) + len(payload)))
        payload.extend(subtable)
    return bytes(directory + payload)

def renamed_names(source):
    fmt, count, strings_at = struct.unpack_from(">HHH", source)
    require(fmt == 0, "Only the inspected format-0 name table is supported")
    replacement = {
        1: "OfflineFreightZH", 2: "Regular", 3: "OfflineFreightZH-Local",
        4: "Offline Freight Chinese", 6: "OfflineFreightZH-Regular",
        16: "OfflineFreightZH", 17: "Regular", 18: "Offline Freight Chinese",
    }
    records, strings = bytearray(), bytearray()
    for index in range(count):
        platform, encoding, language, name, length, offset = struct.unpack_from(">6H", source, 6 + 12 * index)
        value = source[strings_at + offset:strings_at + offset + length]
        require(len(value) == length, "Truncated font name")
        if name in replacement:
            require(platform in (0, 1, 3), "Unexpected font-name encoding")
            value = replacement[name].encode("mac_roman" if platform == 1 else "utf-16-be")
        records.extend(struct.pack(">6H", platform, encoding, language, name, len(value), len(strings)))
        strings.extend(value)
    return struct.pack(">HHH", 0, count, 6 + count * 12) + records + strings

def repack(tables):
    tables = dict(tables)
    tables.pop(b"DSIG", None)
    head = bytearray(tables[b"head"])
    head[8:12] = b"\0" * 4
    tables[b"head"] = bytes(head)
    count = len(tables)
    power = 1 << (count.bit_length() - 1)
    header = struct.pack(">4s4H", b"\0\1\0\0", count, power * 16,
                         power.bit_length() - 1, count * 16 - power * 16)
    directory, payload = bytearray(), bytearray()
    head_at = None
    for tag, raw in sorted(tables.items()):
        offset = 12 + count * 16 + len(payload)
        directory.extend(struct.pack(">4sIII", tag, checksum(raw), offset, len(raw)))
        if tag == b"head":
            head_at = offset
        payload.extend(raw)
        payload.extend(b"\0" * (-len(raw) % 4))
    result = bytearray(header + directory + payload)
    struct.pack_into(">I", result, head_at + 8, (MAGIC_SUM - checksum(result)) & 0xFFFFFFFF)
    require(checksum(result) == MAGIC_SUM, "SFNT checksum adjustment failed")
    return bytes(result)

def validate_codebook(codebook, characters):
    require(set(codebook) == set(characters), "Incomplete codebook")
    require(len(set(codebook.values())) == len(codebook), "Duplicate ASCII token")
    require(all(len(value) == 1 and 33 <= ord(value) < 127 and value not in "\\&"
                for value in codebook.values()), "Unsafe ASCII token")

def widen(text, signed=False):
    return "".join(chr(((b - 256) & 65535) if signed and b >= 128 else b)
                   for b in text.encode("utf-8"))

def raster(font, text):
    mask = font.getmask(text, mode="L")
    return mask.size, bytes(mask), font.getbbox(text), font.getlength(text)

def changes(old, new, path=()):
    require(type(old) is type(new), "Candidate changed a value type")
    if isinstance(old, dict):
        require(old.keys() == new.keys(), "Candidate changed control properties")
        return [item for key in old for item in changes(old[key], new[key], path + (key,))]
    if isinstance(old, list):
        require(len(old) == len(new), "Candidate changed control count")
        return [item for index in range(len(old)) for item in changes(old[index], new[index], path + (index,))]
    return [path] if old != new else []
