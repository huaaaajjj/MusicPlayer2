import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest
import zlib


LIMIT = 4 * 1024 * 1024


def png_bytes(width, height, pixels, channels=3):
    def chunk(kind, contents):
        return struct.pack(">I", len(contents)) + kind + contents + struct.pack(">I", zlib.crc32(kind + contents))

    stride = width * channels
    scanlines = b"".join(b"\0" + pixels[offset:offset + stride] for offset in range(0, len(pixels), stride))
    header = struct.pack(">IIBBBBB", width, height, 8, 2 if channels == 3 else 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(scanlines, 0)) + chunk(b"IEND", b"")


def pad_png(image, size):
    contents = b"\0" * (size - len(image) - 12)
    kind = b"mpAD"
    chunk = struct.pack(">I", len(contents)) + kind + contents + struct.pack(">I", zlib.crc32(kind + contents))
    return image[:-12] + chunk + image[-12:]


class AlbumCoverCompressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix="cover-compression-", dir=OPTIONS.output)).resolve()
        cls.small = png_bytes(32, 24, bytes([30, 120, 200]) * (32 * 24))
        cls.large = cls.root / "超大封面.png"
        cls.large.write_bytes(png_bytes(4096, 3072, random.Random(42).randbytes(4096 * 3072 * 3)))
        print("Test artifacts:", cls.root)

    def prepare(self, data=None, source=None, expected=0):
        folder = Path(tempfile.mkdtemp(dir=self.root))
        if source is None:
            source = folder / "原始封面.png"
            source.write_bytes(data)
        destination = folder / "result.jpg"
        before = hashlib.sha256(source.read_bytes()).digest()
        result = subprocess.run([OPTIONS.driver, "--prepare-cover", str(source), str(destination)], capture_output=True)
        self.assertEqual(result.returncode, expected, result.stderr.decode(errors="replace"))
        self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), before)
        if expected == 0:
            self.assertGreater(destination.stat().st_size, 0)
            self.assertLessEqual(destination.stat().st_size, LIMIT)
            subprocess.run([OPTIONS.ffmpeg, "-v", "error", "-i", str(destination), "-f", "null", "-"], check=True)
        else:
            self.assertFalse(destination.exists())
        return destination

    def dimensions(self, path):
        result = subprocess.check_output([OPTIONS.ffprobe, "-v", "error", "-select_streams", "v:0",
                                          "-show_entries", "stream=width,height", "-of", "json", str(path)])
        stream = json.loads(result)["streams"][0]
        return stream["width"], stream["height"]

    def test_small_image_keeps_original_bytes(self):
        result = self.prepare(self.small)
        self.assertEqual(result.read_bytes(), self.small)

    def test_exactly_four_mib_is_not_recompressed(self):
        image = pad_png(self.small, LIMIT)
        result = self.prepare(image)
        self.assertEqual(result.read_bytes(), image)

    def test_one_byte_above_limit_is_compressed_to_jpeg(self):
        result = self.prepare(pad_png(self.small, LIMIT + 1))
        self.assertTrue(result.read_bytes().startswith(b"\xff\xd8\xff"))
        self.assertEqual(self.dimensions(result), (32, 24))

    def test_large_noisy_image_is_resized_when_quality_is_not_enough(self):
        result = self.prepare(source=self.large)
        width, height = self.dimensions(result)
        self.assertLess(width, 4096)
        self.assertLess(height, 3072)
        self.assertAlmostEqual(width / height, 4 / 3, places=2)

    def test_invalid_oversized_image_is_rejected(self):
        self.prepare(b"Not an image".ljust(LIMIT + 1, b"x"), expected=3)

    def test_missing_file_is_rejected(self):
        folder = Path(tempfile.mkdtemp(dir=self.root))
        destination = folder / "result.jpg"
        result = subprocess.run([OPTIONS.driver, "--prepare-cover", str(folder / "missing.png"), str(destination)])
        self.assertEqual(result.returncode, 3)
        self.assertFalse(destination.exists())

    def test_parallel_compression_cleans_up_temporary_files(self):
        source = self.root / "parallel.png"
        source.write_bytes(pad_png(self.small, LIMIT + 1))
        with ThreadPoolExecutor(max_workers=4) as executor:
            results = list(executor.map(lambda unused: self.prepare(source=source), range(8)))
        self.assertEqual(len({path for path in results}), 8)

    def test_transparency_is_flattened_on_white(self):
        image = png_bytes(16, 16, bytes([0, 0, 255, 0]) * (16 * 16), channels=4)
        result = self.prepare(pad_png(image, LIMIT + 1))
        pixels = subprocess.check_output([OPTIONS.ffmpeg, "-v", "error", "-i", str(result), "-f", "rawvideo",
                                          "-pix_fmt", "rgb24", "-"])
        self.assertTrue(all(value >= 250 for value in pixels))

    def test_jpeg_orientation_is_preserved(self):
        source = self.root / "orientation-source.png"
        source.write_bytes(self.small)
        jpeg = self.root / "orientation.jpg"
        subprocess.run([OPTIONS.ffmpeg, "-v", "error", "-i", str(source), "-frames:v", "1", str(jpeg)], check=True)
        exif = b"Exif\0\0II\x2a\0" + struct.pack("<IHHHIH", 8, 1, 0x0112, 3, 1, 6) + b"\0" * 6
        contents = jpeg.read_bytes()
        oriented = contents[:2] + b"\xff\xe1" + struct.pack(">H", len(exif) + 2) + exif + contents[2:]
        result = self.prepare(oriented.ljust(LIMIT + 1, b"\0"))
        self.assertEqual(self.dimensions(result), (24, 32))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", required=True)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--output", default=str(Path(__file__).parent / "x64"))
    OPTIONS, unittest_arguments = parser.parse_known_args()
    OPTIONS.driver = str(Path(OPTIONS.driver).resolve())
    Path(OPTIONS.output).mkdir(parents=True, exist_ok=True)
    unittest.main(argv=[__file__, *unittest_arguments])
