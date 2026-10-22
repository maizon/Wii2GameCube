import sys
import struct
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pack_guest_dol import GuestDolError, pack, validate


RETURNING_CODE = bytes.fromhex("386000004e800020")


def corrupt(blob: bytes, off: int, val: int) -> bytes:
    x = bytearray(blob)
    struct.pack_into(">I", x, off, val)
    return bytes(x)


class GuestDolTests(unittest.TestCase):
    def test_roundtrip(self):
        dol = pack(RETURNING_CODE)
        self.assertEqual(len(dol), 0x120)
        self.assertEqual(validate(dol), RETURNING_CODE + bytes(24))

    def test_length_caps(self):
        for code in (b"", b"x", b"x" * 4097):
            with self.subTest(code_len=len(code)), self.assertRaises(GuestDolError):
                pack(code)

    def test_reject_missing_return(self):
        with self.assertRaises(GuestDolError):
            pack(b"\x00" * 8)

    def test_corrupt_file_length(self):
        with self.assertRaises(GuestDolError):
            validate(pack(RETURNING_CODE)[:-1])

    def test_bad_load_address(self):
        with self.assertRaises(GuestDolError):
            validate(corrupt(pack(RETURNING_CODE), 0x48, 0x90000000))

    def test_entry_differs(self):
        with self.assertRaises(GuestDolError):
            validate(corrupt(pack(RETURNING_CODE), 0xE0, 0x80018004))

    def test_bss_rejected(self):
        with self.assertRaises(GuestDolError):
            validate(corrupt(pack(RETURNING_CODE), 0xDC, 32))

    def test_data_rejected(self):
        with self.assertRaises(GuestDolError):
            validate(corrupt(pack(RETURNING_CODE), 0x48 + 7 * 4, 0x80020000))

    def test_extra_text_rejected(self):
        with self.assertRaises(GuestDolError):
            validate(corrupt(pack(RETURNING_CODE), 0x90 + 4, 12))

    def test_reserved_header_rejected(self):
        data = bytearray(pack(RETURNING_CODE))
        data[0xF8] = 1
        with self.assertRaises(GuestDolError):
            validate(bytes(data))

    def test_truncated_header(self):
        with self.assertRaises(GuestDolError):
            validate(b"\0" * 200)


if __name__ == "__main__":
    unittest.main()
