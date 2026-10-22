"""Compile exactly the guest source on host CPU to verify portable scene ABI.

These checks are NOT an emulation test; PPC generation and the GC backend
are checked separately in devkitPPC CI and the GameCube runtime.
"""
import ctypes
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAGIC = 0x57324333


class Rect(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in
                ('x', 'y', 'width', 'height', 'yuy2')]


class Scene(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in
                ('magic', 'version', 'frame', 'count')] + [('rects', Rect * 4)]


@unittest.skipUnless(shutil.which("cc"), "native C compiler unavailable")
class SceneTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / "scene.so"
        subprocess.run(["cc", "-O2", "-fPIC", "-shared", "-Werror",
                        "-Wall", "-Wextra", "-o", str(library),
                        str(ROOT / "guest_scene/source/scene.c")],
                       check=True, capture_output=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.function = cls.lib.wii_guest_scene
        cls.function.argtypes = [ctypes.c_uint32, ctypes.POINTER(Scene)]
        cls.function.restype = ctypes.c_uint32

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_abi_size(self):
        self.assertEqual(ctypes.sizeof(Rect), 20)
        self.assertEqual(ctypes.sizeof(Scene), 96)

    def test_output_and_coverage(self):
        for frame in (0, 1, 17, 123, 65535, 0xffffffff):
            with self.subTest(frame=frame):
                scene = Scene()
                signature = self.function(frame, ctypes.byref(scene))
                self.assertEqual(signature, (MAGIC ^ frame ^ 3) & 0xffffffff)
                self.assertEqual((scene.magic, scene.version, scene.frame, scene.count),
                                 (MAGIC, 1, frame, 3))
                for rect in scene.rects[:scene.count]:
                    self.assertGreater(rect.width, 0)
                    self.assertGreater(rect.height, 0)
                    self.assertLessEqual(rect.x + rect.width, 1000)
                    self.assertLessEqual(rect.y + rect.height, 1000)
                    self.assertEqual(rect.yuy2 & 0xFF000000, rect.yuy2 & 0xFF000000)

    def test_determinism(self):
        first, second = Scene(), Scene()
        self.assertEqual(self.function(987, ctypes.byref(first)),
                         self.function(987, ctypes.byref(second)))
        self.assertEqual(bytes(first), bytes(second))

    def test_null_pointer_is_rejected(self):
        self.assertEqual(self.function(1, None), 0)

    def test_motion_changes_across_frames(self):
        a, b = Scene(), Scene()
        self.function(1, ctypes.byref(a))
        self.function(2, ctypes.byref(b))
        self.assertNotEqual((a.rects[0].x, a.rects[0].y),
                            (b.rects[0].x, b.rects[0].y))


if __name__ == "__main__":
    unittest.main()
