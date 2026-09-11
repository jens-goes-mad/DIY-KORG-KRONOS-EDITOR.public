#include "kronos/AssetObfuscation.h"

#include <cstdint>

#include "generated/AssetKey.h"

// The compiled-in-key half of AssetObfuscation. Split out from
// AssetObfuscation.cpp so that file stays testable without the generated
// header (which only exists once CMake's gen_asset_key.py custom command has
// run -- Release/EDITOR_EMBED_RESOURCES builds only). Only ever linked into
// kronos_editor.

namespace kronos {

namespace {

// Must match mask_byte() in tools/gen_asset_key.py exactly.
uint8_t maskByte(unsigned seed, unsigned p) {
    return static_cast<uint8_t>(((seed >> ((p & 3) * 8)) & 0xFF) ^ ((p * 0x2B) & 0xFF) ^ 0x5C);
}

void reconstructKey(uint8_t out[32]) {
    using namespace kronos::asset_key_detail;
    const unsigned char* arrays[4] = {kLayoutTableA, kLayoutTableB, kPadRunLengths, kGlyphAdvance};
    for (unsigned p = 0; p < 32; ++p)
        out[p] = static_cast<uint8_t>(arrays[p % 4][p / 4] ^ maskByte(kAdvanceSeed, p));
}

}  // namespace

std::vector<uint8_t> deobfuscateAsset(const uint8_t* data, std::size_t size) {
    uint8_t key[32];
    reconstructKey(key);
    return deobfuscateAssetWithKey(data, size, key);
}

}  // namespace kronos
