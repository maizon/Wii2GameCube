#!/usr/bin/env python3
"""Build and validate a deliberately constrained Wii-target freestanding DOL.

Unlike a homebrew using libogc and IOS, this one-section fixture contains a
returnable PowerPC function only. The GameCube loader rebases just its pure
position-independent instructions into managed MEM1. No arbitrary Wii DOLs.
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

HEADER_SIZE = 0x100
ORIGINAL_TEXT_ADDRESS = 0x80018000
MAX_CODE_BYTES = 4096
MEM1_START, MEM1_END = 0x80000000, 0x81800000


class GuestDolError(ValueError):
    pass


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def validate(dol: bytes) -> bytes:
    if len(dol) < HEADER_SIZE:
        raise GuestDolError("DOL header truncated")
    offset, address, size = u32(dol, 0x00), u32(dol, 0x48), u32(dol, 0x90)
    if offset != HEADER_SIZE:
        raise GuestDolError("unexpected text section offset")
    if address != ORIGINAL_TEXT_ADDRESS or not (MEM1_START <= address < MEM1_END):
        raise GuestDolError("unexpected Wii text load address")
    if size == 0 or size > MAX_CODE_BYTES or size % 4:
        raise GuestDolError("invalid code section size")
    if len(dol) != HEADER_SIZE + size:
        raise GuestDolError("file size inconsistent with only text section")
    if address + size > MEM1_END:
        raise GuestDolError("section extends beyond MEM1")
    if u32(dol, 0xE0) != address:
        raise GuestDolError("entry must begin at text section start")
    if u32(dol, 0xD8) or u32(dol, 0xDC):
        raise GuestDolError("BSS not supported")
    for section in range(1, 18):
        for off in (0, 0x48, 0x90):
            if u32(dol, off + section * 4):
                raise GuestDolError("unexpected extra text/data section")
    if any(dol[i] for i in list(range(0xE4, HEADER_SIZE))):
        raise GuestDolError("unknown DOL header extension must be zero")
    payload = dol[HEADER_SIZE:]
    if bytes.fromhex("4e800020") not in payload:
        raise GuestDolError("no PowerPC blr return instruction")
    return payload


def pack(code: bytes) -> bytes:
    if not code or len(code) > MAX_CODE_BYTES or len(code) % 4:
        raise GuestDolError("code must be 4-byte aligned and at most 4096 bytes")
    if bytes.fromhex("4e800020") not in code:
        raise GuestDolError("no PowerPC blr return instruction")
    header = bytearray(HEADER_SIZE)
    for off, value in [(0x00, HEADER_SIZE), (0x48, ORIGINAL_TEXT_ADDRESS),
                       (0x90, len(code)), (0xE0, ORIGINAL_TEXT_ADDRESS)]:
        struct.pack_into(">I", header, off, value)
    # DOL toolchain expects section sizes aligned to 32 bytes in strict mode.
    padded = code + bytes((-len(code)) % 32)
    struct.pack_into(">I", header, 0x90, len(padded))
    output = bytes(header) + padded
    assert validate(output) == padded
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("guest_bin", type=Path)
    parser.add_argument("output_dol", type=Path)
    args = parser.parse_args()
    try:
        output = pack(args.guest_bin.read_bytes())
        args.output_dol.write_bytes(output)
    except (OSError, GuestDolError) as exc:
        parser.exit(2, f"guest DOL pack failed: {exc}\n")
    print(f"Wii freestanding DOL: {args.output_dol} ({len(output)} bytes, "
          f"{len(output) - HEADER_SIZE} bytes of PPC instructions)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
