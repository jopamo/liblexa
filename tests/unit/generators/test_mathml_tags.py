"""Check generated MathML names against the pinned W3C specification."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / 'data/w3c/mathml-core'
GENERATOR = ROOT / 'tools/generate/mathml/tags.py'


class MathmlTagsTest(unittest.TestCase):
    def test_full_table_deterministic_and_compilable(self):
        source = (DATA / 'spec.html').read_text()
        names = sorted(re.findall(r'<dfn class="element">([a-z][a-z0-9-]*)</dfn>', source))
        with tempfile.TemporaryDirectory() as folder:
            header = Path(folder) / 'mathml.h'
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(DATA),
                   '--output', str(header)]
            subprocess.run(cmd, check=True)
            first = header.read_text()
            subprocess.run(cmd, check=True)
            self.assertEqual(first, header.read_text())
            self.assertEqual(re.findall(r'^    "([a-z][a-z0-9-]*)",$', first, re.M), names)
            self.assertEqual(len(names), 31)
            c = Path(folder) / 'check.c'
            c.write_text('#include "mathml.h"\nint main(void) {\n'
                         '  return LIBLEXA_MATHML_ELEMENT_COUNT != 31 ||\n'
                         '    liblexa_mathml_elements[0][0] == 0;\n}\n')
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-pedantic', str(c), '-o', str(Path(folder) / 'check')], check=True)
            subprocess.run([str(Path(folder) / 'check')], check=True)

    def test_rejects_bad_checksum_without_overwriting(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'source.json').write_text((DATA / 'source.json').read_text())
            (path / 'spec.html').write_text('<html/>')
            out = path / 'out.h'
            out.write_text('keep')
            result = subprocess.run([sys.executable, str(GENERATOR), '--data-dir',
                                     str(path), '--output', str(out)], capture_output=True,
                                    text=True)
            self.assertIn('checksum', result.stderr)
            self.assertEqual(out.read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
