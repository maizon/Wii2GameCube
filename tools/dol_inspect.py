#!/usr/bin/env python3
"""Conservative static analyzer for Nintendo GameCube/Wii DOL executables.

No reverse engineering, patching, executing, or redistribution of binaries.
Reports structure and memory-pressure indicators; DOES NOT establish compatibility.
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Any

HEADER_SIZE = 0x100
MEM1_START, MEM1_END = 0x80000000, 0x81800000  # 24 MiB
MEM2_START, MEM2_END = 0x90000000, 0x94000000  # 64 MiB (Wii)
ALIAS_MEM1_START, ALIAS_MEM1_END = 0xC0000000, 0xC1800000
ALIAS_MEM2_START, ALIAS_MEM2_END = 0xD0000000, 0xD4000000
UINT32_LIMIT = 1 << 32


class DolError(ValueError):
    pass


def read_be32(buf: bytes, off: int) -> int:
    return struct.unpack_from('>I', buf, off)[0]


def region_for(start: int, size: int) -> str:
    end = start + size
    if MEM1_START <= start and end <= MEM1_END:
        return 'MEM1 (GameCube/Wii shared address range)'
    if MEM2_START <= start and end <= MEM2_END:
        return 'MEM2 (Wii only)'
    if ALIAS_MEM1_START <= start and end <= ALIAS_MEM1_END:
        return 'MEM1 uncached alias (GC/Wii)'
    if ALIAS_MEM2_START <= start and end <= ALIAS_MEM2_END:
        return 'MEM2 uncached alias (Wii only)'
    return 'unknown/outside single supported RAM region'


def inspect(data: bytes) -> dict[str, Any]:
    if len(data) < HEADER_SIZE:
        raise DolError('File is smaller than a 0x100-byte DOL header')

    warnings: list[str] = []
    errors: list[str] = []
    sections: list[dict[str, Any]] = []
    file_intervals: list[tuple[int, int, str]] = []
    memory_intervals: list[tuple[int, int, str]] = []
    offset_fields = [read_be32(data, i * 4) for i in range(18)]
    addresses = [read_be32(data, 0x48 + i * 4) for i in range(18)]
    sizes = [read_be32(data, 0x90 + i * 4) for i in range(18)]
    bss_start = read_be32(data, 0xD8)
    bss_size = read_be32(data, 0xDC)
    entry = read_be32(data, 0xE0)

    for i, (off, addr, size) in enumerate(zip(offset_fields, addresses, sizes)):
        name = f'text{i}' if i < 7 else f'data{i-7}'
        if size == 0:
            if off or addr:
                warnings.append(f'{name}: empty section with nonzero offset/address')
            continue
        region = region_for(addr, size)
        sec = {'name': name, 'file_offset': off, 'address': addr,
               'size': size, 'end_address_exclusive': addr + size,
               'region': region}
        sections.append(sec)
        if off < HEADER_SIZE:
            errors.append(f'{name}: file section begins inside the header')
        if off + size > len(data):
            errors.append(f'{name}: file section exceeds file size')
        if addr + size > UINT32_LIMIT:
            errors.append(f'{name}: memory address overflow')
        if off % 32 or size % 32:
            warnings.append(f'{name}: offset/size not both 32-byte aligned')
        if addr % 32:
            warnings.append(f'{name}: load address not 32-byte aligned')
        if region.startswith('unknown'):
            warnings.append(f'{name}: load address not in a known contiguous RAM region')
        if 'MEM2' in region:
            warnings.append(f'{name}: requires Wii MEM2; not natively available on GameCube')
        if MEM1_START <= addr < MEM1_START + 0x4000:
            warnings.append(f'{name}: overlaps low MEM1 system/reserved area')
        file_intervals.append((off, off + size, name))
        memory_intervals.append((addr, addr + size, name))

    for kind, intervals in [('file', file_intervals), ('load-memory', memory_intervals)]:
        sorted_ranges = sorted(intervals)
        for (a0, a1, n0), (b0, b1, n1) in zip(sorted_ranges, sorted_ranges[1:]):
            if b0 < a1:
                errors.append(f'{kind} ranges overlap: {n0} and {n1}')

    if not sections:
        errors.append('No non-empty text or data sections')
    text_ranges = [(s['address'], s['end_address_exclusive']) for s in sections if s['name'].startswith('text')]
    if not any(start <= entry < end for start, end in text_ranges):
        errors.append('Entry point not inside a text section')
    if bss_size:
        bss_region = region_for(bss_start, bss_size)
        if bss_start + bss_size > UINT32_LIMIT:
            errors.append('BSS memory address overflow')
        if 'MEM2' in bss_region:
            warnings.append('BSS uses Wii MEM2')
        if bss_region.startswith('unknown'):
            warnings.append('BSS address outside known contiguous RAM')
        if any(max(bss_start, a) < min(bss_start + bss_size, b)
               for a, b, _ in memory_intervals):
            warnings.append('BSS overlaps a loaded section (may be intentional)')

    mem1_sections = [s for s in sections if 'MEM1' in s['region']]
    mem2_sections = [s for s in sections if 'MEM2' in s['region']]
    all_sections = sum(s['size'] for s in sections)
    mem1_load_size = sum(s['size'] for s in mem1_sections)
    mem2_load_size = sum(s['size'] for s in mem2_sections)
    if mem1_load_size > 20 * 1024 * 1024:
        warnings.append('Loaded MEM1 sections exceed 20 MiB before stack, heap and framebuffer')

    return {
        'valid_structure': not errors,
        'file_size': len(data), 'entry_point': entry,
        'bss': {'address': bss_start, 'size': bss_size,
                'region': region_for(bss_start, bss_size) if bss_size else 'none'},
        'sections': sections,
        'summary': {'total_loaded_section_bytes': all_sections,
                    'mem1_section_bytes': mem1_load_size,
                    'mem2_section_bytes': mem2_load_size,
                    'uses_mem2': bool(mem2_sections) or (bss_size > 0 and 'MEM2' in region_for(bss_start, bss_size)),
                    'can_execute_on_gamecube': 'NOT DETERMINABLE from DOL static layout'},
        'warnings': warnings, 'errors': errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dol', type=Path, help='Owned/homebrew DOL to inspect')
    parser.add_argument('--json', action='store_true', help='Output structured JSON')
    parser.add_argument('--strict', action='store_true', help='Exit 2 on warnings as well')
    args = parser.parse_args()
    try:
        report = inspect(args.dol.read_bytes())
    except (OSError, DolError) as exc:
        parser.exit(2, f'error: {exc}\n')
    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        print(f'DOL: {args.dol} ({report["file_size"]} bytes)')
        print(f'Entry: 0x{report["entry_point"]:08X}')
        for s in report['sections']:
            print(f'  {s["name"]:<7} addr=0x{s["address"]:08X} size={s["size"]:>9} {s["region"]}')
        print(f'BSS:  0x{report["bss"]["address"]:08X} size={report["bss"]["size"]}')
        for msg in report['warnings']:
            print('WARNING: ' + msg)
        for msg in report['errors']:
            print('ERROR: ' + msg)
        print('Compatibility: NOT DETERMINABLE by this tool')
    if report['errors'] or (args.strict and report['warnings']):
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
