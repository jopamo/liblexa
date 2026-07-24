#!/usr/bin/env python3
"""Generate Unicode 17 normalization properties, decompositions, and compositions."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile


def parse_ucd(text):
    rows = []
    decompositions = []
    previous = -1
    for number, line in enumerate(text.splitlines(), 1):
        fields = line.split(';')
        if len(fields) != 15:
            raise ValueError(f'UnicodeData line {number}: expected 15 fields')
        try:
            cp = int(fields[0], 16)
            ccc = int(fields[3])
            if not previous < cp <= 0x10ffff or not 0 <= ccc <= 255:
                raise ValueError('invalid code point or combining class')
            raw = fields[5].split()
            tag = raw.pop(0) if raw and raw[0].startswith('<') else ''
            if tag and (not tag.endswith('>') or len(tag) < 3):
                raise ValueError('invalid decomposition tag')
            mapping = [int(value, 16) for value in raw]
            if (len(mapping) > 255 or any(value > 0x10ffff or 0xd800 <= value <= 0xdfff
                                          for value in mapping) or
                    (tag and not mapping)):
                raise ValueError('invalid decomposition')
        except ValueError as exc:
            raise ValueError(f'UnicodeData line {number}: {exc}') from exc
        if ccc or mapping:
            rows.append((cp, ccc, int(bool(tag)), len(decompositions), len(mapping)))
            decompositions.extend(mapping)
        previous = cp
    if not rows:
        raise ValueError('no normalization entries')
    return rows, decompositions


def property_ranges(text):
    for number, line in enumerate(text.splitlines(), 1):
        fields = [part.strip() for part in line.split('#', 1)[0].split(';')]
        if len(fields) < 2 or not fields[0] or fields[0].startswith('@'):
            continue
        try:
            bounds = [int(cp, 16) for cp in fields[0].split('..')]
            if len(bounds) not in (1, 2) or bounds[0] > bounds[-1] or bounds[-1] > 0x10ffff:
                raise ValueError('invalid range')
        except ValueError as exc:
            raise ValueError(f'property line {number}: {exc}') from exc
        yield bounds[0], bounds[-1], fields[1], fields[2] if len(fields) > 2 else ''


def derived_tables(ucd, props, exclusions):
    quick = {}
    excluded = set()
    kinds = {'NFC_QC': 0, 'NFD_QC': 2, 'NFKC_QC': 4, 'NFKD_QC': 6}
    for lo, hi, name, value in property_ranges(props):
        if name in kinds:
            if value not in ('N', 'M'):
                raise ValueError('unexpected quick-check value')
            bit = (1 if value == 'N' else 2) << kinds[name]
            for cp in range(lo, hi + 1):
                if quick.get(cp, 0) & (3 << kinds[name]):
                    raise ValueError('overlapping quick-check ranges')
                quick[cp] = quick.get(cp, 0) | bit
        elif name == 'Full_Composition_Exclusion':
            excluded.update(range(lo, hi + 1))
    for lo, hi, _, _ in property_ranges(exclusions):
        if any(cp not in excluded for cp in range(lo, hi + 1)):
            raise ValueError('CompositionExclusions contradicts derived full exclusions')
    pairs = {}
    for line in ucd.splitlines():
        fields = line.split(';')
        mapping = fields[5].split()
        cp = int(fields[0], 16)
        if (len(mapping) == 2 and not mapping[0].startswith('<') and
                int(fields[3]) == 0 and cp not in excluded):
            key = tuple(int(item, 16) for item in mapping)
            if key in pairs:
                raise ValueError('duplicate composition pair')
            pairs[key] = cp
    return sorted(quick.items()), [(a, b, cp) for (a, b), cp in sorted(pairs.items())]


def render(rows, decompositions, quick, pairs, version, checksums):
    output = [f"/* DO NOT EDIT: generated from Unicode {version} UCD.\n",
              *(f" * {name} SHA-256: {checksum}\n"
                for name, checksum in checksums),
              " * See data/unicode/17.0.0/{source.json,LICENSE}. */\n",
              "/* Hangul decomposition and composition use the Unicode algorithm, not these tables. */\n",
              '#ifndef LIBLEXA_GENERATED_NORMALIZATION_H\n#define LIBLEXA_GENERATED_NORMALIZATION_H\n',
              '#include <stdint.h>\n',
              '/* Decomposition tags are 0 = canonical, 1 = compatibility. */\n',
              'typedef struct { uint32_t cp, offset; uint8_t ccc, compatibility, length; } liblexa_normalization_entry;\n',
              'static const uint32_t liblexa_decompositions[] = {\n']
    for index in range(0, len(decompositions), 12):
        output.append('    ' + ', '.join(str(n) for n in decompositions[index:index + 12]) + ',\n')
    output.append('};\nstatic const liblexa_normalization_entry liblexa_normalization[] = {\n')
    output.extend(f'    {{{cp}, {offset}, {ccc}, {compat}, {length}}},\n'
                  for cp, ccc, compat, offset, length in rows)
    output.extend(['};\n', f'#define LIBLEXA_NORMALIZATION_COUNT {len(rows)}\n',
                   f'#define LIBLEXA_DECOMPOSITION_COUNT {len(decompositions)}\n',
                   '/* Two bits per form: NFC, NFD, NFKC, NFKD; 0 = Yes, 1 = No, 2 = Maybe. */\n',
                   'typedef struct { uint32_t cp; uint8_t checks; } liblexa_quick_check;\n',
                   'static const liblexa_quick_check liblexa_quick_checks[] = {\n'])
    output.extend(f'    {{{cp}, {bits}}},\n' for cp, bits in quick)
    output.extend(['};\n', f'#define LIBLEXA_QUICK_CHECK_COUNT {len(quick)}\n',
                   'typedef struct { uint32_t starter, second, composed; } liblexa_composition;\n',
                   'static const liblexa_composition liblexa_compositions[] = {\n'])
    output.extend(f'    {{{a}, {b}, {cp}}},\n' for a, b, cp in pairs)
    output.extend(['};\n', f'#define LIBLEXA_COMPOSITION_COUNT {len(pairs)}\n#endif\n'])
    return ''.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = json.loads((args.data_dir / 'source.json').read_text())
        record = next((item for item in manifest['inputs']
                       if item['file'] == 'ucd/UnicodeData.txt'), None)
        if record is None:
            raise ValueError('UnicodeData.txt missing from provenance')
        version = manifest['version']
        if (not isinstance(version, str)
            or re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version) is None):
            raise ValueError('invalid Unicode version')
        checksums = []
        def read_pinned(name):
            item = next((entry for entry in manifest['inputs'] if entry['file'] == name), None)
            if item is None:
                raise ValueError(f'{name} missing from provenance')
            checksum = item['sha256']
            if (not isinstance(checksum, str)
                or re.fullmatch(r'[0-9a-f]{64}', checksum) is None):
                raise ValueError(f'{name} invalid checksum')
            content = (args.data_dir / name).read_bytes()
            if hashlib.sha256(content).hexdigest() != checksum:
                raise ValueError(f'{name} checksum does not match provenance')
            checksums.append((name, checksum))
            return content.decode('utf-8')

        ucd = read_pinned(record['file'])
        rows, decompositions = parse_ucd(ucd)
        props = read_pinned('ucd/DerivedNormalizationProps.txt')
        exclusions = read_pinned('ucd/CompositionExclusions.txt')
        text = render(rows, decompositions, *derived_tables(ucd, props, exclusions),
                      version, checksums)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', dir=args.output.parent,
                                             prefix='.normalization-', delete=False,
                                             encoding='ascii', newline='\n') as out:
                temp_name = out.name
                out.write(text)
            os.replace(temp_name, args.output)
        finally:
            if temp_name is not None and os.path.exists(temp_name):
                os.unlink(temp_name)
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as exc:
        parser.exit(1, f'normalization: {exc}\n')


if __name__ == '__main__':
    main()
