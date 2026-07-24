#!/usr/bin/env python3
"""Check the installed library and its package closure."""

import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
INHERITED = re.compile(r"croco", re.I)
SYMBOL = re.compile(r"\b(?:croco_|CR_)\w*", re.I)


def run(args, *, env=None):
    result = subprocess.run(args, text=True, capture_output=True, env=env,
                            check=False)
    if result.returncode:
        raise ValueError(
            f"{' '.join(map(str, args))}: {result.stderr.strip()}"
        )
    return result.stdout


def check_compiled_headers(commands):
    # Source paths alone miss a library source including a vendor header.
    # Replay production compiler commands without writing objects or depfiles;
    # -H reports the headers the preprocessor actually opened.
    checked = 0
    headers_seen = 0
    for entry in commands:
        output = entry["output"]
        if ".so.p/" not in output and ".a.p/" not in output:
            continue
        args = shlex.split(entry["command"])
        for option in ("-MQ", "-MF", "-MT", "-o"):
            if option in args:
                index = args.index(option)
                del args[index:index + 2]
        args = [arg for arg in args if arg not in ("-MD", "-MMD", "-c")]
        result = subprocess.run(
            [*args, "-fsyntax-only", "-H"], cwd=entry["directory"],
            capture_output=True, text=True, check=False
        )
        if result.returncode:
            raise ValueError("Liblexa source include audit failed: "
                             + result.stderr.strip())
        for line in result.stderr.splitlines():
            match = re.match(r"^\.+ (.+)$", line)
            if match is None:
                continue
            headers_seen += 1
            header = (Path(entry["directory"]) / match.group(1)).resolve()
            if INHERITED.search(str(header)):
                raise ValueError(f"Liblexa build includes inherited header: {header}")
        checked += 1
    if not checked or not headers_seen:
        raise ValueError("no Liblexa library header dependency trace")


def check_installed_closure(prefix):
    # A clean link map alone does not show old headers or a dormant library
    # left in the package prefix. Notices under share/ may legitimately remain.
    roots = [prefix / "include", prefix / "share/pkgconfig"]
    roots.extend(path for path in prefix.iterdir()
                 if path.is_dir() and re.fullmatch(r"lib(?:64)?", path.name))
    for root in roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if INHERITED.search(str(path.relative_to(root))):
                raise ValueError(f"inherited file in Liblexa install: {path}")


def check_installed_artifacts(prefix, expected):
    # Scan the whole isolated prefix: checking only the known liblexa names
    # would miss a second archive or package file installed under another name.
    for libdir in prefix.iterdir():
        if not libdir.is_dir() or not re.fullmatch(r"lib(?:64)?", libdir.name):
            continue
        for path in libdir.rglob("*"):
            name = path.name
            library_file = path.is_file() and (
                name.endswith((".a", ".la", ".pc", ".so"))
                or re.search(r"\.so\.[^/]+$", name)
            )
            if not path.is_symlink() and not library_file:
                continue
            if path not in expected or not path.resolve().is_relative_to(prefix):
                raise ValueError(f"unaccounted Liblexa install artifact: {path}")
    share_pc = prefix / "share/pkgconfig"
    if share_pc.is_dir():
        for path in share_pc.glob("*.pc"):
            raise ValueError(f"unaccounted Liblexa install artifact: {path}")


def check_libraries(archive, shared, flags, static_flags, label):
    if (INHERITED.search(" ".join(flags + static_flags))
        or SYMBOL.search(run(["nm", "-u", str(archive)]))
        or SYMBOL.search(run(["nm", "-g", "--defined-only", str(archive)]))
        or SYMBOL.search(run(["nm", "-D", "--defined-only", str(shared)]))
        or INHERITED.search(run(["readelf", "-d", str(shared)]))):
        raise ValueError(f"{label} contains or resolves inherited symbols")


def check_metadata_paths(name, env, prefix, libdir):
    for variable, expected in (("prefix", prefix), ("includedir", prefix / "include"),
                               ("libdir", libdir)):
        actual = Path(run(["pkg-config", f"--variable={variable}", name],
                          env=env).strip()).resolve()
        if actual != expected.resolve():
            raise ValueError(f"{name} {variable} points outside Liblexa install: {actual}")


def check_package_flags(name, flags, prefix, libdir):
    # Metadata variables can be correct while Libs/Cflags add other search
    # directories; consumers may then resolve headers or libraries elsewhere.
    for index, flag in enumerate(flags):
        for option, allowed in (("-I", prefix / "include"),
                                ("-isystem", prefix / "include"),
                                ("-iquote", prefix / "include"),
                                ("-idirafter", prefix / "include"),
                                ("-L", libdir)):
            if not flag.startswith(option):
                continue
            path = (flags[index + 1] if flag == option and index + 1 < len(flags)
                    else flag[len(option):])
            if not path or not Path(path).resolve().is_relative_to(allowed):
                raise ValueError(f"{name} {option} path outside Liblexa install: {path}")


