"""Check WHATWG HTML special/formatting sets, namespaces, and output."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / 'data/whatwg/html'
GENERATOR = ROOT / 'tools/generate/html/categories.py'
COMMITTED = ROOT / 'generated/html/html-categories.h'


class HtmlCategoriesTest(unittest.TestCase):
    def test_complete_namespace_aware_categories(self):
        source = (DATA / 'source.html').read_text()
        special = source.split('<dt><dfn>Special</dfn></dt>', 1)[1].split('</dd>', 1)[0]
        special = re.sub(r'<!--.*?-->', '', special, flags=re.S)
        html = re.findall(r'<code>([a-z0-9]+)</code>', special.split("HTML's", 1)[1]
                          .split('<span>MathML', 1)[0])
        foreign = re.findall(r'<span>(MathML|SVG)\s*<code>([a-zA-Z-]+)</code></span>', special)
        expected = sorted([('html', name) for name in html] +
                          [(ns.lower(), name) for ns, name in foreign])
        formatting = source.split('<dt><dfn>Formatting</dfn></dt>', 1)[1].split('</dd>', 1)[0]
        expected_formatting = sorted(re.findall(r'<code>([a-z]+)</code>', formatting))
        with tempfile.TemporaryDirectory() as folder:
            header = Path(folder) / 'categories.h'
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(DATA),
                   '--output', str(header)]
            subprocess.run(cmd, check=True)
            first = header.read_text()
            subprocess.run(cmd, check=True)
            self.assertEqual(first, header.read_text())
            self.assertEqual(first, COMMITTED.read_text())
            manifest = json.loads((DATA / 'source.json').read_text())
            self.assertIn(manifest['html_revision'], first)
            self.assertIn(manifest['inputs'][0]['sha256'], first)
            generated = re.findall(r'    \{"([a-z]+)", "([a-zA-Z0-9-]+)"\},', first)
            self.assertEqual(generated, expected)
            self.assertEqual(re.findall(r'    "([a-z]+)",', first), expected_formatting)
            self.assertIn(('svg', 'foreignObject'), generated)
            self.assertEqual(len(expected), 92)
            def boundary_set(marker):
                fragment = source.split(marker, 1)[1].split('<ul class="brief">', 1)[1]
                fragment = fragment.split('</ul>', 1)[0]
                fragment = re.sub(r'<!--.*?-->', '', fragment, flags=re.S)
                entries = set()
                for li in re.findall(r'<li>(.*?)</li>', fragment, re.S):
                    code = re.search(r'<code>([a-zA-Z-]+)</code>', li)
                    if code is not None:
                        ns = 'mathml' if 'MathML <code>' in li else (
                            'svg' if 'SVG <code>' in li else 'html')
                        entries.add((ns, code.group(1)))
                return entries
            general = boundary_set('particular element in scope</dfn>')
            listing = general | boundary_set('particular element in list item scope</dfn>')
            button = general | boundary_set('particular element in button scope</dfn>')
            table = boundary_set('particular element in table scope</dfn>')
            actual = {(ns, name): int(mask) for ns, name, mask in re.findall(
                r'    \{"([a-z]+)", "([a-zA-Z-]+)", (\d+)\},', first)}
            self.assertEqual(actual, {
                pair: int(pair in general) + 2 * int(pair in listing) +
                4 * int(pair in button) + 8 * int(pair in table)
                for pair in general | listing | button | table})
            self.assertEqual(actual[('svg', 'foreignObject')], 7)
            integration = {}
            for label, kind in [('MathML text integration point', 1),
                                ('HTML integration point', 2)]:
                fragment = source.split(f'<dfn>{label}</dfn>', 1)[1]
                fragment = fragment.split('<ul class="brief">', 1)[1].split('</ul>', 1)[0]
                for li in re.findall(r'<li>(.*?)</li>', fragment, re.S):
                    match = re.search(r'<span>(MathML|SVG)\s*<code>([a-zA-Z-]+)</code></span>', li)
                    if match:
                        namespace, tag = match.groups()
                        integration[(namespace.lower(), tag, kind)] = (
                            int(tag == 'annotation-xml' and kind == 2))
                        if tag == 'annotation-xml':
                            self.assertIn('encoding', li)
                if kind == 2:
                    self.assertIn('text/html', fragment)
                    self.assertIn('application/xhtml+xml', fragment)
            generated_integrations = {
                (ns, tag, int(kind)): int(flag) for ns, tag, kind, flag in re.findall(
                    r'    \{"([a-z]+)", "([a-zA-Z-]+)", (\d+), ([01])\},',
                    first.split('liblexa_html_integration_points[] = {\n', 1)[1])}
            self.assertEqual(generated_integrations, integration)
            self.assertEqual(len(integration), 9)
            c = Path(folder) / 'check.c'
            c.write_text('#include "categories.h"\nint main(void) {\n'
                         '  return LIBLEXA_HTML_SPECIAL_COUNT != 92 ||\n'
                         '    LIBLEXA_HTML_FORMATTING_COUNT != 14 ||\n'
                         '    liblexa_html_special_tags[0].tag[0] == 0 ||\n'
                         '    liblexa_html_formatting_tags[0][0] == 0 ||\n'
                         '    LIBLEXA_HTML_SCOPE_BOUNDARY_COUNT < 20 ||\n'
                         '    LIBLEXA_HTML_INTEGRATION_COUNT != 9;\n}\n')
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-pedantic', str(c), '-o', str(Path(folder) / 'check')], check=True)
            subprocess.run([str(Path(folder) / 'check')], check=True)

    def test_missing_category_preserves_output(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            bad = (DATA / 'source.html').read_bytes().replace(
                b'<dt><dfn>Special</dfn></dt>', b'<dt><dfn>Removed</dfn></dt>', 1)
            (path / 'source.html').write_bytes(bad)
            manifest = json.loads((DATA / 'source.json').read_text())
            next(item for item in manifest['inputs'] if item['file'] ==
                 'source.html')['sha256'] = hashlib.sha256(bad).hexdigest()
            (path / 'source.json').write_text(json.dumps(manifest))
            out = path / 'out.h'
            out.write_text('keep')
            result = subprocess.run([sys.executable, str(GENERATOR), '--data-dir',
                                     str(path), '--output', str(out)], text=True,
                                    capture_output=True)
            self.assertIn('category missing', result.stderr)
            self.assertEqual(out.read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
