#!/usr/bin/env python3
"""Generates a C++ source/header pair embedding every file under a directory
as a byte array, for release builds that ship a single self-contained binary
(see EDITOR_EMBED_RESOURCES in CMakeLists.txt). Generic over which directory
and C++ namespace -- used for both frontend/ (namespace editor_embedded) and
resources/ (namespace editor_embedded_resources), two independent embedded
tables rather than one merged one, since callers already look each up by a
different mechanism (WebView resource fetch vs. EditorBridge's
readResourceFile()).

Usage:
  embed_resources.py <source_dir> <output_header> <output_source> <namespace>
                     [--obfuscate --key <keyfile>]

With --obfuscate, each file's bytes are deflate-compressed then run through a
keystream cipher (see _obfuscate() below) before being written into the
generated arrays, so `strings`/a text editor on the shipped binary reveal
nothing readable. src/kronos/AssetObfuscation.cpp reverses it at load time
using the same key. <keyfile> is 32 raw bytes as produced by
tools/gen_asset_key.py. This is obfuscation, not encryption -- the key ships
in the binary -- but it turns "grab the frontend in seconds" into real work.
"""

import os
import sys
import zlib


def sanitize(relative_path: str) -> str:
    return "".join(c if c.isalnum() else "_" for c in relative_path)


def _is_dev_only(name: str) -> bool:
    """True for files that are part of the dev/test toolchain, not runtime
    assets -- never wanted in a shipped binary. The frontend is served to a
    WebView at runtime; it never fetches a test harness, a generator script,
    or a byte-dump fixture. Embedding those just bloats the binary (and
    sgx2-fixtures.js alone is ~50 KB of extracted Program data).

      *.test.js / *.test.html   component test harnesses
      *.gen.py / *.py           generator + helper scripts (frontend has no .py runtime)
      *fixture.js / *fixtures.js  extracted-bytes test fixtures
    """
    if name.endswith((".test.js", ".test.html")):
        return True
    if name.endswith(".py"):
        return True
    if name.endswith(".js") and (name.endswith("fixture.js") or name.endswith("fixtures.js")):
        return True
    return False


def _rotl8(v: int, r: int) -> int:
    r &= 7
    return ((v << r) | (v >> (8 - r))) & 0xFF if r else v & 0xFF


class _Xorshift64:
    """Must stay identical to the C++ side in AssetObfuscation.cpp."""

    def __init__(self, seed: int):
        self.s = seed & 0xFFFFFFFFFFFFFFFF or 0x9E3779B97F4A7C15

    def next_byte(self) -> int:
        s = self.s
        s ^= (s << 13) & 0xFFFFFFFFFFFFFFFF
        s ^= s >> 7
        s ^= (s << 17) & 0xFFFFFFFFFFFFFFFF
        self.s = s & 0xFFFFFFFFFFFFFFFF
        return self.s & 0xFF


def _obfuscate(data: bytes, key: bytes) -> bytes:
    """deflate (zlib format) -> keystream cipher. Reversed by
    kronos::deobfuscateAsset() in src/kronos/AssetObfuscation.cpp -- keep the
    two in lockstep."""
    comp = zlib.compress(data, 9)
    seed = int.from_bytes(key[0:8], "little")
    prng = _Xorshift64(seed)
    out = bytearray(len(comp))
    for i, b in enumerate(comp):
        ks = (key[i % 32] + i * 0x9E) & 0xFF
        ks = _rotl8(ks, key[(i * 7 + 3) % 32] & 7)
        ks ^= prng.next_byte()
        out[i] = _rotl8(b ^ ks, i & 7)
    return bytes(out)


def main() -> int:
    args = list(sys.argv[1:])
    obfuscate = False
    key = b""

    if "--obfuscate" in args:
        obfuscate = True
        args.remove("--obfuscate")
    if "--key" in args:
        ki = args.index("--key")
        with open(args[ki + 1], "rb") as f:
            key = f.read()
        del args[ki : ki + 2]

    if len(args) != 4:
        print(
            f"usage: {sys.argv[0]} <source_dir> <output_header> <output_source> "
            f"<namespace> [--obfuscate --key <keyfile>]",
            file=sys.stderr,
        )
        return 1
    if obfuscate and len(key) != 32:
        print(f"--obfuscate needs a 32-byte --key file (got {len(key)} bytes)", file=sys.stderr)
        return 1

    frontend_dir, out_header, out_source, namespace = args

    entries = []
    skipped = 0
    for root, _dirs, files in os.walk(frontend_dir):
        for name in sorted(files):
            if _is_dev_only(name):
                skipped += 1
                continue
            abs_path = os.path.join(root, name)
            rel_path = "/" + os.path.relpath(abs_path, frontend_dir).replace(os.sep, "/")
            entries.append(rel_path)
    entries.sort()
    if skipped:
        print(f"embed_resources: skipped {skipped} dev-only file(s) (tests/generators/fixtures)")

    os.makedirs(os.path.dirname(out_header), exist_ok=True)

    with open(out_header, "w") as h:
        h.write("#pragma once\n\n")
        h.write("#include <cstddef>\n#include <vector>\n\n")
        h.write(f"namespace {namespace} {{\n\n")
        h.write("struct EmbeddedFile {\n")
        h.write("    const char* path;\n")
        h.write("    const unsigned char* data;\n")
        h.write("    std::size_t size;\n")
        h.write("};\n\n")
        h.write("const std::vector<EmbeddedFile>& getEmbeddedFiles();\n\n")
        h.write(f"}}  // namespace {namespace}\n")

    with open(out_source, "w") as s:
        header_include = os.path.basename(out_header)
        s.write(f'#include "{header_include}"\n\n')
        if obfuscate:
            s.write("// Asset bytes below are OBFUSCATED (deflate + keystream cipher, see\n")
            s.write("// tools/embed_resources.py). Reversed at load time by\n")
            s.write("// kronos::deobfuscateAsset() -- src/kronos/AssetObfuscation.cpp.\n\n")
        s.write(f"namespace {namespace} {{\n\n")
        s.write("namespace {\n\n")

        var_names = {}
        for rel_path in entries:
            var_name = "kData_" + sanitize(rel_path)
            var_names[rel_path] = var_name
            with open(os.path.join(frontend_dir, rel_path.lstrip("/")), "rb") as f:
                data = f.read()
            if obfuscate:
                data = _obfuscate(data, key)

            s.write(f"const unsigned char {var_name}[] = {{\n")
            for i in range(0, len(data), 20):
                chunk = data[i:i + 20]
                s.write("    " + ",".join(f"0x{b:02x}" for b in chunk) + ",\n")
            s.write("};\n\n")

        s.write("}  // namespace\n\n")
        s.write("const std::vector<EmbeddedFile>& getEmbeddedFiles() {\n")
        s.write("    static const std::vector<EmbeddedFile> files = {\n")
        for rel_path in entries:
            var_name = var_names[rel_path]
            s.write(f'        {{ "{rel_path}", {var_name}, sizeof({var_name}) }},\n')
        s.write("    };\n")
        s.write("    return files;\n")
        s.write("}\n\n")
        s.write(f"}}  // namespace {namespace}\n")

    kind = "obfuscated" if obfuscate else "plain"
    print(f"Embedded {len(entries)} {kind} file(s) from {frontend_dir} into {out_source}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
