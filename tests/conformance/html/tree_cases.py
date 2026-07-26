#!/usr/bin/env python3
"""Compare selected pinned html5lib document trees with the Liblexa DOM.

Compile tree_dump.c against the opt-in candidate with -Isrc,
pkg-config --cflags --libs liblexa-html-candidate, and a staged-library rpath.
Pass the resulting executable here. Known gaps fail; this is not a release gate.
"""

import difflib
import json
from pathlib import Path
import subprocess
import sys


def main(executable):
    corpus = Path(__file__).resolve().parents[2] / "fixtures/html5lib/tree_cases.json"
    cases = json.loads(corpus.read_text())["cases"]
    failed = 0
    for case in cases:
        try:
            run = subprocess.run(
                [executable], input=case["input"].encode("utf-8"),
                capture_output=True, timeout=5, check=False
            )
        except subprocess.TimeoutExpired:
            print(f"{case['id']}: timeout")
            failed += 1
            continue
        expected = "\n".join(case["tree"]) + "\n"
        actual = run.stdout.decode("utf-8", errors="replace")
        if run.returncode or actual != expected:
            failed += 1
            print(f"{case['id']}: FAIL (exit {run.returncode})")
            if run.stderr:
                print(run.stderr.decode("utf-8", errors="replace").strip()[:512])
            print("".join(difflib.unified_diff(
                expected.splitlines(True), actual.splitlines(True),
                fromfile="pinned", tofile="Liblexa"
            ))[:1024], end="")
        else:
            print(f"{case['id']}: PASS")
    print(f"{len(cases) - failed}/{len(cases)} tree cases matched")
    return 1 if failed else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: tree_cases.py TREE_DUMP_EXECUTABLE")
    sys.exit(main(sys.argv[1]))
