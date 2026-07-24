"""Verify the independently generated encoding arrays against public data."""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / 'data/whatwg/encoding'
GENERATOR = ROOT / 'tools/generate/encoding/indexes.py'
COMMITTED = ROOT / 'generated/encoding/encoding-indexes.h'


class EncodingIndexesTest(unittest.TestCase):
    def test_all_indexes_deterministic_and_compilable(self):
        indexes = json.loads((DATA / 'indexes.json').read_text())
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'indexes.h'
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(DATA),
                   '--output', str(output)]
            subprocess.run(cmd, check=True)
            first = output.read_bytes()
            subprocess.run(cmd, check=True)
            self.assertEqual(first, output.read_bytes())
            self.assertEqual(first, COMMITTED.read_bytes())
            self.assertIn(b'DO NOT EDIT:', first)
            manifest = json.loads((DATA / 'source.json').read_text())
            self.assertIn(manifest['revision'].encode(), first)
            self.assertIn(manifest['inputs'][0]['sha256'].encode(), first)
            for name, values in indexes.items():
                symbol = name.replace('-', '_')
                self.assertIn(f'#define LIBLEXA_INDEX_{symbol.upper()}_COUNT {len(values)}'.encode(), first)
                kind = 'liblexa_encoding_range' if name == 'gb18030-ranges' else 'uint32_t'
                match = re.search(
                    rf'static const {kind} liblexa_index_{symbol}\[\] = \{{\n(.*?)\n\}};',
                    first.decode('ascii'), re.S)
                self.assertIsNotNone(match, name)
                if name == 'gb18030-ranges':
                    actual = [[int(a), int(b)] for a, b in
                              re.findall(r'\{(\d+), (\d+)\}', match.group(1))]
                else:
                    actual = [None if token == 'UINT32_MAX' else int(token)
                              for token in re.findall(r'UINT32_MAX|\d+', match.group(1))]
                self.assertEqual(actual, values, name)
            for name in ('euc-kr', 'jis0208'):
                reverse = re.search(
                    rf'static const liblexa_encoding_reverse liblexa_index_{name.replace("-", "_")}_reverse\[\] = \{{\n(.*?)\n\}};',
                    first.decode('ascii'), re.S)
                self.assertIsNotNone(reverse, name)
                actual = [(int(cp), int(pointer)) for cp, pointer in
                          re.findall(r'\{(\d+), (\d+)\}', reverse.group(1))]
                first_pointer = {}
                for pointer, cp in enumerate(indexes[name]):
                    if cp is not None:
                        first_pointer.setdefault(cp, pointer)
                self.assertEqual(actual, sorted(first_pointer.items()), name)
                if name == 'jis0208':
                    self.assertTrue(all(pointer < 8836 for pointer in
                                        first_pointer.values()))
            c = Path(folder) / 'check.c'
            c.write_text('#include "indexes.h"\n'
                         'int main(void) {\n'
                         '  return LIBLEXA_INDEX_BIG5_COUNT != 19782 ||\n'
                         '    liblexa_index_big5[0] != UINT32_MAX ||\n'
                         '    liblexa_index_gb18030_ranges[0].pointer != 0 ||\n'
                         '    liblexa_index_windows_1252[0] != 8364;\n}\n')
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-const-variable', '-pedantic', str(c),
                            '-o', str(Path(folder) / 'check')], check=True)
            subprocess.run([str(Path(folder) / 'check')], check=True)

    def test_rejects_bad_checksum_and_bad_ranges_without_overwriting(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            out = path / 'out.h'
            out.write_text('original')
            manifest = json.loads((DATA / 'source.json').read_text())
            (path / 'source.json').write_text(json.dumps(manifest))
            (path / 'indexes.json').write_text('{}')
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(path),
                   '--output', str(out)]
            result = subprocess.run(cmd, text=True, capture_output=True)
            self.assertIn('checksum', result.stderr)
            self.assertEqual(out.read_text(), 'original')
            from hashlib import sha256
            bad = b'{"gb18030-ranges": [[0, 128], [0, 129]]}'
            (path / 'indexes.json').write_bytes(bad)
            manifest['inputs'][0]['sha256'] = sha256(bad).hexdigest()
            (path / 'source.json').write_text(json.dumps(manifest))
            result = subprocess.run(cmd, text=True, capture_output=True)
            self.assertIn('invalid GB18030 range', result.stderr)
            self.assertEqual(out.read_text(), 'original')


if __name__ == '__main__':
    unittest.main()
