#!/usr/bin/env python3
"""Generate SVG 2 element names from pinned W3C definitions XML."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import xml.etree.ElementTree as ET


def generate(manifest, folder):
    names = set()
    expected = {'definitions.xml', 'definitions-filters.xml',
                'definitions-compositing.xml', 'definitions-masking.xml'}
    files = [record for record in manifest['inputs'] if record['file'] in expected]
    if len(files) != 4 or {record['file'] for record in files} != expected:
        raise ValueError('expected four SVG 2 definition inputs')
    for record in files:
        path = folder / record['file']
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != record['sha256']:
            raise ValueError(f'SVG input checksum does not match provenance: {path.name}')
        root = ET.fromstring(data)
        for element in root.findall('.//{*}element'):
            name = element.get('name')
            if not isinstance(name, str) or re.fullmatch('[A-Za-z][A-Za-z0-9]*', name) is None:
                raise ValueError(f'invalid SVG element name: {name!r}')
            if name in names:
                raise ValueError(f'duplicate SVG element name: {name}')
            names.add(name)
    if len(names) < 50:
        raise ValueError('incomplete SVG element definitions')
    result = ['/* W3C SVG 2 element names; see data/w3c/svg2/{source.json,LICENSE.md}. */\n',
              '#ifndef LIBLEXA_GENERATED_SVG_ELEMENTS_H\n#define LIBLEXA_GENERATED_SVG_ELEMENTS_H\n',
              'static const char *const liblexa_svg_elements[] = {\n']
    result.extend(f'    "{name}",\n' for name in sorted(names))
    result.extend(['};\n', f'#define LIBLEXA_SVG_ELEMENT_COUNT {len(names)}\n#endif\n'])
    return ''.join(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = json.loads((args.data_dir / 'source.json').read_text())
        text = generate(manifest, args.data_dir)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', dir=args.output.parent,
                                             prefix='.svg-tags-', delete=False,
                                             encoding='ascii', newline='\n') as out:
                temp_name = out.name
                out.write(text)
            os.replace(temp_name, args.output)
        finally:
            if temp_name is not None and os.path.exists(temp_name):
                os.unlink(temp_name)
    except (OSError, ValueError, KeyError, TypeError, ET.ParseError) as exc:
        parser.exit(1, f'SVG elements: {exc}\n')


if __name__ == '__main__':
    main()
