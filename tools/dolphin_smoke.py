#!/usr/bin/env python3
"""Run the GameCube DOL in Dolphin, require TWO visible Wii guest scene colors.

An actual emulator screenshot is used: not OCR and not an internal "success"
message from a compiler. Colors are intentionally distinct from the monochrome
Phase 2 fallback. Still not a real GameCube-hardware compatibility guarantee.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

from PIL import Image


def count_guest_colors(image: Image.Image) -> tuple[int, int]:
    """Detect red/yellow and cyan/green scene sprites, sampling every 3 pixels."""
    pixels = image.convert("RGB")
    red = cyan = 0
    for y in range(0, pixels.height, 3):
        for x in range(0, pixels.width, 3):
            r, g, b = pixels.getpixel((x, y))
            if r > 100 and r > g + 34 and r > b + 48:
                red += 1
            if g > 110 and b > 110 and g > r + 32 and b > r + 32:
                cyan += 1
    return red, cyan


def run(dol: Path, destination: Path, seconds: int) -> int:
    if not dol.is_file() or dol.stat().st_size < 256:
        raise RuntimeError(f"DOL file missing/too small: {dol}")
    destination.mkdir(parents=True, exist_ok=True)
    out_image = destination / "dolphin-frame.png"
    log_file = destination / "dolphin-stdout.log"
    json_file = destination / "dolphin-observations.json"

    env = os.environ.copy()
    env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    env["SDL_AUDIODRIVER"] = "dummy"
    env["QT_QPA_PLATFORM"] = "xcb"
    env["QT_LOGGING_RULES"] = "*.debug=false"

    args = ["dolphin-emu", "--batch", "--exec", str(dol.resolve()),
            "--video_backend=OGL"]
    print("Running:", " ".join(args), flush=True)
    print("DISPLAY:", env.get("DISPLAY"), flush=True)

    observations: list[dict] = []
    success = False
    with log_file.open("wb") as output:
        process = subprocess.Popen(args, stdout=output, stderr=subprocess.STDOUT,
                                   env=env, start_new_session=True)
        try:
            for sec in range(seconds):
                time.sleep(1)
                code = process.poll()
                if code is not None:
                    raise RuntimeError(f"Dolphin terminated before visual PASS: code {code}")
                shot = subprocess.run(
                    ["import", "-window", "root", "-silent", str(out_image)],
                    capture_output=True, text=True, timeout=8,
                )
                if shot.returncode != 0:
                    print("Screenshot warning:", shot.stderr.strip(), flush=True)
                    continue
                with Image.open(out_image) as image:
                    red, cyan = count_guest_colors(image)
                    dimensions = image.size
                observations.append({"second": sec + 1, "red_pixels": red,
                                     "cyan_pixels": cyan})
                print(f"t={sec + 1}s image={dimensions} red={red} cyan={cyan}",
                      flush=True)
                if red >= 12 and cyan >= 12 and sec >= 2:
                    success = True
                    break
        finally:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
    json_file.write_text(
        json.dumps({"success": success, "frame_count": len(observations),
                    "observations": observations}, indent=2) + "\n")
    if not success:
        print("FAIL: scene colors not both visible in Dolphin under Xvfb.", flush=True)
        print("See captured PNG/log and observations artifact for diagnosis.",
              flush=True)
        return 1
    print("PASS: runtime Dolphin screenshot contains both independently "
          "colored Phase 3 guest scene sprites.", flush=True)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dol", type=Path)
    parser.add_argument("--artifacts", type=Path, default=Path("dolphin-evidence"))
    parser.add_argument("--seconds", type=int, default=35)
    args = parser.parse_args()
    if not 5 <= args.seconds <= 120:
        parser.error("--seconds must be within 5..120")
    try:
        return run(args.dol, args.artifacts, args.seconds)
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        print(f"Dolphin smoke FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
