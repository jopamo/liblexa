"""Independent generated-data checks against the published WHATWG table."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / 'data/whatwg/html'
GENERATOR = ROOT / 'tools/generate/html/entities.py'
COMMITTED = ROOT / 'generated/html/html-entities.h'


class HtmlEntitiesTest(unittest.TestCase):
    def test_complete_deterministic_table_and_c_compilation(self):
        entries = json.loads((DATA / 'entities.json').read_text())
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'entities.h'
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(DATA),
                   '--output', str(path)]
            subprocess.run(cmd, check=True)
            first = path.read_bytes()
            subprocess.run(cmd, check=True)
            self.assertEqual(first, path.read_bytes())
            self.assertEqual(first, COMMITTED.read_bytes())
            self.assertEqual(first.count(b'    {"&'), len(entries))
            self.assertIn(f'#define LIBLEXA_HTML_ENTITY_COUNT {len(entries)}'.encode(), first)
            self.assertIn(b'{"&NotEqualTilde;", 15, {8770, 824}, 2}', first)
            self.assertIn(b'{"&AElig", 6, {198, 0}, 1}', first)
            c = Path(folder) / 'check.c'
            c.write_text('#include "entities.h"\n'
                         'int main(void) {\n'
                         '  return LIBLEXA_HTML_ENTITY_COUNT != 2231 ||\n'
                         '    liblexa_html_entities[0].name_length == 0;\n}\n')
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-pedantic', str(c), '-o', str(Path(folder) / 'check')],
                           check=True)
            subprocess.run([str(Path(folder) / 'check')], check=True)

    def test_rejects_tampered_input_without_overwriting_output(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            output = path / 'out.h'
            output.write_text('leave this alone')
            for name in ('entities.json', 'source.json'):
                (path / name).write_bytes((DATA / name).read_bytes())
            (path / 'entities.json').write_text('{}')
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(path),
                   '--output', str(output)]
            result = subprocess.run(cmd, text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('checksum', result.stderr)
            self.assertEqual(output.read_text(), 'leave this alone')


if __name__ == '__main__':
    unittest.main()
