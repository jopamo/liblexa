#!/usr/bin/env python3
"""Generate Unicode UTS #46 IDNA mapping intervals from the pinned data."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile

STATUSES = ('valid', 'ignored', 'mapped', 'deviation', 'disallowed')


def parse_table(text):
    entries = []
    mappings = []
    previous = -1
    for line_number, line in enumerate(text.splitlines(), 1):
        raw = line.split('#', 1)[0].strip()
        if not raw:
            continue
        parts = [part.strip() for part in raw.split(';')]
        try:
            bounds = [int(p, 16) for p in parts[0].split('..')]
            if (len(bounds) not in (1, 2) or len(parts) < 2 or len(parts) > 4 or
                    parts[1] not in STATUSES or
                    (len(parts) == 4 and parts[3] not in ('NV8', 'XV8'))):
                raise ValueError('invalid fields')
            lo, hi = bounds[0], bounds[-1]
            if lo != previous + 1 or hi < lo or hi > 0x10ffff:
                raise ValueError('ranges must partition Unicode scalar values')
            mapped = [int(cp, 16) for cp in parts[2].split()] if len(parts) >= 3 else []
            if any(cp > 0x10ffff or 0xd800 <= cp <= 0xdfff for cp in mapped):
                raise ValueError('invalid mapped code point')
            if bool(mapped) != (parts[1] in ('mapped', 'deviation')) and not (
                    parts[1] == 'deviation' and not mapped):
                raise ValueError('unexpected mapping for status')
            if len(mapped) > 255 or len(mappings) + len(mapped) > 0xffffffff:
                raise ValueError('mapping storage exceeds bounds')
        except ValueError as exc:
            raise ValueError(f'line {line_number}: {exc}') from exc
        entries.append((lo, hi, STATUSES.index(parts[1]), len(mappings),
                        len(mapped), {'NV8': 1, 'XV8': 2}.get(parts[3], 0)
                        if len(parts) == 4 else 0))
        mappings.extend(mapped)
        previous = hi
    if previous != 0x10ffff:
        raise ValueError('IDNA table does not cover all Unicode code points')
    return entries, mappings


def render(entries, mappings, version, checksum):
    output = [f"/* DO NOT EDIT: generated from Unicode {version} UTS #46.\n"
              f" * Input SHA-256: {checksum}\n"
              " * See data/unicode/17.0.0/{source.json,LICENSE}. */\n",
              '#ifndef LIBLEXA_GENERATED_IDNA_H\n#define LIBLEXA_GENERATED_IDNA_H\n',
              '#include <stdint.h>\n',
              '/* Status values follow: valid, ignored, mapped, deviation, disallowed. */\n',
              '/* IDNA2008 column: 0 = default, 1 = NV8, 2 = XV8. */\n',
              'typedef struct { uint32_t first, last, offset; uint8_t status, count, idna2008; } liblexa_idna_range;\n',
              'static const uint32_t liblexa_idna_mappings[] = {\n']
    for start in range(0, len(mappings), 12):
        output.append('    ' + ', '.join(str(cp) for cp in mappings[start:start + 12]) + ',\n')
    output.append('};\nstatic const liblexa_idna_range liblexa_idna_ranges[] = {\n')
    output.extend(f'    {{{lo}, {hi}, {offset}, {status}, {count}, {idna2008}}},\n'
                  for lo, hi, status, offset, count, idna2008 in entries)
    output.append('};\n')
    output.append(f'#define LIBLEXA_IDNA_RANGE_COUNT {len(entries)}\n')
    output.append(f'#define LIBLEXA_IDNA_MAPPING_COUNT {len(mappings)}\n#endif\n')
    return ''.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        metadata = json.loads((args.data_dir / 'source.json').read_text())
        record = next((entry for entry in metadata['inputs']
                       if entry['file'] == 'idna/IdnaMappingTable.txt'), None)
        if record is None:
            raise ValueError('IDNA input missing from provenance')
        version = metadata['version']
        checksum = record['sha256']
        if (not isinstance(version, str)
            or re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version) is None
            or not isinstance(checksum, str)
            or re.fullmatch(r'[0-9a-f]{64}', checksum) is None):
            raise ValueError('invalid IDNA version or checksum')
        raw = (args.data_dir / record['file']).read_bytes()
        if hashlib.sha256(raw).hexdigest() != checksum:
            raise ValueError('IDNA data checksum does not match provenance')
        if '# Version: ' + version not in raw.decode('utf-8'):
            raise ValueError('IDNA table version does not match provenance')
        generated = render(*parse_table(raw.decode('utf-8')), version, checksum)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', dir=args.output.parent,
                                             prefix='.idna-', delete=False,
                                             encoding='ascii', newline='\n') as out:
                temp_name = out.name
                out.write(generated)
            os.replace(temp_name, args.output)
        finally:
            if temp_name is not None and os.path.exists(temp_name):
                os.unlink(temp_name)
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as exc:
        parser.exit(1, f'idna: {exc}\n')


if __name__ == '__main__':
    main()
