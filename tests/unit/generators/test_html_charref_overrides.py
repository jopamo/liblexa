"""Compare pinned HTML numeric-reference overrides and committed output."""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / 'data/whatwg/html'
GENERATOR = ROOT / 'tools/generate/html/charref_overrides.py'
COMMITTED = ROOT / 'generated/html/html-charref-overrides.h'


class CharrefOverridesTest(unittest.TestCase):
    def test_source_rows_and_reproducible_header(self):
        manifest = json.loads((DATA / 'source.json').read_text())
        source = (DATA / 'source.html').read_text()
        table = source.split('<table id="table-charref-overrides">', 1)[1]
        table = re.sub(r'<!--.*?-->', '', table.split('</table>', 1)[0],
                       flags=re.S)
        expected = [(int(a, 16), int(b, 16)) for a, b in
                    re.findall(r'<tr><td>0x([0-9A-F]+) <td>0x([0-9A-F]+)', table)]
        with tempfile.TemporaryDirectory() as folder:
            header = Path(folder) / 'charref.h'
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(DATA),
                   '--output', str(header)]
            subprocess.run(cmd, check=True)
            first = header.read_text()
            subprocess.run(cmd, check=True)
            self.assertEqual(first, header.read_text())
            self.assertEqual(first, COMMITTED.read_text())
            self.assertIn(manifest['html_revision'], first)
            self.assertIn(manifest['inputs'][0]['sha256'], first)
            actual = [(int(a), int(b)) for a, b in re.findall(
                r'    \{(\d+), (\d+)\},', first)]
            self.assertEqual(actual, expected)
            c = Path(folder) / 'check.c'
            c.write_text('#include "charref.h"\nint main(void) {\n'
                         '  return LIBLEXA_HTML_CHARREF_OVERRIDE_COUNT != 27 ||\n'
                         '    liblexa_html_charref_overrides[0].input != 128;\n}\n')
            subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-Werror',
                            '-pedantic', str(c), '-o', str(Path(folder) / 'check')],
                           check=True)
            subprocess.run([str(Path(folder) / 'check')], check=True)

    def test_corrupt_source_does_not_replace_output(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            raw = (DATA / 'source.html').read_bytes()
            (directory / 'source.html').write_bytes(raw + b' ')
            (directory / 'source.json').write_bytes(
                (DATA / 'source.json').read_bytes())
            out = directory / 'out.h'
            out.write_text('keep')
            failed = subprocess.run(
                [sys.executable, str(GENERATOR), '--data-dir', str(directory),
                 '--output', str(out)], capture_output=True, text=True)
            self.assertIn('checksum', failed.stderr)
            self.assertEqual(out.read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
