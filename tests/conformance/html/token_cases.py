#!/usr/bin/env python3
"""Compare pinned tokenizer outputs and diagnostic codes across byte feeds."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def unescape(value):
    if isinstance(value, str):
        return re.sub(r"\\u([0-9a-fA-F]{4})", lambda m: chr(int(m[1], 16)), value)
    if isinstance(value, list):
        return [unescape(item) for item in value]
    if isinstance(value, dict):
        return {unescape(key): unescape(item) for key, item in value.items()}
    return value


def main(executable):
    root = Path(__file__).resolve().parents[2] / "fixtures/html5lib/tokenizer"
    manifest = json.loads((root / "source.json").read_text())
    license_bytes = (root / manifest["license"]).read_bytes()
    if hashlib.sha256(license_bytes).hexdigest() != manifest["license_sha256"]:
        raise ValueError("fixture license checksum mismatch")
    failures = runs = excluded = 0
    for filename, digest in manifest["files"].items():
        raw = (root / filename).read_bytes()
        if hashlib.sha256(raw).hexdigest() != digest:
            raise ValueError(f"{filename}: fixture checksum mismatch")
        for index, case in enumerate(json.loads(raw)["tests"]):
            if case.get("doubleEscaped"):
                case = unescape(case)
            for state in case.get("initialStates", ["Data state"]):
                # The pinned WHATWG grammar now parses processing instructions.
                if state == "Data state" and "<?" in case["input"]:
                    excluded += 1
                    continue
                tag = case.get("lastStartTag", "lxaunmatchtag")
                if "lastStartTag" not in case:
                    assert tag not in case["input"].lower()
                for chunk in ("whole", "byte"):
                    runs += 1
                    try:
                        run = subprocess.run(
                            [executable, state, tag, chunk],
                            input=case["input"].encode("utf-8"),
                            capture_output=True, timeout=5, check=False,
                        )
                    except subprocess.TimeoutExpired:
                        failures += 1
                        print(f"{filename}:{index} {state} {chunk}: timeout")
                        continue
                    tokens, errors = [], []
                    for line in run.stdout.splitlines():
                        item = json.loads(line)
                        if item[0] == "Error":
                            errors.append(item[1])
                        elif item[0] == "Character" and tokens and tokens[-1][0] == "Character":
                            tokens[-1][1] += item[1]
                        else:
                            if item[0] == "StartTag":
                                attributes = {}
                                for name, value in item[2]:
                                    attributes.setdefault(name, value)
                                item[2] = attributes
                            tokens.append(item)
                    expected_errors = [error["code"] for error in case.get("errors", [])]
                    if run.returncode or tokens != case["output"] or errors != expected_errors:
                        failures += 1
                        print(f"{filename}:{index} {state} {chunk}: FAIL")
                        print(f"expected {case['output']!r}, {expected_errors!r}")
                        print(f"actual {tokens!r}, {errors!r}, exit {run.returncode}")
                        if run.stderr:
                            print(run.stderr.decode("utf-8", errors="replace")[:512])
    print(f"{runs - failures}/{runs} tokenizer runs matched; {excluded} old PI cases excluded")
    return bool(failures)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: token_cases.py TOKEN_DUMP_EXECUTABLE")
    sys.exit(main(sys.argv[1]))
