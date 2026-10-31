#!/usr/bin/env python3
"""Maizon v0.4 increment on an already-prepared v0.3 xerpi/fakemote checkout.
Fails closed on upstream drift. No IOS installation is performed here.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()
egc = root / "embedded-game-controller" / "embedded-game-controller"
driver = egc / "usb_drivers" / "nintendont_ini_driver.c"

def edit(path, edits):
    data = path.read_text(encoding="utf-8")
    for a, b in edits:
        if data.count(a) != 1:
            raise RuntimeError(f"{path}: anchor count {data.count(a)} != 1: {a[:85]!r}")
        data = data.replace(a, b, 1)
    path.write_text(data, encoding="utf-8")

edit(root / "source" / "button_map.c", [
    ("state->position[ir_analog_axis_map[i] - 1] += val / 16;",
     "state->position[ir_analog_axis_map[i] - 1] += val / 4096;"),
])
edit(root / "source" / "input_device.c", [
    ("BIT(EGC_GAMEPAD_BUTTON_START) | BIT(EGC_GAMEPAD_BUTTON_LEFT_SHOULDER);",
     "BIT(EGC_GAMEPAD_BUTTON_START) | BIT(EGC_GAMEPAD_BUTTON_LEFT_SHOULDER) | BIT(EGC_GAMEPAD_BUTTON_RIGHT_SHOULDER);"),
    ("BIT(EGC_GAMEPAD_BUTTON_START) | BIT(EGC_GAMEPAD_BUTTON_RIGHT_SHOULDER);",
     "BIT(EGC_GAMEPAD_BUTTON_START) | BIT(EGC_GAMEPAD_BUTTON_RIGHT_SHOULDER) | BIT(EGC_GAMEPAD_BUTTON_RIGHT_PADDLE1);"),
])
edit(driver, [
    ("    u8 consecutive_errors;\n    bool request_pending;",
     "    u8 consecutive_errors;\n    bool request_pending;\n    bool had_valid_input;\n    u16 stale_ticks;"),
    ("static void nin_report_callback(egc_usb_transfer_t *transfer)",
     """static s16 nin_axis_to_egc(u8 byte, bool invert)
{
    int value = ((int)byte - 128) * 256;
    if (invert) value = -value;
    if (value > 32767) value = 32767;
    if (value < -32768) value = -32768;
    return (s16)value;
}
static void nin_report_neutral(egc_input_device_t *dev)
{
    egc_input_state_t neutral;
    memset(&neutral, 0, sizeof(neutral));
    neutral.gamepad.touch_points[0].x = -1;
    neutral.gamepad.touch_points[1].x = -1;
    egc_device_driver_report_input(dev, &neutral);
}
static void nin_report_callback(egc_usb_transfer_t *transfer)"""),
    ("""        state.gamepad.axes[EGC_GAMEPAD_AXIS_LEFTX] = egc_u8_to_s16(input.axis[NIN_STICK_X]);
        state.gamepad.axes[EGC_GAMEPAD_AXIS_LEFTY] =
            egc_u8_to_s16((u8)(255 - input.axis[NIN_STICK_Y]));
        state.gamepad.axes[EGC_GAMEPAD_AXIS_RIGHTX] = egc_u8_to_s16(input.axis[NIN_CSTICK_X]);
        state.gamepad.axes[EGC_GAMEPAD_AXIS_RIGHTY] =
            egc_u8_to_s16((u8)(255 - input.axis[NIN_CSTICK_Y]));""",
     """        state.gamepad.axes[EGC_GAMEPAD_AXIS_LEFTX] = nin_axis_to_egc(input.axis[NIN_STICK_X], false);
        state.gamepad.axes[EGC_GAMEPAD_AXIS_LEFTY] = nin_axis_to_egc(input.axis[NIN_STICK_Y], true);
        state.gamepad.axes[EGC_GAMEPAD_AXIS_RIGHTX] = nin_axis_to_egc(input.axis[NIN_CSTICK_X], false);
        state.gamepad.axes[EGC_GAMEPAD_AXIS_RIGHTY] = nin_axis_to_egc(input.axis[NIN_CSTICK_Y], true);"""),
    ("        priv->consecutive_errors = 0;\n        priv->cooldown_ticks = 0;",
     "        priv->consecutive_errors = 0;\n        priv->cooldown_ticks = 0;\n        priv->stale_ticks = 0;\n        priv->had_valid_input = true;"),
    ("""    if (priv->cooldown_ticks) {
        priv->cooldown_ticks--;
        return true;
    }""",
     """    if (priv->had_valid_input && ++priv->stale_ticks >= 50) {
        nin_report_neutral(device); /* release stale HOME/A after 250ms */
        priv->had_valid_input = false;
    }
    if (priv->cooldown_ticks) {
        priv->cooldown_ticks--;
        return true;
    }"""),
    ("    desc->has_rumble = false; /* no HID output-report implementation yet */",
     "    desc->has_rumble = false; /* no HID output-report implementation yet */\n    nin_report_neutral(device); /* fresh state after USB replug */"),
])

usb_h = """#ifndef MAIZON_NIN_USB_DESC_H
#define MAIZON_NIN_USB_DESC_H
#include <stddef.h>
#include <stdint.h>
uint8_t nin_usb_interrupt_in_endpoint(const uint8_t *response, size_t len);
#endif
"""
usb_c = """#include "nin_usb_desc.h"
uint8_t nin_usb_interrupt_in_endpoint(const uint8_t *raw, size_t len)
{
    size_t offset = 36;
    int accepted = 1;
    if (!raw || len < 44) return 0x81;
    for (unsigned i = 0; i < 16 && offset + 2 <= len; ++i) {
        uint8_t desc_len = raw[offset], type = raw[offset + 1];
        if (desc_len < 2 || desc_len > len - offset) break;
        if (type == 4 && desc_len >= 9) {
            const uint8_t klass = raw[offset + 5], protocol = raw[offset + 7];
            accepted = !(klass == 3 && (protocol == 1 || protocol == 2));
        }
        if (type == 5 && desc_len >= 7 && accepted) {
            const uint8_t ep = raw[offset + 2], attr = raw[offset + 3];
            if ((ep & 0x80) && (ep & 0x0f) && ((attr & 3) == 3)) return ep;
        }
        offset += ((size_t)desc_len + 3u) & ~(size_t)3u;
    }
    return 0x81;
}
"""
(egc / "usb_drivers" / "nin_usb_desc.h").write_text(usb_h, encoding="utf-8")
(egc / "usb_drivers" / "nin_usb_desc.c").write_text(usb_c, encoding="utf-8")
edit(egc / "CMakeLists.txt", [
    ("    usb_drivers/nin_hid.c\n", "    usb_drivers/nin_hid.c\n    usb_drivers/nin_usb_desc.c\n"),
])
edit(egc / "backends" / "ogc.c", [
    ('#include "ipc.h"', '#include "ipc.h"\n#include "driver_api.h"\n#include "usb_drivers/nin_usb_desc.h"'),
    ("    u32 dev_id;\n    egc_usb_devdesc_t desc;",
     "    u32 dev_id;\n    u8 intr_in_endpoint;\n    egc_usb_devdesc_t desc;"),
    ("static int usb_hid_v5_get_descriptors(int host_fd, u32 dev_id, egc_usb_devdesc_t *udd)",
     "static int usb_hid_v5_get_descriptors(int host_fd, u32 dev_id,\n                                      egc_usb_devdesc_t *udd, u8 *intr_ep)"),
    ("    if (udd)\n        memcpy(udd, outbuf, sizeof(*udd));\n    return rc;",
     "    if (rc == IOS_OK) {\n        if (udd) memcpy(udd, outbuf, sizeof(*udd));\n        if (intr_ep) *intr_ep = nin_usb_interrupt_in_endpoint(outbuf, sizeof(outbuf));\n    }\n    return rc;"),
    ("""    t->t.endpoint = endpoint;
    t->t.length = length;
    t->t.data = t->buffer;
    t->callback = callback;
    int rc = usb_hid_v5_intr_transfer_async""",
     """    t->t.endpoint = endpoint;
    if (device->driver == &nin_ini_usb_device_driver &&
        endpoint == (EGC_USB_ENDPOINT_IN | 1) &&
        ogc_device_from_input_device(device)->usb.intr_in_endpoint)
        t->t.endpoint = ogc_device_from_input_device(device)->usb.intr_in_endpoint;
    t->t.length = length;
    t->t.data = t->buffer;
    t->callback = callback;
    int rc = usb_hid_v5_intr_transfer_async"""),
    ("ret = usb_hid_v5_get_descriptors(host_fd, dev_id, &device->usb.desc);",
     "ret = usb_hid_v5_get_descriptors(host_fd, dev_id, &device->usb.desc,\n                                     &device->usb.intr_in_endpoint);"),
])
print("v0.4 upgrade: IR zero-centre, 3-key switches, stale release, endpoint detection")
