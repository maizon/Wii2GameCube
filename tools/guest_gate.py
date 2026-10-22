#!/usr/bin/env python3
"""Gate Wii-built *freestanding* PPC payloads before embedding in GameCube host.

Does NOT validate arbitrary Wii programs or confer whole-DOL compatibility.
The guest must consist of one returnable EABI function, and it must have
NO relocations in the extracted .text.guest section.
"""
from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


RELOCATION_HEADER = re.compile(r"^Relocation section '([^']+)'")
TARGET_SECTION = ".text.guest"


def reloc_sections(readelf_output: str) -> set[str]:
    return {name for line in readelf_output.splitlines()
            if (m := RELOCATION_HEADER.search(line)) for name in [m.group(1)]}


def forbid_target_relocations(readelf_output: str) -> None:
    names = reloc_sections(readelf_output)
    bad = [name for name in sorted(names) if
           name in (".rel" + TARGET_SECTION, ".rela" + TARGET_SECTION)]
    if bad:
        raise ValueError("guest code has unresolved relocations: " + ", ".join(bad))


def validate_binary(payload: bytes) -> None:
    if not payload:
        raise ValueError("empty guest payload")
    if len(payload) > 4096:
        raise ValueError("guest payload exceeds 4096 bytes")
    if len(payload) % 4:
        raise ValueError("PPC guest payload must be four-byte aligned")
    # The last instruction of the tiny guest function must return to caller.
    # PowerPC 'blr' is big-endian 4E 80 00 20; code after it can be padding.
    if bytes.fromhex("4e800020") not in payload:
        raise ValueError("return instruction 'blr' absent")


def run_tool(exe: str, *args: str) -> str:
    try:
        return subprocess.run([exe, *args], capture_output=True, text=True,
                              check=True).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        raise ValueError(f"cannot run {exe}: {exc}") from exc


def gate(elf_object: Path, payload_path: Path, tool_prefix: str) -> None:
    obj = str(elf_object)
    payload = payload_path.read_bytes()
    validate_binary(payload)
    table = run_tool(tool_prefix + "readelf", "-W", "-r", obj)
    forbid_target_relocations(table)
    sections = run_tool(tool_prefix + "readelf", "-W", "-S", obj)
    if TARGET_SECTION not in sections:
        raise ValueError(f"object does not contain {TARGET_SECTION}")
    instructions = run_tool(tool_prefix + "objdump", "-d", "-j", TARGET_SECTION, obj)
    # In addition to relocation-free guest, reject out-of-section direct calls.
    # This is an extra guard, NOT complete dynamic code verification.
    if re.search(r"\bbl\s+[^r]", instructions):
        raise ValueError("direct call found inside guest code")
    print(f"PASS freestanding Wii-target PPC guest: {len(payload)} bytes, "
          "return present, target relocations absent")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("obj", type=Path)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--tool-prefix", default="powerpc-eabi-")
    args = parser.parse_args()
    try:
        gate(args.obj, args.binary, args.tool_prefix)
    except (OSError, ValueError) as exc:
        parser.exit(2, f"Guest gate FAILED: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
