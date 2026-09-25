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
