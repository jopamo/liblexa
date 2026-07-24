#!/usr/bin/env python3
"""Generate an immutable C lookup table from the pinned WHATWG entity list."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile


def pairs_unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def source_data(path, manifest):
    record = next((item for item in manifest['inputs']
                   if item['file'] == 'entities.json'), None)
    if record is None:
        raise ValueError('provenance has no entities.json input')
    content = path.read_bytes()
    if hashlib.sha256(content).hexdigest() != record['sha256']:
        raise ValueError('entities.json checksum does not match provenance')
    return json.loads(content, object_pairs_hook=pairs_unique)


def generate(entities, revision, checksum):
    if not isinstance(entities, dict) or not entities:
        raise ValueError('expected a nonempty entity map')
    rows = []
    for name, item in entities.items():
        if (not isinstance(name, str) or not name.startswith('&') or
                not name[1:] or not name[1:].isascii() or
                len(name) > 255 or not name[1:].rstrip(';').isalnum() or
                not all(c.isalnum() for c in name[1:].rstrip(';')) or
                name.count(';') > 1 or
                not isinstance(item, dict) or
                set(item) != {'codepoints', 'characters'}):
            raise ValueError(f'invalid entity: {name!r}')
        cps = item['codepoints']
        if (not isinstance(cps, list) or len(cps) not in (1, 2) or
                any(type(cp) is not int or cp < 0 or cp > 0x10ffff or
                    0xd800 <= cp <= 0xdfff for cp in cps) or
                ''.join(chr(cp) for cp in cps) != item['characters']):
            raise ValueError(f'invalid codepoints: {name!r}')
        rows.append(f'    {{"{name}", {len(name)}, {{{cps[0]}, '
                    f'{cps[1] if len(cps) == 2 else 0}}}, {len(cps)}}},\n')
    return (f"/* DO NOT EDIT: generated from WHATWG HTML entities.json.\n"
            f" * html-build revision: {revision}; input SHA-256: {checksum}.\n"
            " * See data/whatwg/html/source.json and LICENSE. */\n"
            "#ifndef LIBLEXA_GENERATED_HTML_ENTITIES_H\n"
            "#define LIBLEXA_GENERATED_HTML_ENTITIES_H\n"
            "#include <stdint.h>\n"
            "typedef struct {\n"
            "    const char *name;\n"
            "    uint8_t name_length;\n"
            "    uint32_t codepoints[2];\n"
            "    uint8_t codepoint_count;\n"
            "} liblexa_html_entity_entry;\n"
            "static const liblexa_html_entity_entry liblexa_html_entities[] = {\n"
            + ''.join(sorted(rows)) +
            "};\n"
            "#define LIBLEXA_HTML_ENTITY_COUNT " + str(len(rows)) + "\n"
            "#endif\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = json.loads((args.data_dir / 'source.json').read_text(),
                              object_pairs_hook=pairs_unique)
        revision = manifest['html_build_revision']
        record = next(item for item in manifest['inputs']
                      if item['file'] == 'entities.json')
        checksum = record['sha256']
        if (not isinstance(revision, str)
            or re.fullmatch(r'[0-9a-f]{40}', revision) is None
            or not isinstance(checksum, str)
            or re.fullmatch(r'[0-9a-f]{64}', checksum) is None):
            raise ValueError('invalid source revision or checksum')
        text = generate(source_data(args.data_dir / 'entities.json', manifest),
                        revision, checksum)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        # Failure must not leave a partially written header for a subsequent build.
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', dir=args.output.parent,
                                             prefix='.entities-', delete=False,
                                             encoding='ascii', newline='\n') as temp:
                temp_name = temp.name
                temp.write(text)
            os.replace(temp_name, args.output)
        finally:
            if temp_name is not None and os.path.exists(temp_name):
                os.unlink(temp_name)
    except (OSError, ValueError, KeyError, TypeError, StopIteration) as exc:
        parser.exit(1, f'html entities: {exc}\n')


if __name__ == '__main__':
    main()
