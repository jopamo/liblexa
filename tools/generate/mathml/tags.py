#!/usr/bin/env python3
"""Generate MathML Core element names from the pinned W3C specification."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile


def generate(source):
    names = re.findall(r'<dfn class="element">(.*?)</dfn>', source, re.S)
    if (len(names) < 30 or len(names) != len(set(names)) or
            any(re.fullmatch('[a-z][a-z0-9-]*', name) is None for name in names)):
        raise ValueError('MathML Core element definitions missing or invalid')
    result = ['/* W3C MathML Core element names; see data/w3c/mathml-core/{source.json,document-license.html}. */\n',
              '#ifndef LIBLEXA_GENERATED_MATHML_ELEMENTS_H\n',
              '#define LIBLEXA_GENERATED_MATHML_ELEMENTS_H\n',
              'static const char *const liblexa_mathml_elements[] = {\n']
    result.extend(f'    "{name}",\n' for name in sorted(names))
    result.extend(['};\n', f'#define LIBLEXA_MATHML_ELEMENT_COUNT {len(names)}\n',
                   '#endif\n'])
    return ''.join(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = json.loads((args.data_dir / 'source.json').read_text())
        record = next((item for item in manifest['inputs'] if item['file'] == 'spec.html'), None)
        if record is None:
            raise ValueError('spec.html missing from provenance')
        data = (args.data_dir / 'spec.html').read_bytes()
        if hashlib.sha256(data).hexdigest() != record['sha256']:
            raise ValueError('spec.html checksum does not match provenance')
        text = generate(data.decode('utf-8'))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', dir=args.output.parent,
                                             prefix='.mathml-tags-', delete=False,
                                             encoding='ascii', newline='\n') as out:
                temp_name = out.name
                out.write(text)
            os.replace(temp_name, args.output)
        finally:
            if temp_name is not None and os.path.exists(temp_name):
                os.unlink(temp_name)
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as exc:
        parser.exit(1, f'MathML elements: {exc}\n')


if __name__ == '__main__':
    main()
