# Evidence and automated verification

## Human screenshots already provided

Two earlier images proved **separate** Wii/GC native program booting in Dolphin modes.

A later screenshot from the user proved that the Phase 1 GameCube host loaded 80 Wii-configured PPC instruction bytes into MEM1 and obtained **5/5 PASS** plus changing sprite coordinates. This is experimental cross-target PPC execution. It is **not** the execution of the Wii homebrew's libogc/IOS code on GC.

## Phase 2 (no manual user testing required)

The CI automatically:
1. Compiles `guest_probe/guest.c` using Wii-target `wii_rules`.
2. Checks for return instruction and that the `.text.guest` section requires no relocation.
3. Wraps this compiled code into a minimal, strictly validated Wii-format `.dol`.
4. Confirms `dol_inspect.py` accepts its file layout.
5. Repackages the DOL and compares bytes for reproducibility.
6. Builds the GC application embedding the **entire DOL file**, not raw code.
7. Builds the separate standalone Wii homebrew as a baseline.
8. Publishes both DOLs plus the guest fixture in a GitHub Actions artifact.

The new GC program validates the header of its embedded DOL, rebases its only text section to an allocated MEM1 region, invalidates instruction cache, executes the pure Wii-target function, compares five vectors and draws a moving 2D square to the GameCube VI XFB in YUY2 format.

**Important:** GitHub Actions compilation and file structure tests alone do NOT prove successful execution of this new build in Dolphin GC, because emulation is not yet part of CI. We will not ask the user to manually test as a prerequisite to continuing. The Phase 1 image is the verified dynamic baseline.

## Constraints

- The guest DOL is generated from OUR ORIGINAL project-authored freestanding code; no IOS, libogc, system calls or arbitrary game files are executed.
- Its `0x80018000` original address is metadata and code is copied to newly allocated memory, not written directly there.
- Exactly one TEXT section, maximum 4096 bytes of code, no BSS/DATA, entry exactly first instruction, no relocations.
- A real Wii `wii_guest_sample.dol` is built but **not loaded by GC**. Commercial WiiWare and Wii Menu require independent compatibility work.
- Future step: add emulator-driven GameCube execution to CI if a stable reproducible Dolphin runner is available.
