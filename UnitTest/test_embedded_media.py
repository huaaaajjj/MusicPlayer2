import argparse
import base64
import ctypes
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

import mutagen
from mutagen.apev2 import APEv2
from mutagen.asf import ASFByteArrayAttribute
from mutagen.flac import Picture
from mutagen.id3 import APIC, COMM, SYLT, TIT2, TPE1, USLT
from mutagen.mp4 import MP4Cover, MP4FreeForm


PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII="
)
LYRICS = "[00:01.00]中文歌词\n[00:02.00]第二行 / lyrics\n"
FORMATS = {"mp3": "libmp3lame", "flac": "flac", "m4a": "aac", "wav": "pcm_s16le", "wma": "wmav2"}


def seed(path, lyrics=True, cover=True):
    audio = mutagen.File(path)
    if audio.tags is None:
        audio.add_tags()
    if path.suffix in (".mp3", ".wav"):
        audio.tags.add(TIT2(encoding=3, text="Keep title"))
        audio.tags.add(TPE1(encoding=3, text="Keep artist"))
        audio.tags.add(COMM(encoding=3, lang="eng", desc="Keep", text="Keep comment"))
        if lyrics:
            audio.tags.add(USLT(encoding=3, lang="eng", desc="Description is not lyrics", text=LYRICS))
            audio.tags.add(USLT(encoding=3, lang="zho", desc="Alternate", text=LYRICS))
        if cover:
            for description in ("Front", "Back"):
                audio.tags.add(APIC(encoding=3, mime="image/png", type=3, desc=description, data=PNG))
    elif path.suffix == ".flac":
        audio["title"] = "Keep title"
        audio["artist"] = "Keep artist"
        audio["KEEP"] = "Keep custom field"
        if lyrics:
            audio["LYRICS"] = [LYRICS, "Another version"]
            audio["UNSYNCEDLYRICS"] = LYRICS
        if cover:
            for description in ("Front", "Back"):
                picture = Picture()
                picture.data, picture.mime, picture.type = PNG, "image/png", 3
                picture.desc = description
                audio.add_picture(picture)
    elif path.suffix == ".m4a":
        audio["\xa9nam"] = "Keep title"
        audio["\xa9ART"] = "Keep artist"
        audio["----:com.apple.iTunes:KEEP"] = [MP4FreeForm(b"Keep custom field")]
        if lyrics:
            audio["\xa9lyr"] = [LYRICS]
            audio["----:com.apple.iTunes:Lyrics"] = [MP4FreeForm(LYRICS.encode("utf-8"))]
        if cover:
            audio["covr"] = [MP4Cover(PNG, imageformat=MP4Cover.FORMAT_PNG)] * 2
    elif path.suffix == ".wma":
        audio["Title"] = "Keep title"
        audio["Author"] = "Keep artist"
        audio["Copyright"] = "Keep copyright"
        audio["Description"] = "Keep description"
        audio["Rating"] = "Keep rating"
        audio["KEEP"] = "Keep custom field"
        if lyrics:
            audio["WM/Lyrics"] = [LYRICS, LYRICS]
        if cover:
            picture = struct.pack("<BI", 3, len(PNG)) + "image/png\0Front\0".encode("utf-16-le") + PNG
            audio["WM/Picture"] = [ASFByteArrayAttribute(picture)] * 2
    audio.save()


def remaining_tags(path):
    audio = mutagen.File(path)
    return {
        key: repr(value)
        for key, value in audio.tags.items()
        if not any(word in key.upper() for word in ("USLT", "APIC", "LYRICS", "LYR", "COVR", "PICTURE"))
    }


class EmbeddedMediaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix="embedded-media-", dir=OPTIONS.output)).resolve()
        cls.fixtures = cls.root / "fixtures"
        cls.fixtures.mkdir()
        for extension, codec in FORMATS.items():
            subprocess.run(
                [OPTIONS.ffmpeg, "-v", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=0.25",
                 "-c:a", codec, str(cls.fixtures / ("source." + extension))], check=True
            )
        print("Test artifacts:", cls.root)

    def sample(self, extension="mp3", lyrics=True, cover=True):
        folder = Path(tempfile.mkdtemp(dir=self.root))
        path = folder / ("曲目（测试）." + extension)
        shutil.copyfile(self.fixtures / ("source." + extension), path)
        seed(path, lyrics, cover)
        return path

    def export(self, path, expected=0):
        result = subprocess.run([OPTIONS.driver, str(path)], capture_output=True)
        self.assertEqual(result.returncode, expected, result.stderr.decode(errors="replace"))

    def audio_hash(self, path):
        return subprocess.check_output(
            [OPTIONS.ffmpeg, "-v", "error", "-i", str(path), "-map", "0:a:0", "-c", "copy",
             "-f", "hash", "-hash", "sha256", "-"]
        )

    def test_round_trip_preserves_audio_and_other_tags(self):
        for extension in FORMATS:
            with self.subTest(format=extension):
                path = self.sample(extension)
                original_audio = self.audio_hash(path)
                metadata = remaining_tags(path)
                path.with_suffix(".lrc").write_text("Old lyrics", encoding="utf-8")
                path.with_suffix(".png").write_bytes(b"Old artwork")
                path.with_suffix(".jpg").write_bytes(b"Old alternate artwork")
                self.export(path)
                self.assertEqual(path.with_suffix(".lrc").read_text(encoding="utf-8"), LYRICS)
                self.assertEqual(path.with_suffix(".png").read_bytes(), PNG)
                self.assertFalse(path.with_suffix(".jpg").exists())
                self.assertEqual(remaining_tags(path), metadata)
                self.assertEqual(self.audio_hash(path), original_audio)
                tags = mutagen.File(path)
                for key in tags.tags.keys():
                    self.assertFalse(any(word in key.upper() for word in ("USLT", "APIC", "LYRICS", "LYR", "COVR", "PICTURE")), key)
                if extension == "flac":
                    self.assertEqual(tags.pictures, [])
                before_repeat = {item.name: item.read_bytes() for item in path.parent.iterdir()}
                self.export(path, 1)
                self.assertEqual({item.name: item.read_bytes() for item in path.parent.iterdir()}, before_repeat)

    def test_cover_only_keeps_existing_external_lyrics(self):
        path = self.sample(lyrics=False)
        path.with_suffix(".lrc").write_bytes(b"External lyrics")
        self.export(path)
        self.assertEqual(path.with_suffix(".lrc").read_bytes(), b"External lyrics")

    def test_lyrics_only_keeps_existing_external_cover(self):
        path = self.sample(cover=False)
        path.with_suffix(".jpg").write_bytes(b"External artwork")
        self.export(path)
        self.assertEqual(path.with_suffix(".jpg").read_bytes(), b"External artwork")

    def test_empty_audio_does_not_touch_sidecars(self):
        path = self.sample(lyrics=False, cover=False)
        before = path.read_bytes()
        self.export(path, 1)
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(len(list(path.parent.iterdir())), 1)

    def test_failed_lyric_export_leaves_audio_untouched(self):
        path = self.sample()
        before = path.read_bytes()
        path.with_suffix(".lrc").mkdir()
        self.export(path, 3)
        self.assertEqual(path.read_bytes(), before)
        self.assertFalse(list(path.parent.glob("*.tmp")))

    def test_read_only_target_does_not_destroy_existing_cover(self):
        path = self.sample()
        before = path.read_bytes()
        cover = path.with_suffix(".png")
        cover.write_bytes(b"Do not lose this")
        self.assertTrue(ctypes.windll.kernel32.SetFileAttributesW(str(cover), 1))
        try:
            self.export(path, 3)
            self.assertEqual(path.read_bytes(), before)
            self.assertEqual(cover.read_bytes(), b"Do not lose this")
        finally:
            ctypes.windll.kernel32.SetFileAttributesW(str(cover), 128)

    def test_synchronized_lyrics_are_skipped_without_data_loss(self):
        path = self.sample()
        audio = mutagen.File(path)
        audio.tags.add(SYLT(encoding=3, lang="eng", format=2, type=1, text=[("Synchronized", 1000)]))
        audio.save()
        before = path.read_bytes()
        self.export(path, 2)
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(len(list(path.parent.iterdir())), 1)

    def test_mp3_ape_duplicates_are_also_removed(self):
        path = self.sample()
        ape = APEv2()
        ape["Lyrics"] = "APE lyrics"
        ape["Cover Art (Back)"] = b"cover.png\0" + PNG
        ape["KEEP"] = "Keep APE field"
        ape.save(path)
        self.export(path)
        remaining = APEv2(path)
        self.assertNotIn("Lyrics", remaining)
        self.assertNotIn("Cover Art (Back)", remaining)
        self.assertEqual(str(remaining["KEEP"]), "Keep APE field")

    def test_read_only_audio_is_not_modified(self):
        path = self.sample()
        before = path.read_bytes()
        self.assertTrue(ctypes.windll.kernel32.SetFileAttributesW(str(path), 1))
        try:
            self.export(path, 3)
            self.assertEqual(path.read_bytes(), before)
        finally:
            ctypes.windll.kernel32.SetFileAttributesW(str(path), 128)

    def test_empty_lyric_tag_does_not_overwrite_external_lyrics(self):
        path = self.sample(lyrics=False, cover=False)
        audio = mutagen.File(path)
        audio.tags.add(USLT(encoding=3, lang="eng", text=""))
        audio.save()
        path.with_suffix(".lrc").write_bytes(b"Keep real lyrics")
        before = path.read_bytes()
        self.export(path, 2)
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(path.with_suffix(".lrc").read_bytes(), b"Keep real lyrics")

    def test_mp3_preserves_id3_version_and_legacy_tag(self):
        path = self.sample()
        audio = mutagen.File(path)
        audio.save(v1=2, v2_version=3)
        contents = bytearray(path.read_bytes())
        self.assertEqual(contents[-128:-125], b"TAG")
        contents[-125:-95] = b"Different legacy title".ljust(30, b"\0")
        path.write_bytes(contents)
        legacy = bytes(contents[-128:])
        self.export(path)
        self.assertEqual(path.read_bytes()[-128:], legacy)
        self.assertEqual(mutagen.File(path).tags.version, (2, 3, 0))

    def test_m4a_standard_and_freeform_lyrics_are_supported(self):
        for removed_key in ("\xa9lyr", "----:com.apple.iTunes:Lyrics"):
            with self.subTest(removed_key=removed_key):
                path = self.sample("m4a", cover=False)
                audio = mutagen.File(path)
                del audio[removed_key]
                audio.save()
                self.export(path)
                self.assertEqual(path.with_suffix(".lrc").read_text(encoding="utf-8"), LYRICS)
                self.export(path, 1)

    def test_flac_legacy_cover_and_lyrics_are_removed(self):
        path = self.sample("flac")
        audio = mutagen.File(path)
        audio.clear_pictures()
        del audio["LYRICS"]
        audio["COVERART"] = base64.b64encode(PNG).decode("ascii")
        audio["COVERARTMIME"] = "image/png"
        audio.save()
        self.export(path)
        self.assertEqual(path.with_suffix(".png").read_bytes(), PNG)
        self.assertEqual(path.with_suffix(".lrc").read_text(encoding="utf-8"), LYRICS)
        audio = mutagen.File(path)
        for key in ("COVERART", "COVERARTMIME", "UNSYNCEDLYRICS"):
            self.assertNotIn(key, audio)
        self.export(path, 1)

    def test_unknown_artwork_is_not_removed(self):
        path = self.sample(cover=False)
        audio = mutagen.File(path)
        audio.tags.add(APIC(encoding=3, mime="application/unknown", type=3, data=b"Unknown image"))
        audio.save()
        before = path.read_bytes()
        self.export(path, 2)
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(len(list(path.parent.iterdir())), 1)

    def test_failed_duplicate_cleanup_leaves_audio_untouched(self):
        path = self.sample()
        before = path.read_bytes()
        duplicate = path.with_suffix(".jpg")
        duplicate.write_bytes(b"Old cover")
        self.assertTrue(ctypes.windll.kernel32.SetFileAttributesW(str(duplicate), 1))
        try:
            self.export(path, 3)
            self.assertEqual(path.read_bytes(), before)
            self.assertEqual(duplicate.read_bytes(), b"Old cover")
        finally:
            ctypes.windll.kernel32.SetFileAttributesW(str(duplicate), 128)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", required=True)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--output", default=str(Path(__file__).parent / "x64"))
    OPTIONS, unittest_arguments = parser.parse_known_args()
    OPTIONS.driver = str(Path(OPTIONS.driver).resolve())
    Path(OPTIONS.output).mkdir(parents=True, exist_ok=True)
    unittest.main(argv=[__file__, *unittest_arguments])
