"""Check generated normalization fields against UnicodeData.txt."""

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
GENERATOR = ROOT / 'tools/generate/unicode/normalization.py'
COMMITTED = ROOT / 'generated/unicode/normalization-table.h'


class NormalizationTableTest(unittest.TestCase):
    def test_ucd_rows_deterministic_and_compilable(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'normalization.h'
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(DATA),
                   '--output', str(output)]
            subprocess.run(cmd, check=True)
            first = output.read_text()
            subprocess.run(cmd, check=True)
            self.assertEqual(first, output.read_text())
            self.assertEqual(first, COMMITTED.read_text())
            manifest = json.loads((DATA / 'source.json').read_text())
            self.assertIn(manifest['version'], first)
            for filename in ('ucd/UnicodeData.txt',
                             'ucd/DerivedNormalizationProps.txt',
                             'ucd/CompositionExclusions.txt'):
                self.assertIn(next(row['sha256'] for row in manifest['inputs']
                                   if row['file'] == filename), first)
            decomposition = [int(value) for value in re.findall(
                r'\d+', first.split('liblexa_decompositions[] = {\n', 1)[1].split('};', 1)[0])]
            rows = [tuple(map(int, values)) for values in re.findall(
                r'\{(\d+), (\d+), (\d+), (\d+), (\d+)\}',
                first.split('liblexa_normalization[] = {\n', 1)[1])]
            expected = []
            offset = 0
            for line in (DATA / 'ucd/UnicodeData.txt').read_text().splitlines():
                fields = line.split(';')
                cps = fields[5].split()
                compat = int(bool(cps and cps[0].startswith('<')))
                mapped = [int(value, 16) for value in cps[compat:]]
                ccc = int(fields[3])
                if ccc or mapped:
                    expected.append((int(fields[0], 16), offset, ccc, compat, len(mapped)))
                    self.assertEqual(decomposition[offset:offset + len(mapped)], mapped)
                    offset += len(mapped)
            self.assertEqual(rows, expected)
            self.assertEqual(offset, len(decomposition))
            quick = dict((int(cp), int(bits)) for cp, bits in re.findall(
                r'\{(\d+), (\d+)\}', first.split('liblexa_quick_checks[] = {\n', 1)[1]
                .split('};', 1)[0]))
            exclusions = set()
            expected_quick = {}
            shifts = {'NFC_QC': 0, 'NFD_QC': 2, 'NFKC_QC': 4, 'NFKD_QC': 6}
            for line in (DATA / 'ucd/DerivedNormalizationProps.txt').read_text().splitlines():
                parts = [p.strip() for p in line.split('#', 1)[0].split(';')]
                if len(parts) < 2 or not parts[0] or parts[0].startswith('@'):
                    continue
                bounds = [int(n, 16) for n in parts[0].split('..')]
                for cp in range(bounds[0], bounds[-1] + 1):
                    if parts[1] == 'Full_Composition_Exclusion':
                        exclusions.add(cp)
                    elif parts[1] in shifts:
                        expected_quick[cp] = expected_quick.get(cp, 0) | (
                            (1 if parts[2] == 'N' else 2) << shifts[parts[1]])
            self.assertEqual(quick, expected_quick)
            composition = [tuple(map(int, values)) for values in re.findall(
                r'\{(\d+), (\d+), (\d+)\}', first.split(
                    'liblexa_compositions[] = {\n', 1)[1])]
            expected_pairs = []
            for line in (DATA / 'ucd/UnicodeData.txt').read_text().splitlines():
                fields = line.split(';')
                mapping = fields[5].split()
                cp = int(fields[0], 16)
                if (len(mapping) == 2 and not mapping[0].startswith('<') and
                        fields[3] == '0' and cp not in exclusions):
                    expected_pairs.append((*[int(n, 16) for n in mapping], cp))
            self.assertEqual(composition, sorted(expected_pairs))
            c = Path(folder) / 'check.c'
            c.write_text('#include "normalization.h"\nint main(void) {\n'
                         '  return LIBLEXA_NORMALIZATION_COUNT == 0 ||\n'
                         '    liblexa_normalization[0].cp != 0x00a0 ||\n'
                         '    liblexa_decompositions[0] != 0x20;\n}\n')
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-pedantic', str(c), '-o', str(Path(folder) / 'check')], check=True)
            subprocess.run([str(Path(folder) / 'check')], check=True)

    def test_bad_data_does_not_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'ucd').mkdir()
            manifest = json.loads((DATA / 'source.json').read_text())
            bad = b'0041;bad;Lu;0;L;D800;;;;;;;;;\n'
            (path / 'ucd/UnicodeData.txt').write_bytes(bad)
            next(item for item in manifest['inputs'] if item['file'] ==
                 'ucd/UnicodeData.txt')['sha256'] = hashlib.sha256(bad).hexdigest()
            (path / 'source.json').write_text(json.dumps(manifest))
            out = path / 'out.h'
            out.write_text('original')
            result = subprocess.run([sys.executable, str(GENERATOR), '--data-dir',
                                     str(path), '--output', str(out)], text=True,
                                    capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('UnicodeData line 1', result.stderr)
            self.assertEqual(out.read_text(), 'original')


if __name__ == '__main__':
    unittest.main()
