# Test matrix: what the screenshots prove

## Reported by tester, 21 October 2026

Two **independent** screenshots were received:
1. **Wii guest screenshot** (Wii controller overlay): `wii_guest_sample.dol` displays `Wii guest: video initialization completed`. This is evidence the Wii homebrew runs in Wii-mode emulation, **not** on GameCube hardware.
2. **GameCube probe screenshot** (GameCube controller overlay): `gcwcl_probe.dol` displays phase 0 diagnostics and heartbeat. This confirms the GC native program reaches video and its main loop inside Dolphin's GameCube emulation. SD absent, expected.

Neither screenshot proves Wii executables or IOS services function on a real GC.

## New Phase 1 test (embedded PPC guest)

Load **only** the new `gamecube_probe/gcwcl_probe.dol` in Dolphin **GameCube** mode.

Expected output contains:
```
Wii2GameCube - Phase 1 PowerPC guest
Host: GameCube native | Guest: Wii-built PPC
Wii PPC: loaded <nonzero> code bytes into MEM1
Wii->GC 0: (7,11) PASS
Wii->GC 1: (10,2) PASS
PASS Wii-target PPC execution: 5/5
sprite(...,...)
```
The dynamic coordinates should change with each second. A later execution in GameCube hardware is still required.

`Wii PPC` means *freestanding PowerPC instructions compiled by the Wii-configured compiler*: it does **not** mean the standalone `wii_guest_sample.dol` can be started by the GC, nor that Wii's IOS, MEM2, Wii graphics, discs or menu are supported.

## Stop conditions

- A black screen/crash after `Wii PPC: loaded`: collect Dolphin log; investigate ABI, generated instructions, instruction cache, memory bounds.
- A FAIL for any of five cases: capture expected/actual output; do not expand to loading arbitrary DOLs.
- DOL boots but missing embedded payload: build pipeline/artifact issue.
- Do not try executing an ISO/WAD or original Wii menu with this version.

Only after the Phase 1 test succeeds should we tackle a tiny isolated Wii-built DOL with explicit relocation constraints.
