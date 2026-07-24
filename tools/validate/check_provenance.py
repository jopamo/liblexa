#!/usr/bin/env python3
"""Validate source ownership and pinned data."""

import hashlib
import json
from pathlib import Path
import re
import sys


def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate provenance key: {key}")
        result[key] = value
    return result


def check_data_manifest(path):
    data = json.loads(path.read_text(), object_pairs_hook=unique_keys)
    if not isinstance(data, dict) or ("inputs" in data) == ("files" in data):
        raise ValueError(f"{path}: expected one pinned data input list")
    entries = data.get("inputs", data.get("files"))
    if not isinstance(entries, list) or not entries:
        raise ValueError(f"{path}: missing pinned data inputs")
    if "license" in data:
        entries = [*entries, data["license"]]
    names = set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError(f"{path}: invalid pinned data entry")
        name, digest = entry.get("file"), entry.get("sha256")
        if (not isinstance(name, str) or not name
            or Path(name).is_absolute() or ".." in Path(name).parts
            or str(Path(name)) != name or name in names
            or not isinstance(digest, str)
            or re.fullmatch(r"[0-9a-f]{64}", digest) is None):
            raise ValueError(f"{path}: invalid pinned data entry")
        names.add(name)
        input_path = (path.parent / name).resolve(strict=True)
        if not input_path.is_relative_to(path.parent.resolve()) or not input_path.is_file():
            raise ValueError(f"{path}: invalid pinned data path: {name}")
        actual = hashlib.sha256()
        with input_path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                actual.update(chunk)
        if actual.hexdigest() != digest:
            raise ValueError(f"{path}: pinned data checksum mismatch: {name}")


def check(manifest, sources, candidate=False):
    root = manifest.resolve().parents[2]
    data = json.loads(manifest.read_text(), object_pairs_hook=unique_keys)
    if (set(data) != {"schema_version", "components", "candidate_components"}
        or data["schema_version"] != 1):
        raise ValueError("invalid Liblexa provenance schema")
    if not isinstance(data["components"], list) or not isinstance(
        data["candidate_components"], list
    ):
        raise ValueError("Liblexa provenance components must be a list")
    recorded = set()
    names = set()
    for component in data["candidate_components"] if candidate else data["components"]:
        if not isinstance(component, dict) or set(component) != {
            "id", "sources", "origin", "copyright", "license",
            "data_manifests", "public_inputs"
        }:
            raise ValueError("invalid Liblexa provenance component")
        name = component["id"]
        if not isinstance(name, str) or not name or name in names:
            raise ValueError("missing or duplicate Liblexa component ID")
        names.add(name)
        if component["origin"] != "independently authored":
            raise ValueError(f"{name}: origin must be independently authored")
        if not isinstance(component["copyright"], str) or not component["copyright"]:
            raise ValueError(f"{name}: missing copyright")
        if not isinstance(component["license"], str) or not component["license"]:
            raise ValueError(f"{name}: missing license status")
        manifests = component["data_manifests"]
        if (not isinstance(manifests, list)
            or any(not isinstance(entry, str) for entry in manifests)
            or len(manifests) != len(set(manifests))):
            raise ValueError(f"{name}: invalid data manifests")
        for entry in manifests:
            if (not isinstance(entry, str) or Path(entry).is_absolute()
                or ".." in Path(entry).parts or str(Path(entry)) != entry
                or not entry.startswith("data/")
                or not (root / entry).is_file()):
                raise ValueError(f"{name}: invalid data manifest: {entry}")
            check_data_manifest(root / entry)
        inputs = component["public_inputs"]
        if not isinstance(inputs, list) or not inputs or any(
            not isinstance(item, str) or not item for item in inputs
        ):
            raise ValueError(f"{name}: missing public inputs")
        entries = component["sources"]
        if not isinstance(entries, list) or not entries:
            raise ValueError(f"{name}: missing sources")
        for source in entries:
            if (not isinstance(source, str) or Path(source).is_absolute()
                or ".." in Path(source).parts or str(Path(source)) != source):
                raise ValueError(f"{name}: invalid source path")
            path = (root / source).resolve(strict=True)
            try:
                relative = str(path.relative_to(root))
            except ValueError:
                raise ValueError(f"{name}: source outside Liblexa C tree: {source}") from None
            if path.suffix != ".c":
                raise ValueError(f"{name}: source outside Liblexa C tree: {source}")
            if name == "core" and not candidate:
                prefixes = ("src/base/",)
            elif name == "dom-store" and not candidate:
                prefixes = ("src/dom/",)
            elif name == "encoding" and not candidate:
                prefixes = ("src/encoding/",)
            elif name == "unicode" and not candidate:
                prefixes = ("src/unicode/",)
            elif name == "css" and not candidate:
                prefixes = ("src/css/",)
            elif candidate and name == "html-document-tree-serializer":
                prefixes = ("src/html/", "src/serialize/")
            elif candidate:
                prefixes = ("src/html/",)
            else:
                raise ValueError(f"{name}: unknown source component")
            if not relative.startswith(prefixes) or relative in recorded:
                raise ValueError(f"{name}: invalid or duplicate Liblexa source: {source}")
            recorded.add(relative)
    if candidate:
        shipping = {
            source for component in data["components"]
            for source in component["sources"]
        }
        if recorded & shipping:
            raise ValueError(f"candidate and shipping provenance overlap: "
                             f"{sorted(recorded & shipping)}")
    compiled = {str(Path(source).resolve(strict=True).relative_to(root))
                for source in sources}
    if len(compiled) != len(sources):
        raise ValueError("duplicate compiled Liblexa source")
    if compiled != recorded:
        label = "HTML candidate provenance" if candidate else "Liblexa provenance"
        raise ValueError(f"{label} mismatch: missing "
                         f"{sorted(compiled - recorded)}; unshipped "
                         f"{sorted(recorded - compiled)}")


if __name__ == "__main__":
    try:
        candidate = len(sys.argv) > 2 and sys.argv[2] == "--candidate"
        sources = sys.argv[3:] if candidate else sys.argv[2:]
        if not sources:
            sys.exit("usage: check_provenance.py MANIFEST [--candidate] SOURCE ...")
        check(Path(sys.argv[1]), sources, candidate=candidate)
    except (OSError, ValueError, TypeError) as exc:
        sys.exit(f"Liblexa provenance: {exc}")
