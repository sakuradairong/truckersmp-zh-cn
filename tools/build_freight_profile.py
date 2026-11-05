"""Emit a local, ignored compatibility description; never load/execute the DLL.

Only the statically reviewed ETS2 build is accepted as profile input. Its tag
records the reference, not a runtime allowlist. Runtime relocation also requires
instruction, reference-section and call-relationship contracts to remain intact.
"""
import argparse
import pathlib
import struct
import re

parser = argparse.ArgumentParser()
parser.add_argument("core", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
args = parser.parse_args()
data = args.core.read_bytes()
if len(data) != 23868928 or data[:2] != b"MZ":
    raise SystemExit("Unsupported core file; retain the previous package.")
pe = struct.unpack_from("<I", data, 60)[0]
if data[pe:pe + 4] != b"PE\0\0":
    raise SystemExit("Invalid PE header")
machine, sections, stamp = struct.unpack_from("<HHI", data, pe + 4)
opt = pe + 24
if (machine, stamp, struct.unpack_from("<H", data, opt)[0],
        struct.unpack_from("<I", data, opt + 56)[0]) != (0x8664, 0x6AE85968, 0x20B, 0x184C000):
    raise SystemExit("This core build has not been reviewed; no profile was generated.")
table = opt + struct.unpack_from("<H", data, pe + 20)[0]
ranges = []
executable = []
section_flags = []
for i in range(sections):
    size, rva, count, raw = struct.unpack_from("<IIII", data, table + 40 * i + 8)
    ranges.append((rva, count, raw))
    flags = struct.unpack_from("<I", data, table + 40 * i + 36)[0]
    section_flags.append((rva, size, flags))
    if flags & 0x20000000:
        executable.append((rva, data[raw:raw + count]))

def mapped_read(rva, count):
    for start, size, offset in ranges:
        if start <= rva and rva + count <= start + size:
            return data[offset + rva - start:offset + rva - start + count]
    return None

def read(rva, count):
    result = mapped_read(rva, count)
    if result is not None:
        return result
    raise SystemExit("Profile source lies outside the PE file sections")

def reference_matches(candidate, contract):
    at, disp, length, width, region, literal = contract
    operand = mapped_read(candidate + at + disp, 4)
    if operand is None:
        return False
    target = candidate + at + length + struct.unpack("<i", operand)[0]
    targets = [flags for start, extent, flags in section_flags if start <= target and target + max(width, len(literal)) <= start + extent]
    if len(targets) != 1 or not targets[0] & 0x40000000:
        return False
    flags = targets[0]
    actual = "Code" if flags & 0x20000000 else "Writable" if flags & 0x80000000 else "ReadOnly"
    return actual == region and (not literal or mapped_read(target, len(literal)) == literal)

# Mask only rel32 operands. All non-address instructions, stack sizes, field
# offsets, boolean registers and internal branch shapes remain significant.
specs = [
    ("FindPlayer", 0x192270, 0x23, []),
    ("CopyAction", 0x1BA330, 0x40, [(0x0F, 1, 5), (0x1F, 3, 7), (0x3A, 1, 5)]),
    ("OnlineCallback", 0x10C940, 0x4D, [(6, 3, 7), (0x20, 1, 5), (0x42, 1, 5)]),
    ("CopyRequest", 0xF9600, 0xF9, [(0x4D, 1, 5), (0x5C, 1, 5), (0xC6, 1, 5), (0xE2, 1, 5)]),
    ("CopyEvent", 0xFA140, 0x81, [(0x10, 3, 7), (0x1B, 1, 5), (0x2C, 3, 7), (0x74, 1, 5), (0x7A, 3, 7)]),
    ("ManagerConstructor", 0xF92E0, 0x3C, [(0x2B, 3, 7), (0x32, 3, 7)]),
    ("OnlineAction", 0x1BA1E0, 0x48, [(0x12, 1, 5), (0x2A, 3, 7)]),
    ("OnlineCapability", 0x1C15B0, 0x2F, [(0x0A, 3, 7)]),
    ("EditorGenerate", 0x102230, 0x44, []),
    ("EditorReset", 0xFBFF0, 0x63, [(9, 1, 5), (0x11, 1, 5), (0x19, 1, 5), (0x3A, 1, 5), (0x58, 1, 5)]),
    ("SelectAutocomplete", 0x3319D0, 0xA6, [(0x32, 3, 7), (0x5A, 3, 7), (0x7D, 1, 5), (0x91, 1, 5)]),
    # Native queryType compares interned type-token POINTERS, not string bytes.
    # Resolve Button:MenuItem through this reviewed cast's RIP-relative operand.
    # Include the adjacent AutocompleteItem name getter to distinguish an
    # inlined duplicate of the cast body elsewhere in the same module.
    ("AutocompleteItemType", 0x330DC0, 0x48, [(0, 3, 7), (0x0C, 3, 7), (0x18, 3, 7), (0x26, 3, 7), (0x40, 3, 7)]),
    ("SelectCombo", 0x3307B0, 0x27, []),
    ("CheckSet", 0x157990, 0x11, []),
    ("MenuChildren", 0x15E840, 0x18, []),
    ("ElementText", 0x15EE90, 0x0E, []),
    ("RefreshRoute", 0xFCC90, 0x3C, []),
    ("RefreshOwned", 0xFCE10, 0x1D, []),
    ("RefreshTake", 0xFCEB0, 0x67, []),
    ("RefreshGenerate", 0xFCFD0, 0x4B, []),
]
# Short getters are checked at their actual virtual slots rather than searched
# globally: identical leaf bodies may legitimately be folded or repeated.
anchored = [
    ("ElementDisabled", 0x15E980, 8, []),
    ("CheckDisabled", 0x1578D0, 0x11, []),
    ("CheckGet", 0x157950, 0x11, []),
]
lines = ["// Generated locally from the explicitly selected core; do not commit.",
         "#pragma once", "#include <cstddef>", "namespace overlay::freight_profile {",
         'inline constexpr char ReferenceClientVersion[]="0.7.9.4";',
         "enum class Region { Code, ReadOnly, Writable };",
         "struct Reference {unsigned at,displacement,length,width;Region region;const unsigned char* literal;std::size_t literalSize;};",
         "struct Pattern {const char* name;const unsigned char* bytes;const unsigned char* mask;std::size_t size;const Reference* references;std::size_t referenceCount;};"]
for name, rva, size, operands in specs + anchored:
    code = bytearray(read(rva, size))
    mask = bytearray([1] * size)
    references = []
    contracts = []
    for at, disp, length in operands:
        if at + length > size or disp + 4 > length:
            raise SystemExit("Invalid profile range")
        if (disp == 1 and code[at] not in (0xE8, 0xE9)) or (disp == 3 and (code[at] not in (0x48, 0x4C) or code[at + 1] not in (0x89, 0x8B, 0x8D))):
            raise SystemExit(f"Unexpected instruction shape: {name}+{at:x}")
        target = rva + at + length + struct.unpack_from("<i", code, at + disp)[0]
        width = 8 if disp == 3 and code[at + 1] in (0x89, 0x8B) else 1
        targets = [flags for start, extent, flags in section_flags if start <= target and target + width <= start + extent]
        if len(targets) != 1 or not targets[0] & 0x40000000:
            raise SystemExit(f"Unmapped reference: {name}+{at:x}")
        flags = targets[0]
        region = "Code" if flags & 0x20000000 else "Writable" if flags & 0x80000000 else "ReadOnly"
        if (disp == 1) != (region == "Code"):
            raise SystemExit(f"Unexpected reference section: {name}+{at:x}")
        literal = b""
        if region == "ReadOnly":
            # Preserve referenced event/control names, not their old location.
            for start, extent, raw in ranges:
                if start <= target < start + extent:
                    candidate = data[raw + target - start:raw + min(target - start + 256, extent)]
                    end = candidate.find(b"\0")
                    if 0 < end < 256 and all(32 <= c <= 126 for c in candidate[:end]):
                        literal = candidate[:end + 1]
                    break
        literal_name = f"{name}Literal{at}" if literal else "nullptr"
        if literal:
            lines.append(f"inline constexpr unsigned char {literal_name}[]={{" + ",".join(f"0x{x:02x}" for x in literal) + "};")
        references.append(f"{{{at},{disp},{length},{width},Region::{region},{literal_name},{len(literal)}}}")
        contracts.append((at, disp, length, width, region, literal))
        code[at + disp:at + disp + 4] = bytes(4)
        mask[at + disp:at + disp + 4] = bytes(4)
    expression = b"".join(re.escape(bytes([byte])) if significant else b"." for byte, significant in zip(code, mask))
    matches = [base + hit.start() for base, section in executable for hit in re.finditer(b"(?=(" + expression + b"))", section, re.S)]
    # Structurally identical casts differ by their interned type names. Require
    # one candidate satisfying BOTH the code shape and all reference contracts.
    matches = [candidate for candidate in matches if all(reference_matches(candidate, contract) for contract in contracts)]
    if (name not in {entry[0] for entry in anchored} and matches != [rva]) or rva not in matches:
        raise SystemExit(f"Profile input is ambiguous for {name}: {matches}; no usable profile produced.")
    for suffix, values in (("Bytes", code), ("Mask", mask)):
        lines.append(f"inline constexpr unsigned char {name}{suffix}[]={{" + ",".join(f"0x{x:02x}" for x in values) + "};")
    if references:
        lines.append(f"inline constexpr Reference {name}References[]={{" + ",".join(references) + "};")
    refs = f"{name}References" if references else "nullptr"
    lines.append(f'inline constexpr Pattern {name}{{"{name}",{name}Bytes,{name}Mask,sizeof {name}Bytes,{refs},{len(references)}}};')
lines.append("}")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"Local freight profile generated: {args.output}")
