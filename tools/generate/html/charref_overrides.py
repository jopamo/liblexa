#!/usr/bin/env python3
"""Generate numeric character-reference overrides from pinned WHATWG HTML."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile


def extract(source):
    marker = '<table id="table-charref-overrides">'
    if source.count(marker) != 1:
        raise ValueError('numeric reference override table missing or ambiguous')
    table = source.split(marker, 1)[1].split('</table>', 1)[0]
    table = re.sub(r'<!--.*?-->', '', table, flags=re.S)
    entries = [(int(a, 16), int(b, 16)) for a, b in
               re.findall(r'<tr><td>0x([0-9A-F]+) <td>0x([0-9A-F]+)', table)]
    if (len(entries) != 27 or len({before for before, _ in entries}) != 27
        or entries != sorted(entries) or
        any(before < 0x80 or before > 0x9f or after > 0x10ffff
            for before, after in entries)):
        raise ValueError('numeric reference override table incomplete')
    return entries


def render(entries, revision, checksum):
    output = [f'/* DO NOT EDIT: generated from WHATWG HTML source.html.\n'
              f' * Revision: {revision}\n'
              f' * SHA-256: {checksum}\n'
              ' * See data/whatwg/html/{source.json,LICENSE}. */\n',
              '#ifndef LIBLEXA_GENERATED_HTML_CHARREF_OVERRIDES_H\n',
              '#define LIBLEXA_GENERATED_HTML_CHARREF_OVERRIDES_H\n',
              '#include <stdint.h>\n',
              'typedef struct { uint8_t input; uint32_t output; } liblexa_html_charref_override;\n',
              'static const liblexa_html_charref_override liblexa_html_charref_overrides[] = {\n']
    output.extend(f'    {{{before}, {after}}},\n' for before, after in entries)
    output.extend(['};\n', f'#define LIBLEXA_HTML_CHARREF_OVERRIDE_COUNT {len(entries)}\n',
                   '#endif\n'])
    return ''.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = json.loads((args.data_dir / 'source.json').read_text())
        record = next((row for row in manifest['inputs']
                       if row['file'] == 'source.html'), None)
        if record is None:
            raise ValueError('HTML source missing from provenance')
        revision = manifest['html_revision']
        checksum = record['sha256']
        if (not isinstance(revision, str)
            or re.fullmatch(r'[0-9a-f]{40}', revision) is None
            or not isinstance(checksum, str)
            or re.fullmatch(r'[0-9a-f]{64}', checksum) is None):
            raise ValueError('invalid HTML revision or checksum')
        raw = (args.data_dir / record['file']).read_bytes()
        if hashlib.sha256(raw).hexdigest() != checksum:
            raise ValueError('HTML source checksum does not match provenance')
        text = render(extract(raw.decode('utf-8')), revision, checksum)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', dir=args.output.parent,
                                             prefix='.html-charref-', delete=False,
                                             encoding='ascii', newline='\n') as out:
                temporary = out.name
                out.write(text)
            os.replace(temporary, args.output)
        finally:
            if temporary is not None and os.path.exists(temporary):
                os.unlink(temporary)
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as exc:
        parser.exit(1, f'HTML charref overrides: {exc}\n')


if __name__ == '__main__':
    main()
