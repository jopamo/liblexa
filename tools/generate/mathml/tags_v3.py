#!/usr/bin/env python3
"""Generate MathML 3 legacy presentation element names from pinned W3C headings."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile

SECTIONS = ('presm.mglyph', 'presm.mfenced', 'presm.malign')


def generate(source):
    names = set()
    for section in SECTIONS:
        marker = f'id="{section}"'
        if source.count(marker) != 1:
            raise ValueError(f'MathML 3 section missing or ambiguous: {section}')
        heading = source.split(marker, 1)[1].split('</h', 1)[0]
        found = re.findall(r'<code>&lt;([a-z]+)\s*/?&gt;</code>', heading)
        if not found:
            raise ValueError(f'MathML 3 section has no element names: {section}')
        names.update(found)
    result = ['/* MathML 3 presentation elements; see data/w3c/mathml3/{source.json,document-license.html}. */\n',
              '#ifndef LIBLEXA_GENERATED_MATHML3_ELEMENTS_H\n',
              '#define LIBLEXA_GENERATED_MATHML3_ELEMENTS_H\n',
              'static const char *const liblexa_mathml3_elements[] = {\n']
    result.extend(f'    "{name}",\n' for name in sorted(names))
    result.extend(['};\n', f'#define LIBLEXA_MATHML3_ELEMENT_COUNT {len(names)}\n',
                   '#endif\n'])
    return ''.join(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = json.loads((args.data_dir / 'source.json').read_text())
        record = next((item for item in manifest['inputs'] if item['file'] == 'chapter3.html'), None)
        if record is None:
            raise ValueError('chapter3.html missing from provenance')
        data = (args.data_dir / 'chapter3.html').read_bytes()
        if hashlib.sha256(data).hexdigest() != record['sha256']:
            raise ValueError('chapter3.html checksum does not match provenance')
        text = generate(data.decode('utf-8'))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', dir=args.output.parent,
                                             prefix='.mathml3-tags-', delete=False,
                                             encoding='ascii', newline='\n') as out:
                temp_name = out.name
                out.write(text)
            os.replace(temp_name, args.output)
        finally:
            if temp_name is not None and os.path.exists(temp_name):
                os.unlink(temp_name)
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as exc:
        parser.exit(1, f'MathML 3 elements: {exc}\n')


if __name__ == '__main__':
    main()
