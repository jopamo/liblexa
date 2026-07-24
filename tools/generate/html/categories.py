#!/usr/bin/env python3
"""Generate namespace-aware HTML tree-construction categories from WHATWG HTML."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile


def extract(source):
    def definition(name):
        marker = f'<dt><dfn>{name}</dfn></dt>'
        if source.count(marker) != 1:
            raise ValueError(f'HTML category missing or ambiguous: {name}')
        block = source.split(marker, 1)[1].split('</dd>', 1)[0]
        return re.sub(r'<!--.*?-->', '', block, flags=re.S)

    special = definition('Special').split('<p class="note">', 1)[0]
    html = re.findall(r'<code>([a-z0-9]+)</code>', special.split("HTML's", 1)[1]
                      .split('<span>MathML', 1)[0])
    foreign = re.findall(r'<span>(MathML|SVG)\s*<code>([a-zA-Z-]+)</code></span>', special)
    formatting = re.findall(r'<code>([a-z]+)</code>', definition('Formatting'))
    if (len(html) < 70 or len(foreign) < 8 or len(formatting) < 10 or
            len(set(html)) != len(html) or len(set(foreign)) != len(foreign) or
            len(set(formatting)) != len(formatting)):
        raise ValueError('HTML category lists incomplete')
    return sorted([('html', name) for name in html] +
                  [(namespace.lower(), name) for namespace, name in foreign]), sorted(formatting)


def scope_sets(source):
    def scope(marker):
        start = source.index(marker)
        body = source[start:].split('<ul class="brief">', 1)[1].split('</ul>', 1)[0]
        body = re.sub(r'<!--.*?-->', '', body, flags=re.S)
        html = [('html', name) for name in re.findall(r'<li><code>([a-z]+)</code>', body)]
        foreign = [(ns.lower(), name) for ns, name in
                   re.findall(r'<li><span>(MathML|SVG) <code>([a-zA-Z-]+)</code></span>', body)]
        return set(html + foreign)

    general = scope('particular element in scope</dfn>')
    list_item = general | scope('particular element in list item scope</dfn>')
    button = general | scope('particular element in button scope</dfn>')
    table = scope('particular element in table scope</dfn>')
    if (len(general) < 15 or len(list_item - general) != 2 or
            len(button - general) != 1 or len(table) != 3):
        raise ValueError('HTML scope boundaries incomplete')
    all_tags = sorted(general | list_item | button | table)
    return [(namespace, name, int(tag in general) + 2 * int(tag in list_item) +
             4 * int(tag in button) + 8 * int(tag in table))
            for namespace, name in all_tags for tag in [(namespace, name)]]


def integration_points(source):
    def entries(name):
        marker = f'<dfn>{name}</dfn>'
        if source.count(marker) != 1:
            raise ValueError(f'HTML integration point missing or ambiguous: {name}')
        body = source.split(marker, 1)[1].split('<ul class="brief">', 1)[1].split('</ul>', 1)[0]
        return re.findall(r'<span>(MathML|SVG)\s*<code>([a-zA-Z-]+)</code></span>', body)

    math = entries('MathML text integration point')
    html = entries('HTML integration point')
    if (len(math) != 5 or len(html) != 5 or
            len(set(html)) != 4 or
            html.count(('MathML', 'annotation-xml')) != 2):
        raise ValueError('HTML integration point lists incomplete')
    # The annotation-xml HTML integration point also requires an encoding
    # attribute equal (ASCII case-insensitively) to text/html or
    # application/xhtml+xml. The generated flag preserves that condition.
    return sorted((ns.lower(), tag, 1, 0) for ns, tag in math) + sorted(
        (ns.lower(), tag, 2, int(tag == 'annotation-xml')) for ns, tag in set(html))


def render(special, formatting, scopes, integrations, revision, checksum):
    result = [f'/* DO NOT EDIT: generated from WHATWG HTML source.html.\n'
              f' * Revision: {revision}\n'
              f' * SHA-256: {checksum}\n'
              ' * See data/whatwg/html/{source.json,LICENSE}. */\n',
              '#ifndef LIBLEXA_GENERATED_HTML_CATEGORIES_H\n',
              '#define LIBLEXA_GENERATED_HTML_CATEGORIES_H\n',
              'typedef struct { const char *namespace_name, *tag; } liblexa_html_special_tag;\n',
              'static const liblexa_html_special_tag liblexa_html_special_tags[] = {\n']
    result.extend(f'    {{"{namespace}", "{name}"}},\n' for namespace, name in special)
    result.extend(['};\n', f'#define LIBLEXA_HTML_SPECIAL_COUNT {len(special)}\n',
                   'static const char *const liblexa_html_formatting_tags[] = {\n'])
    result.extend(f'    "{name}",\n' for name in formatting)
    result.extend(['};\n', f'#define LIBLEXA_HTML_FORMATTING_COUNT {len(formatting)}\n',
                   '/* Scope mask: general=1, list-item=2, button=4, table=8. */\n',
                   'typedef struct { const char *namespace_name, *tag; unsigned mask; } liblexa_html_scope_boundary;\n',
                   'static const liblexa_html_scope_boundary liblexa_html_scope_boundaries[] = {\n'])
    result.extend(f'    {{"{ns}", "{name}", {mask}}},\n' for ns, name, mask in scopes)
    result.extend(['};\n', f'#define LIBLEXA_HTML_SCOPE_BOUNDARY_COUNT {len(scopes)}\n',
                   '/* kind: MathML text=1, HTML=2. HTML annotation-xml requires encoding\\n'
                   ' * equal, ASCII case-insensitively, to text/html or application/xhtml+xml. */\n',
                   'typedef struct { const char *namespace_name, *tag; unsigned kind, check_encoding; } liblexa_html_integration_point;\n',
                   'static const liblexa_html_integration_point liblexa_html_integration_points[] = {\n'])
    result.extend(f'    {{"{ns}", "{tag}", {kind}, {flag}}},\n'
                  for ns, tag, kind, flag in integrations)
    result.extend(['};\n', f'#define LIBLEXA_HTML_INTEGRATION_COUNT {len(integrations)}\n#endif\n'])
    return ''.join(result)


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
        data = (args.data_dir / 'source.html').read_bytes()
        if hashlib.sha256(data).hexdigest() != checksum:
            raise ValueError('HTML source checksum does not match provenance')
        source = data.decode('utf-8')
        text = render(*extract(source), scope_sets(source),
                      integration_points(source), revision, checksum)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', dir=args.output.parent,
                                             prefix='.html-categories-', delete=False,
                                             encoding='ascii', newline='\n') as out:
                temp_name = out.name
                out.write(text)
            os.replace(temp_name, args.output)
        finally:
            if temp_name is not None and os.path.exists(temp_name):
                os.unlink(temp_name)
    except (OSError, ValueError, KeyError, TypeError, UnicodeError, IndexError) as exc:
        parser.exit(1, f'HTML categories: {exc}\n')


if __name__ == '__main__':
    main()
