"""Check explicit regeneration and mismatch reporting without changing sources."""

from pathlib import Path
import tempfile
import unittest

from tools.validate.verify_generated import verify


class VerifyGeneratedTest(unittest.TestCase):
    def test_matches_and_reports_stale_header(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "source"
            build = Path(directory) / "build"
            generator = root / "tools/generate/sample/table.py"
            generator.parent.mkdir(parents=True)
            generator.write_text(
                "import argparse\n"
                "from pathlib import Path\n"
                "p = argparse.ArgumentParser()\n"
                "p.add_argument('--data-dir', type=Path)\n"
                "p.add_argument('--output', type=Path)\n"
                "a = p.parse_args()\n"
                "a.output.parent.mkdir(parents=True, exist_ok=True)\n"
                "a.output.write_bytes((a.data_dir / 'input.txt').read_bytes())\n"
            )
            data = root / "data/sample"
            data.mkdir(parents=True)
            (data / "input.txt").write_bytes(b"current")
            checked_in = root / "generated/sample/table.h"
            checked_in.parent.mkdir(parents=True)
            checked_in.write_bytes(b"current")
            outputs = (("sample/table.h", "sample/table.py", "sample"),)
            verify(root, build, outputs)
            self.assertEqual((build / "regenerated/sample/table.h").read_bytes(), b"current")
            checked_in.write_bytes(b"stale")
            with self.assertRaisesRegex(ValueError, "regenerated output differs"):
                verify(root, build, outputs)
            self.assertEqual(checked_in.read_bytes(), b"stale")


if __name__ == "__main__":
    unittest.main()
