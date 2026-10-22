import unittest

try:
    from PIL import Image
except ImportError:
    Image = None

try:
    from tools.dolphin_smoke import count_guest_colors
except ImportError:
    count_guest_colors = None


@unittest.skipIf(Image is None or count_guest_colors is None,
                 "Pillow available in dedicated Dolphin CI job only")
class DetectorTests(unittest.TestCase):
    def test_black_and_white_not_accepted(self):
        image = Image.new("RGB", (60, 60), (255, 255, 255))
        self.assertEqual(count_guest_colors(image), (0, 0))

    def test_two_scene_colors(self):
        image = Image.new("RGB", (90, 90), (0, 0, 0))
        for y in range(30):
            for x in range(0, 30):
                image.putpixel((x, y), (255, 130, 40))
            for x in range(40, 70):
                image.putpixel((x, y), (60, 220, 220))
        red, cyan = count_guest_colors(image)
        self.assertGreaterEqual(red, 12)
        self.assertGreaterEqual(cyan, 12)


if __name__ == "__main__":
    unittest.main()
