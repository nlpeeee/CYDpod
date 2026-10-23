import pathlib
import tempfile
import unittest
from tools.media_check import required_fixtures, POSITIVE_FILES, NEGATIVE_FILES

class FixtureMatrixTest(unittest.TestCase):
    def test_missing_partial_and_empty_fixtures_fail(self):
        out = pathlib.Path(__file__).resolve().parent / "out"
        out.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=out) as location:
            directory=pathlib.Path(location)
            with self.assertRaisesRegex(RuntimeError,"stereo-128.aac"):
                required_fixtures(directory)
            for name in POSITIVE_FILES + NEGATIVE_FILES:
                (directory/name).write_bytes(b"x")
            self.assertEqual(len(required_fixtures(directory)),10)
            (directory/NEGATIVE_FILES[-1]).unlink()
            with self.assertRaisesRegex(RuntimeError,"stereo-moov-last.m4a"):
                required_fixtures(directory)
            (directory/NEGATIVE_FILES[-1]).write_bytes(b"")
            with self.assertRaisesRegex(RuntimeError,"stereo-moov-last.m4a"):
                required_fixtures(directory)

if __name__ == "__main__": unittest.main()
