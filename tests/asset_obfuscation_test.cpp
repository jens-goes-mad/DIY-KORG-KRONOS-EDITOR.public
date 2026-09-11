// Round-trip test for kronos::deobfuscateAssetWithKey() -- the decoder half
// of the embedded-frontend obfuscation (see src/kronos/AssetObfuscation.cpp
// and tools/embed_resources.py --obfuscate). The fixture
// (tests/fixtures/asset_obfuscation.{key,plain,obf}) is produced by
// tests/fixtures/gen_asset_obfuscation_fixture.py running the SAME Python
// _obfuscate() the real build uses, with a fixed key -- so this fails if the
// Python encoder and this C++ decoder ever drift apart.
//
// Hand-rolled assertions, no framework -- matching pcg_file_test.cpp.

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "kronos/AssetObfuscation.h"

namespace {

int g_failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                      \
        if (!(expr)) {                                                        \
            ++g_failures;                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        }                                                                     \
    } while (0)

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "FAIL: cannot open %s\n", path.c_str());
        ++g_failures;
        return {};
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    const std::string dir = ASSET_OBFUSCATION_FIXTURE_DIR "/";
    const auto key = readFile(dir + "asset_obfuscation.key");
    const auto obf = readFile(dir + "asset_obfuscation.obf");
    const auto plain = readFile(dir + "asset_obfuscation.plain");

    CHECK(key.size() == 32);
    CHECK(!obf.empty());
    CHECK(!plain.empty());
    // Obfuscation must not be identity -- the whole point is that the shipped
    // bytes don't contain the source.
    CHECK(obf != plain);

    if (g_failures == 0) {
        const auto decoded = kronos::deobfuscateAssetWithKey(obf.data(), obf.size(), key.data());
        CHECK(decoded == plain);
    }

    if (g_failures == 0) {
        std::puts("asset_obfuscation_test: OK");
        return 0;
    }
    std::fprintf(stderr, "asset_obfuscation_test: %d failure(s)\n", g_failures);
    return 1;
}
