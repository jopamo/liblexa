#!/usr/bin/env python3
"""Generate C lookup arrays from the pinned WHATWG Encoding indexes."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile

MISSING = 0xffffffff  # Zero is a valid code point in an index.


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate JSON key: {key}')
        result[key] = value
    return result


def render(indexes, revision, checksum):
    if not isinstance(indexes, dict) or not indexes:
        raise ValueError('expected a nonempty index map')
    text = [f'/* DO NOT EDIT: generated from WHATWG Encoding indexes.json.\n'
            f' * revision: {revision}; input SHA-256: {checksum}.\n'
            ' * See data/whatwg/encoding/source.json and LICENSE. */\n',
            '#ifndef LIBLEXA_GENERATED_ENCODING_INDEXES_H\n',
            '#define LIBLEXA_GENERATED_ENCODING_INDEXES_H\n',
            '#include <stdint.h>\n',
            'typedef struct { uint32_t pointer, codepoint; } liblexa_encoding_range;\n',
            'typedef struct { uint32_t codepoint, pointer; } liblexa_encoding_reverse;\n']
    names = set()
    for name in sorted(indexes):
        if not isinstance(name, str) or re.fullmatch('[a-z0-9]+(?:-[a-z0-9]+)*', name) is None:
            raise ValueError(f'invalid index name: {name!r}')
        symbol = name.replace('-', '_')
        if symbol in names:
            raise ValueError(f'index name collision: {name!r}')
        names.add(symbol)
        values = indexes[name]
        if not isinstance(values, list) or not values or len(values) > MISSING:
            raise ValueError(f'invalid index length: {name}')
        if name == 'gb18030-ranges':
            previous_pointer = -1
            for row in values:
                if (not isinstance(row, list) or len(row) != 2 or
                        any(type(n) is not int or n < 0 or n >= MISSING for n in row) or
                        row[0] <= previous_pointer or row[1] > 0x10ffff or
                        0xd800 <= row[1] <= 0xdfff):
                    raise ValueError('invalid GB18030 range')
                previous_pointer = row[0]
            text.append(f'static const liblexa_encoding_range liblexa_index_{symbol}[] = {{\n')
            text.extend(f'    {{{pointer}, {codepoint}}},\n' for pointer, codepoint in values)
        else:
            for cp in values:
                if (cp is not None and
                        (type(cp) is not int or cp < 0 or cp > 0x10ffff or
                         0xd800 <= cp <= 0xdfff)):
                    raise ValueError(f'invalid code point in {name}')
            text.append(f'static const uint32_t liblexa_index_{symbol}[] = {{\n')
            for offset in range(0, len(values), 12):
                text.append('    ' + ', '.join('UINT32_MAX' if cp is None else str(cp)
                                               for cp in values[offset:offset + 12]) + ',\n')
        text.append('};\n')
        text.append(f'#define LIBLEXA_INDEX_{symbol.upper()}_COUNT {len(values)}\n')
        if name in ('euc-kr', 'jis0208'):
            first_pointer = {}
            for pointer, cp in enumerate(values):
                if cp is not None:
                    first_pointer.setdefault(cp, pointer)
            text.append(f'static const liblexa_encoding_reverse liblexa_index_{symbol}_reverse[] = {{\n')
            text.extend(f'    {{{cp}, {first_pointer[cp]}}},\n'
                        for cp in sorted(first_pointer))
            text.append('};\n')
            text.append(f'#define LIBLEXA_INDEX_{symbol.upper()}_REVERSE_COUNT {len(first_pointer)}\n')
    text.append('#endif\n')
    return ''.join(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        manifest = json.loads((args.data_dir / 'source.json').read_text(),
                              object_pairs_hook=unique_pairs)
        record = next((i for i in manifest['inputs'] if i['file'] == 'indexes.json'), None)
        if record is None:
            raise ValueError('provenance has no indexes.json input')
        revision = manifest['revision']
        checksum = record['sha256']
        if (not isinstance(revision, str)
            or re.fullmatch(r'[0-9a-f]{40}', revision) is None
            or not isinstance(checksum, str)
            or re.fullmatch(r'[0-9a-f]{64}', checksum) is None):
            raise ValueError('invalid source revision or checksum')
        data = (args.data_dir / 'indexes.json').read_bytes()
        if hashlib.sha256(data).hexdigest() != checksum:
            raise ValueError('indexes.json checksum does not match provenance')
        result = render(json.loads(data, object_pairs_hook=unique_pairs),
                        revision, checksum)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', dir=args.output.parent,
                                             prefix='.indexes-', delete=False,
                                             encoding='ascii', newline='\n') as out:
                temp_name = out.name
                out.write(result)
            os.replace(temp_name, args.output)
        finally:
            if temp_name is not None and os.path.exists(temp_name):
                os.unlink(temp_name)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(1, f'encoding indexes: {exc}\n')


if __name__ == '__main__':
    main()
