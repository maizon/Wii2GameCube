import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from guest_gate import forbid_target_relocations, reloc_sections, validate_binary


class GuestGateTests(unittest.TestCase):
    def test_no_relocations(self):
        forbid_target_relocations("There are no relocations in this file.")

    def test_other_sections_may_have_relocations(self):
        text = "Relocation section '.rela.debug_info' at offset 0x123 contains 2 entries:"
        self.assertEqual(reloc_sections(text), {".rela.debug_info"})
        forbid_target_relocations(text)

    def test_guest_rel_rejected(self):
        with self.assertRaises(ValueError):
            forbid_target_relocations("Relocation section '.rel.text.guest' at offset 0x12 contains 1 entry:")

    def test_guest_rela_rejected(self):
        with self.assertRaises(ValueError):
            forbid_target_relocations("Relocation section '.rela.text.guest' at offset 0x12 contains 1 entry:")

    def test_guest_return_accepted(self):
        validate_binary(bytes.fromhex("386000004e800020"))

    def test_guest_return_missing(self):
        with self.assertRaises(ValueError):
            validate_binary(bytes.fromhex("38600000"))

    def test_invalid_sizes(self):
        for blob in (b"", b"x" * 3, bytes.fromhex("4e800020") * 1025):
            with self.subTest(size=len(blob)), self.assertRaises(ValueError):
                validate_binary(blob)


if __name__ == "__main__":
    unittest.main()
