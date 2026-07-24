"""Check legacy MathML 3 headings and reproducible C output."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / 'data/w3c/mathml3'
GENERATOR = ROOT / 'tools/generate/mathml/tags_v3.py'
SECTIONS = ('presm.mglyph', 'presm.mfenced', 'presm.malign')


class Mathml3TagsTest(unittest.TestCase):
    def test_legacy_names_deterministic_and_compilable(self):
        source = (DATA / 'chapter3.html').read_text()
        expected = sorted({name for section in SECTIONS
                           for name in re.findall(r'<code>&lt;([a-z]+)/?&gt;</code>',
                                                  source.split(f'id="{section}"', 1)[1]
                                                  .split('</h', 1)[0])})
        with tempfile.TemporaryDirectory() as folder:
            header = Path(folder) / 'mathml3.h'
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(DATA),
                   '--output', str(header)]
            subprocess.run(cmd, check=True)
            first = header.read_text()
            subprocess.run(cmd, check=True)
            self.assertEqual(first, header.read_text())
            self.assertEqual(re.findall(r'^    "([a-z]+)",$', first, re.M), expected)
            self.assertEqual(expected, ['maligngroup', 'malignmark', 'mfenced', 'mglyph'])
            c = Path(folder) / 'check.c'
            c.write_text('#include "mathml3.h"\nint main(void) {\n'
                         '  return LIBLEXA_MATHML3_ELEMENT_COUNT != 4 ||\n'
                         '    liblexa_mathml3_elements[0][0] == 0;\n}\n')
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-pedantic', str(c), '-o', str(Path(folder) / 'check')], check=True)
            subprocess.run([str(Path(folder) / 'check')], check=True)

    def test_rejects_missing_section_without_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            data = (DATA / 'chapter3.html').read_bytes().replace(b'presm.mfenced', b'presm.removed')
            (path / 'chapter3.html').write_bytes(data)
            manifest = json.loads((DATA / 'source.json').read_text())
            next(item for item in manifest['inputs'] if item['file'] ==
                 'chapter3.html')['sha256'] = hashlib.sha256(data).hexdigest()
            (path / 'source.json').write_text(json.dumps(manifest))
            out = path / 'out.h'
            out.write_text('keep')
            result = subprocess.run([sys.executable, str(GENERATOR), '--data-dir',
                                     str(path), '--output', str(out)], capture_output=True,
                                    text=True)
            self.assertIn('section missing', result.stderr)
            self.assertEqual(out.read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
