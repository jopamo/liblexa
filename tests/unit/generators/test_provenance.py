"""Check nested pinned inputs without permitting paths outside the manifest."""

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.validate.check_provenance import check_data_manifest


class PinnedInputTest(unittest.TestCase):
    def test_nested_input_and_outside_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "data/ucd").mkdir(parents=True)
            input_path = root / "data/ucd/UnicodeData.txt"
            input_path.write_bytes(b"pinned")
            manifest = root / "data/source.json"
            entry = {"file": "ucd/UnicodeData.txt",
                     "sha256": hashlib.sha256(b"pinned").hexdigest()}
            manifest.write_text(json.dumps({"inputs": [entry]}))
            check_data_manifest(manifest)
            for name in ("../outside", "ucd/../../outside"):
                manifest.write_text(json.dumps(
                    {"inputs": [{**entry, "file": name}]}))
                with self.assertRaises(ValueError):
                    check_data_manifest(manifest)
            (root / "outside").write_bytes(b"pinned")
            (root / "data/ucd/link").symlink_to(root)
            manifest.write_text(json.dumps(
                {"inputs": [{**entry, "file": "ucd/link/outside"}]}))
            with self.assertRaises(ValueError):
                check_data_manifest(manifest)


if __name__ == "__main__":
    unittest.main()
