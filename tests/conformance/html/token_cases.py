#!/usr/bin/env python3
"""Compare the pinned tokenizer oracle and partition-equivalent event traces."""

import hashlib
import json
from pathlib import Path
import re
import struct
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


STATES = ("Data state", "RCDATA state", "RAWTEXT state", "Script data state",
          "PLAINTEXT state", "CDATA section state")
PARTITIONS = ("whole", "byte", "seven", "structural", "random")


def batch(executable, cases, partition):
    payload = bytearray()
    for index, case, state, tag in cases:
        data, name = case["input"].encode("utf-8"), tag.encode("ascii")
        if len(data) > 4096 or len(name) >= 128:
            raise ValueError(f"case {index}: dump harness input bound exceeded")
        payload.extend(struct.pack("<III", STATES.index(state), len(data), len(name)))
        payload.extend(data)
        payload.extend(name)
    run = subprocess.run([executable, "--batch", partition], input=payload,
                         capture_output=True, timeout=30, check=False)
    if run.returncode or run.stderr:
        raise ValueError(f"{executable} {partition}: exit {run.returncode}, "
                         f"{run.stderr[:512]!r}")
    records, events = [], []
    for line in run.stdout.splitlines():
        item = json.loads(line)
        if item[0] == "Result":
            records.append((item[1], events))
            events = []
        else:
            events.append(item)
    if events or len(records) != len(cases):
        raise ValueError(f"{executable} {partition}: incomplete batch output")
    return records


def semantic_output(events):
    tokens, errors = [], []
    duplicate_attributes = False
    for item in events:
        if item[0] == "Error":
            errors.append(item[1])
        elif item[0] == "Character" and tokens and tokens[-1][0] == "Character":
            tokens[-1][1] += item[1]
        else:
            item = item.copy()
            if item[0] == "StartTag":
                attributes = dict(item[2])
                duplicate_attributes |= len(attributes) != len(item[2])
                item[2] = attributes
            tokens.append(item)
    return tokens, errors, duplicate_attributes


def main(executables):
    root = Path(__file__).resolve().parents[2] / "fixtures/html5lib/tokenizer"
    manifest = json.loads((root / "source.json").read_text())
    license_bytes = (root / manifest["license"]).read_bytes()
    if hashlib.sha256(license_bytes).hexdigest() != manifest["license_sha256"]:
        raise ValueError("fixture license checksum mismatch")
    if set(manifest["files"]) != {path.name for path in root.glob("*.test")}:
        raise ValueError("tokenizer fixture manifest does not cover all test files")
    failures = runs = excluded = surrogates = xml = 0
    for filename, digest in manifest["files"].items():
        raw = (root / filename).read_bytes()
        if hashlib.sha256(raw).hexdigest() != digest:
            raise ValueError(f"{filename}: fixture checksum mismatch")
        data = json.loads(raw)
        if filename == "xmlViolation.test":
            xml += len(data["xmlViolationTests"])
            continue
        cases = []
        for index, case in enumerate(data["tests"]):
            if case.get("doubleEscaped"):
                case = unescape(case)
            if any(0xd800 <= ord(c) <= 0xdfff for c in case["input"]):
                surrogates += 1
                continue
            for state in case.get("initialStates", ["Data state"]):
                # The pinned WHATWG grammar now parses processing instructions.
                if state == "Data state" and "<?" in case["input"]:
                    excluded += 1
                    continue
                tag = case.get("lastStartTag", "lxaunmatchtag")
                if "lastStartTag" not in case:
                    assert tag not in case["input"].lower()
                cases.append((index, case, state, tag))
        baseline = None
        for executable in executables:
            for partition in PARTITIONS:
                try:
                    records = batch(executable, cases, partition)
                except (ValueError, subprocess.TimeoutExpired) as error:
                    print(f"{filename} {partition}: {error}")
                    return True
                if baseline is None:
                    baseline = records
                for position, ((index, case, state, tag),
                               (result, events)) in enumerate(zip(cases, records)):
                    runs += 1
                    tokens, errors, duplicate_attributes = semantic_output(events)
                    expected_errors = [error["code"] for error in case.get("errors", [])]
                    if (result or duplicate_attributes
                            or tokens != case["output"]
                            or errors != expected_errors
                            or records[position] != baseline[position]):
                        failures += 1
                        print(f"{filename}:{index} {state} {partition} "
                              f"{executable}: FAIL")
                        print(f"expected {case['output']!r}, {expected_errors!r}")
                        print(f"actual {tokens!r}, {errors!r}, result {result}")
                        if records[position] != baseline[position]:
                            print(f"event/offset trace differs from whole input: {events!r}")
                        if duplicate_attributes:
                            print("tokenizer emitted duplicate attributes")
    print(f"{runs - failures}/{runs} tokenizer runs matched; "
          f"{excluded} old PI state runs, {surrogates} surrogate inputs and "
          f"{xml} XML-coercion cases excluded")
    return bool(failures)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: token_cases.py TOKEN_DUMP_EXECUTABLE [...]")
    sys.exit(main(sys.argv[1:]))
