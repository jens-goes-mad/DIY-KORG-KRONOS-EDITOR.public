#include "kronos/AssetObfuscation.h"

#include <istream>
#include <memory>
#include <sstream>
#include <stdexcept>

#include "choc/containers/choc_zlib.h"

namespace kronos {

namespace {

// All three must stay byte-for-byte identical to tools/embed_resources.py's
// _rotl8 / _Xorshift64 / _obfuscate -- the Python side is the encoder, this
// is the decoder, and they share nothing but this hand-kept contract.

uint8_t rotl8(uint8_t v, unsigned r) {
    r &= 7;
    return r ? static_cast<uint8_t>((v << r) | (v >> (8 - r))) : v;
}

uint8_t rotr8(uint8_t v, unsigned r) {
    r &= 7;
    return r ? static_cast<uint8_t>((v >> r) | (v << (8 - r))) : v;
}

struct Xorshift64 {
    uint64_t s;
    explicit Xorshift64(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint8_t nextByte() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<uint8_t>(s & 0xFF);
    }
};

}  // namespace

std::vector<uint8_t> deobfuscateAssetWithKey(const uint8_t* data, std::size_t size, const uint8_t key[32]) {
    // Undo the keystream cipher: recover the deflate stream.
    uint64_t seed = 0;
    for (int i = 0; i < 8; ++i) seed |= static_cast<uint64_t>(key[i]) << (8 * i);
    Xorshift64 prng(seed);

    std::string deflated;
    deflated.resize(size);
    for (std::size_t i = 0; i < size; ++i) {
        uint8_t ks = static_cast<uint8_t>((key[i % 32] + i * 0x9E) & 0xFF);
        ks = rotl8(ks, key[(i * 7 + 3) % 32] & 7);
        ks = static_cast<uint8_t>(ks ^ prng.nextByte());
        deflated[i] = static_cast<char>(rotr8(data[i], static_cast<unsigned>(i & 7)) ^ ks);
    }

    // Inflate (zlib format, matching Python's zlib.compress()).
    try {
        auto source = std::make_shared<std::istringstream>(deflated, std::ios::binary);
        choc::zlib::InflaterStream inflater(source, choc::zlib::InflaterStream::FormatType::zlib);

        std::vector<uint8_t> out;
        char buffer[16384];
        while (inflater.read(buffer, sizeof(buffer)) || inflater.gcount() > 0) {
            out.insert(out.end(), buffer, buffer + inflater.gcount());
            if (inflater.eof()) break;
        }
        if (out.empty() && size > 0) throw std::runtime_error("empty inflate result");
        return out;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("deobfuscateAsset: ") + e.what());
    }
}

}  // namespace kronos
