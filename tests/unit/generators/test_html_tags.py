"""Check generated conforming element names and void flags against HTML."""

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
GENERATOR = ROOT / 'tools/generate/html/tags.py'
COMMITTED = ROOT / 'generated/html/html-tags.h'


class HtmlTagsTest(unittest.TestCase):
    def test_spec_element_index_deterministic_and_compilable(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'tags.h'
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(DATA),
                   '--output', str(output)]
            subprocess.run(cmd, check=True)
            first = output.read_text()
            subprocess.run(cmd, check=True)
            self.assertEqual(first, output.read_text())
            self.assertEqual(first, COMMITTED.read_text())
            manifest = json.loads((DATA / 'source.json').read_text())
            self.assertIn(manifest['html_revision'], first)
            self.assertIn(manifest['inputs'][0]['sha256'], first)
            source = (DATA / 'source.html').read_text()
            table = source.split('<caption>List of elements</caption>', 1)[1]
            table = table.split('<tbody>', 1)[1].split('</table>', 1)[0]
            names = set()
            for row in table.split('<tr>')[1:]:
                names.update(re.findall(r'<code[^>]*>([a-z][a-z0-9-]*)</code>',
                                        row.split('</th>', 1)[0]))
            definition = source.split('<dt><dfn>Void elements</dfn></dt>', 1)[1]
            void = set(re.findall(r'<code>([a-z]+)</code>', definition.split('</dd>', 1)[0]))
            emitted = re.findall(r'    \{"([a-z][a-z0-9-]*)", (\d+), ([01])\},',
                                 first.split('liblexa_html_parser_void_elements[] = {', 1)[0])
            self.assertEqual(emitted, [(name, str(len(name)), str(int(name in void)))
                                       for name in sorted(names)])
            self.assertEqual(len(emitted), 115)
            self.assertEqual(len(void), 13)
            comment = source.split('<!-- non-conforming elements treated as void by the parser', 1)[1]
            legacy = sorted(re.findall(r'[a-z]+', comment.split('are not listed here:', 1)[1]
                                       .split('-->', 1)[0]))
            parser_void = re.findall(r'    \{"([a-z]+)", (\d+), 1\},',
                                     first.split('liblexa_html_parser_void_elements[] = {\n', 1)[1]
                                     .split('};', 1)[0])
            self.assertEqual(parser_void, [(name, str(len(name))) for name in legacy])
            self.assertEqual(legacy, ['basefont', 'bgsound', 'frame', 'keygen', 'param'])
            section = source.split('<h3>Non-conforming features</h3>', 1)[1].split('</dl>', 1)[0]
            obsolete = sorted(set(re.findall(r'<code>([a-z]+)</code>', ''.join(
                re.findall(r'<dt[^>]*>(.*?)</dt>', section, re.S)))))
            generated = re.findall(r'    \{"([a-z]+)", (\d+), ([01])\},',
                                   first.split('liblexa_html_obsolete_elements[] = {\n', 1)[1]
                                   .split('};', 1)[0])
            self.assertEqual(generated, [(name, str(len(name)), str(int(name in legacy)))
                                         for name in obsolete])
            self.assertEqual(len(obsolete), 29)
            svg = source.split('<th> Tag name <th> Element name', 1)[1].split('</table>', 1)[0]
            svg = re.sub(r'<!--.*?-->', '', svg, flags=re.S)
            adjustments = re.findall(
                r'<tr>\s*<td>\s*<code data-x=\"\">([a-z]+)</code>\s*'
                r'<td>\s*<code data-x=\"\">([A-Za-z]+)</code>', svg)
            generated_svg = re.findall(r'    \{\"([a-z]+)\", \"([A-Za-z]+)\"\},',
                                      first.split('liblexa_svg_tag_names[] = {\n', 1)[1]
                                      .split('};', 1)[0])
            self.assertEqual(generated_svg, sorted(adjustments))
            self.assertIn(('foreignobject', 'foreignObject'), adjustments)
            svg_attrs = source.split(
                '<th> Attribute name on token <th> Attribute name on element', 1)[1]
            svg_attrs = svg_attrs.split('</table>', 1)[0]
            attributes = re.findall(
                r'<tr>\s*<td>\s*<code data-x=\"\">([a-z]+)</code>\s*'
                r'<td>\s*<code data-x=\"\">([A-Za-z]+)</code>', svg_attrs)
            emitted_attrs = re.findall(
                r'    \{\"([a-z]+)\", \"([A-Za-z]+)\"\},',
                first.split('liblexa_svg_attribute_names[] = {\n', 1)[1]
                .split('};', 1)[0])
            self.assertEqual(emitted_attrs, sorted(attributes))
            self.assertGreaterEqual(len(emitted_attrs), 50)
            self.assertIn(('viewbox', 'viewBox'), emitted_attrs)
            fragment = source.split('Set the state of the <span>HTML parser</span>', 1)[1]
            fragment = fragment.split('<dl class="switch">', 1)[1].split('</dl>', 1)[0]
            expected_states = {}
            for terms, action in re.findall(r'((?:<dt>.*?</dt>\s*)+)<dd>(.*?)</dd>',
                                            fragment, re.S):
                tag_names = re.findall(r'<dt><code>([a-z]+)</code></dt>', terms)
                if tag_names:
                    if 'RCDATA state' in action:
                        state = 1
                    elif 'RAWTEXT' in action:
                        state = 2
                    elif 'script data state' in action:
                        state = 3
                    elif 'PLAINTEXT state' in action:
                        state = 4
                    else:
                        self.fail('unrecognized fragment tokenizer rule')
                    expected_states.update({name: (state, int(name == 'noscript'))
                                            for name in tag_names})
            states = {name: (int(state), int(flag)) for name, state, flag in re.findall(
                r'    \{"([a-z]+)", (\d+), ([01])\},',
                first.split('liblexa_html_fragment_states[] = {\n', 1)[1])}
            self.assertEqual(states, expected_states)
            self.assertEqual(states['noscript'], (2, 1))
            c = Path(folder) / 'check.c'
            c.write_text('#include "tags.h"\nint main(void) {\n'
                         '  return LIBLEXA_HTML_ELEMENT_COUNT != 115 ||\n'
                         '    liblexa_html_elements[0].length == 0 ||\n'
                         '    LIBLEXA_HTML_PARSER_VOID_COUNT != 5 ||\n'
                         '    LIBLEXA_HTML_OBSOLETE_COUNT != 29 ||\n'
                         '    LIBLEXA_SVG_TAG_NAME_COUNT < 20 ||\n'
                         '    LIBLEXA_SVG_ATTRIBUTE_NAME_COUNT < 50 ||\n'
                         '    LIBLEXA_HTML_FRAGMENT_STATE_COUNT != 10;\n}\n')
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-pedantic', str(c), '-o', str(Path(folder) / 'check')], check=True)
            subprocess.run([str(Path(folder) / 'check')], check=True)

    def test_tampering_and_missing_index_preserve_output(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            manifest = json.loads((DATA / 'source.json').read_text())
            data = (DATA / 'source.html').read_bytes()
            (path / 'source.html').write_bytes(data + b' ')  # bad checksum
            (path / 'source.json').write_text(json.dumps(manifest))
            out = path / 'out.h'
            out.write_text('keep')
            cmd = [sys.executable, str(GENERATOR), '--data-dir', str(path),
                   '--output', str(out)]
            result = subprocess.run(cmd, text=True, capture_output=True)
            self.assertIn('checksum', result.stderr)
            broken = b'<html></html>'
            (path / 'source.html').write_bytes(broken)
            manifest['inputs'][0]['sha256'] = hashlib.sha256(broken).hexdigest()
            (path / 'source.json').write_text(json.dumps(manifest))
            result = subprocess.run(cmd, text=True, capture_output=True)
            self.assertIn('element index', result.stderr)
            self.assertEqual(out.read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
