#!/usr/bin/env python3
"""Regenerates tests/fixtures/asset_obfuscation.{key,plain,obf} -- the
round-trip fixture for asset_obfuscation_test.cpp.

Run from the repo root:  python3 tests/fixtures/gen_asset_obfuscation_fixture.py

The key here is FIXED (not random) on purpose: the committed .obf must stay
reproducible so the C++ test can assert an exact decode. This exercises the
same _obfuscate() the real build uses, so if the Python encoder and the C++
decoder ever drift apart, the test fails.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))

from embed_resources import _obfuscate  # noqa: E402

KEY = bytes((i * 7 + 3) & 0xFF for i in range(32))

PLAIN = (
    b"// a representative frontend asset\n"
    b"export function decode(bytes) {\n"
    b"  return bytes.map((b, i) => b ^ (i & 0xff));\n"
    b"}\n"
    + b"const FILLER = '" + (b"kronos-" * 400) + b"';\n"
)


def main() -> int:
    with open(os.path.join(HERE, "asset_obfuscation.key"), "wb") as f:
        f.write(KEY)
    with open(os.path.join(HERE, "asset_obfuscation.plain"), "wb") as f:
        f.write(PLAIN)
    with open(os.path.join(HERE, "asset_obfuscation.obf"), "wb") as f:
        f.write(_obfuscate(PLAIN, KEY))
    print("wrote asset_obfuscation.{key,plain,obf}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
