"""Check pinned Unicode joining-type data and committed regeneration."""

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
GENERATOR = ROOT / 'tools/generate/unicode/joining_types.py'
COMMITTED = ROOT / 'generated/unicode/joining-types.h'
INPUT = 'ucd/extracted/DerivedJoiningType.txt'


class JoiningTypesTest(unittest.TestCase):
    def test_all_ranges_and_regeneration(self):
        manifest = json.loads((DATA / 'source.json').read_text())
        checksum = next(row['sha256'] for row in manifest['inputs']
                        if row['file'] == INPUT)
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'joining.h'
            command = [sys.executable, str(GENERATOR), '--data-dir',
                       str(DATA), '--output', str(output)]
            subprocess.run(command, check=True)
            result = output.read_text()
            subprocess.run(command, check=True)
            self.assertEqual(result, output.read_text())
            self.assertEqual(result, COMMITTED.read_text())
            self.assertIn(manifest['version'], result)
            self.assertIn(checksum, result)
            rows = [(int(first), int(last), kind) for first, last, kind in
                    re.findall(r"    \{(\d+), (\d+), '([CDLRT])'\},", result)]
            expected = []
            for line in (DATA / INPUT).read_text().splitlines():
                value = line.split('#', 1)[0].strip()
                if value:
                    bounds, kind = [part.strip() for part in value.split(';')]
                    points = [int(cp, 16) for cp in bounds.split('..')]
                    expected.append((points[0], points[-1], kind))
            self.assertEqual(rows, sorted(expected))
            self.assertTrue(all(a[1] < b[0] for a, b in zip(rows, rows[1:])))
            c = Path(folder) / 'check.c'
            c.write_text('#include "joining.h"\nint main(void) {\n'
                         '  return LIBLEXA_JOINING_RANGE_COUNT != 542 ||\n'
                         '    liblexa_joining_ranges[0].first == 0;\n}\n')
            subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-Werror',
                            '-pedantic', str(c), '-o', str(Path(folder) / 'check')],
                           check=True)
            subprocess.run([str(Path(folder) / 'check')], check=True)

    def test_corrupt_input_does_not_replace_output(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            manifest = json.loads((DATA / 'source.json').read_text())
            raw = (DATA / INPUT).read_bytes()
            target = directory / INPUT
            target.parent.mkdir(parents=True)
            target.write_bytes(raw + b' ')
            (directory / 'source.json').write_text(json.dumps(manifest))
            out = directory / 'out.h'
            out.write_text('keep')
            command = [sys.executable, str(GENERATOR), '--data-dir',
                       str(directory), '--output', str(out)]
            failed = subprocess.run(command, capture_output=True, text=True)
            self.assertIn('checksum', failed.stderr)
            self.assertEqual(out.read_text(), 'keep')
            malformed = raw + b'\n0640 ; C\n'
            target.write_bytes(malformed)
            next(row for row in manifest['inputs'] if row['file'] == INPUT)[
                'sha256'] = hashlib.sha256(malformed).hexdigest()
            (directory / 'source.json').write_text(json.dumps(manifest))
            failed = subprocess.run(command, capture_output=True, text=True)
            self.assertIn('overlap', failed.stderr)
            self.assertEqual(out.read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
