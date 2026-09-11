#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kronos {

// Reverses the transform tools/embed_resources.py applies to embedded
// frontend assets under --obfuscate: a keystream cipher on top of a
// deflate-compressed payload. Used by the Release/hardened build's resource
// fetch path (src/main.cpp's loadFrontendResource(), and the private
// module's own EmbeddedSgx2Assets table) so `strings`/a text editor on the
// shipped binary reveal nothing readable.
//
// This is OBFUSCATION, not encryption. The key is compiled into the binary
// (generated/AssetKey.h, from tools/gen_asset_key.py) because the app has to
// decode its own assets with no user input -- a determined reverse-engineer
// can always recover it. The goal is to turn "pull the frontend out in
// seconds" into real, deliberate effort.
//
// Throws std::runtime_error if the input isn't a well-formed obfuscated
// asset (wrong key, truncated, or not obfuscated at all).
//
// deobfuscateAsset() uses the key compiled into this binary (generated/
// AssetKey.h, reassembled in AssetKey.cpp -- only linked into kronos_editor).
// deobfuscateAssetWithKey() takes an explicit 32-byte key and is the pure,
// dependency-light half (AssetObfuscation.cpp), so a test target can
// round-trip it against a fixed key without the generated header.
std::vector<uint8_t> deobfuscateAsset(const uint8_t* data, std::size_t size);
std::vector<uint8_t> deobfuscateAssetWithKey(const uint8_t* data, std::size_t size, const uint8_t key[32]);

}  // namespace kronos
