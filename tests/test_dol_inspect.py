import sys
import struct
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from dol_inspect import DolError, inspect, region_for


def sample(addr=0x80005000, bss_addr=0x80009000, bss_size=0x200, entry=None):
    data = bytearray(0x120)
    struct.pack_into('>I', data, 0x00, 0x100)  # text0 file offset
    struct.pack_into('>I', data, 0x48, addr)   # text0 load address
    struct.pack_into('>I', data, 0x90, 0x20)   # text0 size
    struct.pack_into('>I', data, 0xD8, bss_addr)
    struct.pack_into('>I', data, 0xDC, bss_size)
    struct.pack_into('>I', data, 0xE0, addr if entry is None else entry)
    return data


class DolTests(unittest.TestCase):
    def test_valid_shared_mem1(self):
        rep = inspect(sample())
        self.assertTrue(rep['valid_structure'])
        self.assertFalse(rep['summary']['uses_mem2'])
        self.assertEqual(rep['summary']['can_execute_on_gamecube'], 'NOT DETERMINABLE from DOL static layout')

    def test_mem2_detected(self):
        rep = inspect(sample(addr=0x90005000, bss_addr=0x90008000))
        self.assertTrue(rep['valid_structure'])
        self.assertTrue(rep['summary']['uses_mem2'])

    def test_bss_only_mem2(self):
        rep = inspect(sample(bss_addr=0x90000000))
        self.assertTrue(rep['summary']['uses_mem2'])

    def test_truncated_header(self):
        with self.assertRaises(DolError):
            inspect(b'\0' * 255)

    def test_bad_file_offset(self):
        data = sample()
        struct.pack_into('>I', data, 0, 0xFFFF0000)
        self.assertFalse(inspect(data)['valid_structure'])

    def test_bad_entry(self):
        self.assertFalse(inspect(sample(entry=0x80009000))['valid_structure'])

    def test_address_boundary_and_overflow(self):
        # A 32-byte section ending exactly at 2^32 does not overflow.
        boundary = inspect(sample(addr=0xFFFFFFE0))
        self.assertEqual(boundary['sections'][0]['end_address_exclusive'], 0x100000000)
        self.assertFalse(any('address overflow' in e for e in boundary['errors']))
        # A 64-byte section beginning there DOES overflow the 32-bit address space.
        data = sample(addr=0xFFFFFFE0)
        data.extend(b'\0' * 0x20)
        struct.pack_into('>I', data, 0x90, 0x40)
        overflow = inspect(data)
        self.assertTrue(any('address overflow' in e for e in overflow['errors']))

    def test_overlap(self):
        data = sample()
        struct.pack_into('>I', data, 0x04, 0x100)        # text1 file offset
        struct.pack_into('>I', data, 0x4C, 0x80008000)   # text1 addr
        struct.pack_into('>I', data, 0x94, 0x20)        # text1 size
        self.assertTrue(any('file ranges overlap' in e for e in inspect(data)['errors']))

    def test_file_overflow(self):
        data = sample()
        struct.pack_into('>I', data, 0x90, 0x1000)
        self.assertTrue(any('exceeds file size' in e for e in inspect(data)['errors']))

    def test_mem1_end(self):
        self.assertIn('unknown', region_for(0x817FFFF0, 0x20))


if __name__ == '__main__':
    unittest.main()
