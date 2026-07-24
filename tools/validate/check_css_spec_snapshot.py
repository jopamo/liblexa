#!/usr/bin/env python3
"""Verify the pinned, specification-derived CSS inputs used by new generators."""

import hashlib
import json
from pathlib import Path
import re
import sys


def main():
    root = Path(__file__).resolve().parents[2] / "data/webref"
    manifest = json.loads((root / "source.json").read_text())
    revision = manifest["revision"]
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("invalid pinned WebRef revision")

    files = manifest["inputs"] + [manifest["license"]]
    names = []
    for entry in files:
        name = entry["file"]
        if not re.fullmatch(r"[a-z0-9-]+\.json|LICENSE", name):
            raise ValueError(f"invalid input name: {name}")
        if name in names:
            raise ValueError(f"duplicate input: {name}")
        names.append(name)
        remote = "LICENSE" if name == "LICENSE" else f"ed/css/{name}"
        expected_url = f"https://raw.githubusercontent.com/w3c/webref/{revision}/{remote}"
        if entry["url"] != expected_url:
            raise ValueError(f"{name}: URL differs from pinned revision")
        data = (root / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"{name}: content differs from pinned checksum")
        if name != "LICENSE":
            spec = json.loads(data)["spec"]
            if spec["url"] != entry["specification_url"]:
                raise ValueError(f"{name}: specification URL differs")

    required = {
        "css-backgrounds.json", "css-box.json", "css-cascade.json",
        "css-color.json", "css-conditional.json", "css-display.json", "css-exclusions.json", "css-flexbox.json",
        "css-fonts.json", "css-inline.json", "css-namespaces.json", "css-overflow.json",
        "css-page-floats.json", "css-position.json",
        "css-pseudo.json", "css-sizing.json",
        "css-syntax.json", "css-text-decor.json", "css-text.json", "css-ui.json",
        "css-values.json", "css-writing-modes.json", "selectors.json",
    }
    if not required.issubset(names):
        raise ValueError("missing pinned CSS/Selectors inputs")
    csswg = root.parent / "csswg"
    spec_manifest = json.loads((csswg / "source.json").read_text())
    revision = spec_manifest["revision"]
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("invalid pinned CSSWG revision")
    if spec_manifest["specification_url"] != "https://drafts.csswg.org/css-syntax-3/":
        raise ValueError("unexpected CSS Syntax specification URL")
    if len(spec_manifest["files"]) != 7:
        raise ValueError("expected CSS Syntax, CSS Backgrounds, CSS Color, CSS Display, CSS Fonts, CSS2 and license sources")
    for entry, filename, remote in zip(
        spec_manifest["files"],
        ("css-syntax-3.bs", "css-backgrounds-3.bs", "css-color-4.bs",
         "css-display-4.bs", "css-fonts-4.bs", "css2-overview.bs", "LICENSE.md"),
        ("css-syntax-3/Overview.bs", "css-backgrounds-3/Overview.bs",
         "css-color-4/Overview.bs", "css-display-4/Overview.bs",
         "css-fonts-4/Overview.bs", "css2/Overview.bs", "LICENSE.md"),
    ):
        if entry["file"] != filename:
            raise ValueError(f"unexpected CSSWG input: {entry['file']}")
        url = f"https://raw.githubusercontent.com/w3c/csswg-drafts/{revision}/{remote}"
        if entry["url"] != url:
            raise ValueError(f"{filename}: URL differs from pinned revision")
        if hashlib.sha256((csswg / filename).read_bytes()).hexdigest() != entry["sha256"]:
            raise ValueError(f"{filename}: content differs from pinned checksum")
        if filename == "css-backgrounds-3.bs" and entry.get("specification_url") != "https://drafts.csswg.org/css-backgrounds-3/":
            raise ValueError("unexpected CSS Backgrounds 3 specification URL")
        if filename == "css-color-4.bs" and entry.get("specification_url") != "https://drafts.csswg.org/css-color-4/":
            raise ValueError("unexpected CSS Color 4 specification URL")
        if filename == "css-display-4.bs" and entry.get("specification_url") != "https://drafts.csswg.org/css-display-4/":
            raise ValueError("unexpected CSS Display 4 specification URL")
        if filename == "css-fonts-4.bs" and entry.get("specification_url") != "https://drafts.csswg.org/css-fonts-4/":
            raise ValueError("unexpected CSS Fonts 4 specification URL")
        if filename == "css2-overview.bs" and entry.get("specification_url") != "https://drafts.csswg.org/css2/":
            raise ValueError("unexpected CSS2 specification URL")
    print("Pinned CSS specification inputs verified")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as exc:
        sys.exit(f"CSS specification snapshot: {exc}")
