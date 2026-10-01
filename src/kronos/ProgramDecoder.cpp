#include "ProgramDecoder.h"

namespace kronos {

namespace {

// Same 24-byte-field-4-bytes-in shape as every other bank record type in
// this format (Combi, and the rest of Program's own record beyond just
// the name) -- space/NUL-padded, NOT NUL-terminated, so a full-length
// 24-character name has no terminator at all and trailing NUL/space must
// be trimmed rather than scanned-for. See docs/content/format/index.md §5.
constexpr size_t kNameOffset = 4;
constexpr size_t kNameLength = 24;

// See ProgramFields::exiAlgorithmType's own doc comment for the full
// derivation (Prog_EXi_Common.txt's SysEx offset 2857 + this format's
// confirmed 4-byte marker shift, verified against two real templates).
constexpr size_t kExiAlgorithmTypeOffset = 2861;

}  // namespace

ProgramFields decodeProgramFields(const uint8_t* record, size_t recordSize, int bank, int number) {
    ProgramFields fields;
    fields.bank = bank;
    fields.number = number;

    if (kExiAlgorithmTypeOffset < recordSize) fields.exiAlgorithmType = record[kExiAlgorithmTypeOffset];

    if (kNameOffset + kNameLength > recordSize) return fields;  // leaves name empty

    size_t len = kNameLength;
    while (len > 0) {
        uint8_t c = record[kNameOffset + len - 1];
        if (c != 0 && c != ' ') break;
        --len;
    }
    fields.name = std::string(reinterpret_cast<const char*>(record + kNameOffset), len);
    return fields;
}

// Standard FNV-1a 64-bit. Collisions between genuinely different records
// are astronomically unlikely at the ~2500-record scale these files run,
// so a hash match is trusted directly without a follow-up byte-compare.
uint64_t hashProgramRecord(const uint8_t* record, size_t recordSize) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < recordSize; ++i) {
        hash ^= record[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// Same FNV-1a as hashProgramRecord(), skipping the bytes whose value depends
// on WHERE a Program sits or which instrument saved it rather than on the
// sound (2026-09-26, checked against real backups -- K1_20260418.PCG vs
// INIT.PCG: the same Program at another slot differed in exactly these bytes
// and nothing else in 1,244 of 1,711 same-name pairs):
//  - bytes 0-3, the record header: slot 0 of a bank carries bank-level
//    metadata there (0000 0000, 0000 0001, 0000 0002 ... counting up by bank),
//    other slots hold stale leftover bytes. Never sound data.
//  - bytes 2692-2693, "Drum Track > Program Number/Bank": a REFERENCE to
//    another Program slot (Prog_HD-1.txt / Prog_EXi_Common.txt offsets
//    2688/2689, + this format's +4 shift), which differs whenever two
//    instruments lay their Programs out differently.
// `ignoreName` additionally skips the 24-byte name field (bytes 4..27).
uint64_t hashProgramRecordForComparison(const uint8_t* record, size_t recordSize, bool ignoreName) {
    constexpr size_t kHeaderBytes = 4;
    constexpr size_t kDrumTrackRefOffset = 2692;  // 2 bytes: Program Number, Program Bank
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < recordSize; ++i) {
        if (i < kHeaderBytes) continue;
        if (i == kDrumTrackRefOffset || i == kDrumTrackRefOffset + 1) continue;
        if (ignoreName && i >= kNameOffset && i < kNameOffset + kNameLength) continue;
        hash ^= record[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

MultisampleBankKind classifyMultisampleBankUuid(const uint8_t* uuid16) {
    bool zeroPrefix = true;
    for (int i = 0; i < 15; ++i) zeroPrefix = zeroPrefix && uuid16[i] == 0;
    if (zeroPrefix && uuid16[15] <= 1) return MultisampleBankKind::Invalid;

    static const uint8_t kLegacyPrefix[15] = {'K', 'O', 'R', 'G', 0, 0, 0, 0, 0, 0, 0, 0, 'M', 'S', 0};
    for (int i = 0; i < 15; ++i)
        if (uuid16[i] != kLegacyPrefix[i]) return MultisampleBankKind::Generated;
    int legacyBank = uuid16[15] >> 1;  // bit 0 is the mono/stereo flag
    if (legacyBank == 0) return MultisampleBankKind::Rom;
    if (legacyBank == 1) return MultisampleBankKind::OldRam;
    return MultisampleBankKind::Exs;
}

// See the header's doc comment for where every offset/value comes from.
Hd1SampleSources hd1PlayedSampleSources(const uint8_t* record, size_t recordSize) {
    constexpr size_t kOscillatorModeOffset = 2562;
    constexpr size_t kZoneOffsets[2] = {2778, 3244};  // OSC1, OSC2
    constexpr size_t kZoneStride = 22;
    constexpr int kZonesPerOscillator = 8;
    constexpr int kModeSingle = 0, kModeDouble = 1;
    constexpr int kMsTypeMultisample = 1;

    Hd1SampleSources sources;
    if (kZoneOffsets[1] + kZonesPerOscillator * kZoneStride > recordSize) return sources;
    int mode = record[kOscillatorModeOffset] & 0x07;
    int oscillators = mode == kModeSingle ? 1 : mode == kModeDouble ? 2 : 0;
    for (int osc = 0; osc < oscillators; ++osc) {
        for (int z = 0; z < kZonesPerOscillator; ++z) {
            const uint8_t* zone = record + kZoneOffsets[osc] + z * kZoneStride;
            if ((zone[0] & 0x03) != kMsTypeMultisample) continue;
            switch (classifyMultisampleBankUuid(zone + 1)) {
                case MultisampleBankKind::Generated:
                case MultisampleBankKind::OldRam: sources.user = true; break;
                case MultisampleBankKind::Exs: sources.exs = true; break;
                default: break;
            }
        }
    }
    return sources;
}

ProgramInfo decodeProgramInfo(const uint8_t* record, size_t recordSize, int bank, int number, ProgramBankType bankType) {
    ProgramFields fields = decodeProgramFields(record, recordSize, bank, number);
    ProgramInfo info;
    info.bank = fields.bank;
    info.number = fields.number;
    info.name = fields.name;
    info.contentHash = hashProgramRecord(record, recordSize);
    info.bankType = bankType;
    info.exiAlgorithmType = fields.exiAlgorithmType;
    if (bankType == ProgramBankType::Hd1) {
        const Hd1SampleSources sources = hd1PlayedSampleSources(record, recordSize);
        info.usesUserSamples = sources.user;
        info.usesExsSamples = sources.exs;
    }
    return info;
}

// Expected per-record stride for each bank type. CORRECTED 2026-08-13
// (docs/content/format/index.md §5.5): this used to claim EXi records are
// 3706 bytes (docs/external/README.md's Synthify-Kronos-PCG-File-
// Structures.xlsx) -- that figure was never actually checked against real
// bytes. Confirmed directly against two independent real backup files
// (programBankInfo() over both): every one of the 20 PRG1 sub-banks, HD-1
// or EXi alike, uses 4960-byte records -- also exactly what Korg's own
// Prog_EXi_Common.txt independently states ("EXi Program Size: 4960
// byte"), a third, unrelated confirmation. Used only as a cross-check
// against the chunk tag, never as the primary signal -- this project's
// parser always reads the real per-bank value from the file (see
// PcgFile.cpp) rather than hardcoding either number as authoritative, so a
// genuine future stride difference would still be caught, not masked.
constexpr uint32_t kHd1ProgramRecordSize = 4960;
constexpr uint32_t kExiProgramRecordSize = 4960;

ProgramBankTypeResult classifyProgramBankType(const std::string& chunkTag, uint32_t bytesPerRecord) {
    ProgramBankTypeResult result;
    result.type = (chunkTag == "MBK1") ? ProgramBankType::Exi : ProgramBankType::Hd1;
    uint32_t expected = (result.type == ProgramBankType::Exi) ? kExiProgramRecordSize : kHd1ProgramRecordSize;
    result.tagMatchesStride = (bytesPerRecord == expected);
    return result;
}

}  // namespace kronos
