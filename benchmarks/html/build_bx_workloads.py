#!/usr/bin/env python3
"""Build the workload probe against bx sources and a shared Liblexa build."""

import argparse
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bx_source", type=Path)
    parser.add_argument("liblexa_build", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--cflags", default="-O3 -DNDEBUG")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    bx = args.bx_source.resolve() / "src"
    libraries = args.liblexa_build.resolve() / "src"
    sources = [
        bx / "lib/fetch/html" / name
        for name in ("parser.c", "markdown.c", "site_markdown.c", "lore_markdown.c")
    ] + [bx / "lib/fetch/net/url.c", bx / "lib/markdown/writer.c"]
    for path in [*sources, libraries / "liblexa.so",
                 libraries / "liblexa-html-candidate.so"]:
        if not path.is_file():
            parser.error(f"missing input: {path}")
    curl = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "libcurl"], text=True))
    command = [
        args.cc, "-std=c17", "-D_POSIX_C_SOURCE=200809L",
        "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        *shlex.split(args.cflags), "-I", str(root / "include"),
        "-I", str(bx), str(root / "benchmarks/html/bx_workloads.c"),
        *map(str, sources), "-L", str(libraries),
        "-llexa-html-candidate", "-llexa", *curl,
        f"-Wl,-rpath,{libraries}",
        *[f"-Wl,--wrap={name}" for name in (
            "lxa_html_document_parse", "lxa_html_serialize_document",
            "lxa_dom_nodes_text_content")],
        "-o", str(args.output),
    ]
    print(shlex.join(command), flush=True)
    subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
