#pragma once

#include <cstddef>
#include <cstdint>

namespace kronos {

// KARMA GE references -- see docs/content/format/index.md §6.4. Shared by
// ProgramDecoder (one KARMA module) and CombiDecoder (modules A-D): a
// Program's single module sits exactly where a Combi's Module A does.
//
// Korg's tables (Prog_HD-1.txt / Prog_EXi_Common.txt /
// CombiAndSongTimbreSet.txt) put Module A's GE Number at SysEx offset 1814
// and its "Run GE" (bit 5) at 2108; Modules B-D follow at a 744-byte stride
// (2558/3302/4046). File offset = SysEx offset + 4, the same shift as the
// Combi Timbres. Checked against real bytes (2026-11-15): read
// little-endian, every GE Number in four real files stays within Korg's
// 0000~0DFF; and "Run GE", fixed On for Programs per Korg's table, is set in
// all 6,656 Programs of three real files.
constexpr size_t kKarmaGeNumberOffset = 1814 + 4;
constexpr size_t kKarmaModuleStride = 744;
constexpr size_t kKarmaRunGeRelativeOffset = 2108 - 1814;
constexpr uint8_t kKarmaRunGeBit = 0x20;
constexpr int kKarmaCombiModuleCount = 4;

// GE Number of KARMA module `module` (0 = A): 0-2047 one of Korg's preset
// GEs, 2048 + k slot k of User GE bank U-A (from a .KGE file). -1 if the
// record is too short.
inline int decodeKarmaGeNumber(const uint8_t* record, size_t recordSize, int module) {
    const size_t off = kKarmaGeNumberOffset + static_cast<size_t>(module) * kKarmaModuleStride;
    if (off + 2 > recordSize) return -1;
    return record[off] | (record[off + 1] << 8);
}

// "Run GE" of KARMA module `module` -- for a Combi, whether that module plays
// at all (Korg's Parameter Guide: off = muted, stored with the Combination).
// Always set for a Program. false if the record is too short.
inline bool decodeKarmaRunGe(const uint8_t* record, size_t recordSize, int module) {
    const size_t off = kKarmaGeNumberOffset + static_cast<size_t>(module) * kKarmaModuleStride +
                       kKarmaRunGeRelativeOffset;
    return off < recordSize && (record[off] & kKarmaRunGeBit) != 0;
}

}  // namespace kronos
