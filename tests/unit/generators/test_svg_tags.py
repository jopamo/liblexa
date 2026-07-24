"""Check generated SVG names against pinned W3C XML definitions."""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / 'data/w3c/svg2'
GENERATOR = ROOT / 'tools/generate/svg/tags.py'


class SvgTagsTest(unittest.TestCase):
    def test_full_table_deterministic_and_compilable(self):
        manifest = json.loads((DATA / 'source.json').read_text())
        names = sorted(e.get('name') for record in manifest['inputs']
                       for e in ET.parse(DATA / record['file']).getroot().findall('.//{*}element'))
        with tempfile.TemporaryDirectory() as folder:
            header = Path(folder) / 'svg.h'
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(DATA),
                   '--output', str(header)]
            subprocess.run(cmd, check=True)
            first = header.read_text()
            subprocess.run(cmd, check=True)
            self.assertEqual(first, header.read_text())
            self.assertEqual(re.findall(r'^    "([A-Za-z0-9]+)",$', first, re.M), names)
            self.assertEqual(len(names), 58)
            c = Path(folder) / 'check.c'
            c.write_text('#include "svg.h"\nint main(void) {\n'
                         '  return LIBLEXA_SVG_ELEMENT_COUNT != 58 ||\n'
                         '    liblexa_svg_elements[0][0] == 0;\n}\n')
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-pedantic', str(c), '-o', str(Path(folder) / 'check')], check=True)
            subprocess.run([str(Path(folder) / 'check')], check=True)

    def test_rejects_bad_checksum_without_overwriting(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            manifest = json.loads((DATA / 'source.json').read_text())
            for item in manifest['inputs']:
                (path / item['file']).write_bytes((DATA / item['file']).read_bytes())
            (path / manifest['inputs'][0]['file']).write_text('<definitions/>')
            (path / 'source.json').write_text(json.dumps(manifest))
            out = path / 'out.h'
            out.write_text('keep')
            result = subprocess.run([sys.executable, str(GENERATOR), '--data-dir',
                                     str(path), '--output', str(out)], capture_output=True,
                                    text=True)
            self.assertIn('checksum', result.stderr)
            self.assertEqual(out.read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
