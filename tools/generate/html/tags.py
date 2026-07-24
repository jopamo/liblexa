#!/usr/bin/env python3
"""Generate pinned HTML element names, parser categories and SVG adjustments."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile


def extract(source):
    try:
        start = source.index('<caption>List of elements</caption>')
        body = source.index('<tbody>', start)
        end = source.index('</table>', body)
        marker = '<dt><dfn>Void elements</dfn></dt>'
        void_start = source.index(marker)
        void_end = source.index('</dd>', void_start)
        legacy_start = source.index('<!-- non-conforming elements treated as void by the parser', void_end)
        legacy_end = source.index('-->', legacy_start)
        obsolete_start = source.index('<h3>Non-conforming features</h3>')
        obsolete_end = source.index('</dl>', obsolete_start)
        svg_start = source.index('<th> Tag name <th> Element name')
        svg_end = source.index('</table>', svg_start)
        svg_attr_start = source.index('<th> Attribute name on token <th> Attribute name on element')
        svg_attr_end = source.index('</table>', svg_attr_start)
        fragment_start = source.index('Set the state of the <span>HTML parser</span>')
        fragment_start = source.index('<dl class="switch">', fragment_start)
        fragment_end = source.index('</dl>', fragment_start)
    except ValueError as exc:
        raise ValueError('HTML specification element index or void definition missing') from exc
    code = re.compile(r'<code(?: [^>]*)?>([a-z][a-z0-9-]*)</code>')
    names = set()
    for row in source[body:end].split('<tr>')[1:]:
        first_cell = row.split('</th>', 1)[0]
        names.update(code.findall(first_cell))
    void = set(code.findall(source[void_start:void_end]))
    legacy = source[legacy_start:legacy_end].split('are not listed here:', 1)[1]
    parser_void = sorted(re.findall(r'[a-z]+', legacy))
    obsolete = sorted(set(re.findall(r'<code>([a-z]+)</code>', ''.join(
        re.findall(r'<dt[^>]*>(.*?)</dt>', source[obsolete_start:obsolete_end], re.S)))))
    svg_table = re.sub(r'<!--.*?-->', '', source[svg_start:svg_end], flags=re.S)
    svg = re.findall(r'<tr>\s*<td>\s*<code data-x=\"\">([a-z]+)</code>\s*'
                     r'<td>\s*<code data-x=\"\">([A-Za-z]+)</code>', svg_table)
    svg_attributes = re.findall(
        r'<tr>\s*<td>\s*<code data-x=\"\">([a-z]+)</code>\s*'
        r'<td>\s*<code data-x=\"\">([A-Za-z]+)</code>',
        source[svg_attr_start:svg_attr_end])
    fragment = source[fragment_start:fragment_end]
    modes = {'RCDATA state': 1, 'RAWTEXT state': 2, 'script data state': 3,
             'PLAINTEXT state': 4}
    fragment_states = {}
    for terms, rule in re.findall(r'((?:<dt>.*?</dt>\s*)+)<dd>(.*?)</dd>', fragment, re.S):
        names_in_rule = re.findall(r'<dt><code>([a-z]+)</code></dt>', terms)
        if not names_in_rule:
            continue  # The final "any other element" clause defaults to data state.
        states = [state for state in modes if f'<span>{state}</span>' in rule or
                  (state == 'RAWTEXT state' and '<span>RAWTEXT\n' in rule)]
        if len(states) != 1 or any(name in fragment_states for name in names_in_rule):
            raise ValueError('ambiguous HTML fragment tokenizer state')
        for name in names_in_rule:
            fragment_states[name] = (modes[states[0]], int(name == 'noscript'))
    if (len(names) < 100 or len(void) < 10 or not void <= names or
            len(parser_void) != len(set(parser_void)) or
            not parser_void or set(parser_void) & names or
            len(obsolete) < 20 or set(obsolete) & names or
            not set(parser_void) <= set(obsolete) or len(svg) < 20 or
            len({name for name, _ in svg}) != len(svg) or
            len(svg_attributes) < 50 or
            len({name for name, _ in svg_attributes}) != len(svg_attributes) or
            len(fragment_states) < 10 or 'noscript' not in fragment_states):
        raise ValueError('HTML specification element list incomplete')
    return (sorted(names), void, parser_void, obsolete, sorted(svg),
            sorted(svg_attributes), sorted(fragment_states.items()))


def render(names, void, parser_void, obsolete, svg, svg_attributes, fragment_states,
           revision, checksum):
    output = [f"/* DO NOT EDIT: generated from WHATWG HTML source.html.\n"
              f" * HTML revision: {revision}; input SHA-256: {checksum}.\n"
              " * See data/whatwg/html/source.json and LICENSE. */\n",
              '/* This list does not include legacy, custom, or parser-only tag names. */\n',
              '#ifndef LIBLEXA_GENERATED_HTML_TAGS_H\n#define LIBLEXA_GENERATED_HTML_TAGS_H\n',
              '#include <stdint.h>\n',
              'typedef struct { const char *name; uint8_t length, is_void; } liblexa_html_element;\n',
              'static const liblexa_html_element liblexa_html_elements[] = {\n']
    output.extend(f'    {{"{name}", {len(name)}, {int(name in void)}}},\n' for name in names)
    output.extend(['};\n', f'#define LIBLEXA_HTML_ELEMENT_COUNT {len(names)}\n',
                   '/* Non-conforming names with special parser void handling in HTML syntax. */\n',
                   'static const liblexa_html_element liblexa_html_parser_void_elements[] = {\n'])
    output.extend(f'    {{"{name}", {len(name)}, 1}},\n' for name in parser_void)
    output.extend(['};\n',
                   f'#define LIBLEXA_HTML_PARSER_VOID_COUNT {len(parser_void)}\n',
                   '/* Entirely obsolete element names; parser void behavior is recorded above. */\n',
                   'static const liblexa_html_element liblexa_html_obsolete_elements[] = {\n'])
    output.extend(f'    {{"{name}", {len(name)}, {int(name in parser_void)}}},\n'
                  for name in obsolete)
    output.extend(['};\n', f'#define LIBLEXA_HTML_OBSOLETE_COUNT {len(obsolete)}\n',
                   '/* SVG tag-name case adjustments in HTML foreign content. */\n',
                   'typedef struct { const char *lowercase, *adjusted; } liblexa_svg_tag_name;\n',
                   'static const liblexa_svg_tag_name liblexa_svg_tag_names[] = {\n'])
    output.extend(f'    {{"{before}", "{after}"}},\n' for before, after in svg)
    output.extend(['};\n', f'#define LIBLEXA_SVG_TAG_NAME_COUNT {len(svg)}\n',
                   '/* SVG attribute-name case adjustments in HTML foreign content. */\n',
                   'typedef liblexa_svg_tag_name liblexa_svg_attribute_name;\n',
                   'static const liblexa_svg_attribute_name liblexa_svg_attribute_names[] = {\n'])
    output.extend(f'    {{"{before}", "{after}"}},\n'
                  for before, after in svg_attributes)
    output.extend(['};\n',
                   f'#define LIBLEXA_SVG_ATTRIBUTE_NAME_COUNT {len(svg_attributes)}\n',
                   '/* Fragment tokenizer states: 1=RCDATA, 2=RAWTEXT, 3=script, 4=PLAINTEXT.\\n'
                   ' * noscript uses RAWTEXT only when scripting is enabled; otherwise data state. */\n',
                   'typedef struct { const char *tag; uint8_t state, requires_scripting; } liblexa_html_fragment_state;\n',
                   'static const liblexa_html_fragment_state liblexa_html_fragment_states[] = {\n'])
    output.extend(f'    {{"{name}", {state}, {flag}}},\n'
                  for name, (state, flag) in fragment_states)
    output.extend(['};\n',
                   f'#define LIBLEXA_HTML_FRAGMENT_STATE_COUNT {len(fragment_states)}\n#endif\n'])
    return ''.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = json.loads((args.data_dir / 'source.json').read_text())
        record = next((item for item in manifest['inputs'] if item['file'] == 'source.html'), None)
        if record is None:
            raise ValueError('HTML source missing from provenance')
        revision = manifest['html_revision']
        checksum = record['sha256']
        if (not isinstance(revision, str)
            or re.fullmatch(r'[0-9a-f]{40}', revision) is None
            or not isinstance(checksum, str)
            or re.fullmatch(r'[0-9a-f]{64}', checksum) is None):
            raise ValueError('invalid HTML revision or checksum')
        data = (args.data_dir / record['file']).read_bytes()
        if hashlib.sha256(data).hexdigest() != checksum:
            raise ValueError('HTML source checksum does not match provenance')
        text = render(*extract(data.decode('utf-8')), revision, checksum)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', dir=args.output.parent,
                                             prefix='.html-tags-', delete=False,
                                             encoding='ascii', newline='\n') as out:
                temp_name = out.name
                out.write(text)
            os.replace(temp_name, args.output)
        finally:
            if temp_name is not None and os.path.exists(temp_name):
                os.unlink(temp_name)
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as exc:
        parser.exit(1, f'html tags: {exc}\n')


if __name__ == '__main__':
    main()
