#include "CombiDecoder.h"

namespace kronos {

// A Combi's 16 Timbres each reference a Program at a fixed 188-byte stride
// starting 4806 bytes into the Combi's own record, byte 0 = number, byte 1
// = raw bank code. Confirmed with real Combis (known Timbre->Program
// assignments) diffed against -- see docs/content/format/index.md's "Combi
// Timbre references" section. File-scope (not anonymous-namespace) since
// timbreByteOffset() below needs the same values decodeCombiFields() uses
// -- one definition, not two copies to keep in sync.
constexpr size_t kTimbreBaseOffset = 4806;
constexpr size_t kTimbreStride = 188;
constexpr int kTimbreCount = 16;

namespace {

// Same 24-byte-field-4-bytes-in shape as every other bank record type in
// this format (Program, and the rest of Combi's own record beyond just the
// name) -- space/NUL-padded, NOT NUL-terminated, so a full-length
// 24-character name has no terminator at all and trailing NUL/space must
// be trimmed rather than scanned-for. See docs/content/format/index.md §5.
constexpr size_t kNameOffset = 4;
constexpr size_t kNameLength = 24;

// Top 3 bits of the status byte (offset+2 within a Timbre block) -- see
// TimbreStatus's doc comment in PcgFile.h. The lower 5 bits are a separate,
// unrelated field (the Timbre's own 0-based index) and are ignored here.
TimbreStatus decodeTimbreStatus(uint8_t statusByte) {
    switch ((statusByte >> 5) & 0x07) {
        case 0: return TimbreStatus::Off;
        case 1: return TimbreStatus::Internal;
        case 3: return TimbreStatus::External;
        case 4: return TimbreStatus::Ex2;
        default: return TimbreStatus::Unknown;
    }
}

}  // namespace

CombiFields decodeCombiFields(const uint8_t* record, size_t recordSize, int bank, int number) {
    CombiFields fields;
    fields.bank = bank;
    fields.number = number;

    if (kNameOffset + kNameLength <= recordSize) {
        size_t len = kNameLength;
        while (len > 0) {
            uint8_t c = record[kNameOffset + len - 1];
            if (c != 0 && c != ' ') break;
            --len;
        }
        fields.name = std::string(reinterpret_cast<const char*>(record + kNameOffset), len);
    }

    fields.timbres.reserve(kTimbreCount);
    for (int i = 0; i < kTimbreCount; ++i) {
        size_t h = kTimbreBaseOffset + static_cast<size_t>(i) * kTimbreStride;
        TimbreRef ref;
        if (h + 2 < recordSize) {
            ref.number = record[h];
            ref.rawBankCode = record[h + 1];
            ref.status = decodeTimbreStatus(record[h + 2]);
            ref.isDefault = (ref.number == 0 && ref.rawBankCode == 0);
        }
        fields.timbres.push_back(ref);
    }
    return fields;
}

size_t timbreByteOffset(int timbreIndex) {
    return kTimbreBaseOffset + static_cast<size_t>(timbreIndex) * kTimbreStride;
}

void writeTimbreProgramRef(uint8_t* record, size_t recordSize, int timbreIndex, int number, int rawBankCode) {
    if (timbreIndex < 0 || timbreIndex >= kTimbreCount) return;
    size_t h = timbreByteOffset(timbreIndex);
    if (h + 2 >= recordSize) return;
    record[h] = static_cast<uint8_t>(number);
    record[h + 1] = static_cast<uint8_t>(rawBankCode);
}

}  // namespace kronos
