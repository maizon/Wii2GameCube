#!/usr/bin/env python3
"""Use the Nintendont INI-compatible generic driver before DragonRise fallback.

Keep Sony DS3/DS4 specialised drivers ahead of generic for rumble,
LEDs and PS4 touchpad. The 0079:0006 USB Joystick must select the
INI-derived mapping and not be shadowed by the DragonRise handler.
"""
from pathlib import Path
import sys

egc = Path(sys.argv[1]).resolve() / "embedded-game-controller" / "embedded-game-controller" / "egc.c"
src = egc.read_text()
old = "    &ds4_usb_device_driver,\n    &dr_usb_device_driver,\n    &nin_ini_usb_device_driver,"
new = "    &ds4_usb_device_driver,\n    &nin_ini_usb_device_driver,\n    &dr_usb_device_driver,"
if src.count(old) != 1:
    raise SystemExit(f"Unexpected device driver priority in {egc}; refusing to change source.")
egc.write_text(src.replace(old,new,1))
print("OK: driver order DS3 > DS4 > Nintendont INI > DragonRise fallback")
