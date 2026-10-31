#!/usr/bin/env python3
"""Diagnostic only: use original DragonRise 0079:0006 driver instead of custom
INI driver. This is an isolation test, not a proven hardware repair.
"""
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve()
egc=root / 'embedded-game-controller' / 'embedded-game-controller'
f=egc / 'egc.c'
s=f.read_text()
a='    &ds4_usb_device_driver,\n    &nin_ini_usb_device_driver,\n    &dr_usb_device_driver,'
b='    &ds4_usb_device_driver,\n    &dr_usb_device_driver,\n    &nin_ini_usb_device_driver,'
assert s.count(a)==1, 'Cannot prove original DragonRise fallback position'
f.write_text(s.replace(a,b))
assert f.read_text().index('&dr_usb_device_driver') < f.read_text().index('&nin_ini_usb_device_driver')
print('PASS: 0079:0006 uses original upstream DragonRise driver')
