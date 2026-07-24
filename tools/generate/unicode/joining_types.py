#!/usr/bin/env python3
"""Generate Unicode joining-type intervals from the pinned UCD snapshot."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile


def parse(text):
    rows = []
    for number, line in enumerate(text.splitlines(), 1):
        value = line.split('#', 1)[0].strip()
        if not value:
            continue
        match = re.fullmatch(
            r'([0-9A-F]{4,6})(?:\.\.([0-9A-F]{4,6}))?\s*;\s*([CDLRT])',
            value)
        if match is None:
            raise ValueError(f'joining type line {number}: invalid entry')
        first = int(match[1], 16)
        last = int(match[2], 16) if match[2] else first
        if last < first or last > 0x10ffff:
            raise ValueError(f'joining type line {number}: invalid range')
        rows.append((first, last, match[3]))
    rows.sort()
    if len(rows) < 500:
        raise ValueError('joining type table incomplete')
    for previous, current in zip(rows, rows[1:]):
        if previous[1] >= current[0]:
            raise ValueError('joining type ranges overlap')
    return rows


def render(rows, version, checksum):
    output = [f'/* DO NOT EDIT: Unicode {version} DerivedJoiningType.txt.\n',
              f' * Input SHA-256: {checksum}\n',
              ' * See data/unicode/17.0.0/{source.json,LICENSE}. */\n',
              '#ifndef LIBLEXA_GENERATED_JOINING_TYPES_H\n',
              '#define LIBLEXA_GENERATED_JOINING_TYPES_H\n',
              '#include <stdint.h>\n',
              '/* Unlisted code points have joining type U (non-joining). */\n',
              'typedef struct { uint32_t first, last; char type; } liblexa_joining_range;\n',
              'static const liblexa_joining_range liblexa_joining_ranges[] = {\n']
    output.extend(f"    {{{first}, {last}, '{kind}'}},\n"
                  for first, last, kind in rows)
    output.extend(['};\n', f'#define LIBLEXA_JOINING_RANGE_COUNT {len(rows)}\n',
                   '#endif\n'])
    return ''.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = json.loads((args.data_dir / 'source.json').read_text())
        version = manifest['version']
        record = next((row for row in manifest['inputs']
                       if row['file'] == 'ucd/extracted/DerivedJoiningType.txt'), None)
        if record is None:
            raise ValueError('joining type input missing from provenance')
        checksum = record['sha256']
        if (not isinstance(version, str)
            or re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version) is None
            or not isinstance(checksum, str)
            or re.fullmatch(r'[0-9a-f]{64}', checksum) is None):
            raise ValueError('invalid joining type version or checksum')
        data = (args.data_dir / record['file']).read_bytes()
        if hashlib.sha256(data).hexdigest() != checksum:
            raise ValueError('joining type checksum does not match provenance')
        text = data.decode('utf-8')
        if f'DerivedJoiningType-{version}.txt' not in text:
            raise ValueError('joining type version does not match provenance')
        result = render(parse(text), version, checksum)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', dir=args.output.parent,
                                             prefix='.joining-types-', delete=False,
                                             encoding='ascii', newline='\n') as out:
                temporary = out.name
                out.write(result)
            os.replace(temporary, args.output)
        finally:
            if temporary is not None and os.path.exists(temporary):
                os.unlink(temporary)
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as exc:
        parser.exit(1, f'joining types: {exc}\n')


if __name__ == '__main__':
    main()
