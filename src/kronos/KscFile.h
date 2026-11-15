#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kronos {

// Parser for Korg's plain-text `.KSC` sample-bank scripts -- the first
// building block of STATE.md's KSC plan (step 1). Text only: no `.KMP`/`.KSF`
// bytes are read here, and nothing is ever written back (the plan is
// permanently read-only). Layout and every claim below: format doc §5.9.
//
// Every file starts with `#KORG Script Version 1.0`; three kinds are seen in
// real files, told apart by their lines:
//   - LoadScript (`X.KSC`): `#uuid:<uuid>`, the bank's `.KMP` files one per
//     line (line order = MS Number), its loose `.KSF` files, then one
//     `#>User.0.2.<file>` line per listed file (accepted, not decoded).
//   - Manifest (`X_UserBank.KSC`): `#>>uuid:<uuid>.MS<n>.1.0.<name>` /
//     `.DS<n>.1.0.<name>`, footer `#>uuid:<uuid>.<msSlots>.<dsSlots>.<name>`.
//   - FullBackup (`PRELOAD.KSC`): `#>EXS<e>.MS<n>.1.0.<name>` / `.DS<n>...`,
//     footer `#>Bank<e>.<msSlots>.<dsSlots>.<name>` per EXs library. No
//     UUIDs: which legacy UUID bank number `EXS<e>` stands for is not proven
//     (e+2 fits five libraries, §5.9), so only `exsIndex` is kept.
enum class KscKind { Unknown, LoadScript, Manifest, FullBackup };

struct KscBank {
    // Bank UUID as stored in the KSC, byte 15 bit 0 (the mono/stereo flag,
    // §5.8) cleared. A Program zone referencing a stereo multisample has that
    // bit SET, so callers must clear it on the Program side too before
    // comparing. All-zero with hasUuid=false for a FullBackup library.
    std::array<uint8_t, 16> uuid{};
    bool hasUuid = false;
    int exsIndex = -1;  // FullBackup only: the `<e>` of `EXS<e>` / `Bank<e>`
    std::string name;   // footer name ("SGC SAMPLES", "EXs3 Brass & Woodwinds"); empty without a footer
    // Footer counts -- number RANGES (highest index + 1), not line counts:
    // real manifests have gaps. -1 without a footer.
    int msSlots = -1;
    int dsSlots = -1;
    std::map<int, std::string> multisamples;  // MS Number -> name (Manifest/FullBackup)
    std::map<int, std::string> samples;       // DS Number -> name (Manifest/FullBackup)
    std::vector<std::string> multisampleFiles;  // LoadScript: `.KMP` files, index = MS Number
    std::vector<std::string> sampleFiles;       // LoadScript: loose `.KSF` files
};

struct KscFile {
    KscKind kind = KscKind::Unknown;  // Unknown also when the header line is missing
    std::vector<KscBank> banks;       // file order; one for LoadScript/Manifest seen so far
    int unrecognizedLines = 0;        // non-empty lines none of the shapes above matched
};

// Never throws: a line that doesn't match is counted in unrecognizedLines
// and skipped. Accepts LF and CRLF line ends. Names keep their inner spaces
// (`"Rosanna Hit           -L"`); only trailing spaces are trimmed.
KscFile parseKsc(const std::string& text);

// `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx` (case-insensitive) -> 16 bytes, as
// written (no bit masking). False on any other shape.
bool parseUuid(const std::string& text, std::array<uint8_t, 16>& out);

}  // namespace kronos