def check(build, prefix, candidate_html=False):
    check_installed_closure(prefix)
    pc_files = list(prefix.glob("lib*/pkgconfig/liblexa.pc"))
    if len(pc_files) != 1:
        raise ValueError("expected exactly one installed liblexa.pc")
    libdir = pc_files[0].parents[1]
    shared = libdir / "liblexa.so"
    archive = libdir / "liblexa.a"
    for path in (shared, archive):
        if not path.is_file():
            raise ValueError(f"missing installed library: {path}")

    commands = json.loads((build / "compile_commands.json").read_text())
    if not isinstance(commands, list) or not commands:
        raise ValueError("no Liblexa compile commands")
    # Check the library compile graph below; tests and benchmarks may live
    # outside the production source tree.
    candidate_marker = "liblexa-html-candidate."
    candidate_sources = {
        str((Path(entry["directory"]) / entry["file"]).resolve(strict=True))
        for entry in commands if candidate_marker in entry["output"]
        and (".so.p/" in entry["output"] or ".a.p/" in entry["output"])
    }
    if candidate_html and str(ROOT / "src/html/document.c") not in candidate_sources:
        raise ValueError("HTML candidate document missing from compile graph")
    if not candidate_html and candidate_sources:
        raise ValueError("HTML candidate compiled without candidate audit")
    if candidate_html:
        run([sys.executable, str(ROOT / "tools/validate/check_provenance.py"),
             str(ROOT / "tools/validate/provenance.json"),
             "--candidate", *sorted(candidate_sources)])
    library_sources = sorted({
        str((Path(entry["directory"]) / entry["file"]).resolve(strict=True))
        for entry in commands
        if ".so.p/" in entry["output"] or ".a.p/" in entry["output"]
        if candidate_marker not in entry["output"]
    })
    if str(ROOT / "src/encoding/stream.c") not in library_sources:
        raise ValueError("decoder missing from library compile graph")
    run([sys.executable, str(ROOT / "tools/validate/check_provenance.py"),
         str(ROOT / "tools/validate/provenance.json"),
         *library_sources])
    check_compiled_headers(commands)

    env = dict(os.environ, PKG_CONFIG_LIBDIR=str(pc_files[0].parent),
               PKG_CONFIG_PATH=str(pc_files[0].parent))
    check_metadata_paths("liblexa", env, prefix, libdir)
    flags = shlex.split(run(["pkg-config", "--cflags", "--libs", "liblexa"],
                            env=env))
    static_flags = shlex.split(
        run(["pkg-config", "--static", "--libs", "liblexa"], env=env)
    )
    for package_flags in (flags, static_flags):
        check_package_flags("liblexa", package_flags, prefix, libdir)
    check_libraries(archive, shared, flags, static_flags, "Liblexa package")

    candidate_archive = libdir / "liblexa-html-candidate.a"
    candidate_flags = []
    candidate_static_flags = []
    candidate_shared = libdir / "liblexa-html-candidate.so"
    candidate_pc = libdir / "pkgconfig/liblexa-html-candidate.pc"
    candidate_header = prefix / "include/liblexa/html.h"
    candidate_header_root = prefix / "include/liblexa"
    public_candidate_headers = {
        "html.h", "serialize.h",
    }
    installed_candidate_headers = {
        str(path.relative_to(candidate_header_root))
        for path in [*(candidate_header_root / "html").rglob("*"),
                     *(candidate_header_root / "serialization").rglob("*"),
                     candidate_header_root / "html.h",
                     candidate_header_root / "serialize.h"]
        if path.is_file()
    }
    if candidate_html:
        for path in (candidate_shared, candidate_archive, candidate_pc,
                     candidate_header):
            if not path.is_file():
                raise ValueError(f"missing installed HTML candidate file: {path}")
        check_metadata_paths("liblexa-html-candidate", env, prefix, libdir)
        if installed_candidate_headers != public_candidate_headers:
            difference = installed_candidate_headers ^ public_candidate_headers
            raise ValueError(f"unexpected HTML candidate header set: {sorted(difference)}")
        requires = run(["pkg-config", "--print-requires",
                        "liblexa-html-candidate"], env=env).splitlines()
        if not any(line.split()[0] == "liblexa" for line in requires if line.split()):
            raise ValueError("HTML candidate does not require liblexa")
        candidate_flags = shlex.split(run(
            ["pkg-config", "--cflags", "--libs", "liblexa-html-candidate"],
            env=env))
        candidate_static_flags = shlex.split(run(
            ["pkg-config", "--static", "--libs", "liblexa-html-candidate"],
            env=env))
        for package_flags in (candidate_flags, candidate_static_flags):
            check_package_flags("liblexa-html-candidate", package_flags,
                                prefix, libdir)
        check_libraries(candidate_archive, candidate_shared,
                        candidate_flags, candidate_static_flags,
                        "HTML candidate")
    elif any(path.exists() for path in (candidate_shared, candidate_archive,
                                        candidate_pc)) or installed_candidate_headers:
        raise ValueError("HTML candidate installed in production prefix")

    expected_artifacts = {archive, shared, pc_files[0]}
    if candidate_html:
        expected_artifacts.update((candidate_archive, candidate_shared,
                                   candidate_pc))
    check_installed_artifacts(prefix, expected_artifacts)

    headers = sorted((prefix / "include/liblexa").rglob("*.h"))
    if not headers:
        raise ValueError("no installed Liblexa headers")
    includes = "".join(
        f'#include <{header.relative_to(prefix / "include")}>\n'
        for header in headers
    )
    with tempfile.TemporaryDirectory(prefix="liblexa-consumer-") as directory:
        temp = Path(directory)
        source = temp / "includes.c"
        source.write_text(includes + "int main(void) { return 0; }\n")
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc:
            raise ValueError("empty C compiler command")
        options = json.loads((build / "meson-info/intro-buildoptions.json").read_text())
        sanitizers = next((option["value"] for option in options
                           if option["name"] == "b_sanitize"), [])
        if isinstance(sanitizers, str):
            sanitizers = [] if sanitizers == "none" else sanitizers.split(",")
        if sanitizers:
            cc.append("-fsanitize=" + ",".join(sanitizers))
        result = subprocess.run(
            [*cc, "-std=c17", "-Werror", "-H", "-I" + str(prefix / "include"),
             "-fsyntax-only", str(source)], capture_output=True, text=True
        )
        if result.returncode or INHERITED.search(result.stderr):
            raise ValueError("installed headers use an inherited include: "
                             + result.stderr.strip())
        consumer = ROOT / "tests/integration/core_consumer.c"
        executable = temp / "shared"
        run([*cc, "-std=c17", "-Werror", str(consumer), *flags,
             "-Wl,-rpath," + str(libdir), "-o", str(executable)], env=env)
        run([str(executable)], env=env)
        executable = temp / "static"
        link_map = temp / "liblexa.map"
        run([*cc, "-std=c17", "-Werror",
             "-I" + str(prefix / "include"), str(consumer),
             "-Wl,--whole-archive", str(archive), "-Wl,--no-whole-archive",
             *(arg for arg in static_flags if arg != "-llexa"),
             "-Wl,-Map=" + str(link_map), "-o", str(executable)], env=env)
        if INHERITED.search(link_map.read_text()):
            raise ValueError("static link map contains an inherited library")
        run([str(executable)], env=env)
        if candidate_html:
            candidate_consumer = ROOT / "tests/integration/install/html_consumer.c"
            executable = temp / "candidate-shared"
            run([*cc, "-std=c17", "-Werror",
                 "-I" + str(prefix / "include"), str(candidate_consumer),
                 *candidate_flags, "-Wl,-rpath," + str(libdir),
                 "-o", str(executable)], env=env)
            run([str(executable)], env=env)
            executable = temp / "candidate-static"
            link_map = temp / "html-candidate.map"
            run([*cc, "-std=c17", "-Werror",
                 "-I" + str(prefix / "include"), str(candidate_consumer),
                 "-Wl,--whole-archive", str(candidate_archive), str(archive),
                 "-Wl,--no-whole-archive",
                 *(arg for arg in candidate_static_flags
                   if arg not in ("-llexa", "-llexa-html-candidate")),
                 "-Wl,-Map=" + str(link_map), "-o", str(executable)], env=env)
            if INHERITED.search(link_map.read_text()):
                raise ValueError("HTML candidate link map contains inherited library")
            run([str(executable)], env=env)


if __name__ == "__main__":
    try:
        if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4
                                         and sys.argv[3] != "--candidate-html"):
            sys.exit("usage: check_install.py BUILD PREFIX [--candidate-html]")
        check(Path(sys.argv[1]).resolve(strict=True),
              Path(sys.argv[2]).resolve(strict=True),
              candidate_html=len(sys.argv) == 4)
    except (OSError, KeyError, TypeError, ValueError) as exc:
        sys.exit(f"Liblexa install: {exc}")
