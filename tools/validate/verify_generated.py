#!/usr/bin/env python3
"""Regenerate checked-in tables in the build directory and verify their bytes."""

import argparse
from pathlib import Path
import subprocess
import sys


OUTPUTS = (
    ("encoding/encoding-indexes.h", "encoding/indexes.py", "whatwg/encoding"),
    ("html/html-categories.h", "html/categories.py", "whatwg/html"),
    ("html/html-charref-overrides.h", "html/charref_overrides.py", "whatwg/html"),
    ("html/html-entities.h", "html/entities.py", "whatwg/html"),
    ("html/html-tags.h", "html/tags.py", "whatwg/html"),
    ("unicode/idna-table.h", "unicode/idna.py", "unicode/17.0.0"),
    ("unicode/joining-types.h", "unicode/joining_types.py", "unicode/17.0.0"),
    ("unicode/normalization-table.h", "unicode/normalization.py", "unicode/17.0.0"),
)


def verify(root, build_dir, outputs=OUTPUTS):
    for name, generator, data in outputs:
        checked_in = root / "generated" / name
        output = build_dir / "regenerated" / name
        if output.resolve() == checked_in.resolve():
            raise ValueError(f"build output would overwrite {checked_in}")
        subprocess.run(
            [sys.executable, str(root / "tools/generate" / generator),
             "--data-dir", str(root / "data" / data), "--output", str(output)],
            check=True,
        )
        if output.read_bytes() != checked_in.read_bytes():
            raise ValueError(f"{name}: regenerated output differs from {checked_in}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    try:
        verify(Path(__file__).resolve().parents[2], args.build_dir.resolve())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"generated tables: {exc}\n")
    print("All checked-in generated tables match pinned inputs")


if __name__ == "__main__":
    main()
