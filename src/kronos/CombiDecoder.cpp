#include "CombiDecoder.h"

#include <algorithm>

namespace kronos {

// Byte ranges/offsets below are all CONFIRMED against docs/external/KORG/
// CombiAndSongTimbreSet.txt's own SysEx-offset table (declared "Combination
// Size: 7810 byte") via the same "+4 byte shift" rule kTimbreBaseOffset
// above already independently confirms twice (see decodeCombiMasterVolume()'s
// own doc comment in CombiDecoder.h) -- every constant here is that table's
// own SysEx offset plus 4, not a fresh guess. Stride/boundary math (not
// individually re-derived per slot):
//   Insert Effect1 starts at SysEx 88, Master Effect1 at 976 -- 12 equal
//   slots means (976-88)/12 = 74 bytes/slot exactly.
//   Master Effect2 starts at 1044 (976+68); Total Effect2 starts at 1184
//   (1116+68) -- both confirm the same 68-byte MFX/TFX slot stride.
//   Master Effect's own shared Return1/Return2/Chain Direction/Chain
//   Switch/Chain Level bytes (1048-1051) sit right after Master Effect1+2,
//   before a 64-byte reserved gap (1052-1115, all rows marked `*` --
//   unknown/unused) that precedes Total Effect1.
constexpr size_t kIfxBase = 88 + 4;
constexpr size_t kIfxStride = 74;
constexpr int kIfxCount = 12;
constexpr size_t kMfxRangeStart = 976 + 4;
constexpr size_t kMfxRangeEnd = 1051 + 4;    // inclusive
constexpr size_t kTfxRangeStart = 1116 + 4;
constexpr size_t kTfxRangeEnd = 1187 + 4;    // inclusive -- Master Volume (1188+4) is separate, see below
constexpr size_t kMasterVolumeOffset = 1188 + 4;
// "(Track EQ) Trim"/"Bypass" (shared byte, SysEx 4848), "Mid Frequency"
// (4850), "Low Gain" (4851), "Mid Gain" (4852), "High Gain" (4853) --
// relative to EACH Timbre's own base (Timbre1's SysEx start is 4802, so
// these are offsets 46/48/49/50/51 within that Timbre's 188-byte block;
// SysEx 4849 is a reserved gap byte, skipped). Added to timbreByteOffset(i)
// below, which already bakes in the confirmed +4 shift via
// kTimbreBaseOffset, so no separate +4 needed on these relative values.
constexpr size_t kTimbreEqRelativeOffsets[] = {46, 48, 49, 50, 51};
// "Volume" (SysEx 4807, relative offset 5) -- CONFIRMED directly against
// two real Kronos backups (see decodeCombiTimbreVolume()'s own doc comment
// in CombiDecoder.h), not just via the +4-shift rule alone.
constexpr size_t kTimbreVolumeRelativeOffset = 5;
// Everything else per-Timbre between its own 3-byte reference/status block
// (relative 0-2) and its EQ bytes (relative 46-51 above) -- Bend Range,
// Transpose, Detune, Delay settings, Pan, Send1/2, Drum Kit Patch IFX1-12,
// Bus Select/Rec Bus/Chord/Max-Notes bits, (Filter) transmit toggles --
// pooled into one "Mixer" category rather than named individually (see
// describeCombiDivergence()'s own doc comment in CombiDecoder.h for why).
// Relative 3 = right after the status byte; relative 45 = right before EQ.
// Volume's own relative offset (5) is excluded so a Volume-only change
// isn't ALSO reported as a vaguer "Mixer differs".
constexpr size_t kTimbreMixerRelativeStart = 3;
constexpr size_t kTimbreMixerRelativeEnd = 45;  // inclusive

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

std::string timbreStatusName(TimbreStatus status) {
    switch (status) {
        case TimbreStatus::Off: return "Off";
        case TimbreStatus::Internal: return "Internal";
        case TimbreStatus::External: return "External";
        case TimbreStatus::Ex2: return "Ex2";
        default: return "Unknown";
    }
}

// "<bank name or raw code> <number>", or "(empty)" for an unassigned slot --
// used only by describeCombiDivergence() below, for a human-readable
// Timbre-reference change line.
std::string describeTimbreRef(const TimbreRef& t) {
    if (t.isDefault) return "(empty)";
    std::string bankName = timbreBankName(t.rawBankCode);
    return (bankName.empty() ? ("code " + std::to_string(t.rawBankCode)) : bankName) + " " + std::to_string(t.number);
}

// True if ANY byte in [start, endInclusive] differs between the two
// records (or falls outside either record's own size, treated as "can't
// tell, so don't claim equal" -- a truncated/malformed record shouldn't
// silently under-report a divergence its own missing bytes might hide).
bool combiRangeDiffers(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, size_t start, size_t endInclusive) {
    for (size_t i = start; i <= endInclusive; ++i) {
        if (i >= a.size() || i >= b.size()) return true;
        if (a[i] != b[i]) return true;
    }
    return false;
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

uint64_t hashCombiRecord(const uint8_t* record, size_t recordSize) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < recordSize; ++i) {
        hash ^= record[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
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

int decodeCombiMasterVolume(const uint8_t* record, size_t recordSize) {
    if (kMasterVolumeOffset >= recordSize) return -1;
    return record[kMasterVolumeOffset];
}

int decodeCombiTimbreVolume(const uint8_t* record, size_t recordSize, int timbreIndex) {
    if (timbreIndex < 0 || timbreIndex >= kTimbreCount) return -1;
    const size_t offset = timbreByteOffset(timbreIndex) + kTimbreVolumeRelativeOffset;
    if (offset >= recordSize) return -1;
    return record[offset];
}

std::vector<CombiChange> describeCombiDivergence(const CombiInfo& a, const CombiInfo& b, const std::vector<uint8_t>& recordA,
                                                   const std::vector<uint8_t>& recordB) {
    std::vector<CombiChange> changes;

    // Named, with values -- Timbre reference, status, and Volume. Reference
    // and status are checked INDEPENDENTLY (entry 94 fix -- this used to be
    // an `else if`, so a Timbre whose reference AND status both changed in
    // the same comparison silently reported only the reference, leaving the
    // status change invisible and unresolvable). Each still maps to its own
    // distinct byte range (ref = the 2 number/bankCode bytes; status = the
    // 1 byte right after), so resolving one never touches the other.
    for (size_t i = 0; i < a.timbres.size() && i < b.timbres.size(); ++i) {
        const TimbreRef& ta = a.timbres[i];
        const TimbreRef& tb = b.timbres[i];
        const size_t timbreBase = timbreByteOffset(static_cast<int>(i));
        if (ta.isDefault != tb.isDefault || ta.number != tb.number || ta.rawBankCode != tb.rawBankCode) {
            changes.push_back({"Timbre " + std::to_string(i + 1) + ": " + describeTimbreRef(ta) + " -> " + describeTimbreRef(tb),
                                {{timbreBase, 2}}});
        }
        if (ta.status != tb.status) {
            changes.push_back({"Timbre " + std::to_string(i + 1) + " status: " + timbreStatusName(ta.status) + " -> " +
                                    timbreStatusName(tb.status),
                                {{timbreBase + 2, 1}}});
        }

        const int timbreVolumeA = decodeCombiTimbreVolume(recordA.data(), recordA.size(), static_cast<int>(i));
        const int timbreVolumeB = decodeCombiTimbreVolume(recordB.data(), recordB.size(), static_cast<int>(i));
        if (timbreVolumeA >= 0 && timbreVolumeB >= 0 && timbreVolumeA != timbreVolumeB) {
            changes.push_back({"Timbre " + std::to_string(i + 1) + " Volume " + std::to_string(timbreVolumeA) + " -> " +
                                    std::to_string(timbreVolumeB),
                                {{timbreBase + kTimbreVolumeRelativeOffset, 1}}});
        }
    }

    // Named, with value -- Master Volume.
    const int volumeA = decodeCombiMasterVolume(recordA.data(), recordA.size());
    const int volumeB = decodeCombiMasterVolume(recordB.data(), recordB.size());
    if (volumeA >= 0 && volumeB >= 0 && volumeA != volumeB) {
        changes.push_back({"Master Volume " + std::to_string(volumeA) + " -> " + std::to_string(volumeB),
                            {{kMasterVolumeOffset, 1}}});
    }

    // Coarse, name only -- IFX1..12, MFX, TFX. See describeCombiDivergence()'s
    // own doc comment in CombiDecoder.h for exactly what each covers and why.
    // Each entry's range is the WHOLE declared section (not just whichever
    // byte(s) actually differ within it -- these categories never tracked
    // that, by design).
    for (int slot = 0; slot < kIfxCount; ++slot) {
        const size_t start = kIfxBase + static_cast<size_t>(slot) * kIfxStride;
        if (combiRangeDiffers(recordA, recordB, start, start + kIfxStride - 1)) {
            changes.push_back({"IFX" + std::to_string(slot + 1) + " differs", {{start, kIfxStride}}});
        }
    }
    if (combiRangeDiffers(recordA, recordB, kMfxRangeStart, kMfxRangeEnd)) {
        changes.push_back({"MFX differs", {{kMfxRangeStart, kMfxRangeEnd - kMfxRangeStart + 1}}});
    }
    if (combiRangeDiffers(recordA, recordB, kTfxRangeStart, kTfxRangeEnd)) {
        changes.push_back({"TFX differs", {{kTfxRangeStart, kTfxRangeEnd - kTfxRangeStart + 1}}});
    }

    // Coarse, name only -- EQ, pooled across all 16 Timbres (fires once if
    // ANY Timbre's own Track EQ bytes differ anywhere; range covers ALL 16
    // Timbres' own EQ bytes, not just the one(s) that actually differ).
    {
        bool eqDiffers = false;
        std::vector<CombiChangeRange> eqRanges;
        for (int t = 0; t < kTimbreCount; ++t) {
            const size_t base = timbreByteOffset(t);
            for (size_t rel : kTimbreEqRelativeOffsets) {
                eqRanges.push_back({base + rel, 1});
                if (combiRangeDiffers(recordA, recordB, base + rel, base + rel)) eqDiffers = true;
            }
        }
        if (eqDiffers) changes.push_back({"EQ differs", std::move(eqRanges)});
    }

    // Coarse, name only -- Mixer, pooled across all 16 Timbres (everything
    // between a Timbre's own reference/status block and its EQ bytes,
    // EXCLUDING Volume -- see kTimbreMixerRelativeStart/End's own comment).
    {
        bool mixerDiffers = false;
        std::vector<CombiChangeRange> mixerRanges;
        for (int t = 0; t < kTimbreCount; ++t) {
            const size_t base = timbreByteOffset(t);
            for (size_t rel = kTimbreMixerRelativeStart; rel <= kTimbreMixerRelativeEnd; ++rel) {
                if (rel == kTimbreVolumeRelativeOffset) continue;
                mixerRanges.push_back({base + rel, 1});
                if (combiRangeDiffers(recordA, recordB, base + rel, base + rel)) mixerDiffers = true;
            }
        }
        if (mixerDiffers) changes.push_back({"Mixer differs", std::move(mixerRanges)});
    }

    // Catch-all: the two records ARE known to differ (contentHash), but
    // nothing above caught it -- surface that rather than silently dropping
    // a real divergence just because it isn't categorized yet. Its own
    // range is the ENTIRE record -- the only honest answer when nothing
    // more specific was ever identified; resolving it is a full overwrite.
    if (changes.empty() && a.contentHash != b.contentHash) {
        changes.push_back({"Other section differs", {{0, std::max(recordA.size(), recordB.size())}}});
    }

    return changes;
}

}  // namespace kronos
