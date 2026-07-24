"""Check generated UTS #46 intervals against the pinned Unicode data."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / 'data/unicode/17.0.0'
GENERATOR = ROOT / 'tools/generate/unicode/idna.py'
COMMITTED = ROOT / 'generated/unicode/idna-table.h'
STATUS = {'valid': 0, 'ignored': 1, 'mapped': 2, 'deviation': 3, 'disallowed': 4}


class IdnaTableTest(unittest.TestCase):
    def test_every_mapping_matches_unicode_and_compiles(self):
        with tempfile.TemporaryDirectory() as folder:
            header = Path(folder) / 'idna.h'
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(DATA),
                   '--output', str(header)]
            subprocess.run(cmd, check=True)
            output = header.read_text()
            subprocess.run(cmd, check=True)
            self.assertEqual(output, header.read_text())
            self.assertEqual(output, COMMITTED.read_text())
            manifest = json.loads((DATA / 'source.json').read_text())
            self.assertIn(manifest['version'], output)
            self.assertIn(next(row['sha256'] for row in manifest['inputs']
                               if row['file'] == 'idna/IdnaMappingTable.txt'),
                          output)
            mappings = [int(n) for n in re.findall(r'\d+', output.split('liblexa_idna_mappings[] = {\n', 1)[1]
                                                 .split('};', 1)[0])]
            rows = [tuple(map(int, row)) for row in
                    re.findall(r'\{(\d+), (\d+), (\d+), (\d+), (\d+), (\d+)\}',
                               output.split('liblexa_idna_ranges[] = {\n', 1)[1])]
            expected = []
            offset = 0
            for line in (DATA / 'idna/IdnaMappingTable.txt').read_text().splitlines():
                text = line.split('#', 1)[0].strip()
                if not text:
                    continue
                parts = [p.strip() for p in text.split(';')]
                bounds = [int(p, 16) for p in parts[0].split('..')]
                cps = [int(p, 16) for p in parts[2].split()] if len(parts) > 2 else []
                expected.append((bounds[0], bounds[-1], offset, STATUS[parts[1]],
                                 len(cps), {'NV8': 1, 'XV8': 2}.get(parts[-1], 0)))
                self.assertEqual(mappings[offset:offset + len(cps)], cps)
                offset += len(cps)
            self.assertEqual(rows, expected)
            self.assertEqual(offset, len(mappings))
            c = Path(folder) / 'check.c'
            c.write_text('#include "idna.h"\nint main(void) {\n'
                         '  return LIBLEXA_IDNA_RANGE_COUNT != 9262 ||\n'
                         '    liblexa_idna_ranges[0].first != 0 ||\n'
                         '    liblexa_idna_ranges[LIBLEXA_IDNA_RANGE_COUNT - 1].last != 0x10ffff;\n}\n')
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-const-variable', '-pedantic', str(c),
                            '-o', str(Path(folder) / 'check')], check=True)
            subprocess.run([str(Path(folder) / 'check')], check=True)

    def test_rejects_gaps_and_preserves_output(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'idna').mkdir()
            manifest = json.loads((DATA / 'source.json').read_text())
            bad = (DATA / 'idna/IdnaMappingTable.txt').read_text().replace(
                '0000..002C    ; valid', '0001..002C    ; valid', 1).encode()
            (path / 'idna/IdnaMappingTable.txt').write_bytes(bad)
            next(item for item in manifest['inputs'] if item['file'] ==
                 'idna/IdnaMappingTable.txt')['sha256'] = hashlib.sha256(bad).hexdigest()
            (path / 'source.json').write_text(json.dumps(manifest))
            output = path / 'out.h'
            output.write_text('keep')
            result = subprocess.run([sys.executable, str(GENERATOR), '--data-dir', str(path),
                                     '--output', str(output)], text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('ranges must partition', result.stderr)
            self.assertEqual(output.read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
