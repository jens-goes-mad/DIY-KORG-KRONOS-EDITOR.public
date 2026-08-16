#include "PcgFile.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <optional>
#include <unordered_map>

#include "CombiDecoder.h"
#include "ProgramDecoder.h"

namespace kronos {

namespace {

bool isUpperOrDigit(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// Chunk tags observed in this format (KORG, PCG1, DIV1, SLS1, SLD1, SDB1)
// are all 4 uppercase-alphanumeric characters starting with a letter.
bool looksLikeTag(const uint8_t* p) {
    return p[0] >= 'A' && p[0] <= 'Z' && isUpperOrDigit(p[1]) && isUpperOrDigit(p[2]) && isUpperOrDigit(p[3]);
}

uint32_t readU32BE(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

struct ChunkInfo {
    size_t contentStart = 0;
    size_t contentEnd = 0;
    std::string tag;
};

// Every chunk in this format has a fixed 12-byte header: a 4-char tag, a
// u32be content size, and a 4-byte field of still-unknown purpose ("dwX")
// -- CONFIRMED via docs/external/Synthify-Kronos-PCG-File-Structures.xlsx,
// which independently documents this exact structure ("TAG1, size, dwX,
// Data"). This corrects this project's own earlier model (an AMBIGUOUS
// 4-byte field sometimes preceding the tag, tried at two candidate
// positions) -- the unknown field is not a prefix before the tag at all,
// it's the third word of a fixed header, right after size and right
// before content. See docs/content/format/index.md §1 for the full story of how this
// was found (cross-referencing two independent official/community
// documents against this project's own byte-level findings) and its
// real-world consequence: every top-level chunk's `contentStart` was
// previously computed 4 bytes too early (`p+8`, tag+size only, missing
// dwX), which drifted the whole-file top-level walk (topLevelChunkTags())
// out of sync after the first chunk or two -- see the Internals pane's
// Format Blind Spot notes in STATE.md.
std::optional<ChunkInfo> readChunk(const std::vector<uint8_t>& data, size_t pos, size_t end) {
    if (pos + 12 > end) return std::nullopt;
    if (!looksLikeTag(&data[pos])) return std::nullopt;

    uint32_t size = readU32BE(&data[pos + 4]);
    size_t contentStart = pos + 12;
    size_t contentEnd = contentStart + size;
    if (contentEnd < contentStart || contentEnd > end) return std::nullopt;

    ChunkInfo info;
    info.contentStart = contentStart;
    info.contentEnd = contentEnd;
    info.tag.assign(reinterpret_cast<const char*>(&data[pos]), 4);
    return info;
}

// Depth-first walk collecting every chunk anywhere in the hierarchy whose
// tag satisfies `wanted` (e.g. matches one specific tag, or one of several --
// MBK1/PBK1 Program banks are interleaved in file order under PRG1, so
// collecting "either tag" in one walk is what preserves that order). The
// depth cap is a safety net against pathological/corrupt input, not
// something normal files should hit.
void collectChunks(const std::vector<uint8_t>& data, size_t start, size_t end,
                    const std::function<bool(const std::string&)>& wanted, std::vector<ChunkInfo>& out, int depth) {
    if (depth > 64) return;
    size_t pos = start;
    while (pos + 12 <= end) {
        auto chunk = readChunk(data, pos, end);
        if (!chunk) break;
        if (wanted(chunk->tag)) out.push_back(*chunk);
        collectChunks(data, chunk->contentStart, chunk->contentEnd, wanted, out, depth + 1);
        pos = chunk->contentEnd + (chunk->contentEnd % 2);
    }
}

void collectChunks(const std::vector<uint8_t>& data, size_t start, size_t end, const std::string& wantedTag,
                    std::vector<ChunkInfo>& out, int depth) {
    collectChunks(
        data, start, end, [&wantedTag](const std::string& tag) { return tag == wantedTag; }, out, depth);
}

// A Set List record: 4-byte marker + 24-byte null-padded ASCII name.
constexpr size_t kRecordSize = 28;
constexpr size_t kMarkerSize = 4;

std::string readRecordName(const uint8_t* data, size_t off, size_t end) {
    if (off + kRecordSize > end) return {};
    const uint8_t* nameStart = data + off + kMarkerSize;
    const uint8_t* nameEnd = data + off + kRecordSize;
    const uint8_t* nul = std::find(nameStart, nameEnd, uint8_t{0});
    return std::string(reinterpret_cast<const char*>(nameStart), static_cast<size_t>(nul - nameStart));
}

// SBK1 per-Set-List block: a name/header record (kSbkHeaderSize bytes,
// not re-parsed here -- SDB1 already gave us the name), followed by 128
// song parameter records on a fixed stride. Offsets confirmed by diffing
// setlist_test.PCG and test_1.PCG, files the project owner built
// specifically to isolate one parameter per group of slots -- see
// docs/content/format/index.md's "SBK1" section (§4.3-4.4).
//
// +12 and +17 are each shared by two fields, and +13 by two more --
// Font size and Transpose are packed a few bits at a time into otherwise-
// unrelated bytes (Type+Color, Bank), presumably because this format
// predates spare bytes being cheap. Every mask below exists so reading
// (or writing) one field never touches bits that belong to another.
constexpr size_t kSbkHeaderSize = 40;
constexpr size_t kSbkRecordSize = 542;
constexpr size_t kSbkTypeColorOffset = 12;  // bits0-1: Type (0=Combi/1=Program/2=Song); bits2-5: (color-1); bits6-7: Font size low 2 bits
constexpr uint8_t kSbkTypeMask = 0x03;           // bits 0-1 -- Performance Type, confirmed 2 bits wide via
                                                  // docs/external/KORG/SetList.txt (2026-08-08), not the single
                                                  // bit this project originally assumed -- see docs/content/format/index.md
constexpr uint8_t kSbkTypeColorMask = 0x3F;      // bits 0-5 -- Type+Color's own bits
constexpr uint8_t kSbkFontSizeLowMask = 0xC0;    // bits 6-7 of +12
constexpr size_t kSbkBankOffset = 13;       // bits0-4: bank; bits5-7: Transpose high 3 bits
constexpr uint8_t kSbkBankMask = 0x1F;           // bits 0-4 -- Bank's own bits
constexpr uint8_t kSbkTransposeHighMask = 0xE0;  // bits 5-7 of +13
constexpr size_t kSbkNumberOffset = 14;
constexpr size_t kSbkHoldTimeOffset = 15;  // stored value = Hold Time + 1
constexpr size_t kSbkVolumeOffset = 16;
constexpr size_t kSbkFontTransposeOffset = 17;   // bit4: Font size high bit; bits5-7: Transpose low 3 bits; bit3 and bits0-2 still unexplained, see docs/content/format/index.md
constexpr uint8_t kSbkFontSizeHighMask = 0x10;   // bit 4 of +17
constexpr uint8_t kSbkTransposeLowMask = 0xE0;   // bits 5-7 of +17
constexpr size_t kSbkCommentOffset = 18;
// 512 bytes, confirmed via docs/external/KORG/SetList.txt (2026-08-08) --
// NOT kSbkRecordSize-18(=524), this project's original assumption before
// that source was available. The remaining 12 bytes at the very end of
// each 542-byte song record (530..541) are not comment space -- same
// width as the 12 unexplained bytes at the record's own start (+0..+11),
// a symmetric shape this project doesn't have an explanation for yet.
// Getting this wrong isn't just a decode inaccuracy: the encoder
// (frontend/components/kronos/setlist-editor-comment-and-font.js) writes
// into this same span, so a too-generous bound here risked a long comment overwriting
// whatever those trailing 12 bytes actually are.
constexpr size_t kSbkCommentMaxLength = 512;

// The Comment field can contain embedded \r\n line breaks, so unlike
// readRecordName() this only stops at a genuine NUL byte, not otherwise.
// Scans up to kSbkCommentMaxLength bytes, the largest this field could
// possibly be.
std::string readComment(const uint8_t* data, size_t songOff, size_t end) {
    size_t start = songOff + kSbkCommentOffset;
    size_t recordEnd = songOff + kSbkCommentOffset + kSbkCommentMaxLength;
    if (start >= end) return {};
    const uint8_t* commentStart = data + start;
    const uint8_t* scanEnd = data + std::min(recordEnd, end);
    const uint8_t* nul = std::find(commentStart, scanEnd, uint8_t{0});
    return std::string(reinterpret_cast<const char*>(commentStart), static_cast<size_t>(nul - commentStart));
}

SlotParams readSlotParams(const uint8_t* data, size_t songOff, size_t end) {
    SlotParams params;
    if (songOff + kSbkFontTransposeOffset + 1 > end) return params;  // leaves found=false

    uint8_t typeColor = data[songOff + kSbkTypeColorOffset];
    uint8_t bankByte = data[songOff + kSbkBankOffset];
    uint8_t fontTransposeByte = data[songOff + kSbkFontTransposeOffset];

    // Was `(typeColor & 0x01) != 0` -- only bit 0. Confirmed 2 bits wide
    // (0=Combi/1=Program/2=Song) via docs/external/KORG/SetList.txt
    // (2026-08-08) -- isProgram still only distinguishes "is this a
    // Program slot" (true for exactly type==1), so a Song slot (type==2)
    // isn't separately represented here yet (nothing else in this app
    // handles a third slot type), but the read is now correct rather than
    // accidentally-right: the old bit-0-only check would have also
    // treated the unused/invalid value 3 as a Program.
    params.isProgram = (typeColor & kSbkTypeMask) == 1;
    params.color = ((typeColor & kSbkTypeColorMask) >> 2) + 1;
    params.bank = bankByte & kSbkBankMask;
    params.number = data[songOff + kSbkNumberOffset];
    params.holdTime = static_cast<int>(data[songOff + kSbkHoldTimeOffset]) - 1;
    params.volume = data[songOff + kSbkVolumeOffset];

    // Font size: 3 bits, low 2 in +12's top bits, high 1 in +17 bit 4 --
    // see docs/content/format/index.md §4.4. Enum order (S,XS,M,L,XL) matches this
    // value directly, no further lookup needed.
    int fontSizeValue = ((fontTransposeByte & kSbkFontSizeHighMask) ? 4 : 0) |
                         ((typeColor & 0x80) ? 2 : 0) | ((typeColor & 0x40) ? 1 : 0);
    params.fontSize = static_cast<FontSize>(fontSizeValue);

    // Transpose: 6-bit two's complement, high 3 bits in +13's top bits,
    // low 3 bits in +17's top bits -- see docs/content/format/index.md §4.4.
    int unsigned6 = ((bankByte & kSbkTransposeHighMask) >> 2) | ((fontTransposeByte & kSbkTransposeLowMask) >> 5);
    params.transpose = unsigned6 >= 32 ? unsigned6 - 64 : unsigned6;

    params.found = true;
    return params;
}

}  // namespace

// Confirmed via real Combi samples the project owner provided directly,
// cross-checked against an independent external reference (DaBlick/
// PCG-Tools' "PCG Structure Kronos.txt", see docs/references/) -- both
// sources agree at every point they overlap (INT-A/B/C, and USER-F's code
// independently explaining a byte this project had first read from its own
// sample but the project owner had misremembered the Program number for).
// See docs/content/format/index.md's "Combi Timbre references" section for the full
// derivation. Every entry below is a directly-verified byte value, from one
// source or the other -- not an extrapolation.
//
// `programBankIndex` is this project's own PBK1 file-order Program bank
// index (ProgramInfo::bank, see docs/content/format/index.md §5.2); `rawBankCode` is the
// completely separate number a Combi Timbre slot's own byte actually
// stores (TimbreRef::rawBankCode). The two coincide for INT-A..D (both use
// 0..3) but diverge for every other confirmed bank -- one shared table so
// isConfirmedTimbreProgramBank() and the two Combi-usage functions below
// can't drift out of sync with each other as more codes get confirmed
// later.
//
// NO `name` FIELD, DELIBERATELY (2026-08-11): a bank's display name is a
// pure function of its `programBankIndex` -- `frontend/pane.js`'s
// PROGRAM_BANK_NAMES array is this project's one and only source of truth
// for that (see its own doc comment, and PcgFile.h's note above
// datasetInternals()). This table used to also carry a `name` string
// per entry, redundant with PROGRAM_BANK_NAMES for every index that
// appears in both -- which is exactly how the two drifted out of sync
// this same day: this table's indices got corrected (USER-A/D/F/AA
// 8/11/13/14 -> 6/9/11/13, see git history) without anyone noticing
// PROGRAM_BANK_NAMES still had the old wrong order, since nothing forced
// the two to agree. Removed the field instead of just re-syncing the
// strings, so there is now exactly one place that spells out "index 6 =
// USER-A" -- timbreBankName() below only resolves names that have NO
// confirmed index (kConfirmedTimbreBankNamesOnly); for everything in this
// table, `frontend/library.js`'s formatTimbreRef() looks up
// PROGRAM_BANK_NAMES[programBankIndex] instead of reading a name sent over
// the bridge.
//
// CORRECTED 2026-08-10: USER-A/D/F/AA's programBankIndex values below were
// originally 8/11/13/14 -- an extrapolation from the INT-A..D anchors that
// assumed INT ran A..G (7 banks, indices 0..6) with USER-A starting at
// index 8. Real hardware + a real file both contradicted this: the project
// owner confirmed GM and g(d) are NOT stored PBK1/MBK1 chunks at all (no
// "INT-G" bank exists), and confirmed real Program names at file-order
// index 6 ("Doubled Screamer") and index 9/11/13 ("Vibraphone 2"/"Harmonic
// Bass/Lead"/"The Temple SW1") match USER-A/D/F/AA position 0 on real
// hardware -- see setlist_test_2.PCG, index 6 record 47 = "EXi Overdrive
// Organ", exactly the Program the project owner said Combi U-A 016 Timbre 2
// (raw bank 17, raw number 47) should resolve to (the app had been showing
// "Xfade StagePianoATK Kn5", index 8 record 47, before this fix). The real
// scheme is 6 INT-A..F (index 0..5) + 7 USER-A..G (index 6..12, raw code
// 17..23) + 7 USER-AA..GG (index 13..19, raw code 24..30) = 20 banks total.
//
// ALL 20 PROGRAM BANK INDICES CONFIRMED (2026-08-11): the project owner
// checked position-0 of every single one of the 20 Program banks against
// real hardware at once (see PROGRAM_BANK_NAMES's own doc comment in
// frontend/pane.js and docs/content/format/index.md §5.2), which is what
// caught PROGRAM_BANK_NAMES's OWN copy of this same bug (it still had the
// old 8/11/13/14-style indices, separately from this table -- see
// kConfirmedTimbreBanks's "NO name FIELD" note below for the dedup fix
// that followed). One side effect: every remaining entry that used to sit
// in the now-removed kConfirmedTimbreBankNamesOnly table (INT-F/USER-B/C/
// CC/DD -- confirmed by raw code via a real Combi Timbre check, but
// without a confirmed index) now HAS a confirmed index too, since §5.2's
// full order is known -- promoted here. INT-F=5 was double-checked
// directly: Combi U-A 002 Timbre 2 (raw bank 5, raw number 71) had been
// showing no Program name at all (the exact same "confirmed name, no
// index" bug this promotion fixes) -- index 5/record 71 in
// setlist_test_2.PCG reads "Vokal Dancing", matching what the project
// owner found on the real unit ("Vocal Dancing").
//
// CORRECTED 2026-08-14, RETRACTING a 2026-08-11 misreading: this table
// briefly had `{10, 4}` (claiming raw code 4 = USER-E, "a genuine
// surprise" breaking every other single-letter USER bank's contiguous
// 17-23 block). That was wrong -- the project owner re-checked the exact
// same real Combi (I-A 000 "K-Lab: Katja's House" Timbre 7, raw bytes
// program=61/bank=4, byte-identical to before) and confirmed real hardware
// actually shows `INT-E`, not `USER-E`, for that reference. So there was
// no anomaly at all: `INT-A..F` are simply raw codes 0-5 in order (index
// == code for all six, the same coincidence as `INT-A..D`), exactly what
// the "obvious" extrapolation always said. `USER-E` (raw code 21) is
// confirmed separately, via a different real Combi (I-A 001 "Stradivarius
// Goes POP" Timbre 7, program=73/bank=21) -- and 21 turns out to be
// exactly the "obvious" gap in `USER-A..G`'s 17-23 block after all
// (A=17,B=18,C=19,D=20,E=21,F=22,G=23, fully contiguous, no anomaly there
// either). Left as a methodology note, not scrubbed from history: the
// original "genuine surprise" framing was itself the actual mistake here,
// caught only because the project owner re-verified a specific real
// hardware reading rather than trusting the first transcription -- exactly
// the kind of double-check this project's whole method depends on.
//
// USER-BB (raw code 25) and USER-EE (raw code 28) fit the expected `+11`-
// offset double-letter pattern exactly and were confirmed independently,
// via the same "K-Lab: Katja's House" Combi's Timbre 9 (program=29/bank=25)
// and Combi U-A 014 "KARMA Org 1'2'3  Piano 4" Timbre 7 (program=1/bank=28).
//
// ALL 20 PROGRAM BANK INDICES NOW HAVE A CONFIRMED RAW CODE (2026-08-14):
// USER-FF (index 18) was the last gap -- confirmed via Combi U-A 090 "Days
// like this" Timbre 1/2 (program=87/bank=29, program=85/bank=29 in
// setlist_test_2.PCG), fitting the expected `+11` offset exactly (this
// time actually correct, unlike the INT-E/USER-E episode above). This
// table is now complete: every Program bank has a known raw Combi Timbre
// code, and vice versa.
struct ConfirmedTimbreBank {
    int programBankIndex;
    int rawBankCode;
};
constexpr ConfirmedTimbreBank kConfirmedTimbreBanks[] = {
    {0, 0},   {1, 1},   {2, 2},   {3, 3},   {4, 4},
    {5, 5},   {6, 17},  {7, 18},  {8, 19},  {9, 20},
    {10, 21}, {11, 22}, {12, 23}, {13, 24}, {14, 25},
    {15, 26}, {16, 27}, {17, 28}, {18, 29}, {19, 30},
};

// Was a `name` field, DELIBERATELY REMOVED (2026-08-11): a bank's display
// name is a pure function of its `programBankIndex` --
// `frontend/pane.js`'s PROGRAM_BANK_NAMES array is this project's one and
// only source of truth for that (see its own doc comment, and PcgFile.h's
// note above datasetInternals()). This table used to also carry a `name`
// string per entry, redundant with PROGRAM_BANK_NAMES for every index that
// appears in both -- which is exactly how the two drifted out of sync the
// same day: this table's indices got corrected (USER-A/D/F/AA 8/11/13/14
// -> 6/9/11/13, see git history) without anyone noticing PROGRAM_BANK_NAMES
// still had the old wrong order, since nothing forced the two to agree.
// Removed the field instead of just re-syncing the strings, so there is
// now exactly one place that spells out "index 6 = USER-A" --
// `frontend/library.js`'s formatTimbreRef() looks up
// PROGRAM_BANK_NAMES[programBankIndex] for every entry in this table.
struct ConfirmedTimbreBankName {
    int rawBankCode;
    const char* name;
};
// REINTRODUCED 2026-08-12 -- removed on 2026-08-11 as permanently empty
// (every prior name-only entry had gained a confirmed index once §5.2's
// full order was known), with a note that a code needing this again was
// "unlikely but not structurally impossible." It happened: raw code 6 is
// `GM`, confirmed via a real Combi (U-A 030 "Bad Name" Timbre 2,
// program=91/bank=6 in setlist_test_2.PCG, matching the project owner's
// hardware report exactly). Unlike every previous name-only entry, GM is
// NOT "not yet confirmed" -- it's PERMANENTLY indexless: GM is fixed
// MIDI-spec content, not one of the 20 stored PBK1/MBK1 Program banks at
// all (§5.2/§5.4), so there is no PBK1 file-order index for it to ever
// gain, and it must never move to kConfirmedTimbreBanks above no matter
// how much more evidence accumulates. No Program NAME is shown for a GM
// reference either (formatTimbreRef() in library.js only looks up a name
// via the `programs` array, which has nothing for GM) -- showing one would
// need a hardcoded General MIDI instrument-name table, a real but separate
// feature decision, not implied by just labeling the bank.
//
// G(1)..G(4) (codes 7-10), confirmed 2026-08-12 the same way -- sit right
// after GM (6) as a contiguous block, consistent with the project owner's
// earlier real-hardware note that the Program bank browser shows "GM" then
// "g(d)" right after INT-F (§5.2) -- these are very likely that "g(d)"
// family, though this project doesn't know Korg's own official name/
// meaning for G(1)..G(4) specifically (four separate GM-variant/drum-kit
// banks? not guessing). Same permanently-indexless treatment as GM: no
// Program name, no jump button. The project owner also reported the
// specific Program names found there (program 122/"123": "Rain"/
// "Thunder"/"Wind"/"Stream" for G(1)/G(2)/G(3)/G(4) respectively) --
// useful as verification that these are real, distinct, content-bearing
// banks, but NOT stored anywhere in this codebase (no per-program name
// table exists for these -- same "separate feature decision" as GM's own
// instrument names above).
//
// g(5)/g(6)/g(7)/g(9) (codes 11/12/13/15), confirmed 2026-08-13 the same
// way -- extend the same contiguous block right past G(4), no gap (g(8),
// code 14, hasn't been checked yet -- not assumed just because the rest of
// the run fits). Lowercase here, matching exactly what the project owner
// reported this time (G(1)..G(4) were reported uppercase) -- kept
// verbatim rather than normalized, since neither this project nor the
// project owner has confirmed which casing (if either consistently) the
// real Kronos UI actually uses. The reported Program names ("Bubble"/
// "Seashore"/"Jetplane"/"Polyphonic Synth") continuing right after G(4)'s
// "Stream" in a nature/SFX theme is suggestive of a General MIDI 2 SFX
// Kit note sequence (Rain/Thunder/Wind/Stream/Bubbles/... is a real,
// externally documented GM2 order) -- worth noting, not asserted as
// confirmed without checking that specific external spec directly.
constexpr ConfirmedTimbreBankName kConfirmedTimbreBankNamesOnly[] = {
    {6, "GM"},      // confirmed 2026-08-12 against real hardware -- permanently indexless, see doc comment above
    {7, "G(1)"},    // confirmed 2026-08-12 against real hardware -- permanently indexless, see doc comment above
    {8, "G(2)"},    // confirmed 2026-08-12 against real hardware -- permanently indexless, see doc comment above
    {9, "G(3)"},    // confirmed 2026-08-12 against real hardware -- permanently indexless, see doc comment above
    {10, "G(4)"},   // confirmed 2026-08-12 against real hardware -- permanently indexless, see doc comment above
    {11, "g(5)"},   // confirmed 2026-08-13 against real hardware -- permanently indexless, see doc comment above
    {12, "g(6)"},   // confirmed 2026-08-13 against real hardware -- permanently indexless, see doc comment above
    {13, "g(7)"},   // confirmed 2026-08-13 against real hardware -- permanently indexless, see doc comment above
    {15, "g(9)"},   // confirmed 2026-08-13 against real hardware -- permanently indexless, see doc comment above
};

std::string timbreBankName(int rawBankCode) {
    for (const auto& b : kConfirmedTimbreBankNamesOnly) {
        if (b.rawBankCode == rawBankCode) return b.name;
    }
    return "";
}

bool isConfirmedTimbreProgramBank(int programBank) {
    for (const auto& b : kConfirmedTimbreBanks) {
        if (b.programBankIndex == programBank) return true;
    }
    return false;
}

// Internal linkage -- pure implementation detail of combiUsagesForProgram()/
// combiUsageCounts() below, not declared in PcgFile.h since nothing outside
// this file needs the raw-code translation itself, only its effect.
//
// The confirmed Timbre raw bank code for a PBK1 file-order Program bank
// index, or -1 if that bank isn't independently confirmed yet -- see
// kConfirmedTimbreBanks above.
static int confirmedTimbreCodeForProgramBank(int programBank) {
    for (const auto& b : kConfirmedTimbreBanks) {
        if (b.programBankIndex == programBank) return b.rawBankCode;
    }
    return -1;
}

// Reverse of the above -- the confirmed PBK1 file-order Program bank index
// for a Timbre's raw bank code, or -1 if that code isn't confirmed yet.
static int programBankForConfirmedTimbreCode(int rawBankCode) {
    for (const auto& b : kConfirmedTimbreBanks) {
        if (b.rawBankCode == rawBankCode) return b.programBankIndex;
    }
    return -1;
}

bool PcgFile::load(const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Could not open file: " + path;
        return false;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return loadFromMemory(std::move(data), error);
}

bool PcgFile::save(const std::string& path, std::string& error) {
    if (data_.empty()) {
        error = "No file loaded";
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "Could not open file for writing: " + path;
        return false;
    }

    file.write(reinterpret_cast<const char*>(data_.data()), static_cast<std::streamsize>(data_.size()));
    if (!file) {
        error = "Failed writing to file: " + path;
        return false;
    }

    dirty_ = false;  // now on disk -- see isDirty()
    return true;
}

bool PcgFile::loadFromMemory(std::vector<uint8_t> data, std::string& error) {
    setlists_.clear();
    sdbSongsStart_.clear();
    dirty_ = false;  // a freshly loaded file is never dirty -- see isDirty()

    if (data.size() < 16 || std::memcmp(data.data(), "KORG", 4) != 0) {
        error = "Not a KORG PCG/SNG file (missing 'KORG' magic)";
        return false;
    }

    // SDB1 is optional -- Set Lists are just one of several categories the
    // Kronos's own backup dialog lets you include/exclude, and third-party
    // sound-bank-only PCG distributions routinely omit them entirely.
    // Confirmed against two real donated files (HALEN-SPLIT.PCG, JMJ KRONOS
    // 2.PCG) with zero SLS1/SDB1/SBK1 anywhere in the hierarchy -- just
    // PRG1/CBK1 (one also has WSQ1/DPI1 Drum Sample data), no Set Lists at
    // all. Both failed to load entirely before this fix, since every other
    // real file this project had tested against so far happened to include
    // at least one Set List. No SDB1 chunk just means zero Set Lists to
    // show; the loop below already no-ops on an empty `sdbChunks`, so
    // nothing else needs to change to support this.
    std::vector<ChunkInfo> sdbChunks;
    collectChunks(data, 16, data.size(), "SDB1", sdbChunks, 0);

    // A single SDB1 chunk holds all of the unit's Set Lists (128 on a real
    // Kronos), not just one -- see README.md for how this was derived by
    // searching the file for known song/Set List names.
    for (const auto& sdb : sdbChunks) {
        if (sdb.contentStart + 8 > sdb.contentEnd) continue;

        uint32_t numSetlists = readU32BE(&data[sdb.contentStart]);
        uint32_t bytesPerSetlist = readU32BE(&data[sdb.contentStart + 4]);
        size_t setlistsStart = sdb.contentStart + 8;

        constexpr uint32_t kSongsPerSetlist = 128;  // 1 name record + 128 song records per Set List
        if (bytesPerSetlist != (kSongsPerSetlist + 1) * kRecordSize) continue;  // doesn't match the known layout
        if (setlistsStart + static_cast<size_t>(bytesPerSetlist) * numSetlists > data.size()) continue;

        for (uint32_t s = 0; s < numSetlists; ++s) {
            size_t setlistOff = setlistsStart + static_cast<size_t>(s) * bytesPerSetlist;

            Setlist setlist;
            setlist.index = static_cast<int>(s);
            setlist.name = readRecordName(data.data(), setlistOff, data.size());

            size_t songsStart = setlistOff + kRecordSize;
            for (uint32_t k = 0; k < kSongsPerSetlist; ++k) {
                size_t songOff = songsStart + static_cast<size_t>(k) * kRecordSize;
                Song song;
                song.index = static_cast<int>(k);
                song.name = readRecordName(data.data(), songOff, data.size());
                setlist.songs.push_back(std::move(song));
            }

            setlists_.push_back(std::move(setlist));
            sdbSongsStart_.push_back(songsStart);
        }
    }

    // SBK1 (nested inside SLS1 > STL1) holds the real per-slot parameters --
    // Program/Combi/bank/number/Hold Time/Volume/Color. Optional: if it's
    // missing or doesn't match the known layout, Set List names from SDB1
    // above still work fine, just without these extra fields (params.found
    // stays false). See README.md for how this chunk was found and decoded.
    std::vector<ChunkInfo> sbkChunks;
    collectChunks(data, 16, data.size(), "SBK1", sbkChunks, 0);

    sbkSongsStart_.assign(setlists_.size(), static_cast<size_t>(-1));

    for (const auto& sbk : sbkChunks) {
        if (sbk.contentStart + 8 > sbk.contentEnd) continue;

        uint32_t numSetlists = readU32BE(&data[sbk.contentStart]);
        uint32_t bytesPerSetlist = readU32BE(&data[sbk.contentStart + 4]);
        size_t setlistsStart = sbk.contentStart + 8;

        constexpr uint32_t kSongsPerSetlist = 128;
        if (bytesPerSetlist != kSbkHeaderSize + kSongsPerSetlist * kSbkRecordSize) continue;
        if (setlistsStart + static_cast<size_t>(bytesPerSetlist) * numSetlists > data.size()) continue;

        for (uint32_t s = 0; s < numSetlists && s < setlists_.size(); ++s) {
            size_t setlistOff = setlistsStart + static_cast<size_t>(s) * bytesPerSetlist;
            size_t songsStart = setlistOff + kSbkHeaderSize;

            sbkSongsStart_[s] = songsStart;

            auto& songs = setlists_[s].songs;
            for (uint32_t k = 0; k < kSongsPerSetlist && k < songs.size(); ++k) {
                size_t songOff = songsStart + static_cast<size_t>(k) * kSbkRecordSize;
                songs[k].params = readSlotParams(data.data(), songOff, data.size());
                songs[k].comment = readComment(data.data(), songOff, data.size());
            }
        }
    }

    // CBK1 (Combi banks, nested CMB1 > CBK1) -- cross-referenced by each
    // slot's bank/number to show the instrument's real name. Confirmed
    // against known real names the project owner pointed out directly
    // (e.g. "Dont stop believin" as a Combi record matching its Set List
    // slot exactly). Optional, same as SBK1: missing/malformed just leaves
    // instrumentName empty rather than failing the whole load.
    //
    // Decoded via a standalone per-record decoder
    // (src/kronos/CombiDecoder.h), same pattern as Programs below --
    // combiBankLocations_ records each bank's location so decodeCombi()
    // can re-decode any single Combi on demand later, straight from the
    // retained raw bytes (data_, set at the end of this function).
    std::vector<ChunkInfo> cbkChunks;
    collectChunks(data, 16, data.size(), "CBK1", cbkChunks, 0);

    combis_.clear();
    combiBankLocations_.clear();
    std::vector<std::vector<std::string>> combiBankNames;  // [bank][number]

    for (size_t bankIdx = 0; bankIdx < cbkChunks.size(); ++bankIdx) {
        const auto& chunk = cbkChunks[bankIdx];
        if (chunk.contentStart + 8 > chunk.contentEnd) continue;

        uint32_t numRecords = readU32BE(&data[chunk.contentStart]);
        uint32_t bytesPerRecord = readU32BE(&data[chunk.contentStart + 4]);
        size_t recordsStart = chunk.contentStart + 8;

        if (bytesPerRecord == 0) continue;
        if (recordsStart + static_cast<size_t>(bytesPerRecord) * numRecords > chunk.contentEnd) continue;

        combiBankLocations_.push_back({recordsStart, numRecords, bytesPerRecord});

        for (uint32_t i = 0; i < numRecords; ++i) {
            size_t off = recordsStart + static_cast<size_t>(i) * bytesPerRecord;
            const uint8_t* record = &data[off];
            CombiFields fields = decodeCombiFields(record, bytesPerRecord, static_cast<int>(bankIdx), static_cast<int>(i));
            combis_.push_back({fields.bank, fields.number, fields.name, fields.timbres});

            if (fields.bank >= static_cast<int>(combiBankNames.size())) combiBankNames.resize(fields.bank + 1);
            if (fields.number >= static_cast<int>(combiBankNames[fields.bank].size())) {
                combiBankNames[fields.bank].resize(fields.number + 1);
            }
            combiBankNames[fields.bank][fields.number] = fields.name;
        }
    }

    // Programs: the first field decoded via a standalone per-record
    // decoder (src/kronos/ProgramDecoder.h) instead of inline in a
    // generic bank-walking helper -- see STATE.md's "ARCHITECTURE:
    // DECODER/ENCODER REFACTOR". This single walk populates both
    // programs_ (the table/dedup view) and programBankNames (the
    // instrument-name cross-reference below) from the same decoded
    // fields, and also records each bank's location in
    // programBankLocations_ so decodeProgram() can re-decode any single
    // Program on demand later, straight from the retained raw bytes
    // (data_, set at the end of this function) -- not just once, here.
    std::vector<ChunkInfo> programBankChunks;
    collectChunks(
        data, 16, data.size(), [](const std::string& tag) { return tag == "MBK1" || tag == "PBK1"; },
        programBankChunks, 0);

    programs_.clear();
    programBankLocations_.clear();
    std::vector<std::vector<std::string>> programBankNames;  // [bank][number]

    for (size_t bankIdx = 0; bankIdx < programBankChunks.size(); ++bankIdx) {
        const auto& chunk = programBankChunks[bankIdx];
        if (chunk.contentStart + 8 > chunk.contentEnd) continue;

        uint32_t numRecords = readU32BE(&data[chunk.contentStart]);
        uint32_t bytesPerRecord = readU32BE(&data[chunk.contentStart + 4]);
        size_t recordsStart = chunk.contentStart + 8;

        if (bytesPerRecord == 0) continue;
        if (recordsStart + static_cast<size_t>(bytesPerRecord) * numRecords > chunk.contentEnd) continue;

        // See ProgramBankType's doc comment in PcgFile.h -- bank type is
        // read per-file from data already parsed here (the chunk's own tag,
        // cross-checked against its declared stride), not looked up in a
        // fixed table.
        ProgramBankType bankType = classifyProgramBankType(chunk.tag, bytesPerRecord).type;
        programBankLocations_.push_back({recordsStart, numRecords, bytesPerRecord, bankType});

        for (uint32_t i = 0; i < numRecords; ++i) {
            size_t off = recordsStart + static_cast<size_t>(i) * bytesPerRecord;
            const uint8_t* record = &data[off];
            ProgramFields fields = decodeProgramFields(record, bytesPerRecord, static_cast<int>(bankIdx), static_cast<int>(i));
            uint64_t hash = hashProgramRecord(record, bytesPerRecord);
            programs_.push_back({fields.bank, fields.number, fields.name, hash, bankType, fields.exiAlgorithmType});

            if (fields.bank >= static_cast<int>(programBankNames.size())) programBankNames.resize(fields.bank + 1);
            if (fields.number >= static_cast<int>(programBankNames[fields.bank].size())) {
                programBankNames[fields.bank].resize(fields.number + 1);
            }
            programBankNames[fields.bank][fields.number] = fields.name;
        }
    }

    for (auto& setlist : setlists_) {
        for (auto& song : setlist.songs) {
            if (!song.params.found) continue;
            const auto& banks = song.params.isProgram ? programBankNames : combiBankNames;
            int bank = song.params.bank;
            int number = song.params.number;
            if (bank < 0 || bank >= static_cast<int>(banks.size())) continue;
            if (number < 0 || number >= static_cast<int>(banks[bank].size())) continue;
            song.instrumentName = banks[bank][number];
        }
    }

    // Retained rather than discarded, now that decodeProgram() (and
    // future per-record decoders) can re-read from it on demand -- see
    // STATE.md's "ARCHITECTURE: DECODER/ENCODER REFACTOR". Moved, not
    // copied: nothing above this point needs the local `data` again.
    data_ = std::move(data);

    return true;
}

namespace {

std::vector<SetlistUsage> setlistUsagesFor(const std::vector<Setlist>& setlists, bool isProgram, int bank,
                                            int number) {
    std::vector<SetlistUsage> usages;
    for (const auto& setlist : setlists) {
        for (const auto& song : setlist.songs) {
            if (!song.params.found || song.params.isProgram != isProgram) continue;
            if (song.params.bank != bank || song.params.number != number) continue;
            usages.push_back({setlist.index, setlist.name, song.index});
        }
    }
    return usages;
}

}  // namespace

std::vector<SetlistUsage> PcgFile::programSetlistUsages(int bank, int number) const {
    return setlistUsagesFor(setlists_, true, bank, number);
}

std::vector<SetlistUsage> PcgFile::combiSetlistUsages(int bank, int number) const {
    return setlistUsagesFor(setlists_, false, bank, number);
}

std::vector<std::vector<int>> PcgFile::setlistUsageCounts(bool isProgram) const {
    std::vector<std::vector<int>> counts;
    for (const auto& setlist : setlists_) {
        for (const auto& song : setlist.songs) {
            if (!song.params.found || song.params.isProgram != isProgram) continue;
            int bank = song.params.bank;
            int number = song.params.number;
            if (bank < 0 || number < 0) continue;
            if (bank >= static_cast<int>(counts.size())) counts.resize(bank + 1);
            if (number >= static_cast<int>(counts[bank].size())) counts[bank].resize(number + 1, 0);
            counts[bank][number]++;
        }
    }
    return counts;
}

std::vector<CombiUsage> PcgFile::combiUsagesForProgram(int bank, int number) const {
    std::vector<CombiUsage> usages;
    // `bank` is a PBK1 file-order index; a Timbre's own rawBankCode uses a
    // different number space that only coincides with it for INT-A..D --
    // translate before comparing, see kConfirmedTimbreBanks's doc comment.
    const int rawCode = confirmedTimbreCodeForProgramBank(bank);
    if (rawCode < 0) return usages;

    for (const auto& combi : combis_) {
        bool referenced = false;
        bool active = false;
        for (const auto& t : combi.timbres) {
            if (t.isDefault || t.rawBankCode != rawCode || t.number != number) continue;
            referenced = true;
            if (t.status != TimbreStatus::Off) active = true;
        }
        if (referenced) usages.push_back({combi.bank, combi.number, combi.name, active});
    }
    return usages;
}

std::vector<std::vector<int>> PcgFile::combiUsageCounts() const {
    std::vector<std::vector<int>> counts;
    for (const auto& combi : combis_) {
        for (const auto& t : combi.timbres) {
            if (t.isDefault) continue;
            // Reverse direction from combiUsagesForProgram() above -- t.rawBankCode
            // is the Timbre's own number space, translate to this project's
            // PBK1 file-order index before indexing `counts` by it (callers,
            // e.g. EditorBridge::listPrograms(), index this table by
            // ProgramInfo::bank, a file-order index).
            const int bank = programBankForConfirmedTimbreCode(t.rawBankCode);
            if (bank < 0) continue;
            int number = t.number;
            if (bank >= static_cast<int>(counts.size())) counts.resize(bank + 1);
            if (number >= static_cast<int>(counts[bank].size())) counts[bank].resize(number + 1, 0);
            counts[bank][number]++;
        }
    }
    return counts;
}

std::vector<std::vector<ProgramInfo>> PcgFile::findDuplicatePrograms() const {
    std::unordered_map<uint64_t, std::vector<ProgramInfo>> byHash;
    for (const auto& program : programs_) byHash[program.contentHash].push_back(program);

    std::vector<std::vector<ProgramInfo>> groups;
    for (auto& [hash, group] : byHash) {
        if (group.size() < 2) continue;
        std::sort(group.begin(), group.end(), [](const ProgramInfo& a, const ProgramInfo& b) {
            return a.bank != b.bank ? a.bank < b.bank : a.number < b.number;
        });
        groups.push_back(std::move(group));
    }
    // unordered_map iteration order isn't deterministic run-to-run -- sort
    // groups themselves so callers (and tests) see a stable order.
    std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) {
        return a.front().bank != b.front().bank ? a.front().bank < b.front().bank : a.front().number < b.front().number;
    });
    return groups;
}

void PcgFile::repointOneSetlistSlot(int setlistIndex, int songIndex, int toBank, int toNumber) {
    auto bytes = songRecordBytes(setlistIndex, songIndex);
    if (!bytes) return;  // shouldn't happen -- callers always have a live (setlistIndex, songIndex) in hand
    (*bytes)[kSbkBankOffset] = static_cast<uint8_t>(((*bytes)[kSbkBankOffset] & ~kSbkBankMask) | (toBank & kSbkBankMask));
    (*bytes)[kSbkNumberOffset] = static_cast<uint8_t>(toNumber);
    putSongRecordBytes(setlistIndex, songIndex, *bytes);
}

std::vector<std::pair<int, int>> PcgFile::findSetlistReferences(bool isProgram, int bank, int number) const {
    std::vector<std::pair<int, int>> hits;
    for (const auto& setlist : setlists_) {
        for (const auto& song : setlist.songs) {
            if (!song.params.found || song.params.isProgram != isProgram) continue;
            if (song.params.bank != bank || song.params.number != number) continue;
            hits.push_back({setlist.index, song.index});
        }
    }
    return hits;
}

int PcgFile::repointSetlistReferences(bool isProgram, int fromBank, int fromNumber, int toBank, int toNumber) {
    auto hits = findSetlistReferences(isProgram, fromBank, fromNumber);
    for (const auto& [setlistIndex, songIndex] : hits) repointOneSetlistSlot(setlistIndex, songIndex, toBank, toNumber);
    return static_cast<int>(hits.size());
}

PcgFile::ResolveDuplicatesResult PcgFile::resolveDuplicates(int keepBank, int keepNumber,
                                                              const std::vector<uint8_t>& hd1InitBytes,
                                                              const std::vector<uint8_t>& exiInitBytes) {
    ResolveDuplicatesResult result;

    auto keepIt = std::find_if(programs_.begin(), programs_.end(), [&](const ProgramInfo& p) {
        return p.bank == keepBank && p.number == keepNumber;
    });
    if (keepIt == programs_.end()) {
        result.error = "No such Program to keep";
        return result;
    }
    const uint64_t keepHash = keepIt->contentHash;

    std::vector<ProgramInfo> duplicates;
    for (const auto& p : programs_) {
        if (p.bank == keepBank && p.number == keepNumber) continue;
        if (p.contentHash == keepHash) duplicates.push_back(p);
    }

    // Validate every distinct bank type's template size BEFORE writing
    // anything -- all-or-nothing, see this method's own doc comment.
    for (const auto& dup : duplicates) {
        const auto& loc = programBankLocations_[static_cast<size_t>(dup.bank)];
        const auto& templateBytes = loc.bankType == ProgramBankType::Hd1 ? hd1InitBytes : exiInitBytes;
        if (templateBytes.size() != loc.bytesPerRecord) {
            result.error = "Init Program template size (" + std::to_string(templateBytes.size()) +
                            " bytes) doesn't match bank " + std::to_string(dup.bank) + "'s own record size (" +
                            std::to_string(loc.bytesPerRecord) + " bytes)";
            return result;
        }
    }

    const int keepRawCode = confirmedTimbreCodeForProgramBank(keepBank);

    for (const auto& dup : duplicates) {
        const auto& loc = programBankLocations_[static_cast<size_t>(dup.bank)];
        const auto& templateBytes = loc.bankType == ProgramBankType::Hd1 ? hd1InitBytes : exiInitBytes;
        putProgramRecordBytes(dup.bank, dup.number, templateBytes);
        result.clearedPrograms++;

        result.setlistRefsRepointed += repointSetlistReferences(/*isProgram=*/true, dup.bank, dup.number, keepBank, keepNumber);

        // dupRawCode<0 means dup.bank itself has no confirmed Timbre code --
        // structurally impossible to know whether any Combi Timbre
        // references it at all (same reasoning as combiUsagesForProgram()'s
        // own early-return for an unconfirmed bank), so there's nothing to
        // find, count, or repoint for this duplicate's Combi side. Nothing
        // else here depends on Set List repointing above having already
        // run -- Combi Timbre references are a completely separate byte
        // range/record type, untouched by it.
        const int dupRawCode = confirmedTimbreCodeForProgramBank(dup.bank);
        if (dupRawCode < 0) continue;

        for (const auto& combi : combis_) {
            std::vector<int> matchingTimbres;
            for (int i = 0; i < static_cast<int>(combi.timbres.size()); ++i) {
                const auto& t = combi.timbres[static_cast<size_t>(i)];
                if (!t.isDefault && t.rawBankCode == dupRawCode && t.number == dup.number) matchingTimbres.push_back(i);
            }
            if (matchingTimbres.empty()) continue;

            // We KNOW these Timbres reference dup.bank/dup.number (dupRawCode
            // is confirmed) but can't safely translate keepBank to its own
            // raw code -- count them as skipped rather than writing a
            // guessed destination code.
            if (keepRawCode < 0) {
                result.combiRefsSkipped += static_cast<int>(matchingTimbres.size());
                continue;
            }

            auto bytes = combiRecordBytes(combi.bank, combi.number);
            if (!bytes) continue;  // shouldn't happen -- this combi was just read from combis_ itself
            for (int i : matchingTimbres) {
                writeTimbreProgramRef(bytes->data(), bytes->size(), i, keepNumber, keepRawCode);
                result.combiRefsRepointed++;
            }
            putCombiRecordBytes(combi.bank, combi.number, *bytes);
        }
    }

    result.ok = true;
    return result;
}

namespace {

// Case-insensitive "does this Combi's name look like an empty/placeholder
// slot" check, used by copyCombi() below to decide whether a drop target is
// safe to copy into. A substring match (not exact-equals) so it also
// catches moveCombiToBank()'s own vacated-slot rename, "- Init Combi -", not
// just Korg's literal "Init Combi" -- both represent "nothing real lives
// here." Deliberately separate from moveCombiToBank()'s OWN filler search
// (an exact, case-sensitive match on "Init Combi"), which is answering a
// different question (find a byte-identical DONOR to vacate a slot into,
// not "is this slot safe to overwrite") and isn't touched by this helper.
bool looksLikeEmptyCombiName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    return lower.find("init combi") != std::string::npos;
}

// Case-insensitive "does this Program's name look like an untouched/empty
// slot" check -- CORRECTED 2026-08-15. Every "is this slot free" check
// below used to test `name.empty()` (a literal blank string), which is
// wrong for any file actually written by real Kronos hardware: a genuinely
// untouched Program slot's name field holds Korg's own real factory
// content, `"Init Program"` (HD-1) or `"Init EXi Program"` (EXi) --
// confirmed against two independent real backup files, see
// docs/content/format/index.md §5.5. `name.empty()` only ever matched this
// app's OWN synthetic test fixtures, never a real file -- reported
// directly: a real personal Kronos backup's cross-dataset Combi copy
// reported ZERO free destination banks anywhere, for a dataset that in
// fact had plenty of genuinely-unused slots. This app's own two "cleared
// slot" template names (resources/Init-Program-HD1.raw/-EXi.raw,
// customized 2026-08-14 to read as `"- Init Program (HD1) -"`/`"- Init
// Program (EXi) -"` so a cleared slot looks visibly different from Korg's
// own factory content) are matched too, via the substring check -- a slot
// THIS app cleared must keep reading as free. `name.empty()` is still
// accepted directly (never actually produced by real hardware per the
// above, but harmless to keep recognizing, and it's what this project's
// OWN synthetic test fixtures still use for a "free" slot).
bool looksLikeEmptyProgramName(const std::string& name) {
    if (name.empty()) return true;
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    return lower == "init exi program" || lower.find("init program") != std::string::npos;
}

}  // namespace

PcgFile::CombiRearrangeResult PcgFile::swapCombis(int bankA, int numberA, int bankB, int numberB) {
    CombiRearrangeResult result;

    if (bankA < 0 || bankA >= static_cast<int>(combiBankLocations_.size()) || numberA < 0 ||
        static_cast<uint32_t>(numberA) >= combiBankLocations_[static_cast<size_t>(bankA)].numRecords) {
        result.error = "No such Combi at the first position";
        return result;
    }
    if (bankB < 0 || bankB >= static_cast<int>(combiBankLocations_.size()) || numberB < 0 ||
        static_cast<uint32_t>(numberB) >= combiBankLocations_[static_cast<size_t>(bankB)].numRecords) {
        result.error = "No such Combi at the second position";
        return result;
    }

    if (bankA == bankB && numberA == numberB) {
        result.ok = true;  // no-op, same as reorderSong()'s own fromIndex==toIndex convention
        return result;
    }

    auto bytesA = combiRecordBytes(bankA, numberA);
    auto bytesB = combiRecordBytes(bankB, numberB);
    if (!bytesA || !bytesB) {
        result.error = "Couldn't read one of the two Combi records";
        return result;
    }

    putCombiRecordBytes(bankA, numberA, *bytesB);
    putCombiRecordBytes(bankB, numberB, *bytesA);

    // NOT two sequential repointSetlistReferences() calls -- A's referrers
    // are moved to B, then a second call searching for "whoever now
    // references B" would immediately re-catch the slots the FIRST call
    // just wrote (they now legitimately reference B) and swap them straight
    // back to A. A single pass checking each song against both original
    // positions avoids that entirely: since bankA/numberA != bankB/numberB,
    // no song can match both, so there's no ambiguity about which way a
    // given song moves.
    for (auto& setlist : setlists_) {
        for (auto& song : setlist.songs) {
            if (!song.params.found || song.params.isProgram) continue;
            int toBank, toNumber;
            if (song.params.bank == bankA && song.params.number == numberA) {
                toBank = bankB;
                toNumber = numberB;
            } else if (song.params.bank == bankB && song.params.number == numberB) {
                toBank = bankA;
                toNumber = numberA;
            } else {
                continue;
            }

            auto bytes = songRecordBytes(setlist.index, song.index);
            if (!bytes) continue;  // shouldn't happen -- this song was just read from setlists_ itself
            (*bytes)[kSbkBankOffset] =
                static_cast<uint8_t>(((*bytes)[kSbkBankOffset] & ~kSbkBankMask) | (toBank & kSbkBankMask));
            (*bytes)[kSbkNumberOffset] = static_cast<uint8_t>(toNumber);
            putSongRecordBytes(setlist.index, song.index, *bytes);
            result.setlistRefsRepointed++;
        }
    }

    result.ok = true;
    return result;
}

PcgFile::CombiRearrangeResult PcgFile::moveCombiWithinBank(int bank, int fromNumber, int toNumber) {
    CombiRearrangeResult result;

    if (bank < 0 || bank >= static_cast<int>(combiBankLocations_.size())) {
        result.error = "No such Combi bank";
        return result;
    }
    const uint32_t count = combiBankLocations_[static_cast<size_t>(bank)].numRecords;
    if (fromNumber < 0 || static_cast<uint32_t>(fromNumber) >= count || toNumber < 0 ||
        static_cast<uint32_t>(toNumber) >= count) {
        result.error = "Combi index out of range";
        return result;
    }
    if (fromNumber == toNumber) {
        result.ok = true;
        return result;
    }

    auto movingBytes = combiRecordBytes(bank, fromNumber);
    if (!movingBytes) {
        result.error = "Couldn't read the moving Combi's record";
        return result;
    }

    // Snapshot exactly WHO references the MOVING record's own original
    // position, before any writes happen -- a single search-then-write
    // repointSetlistReferences() call for this can't be placed safely
    // anywhere in this function: the shift below reads from AND writes to
    // enough of the [fromNumber..toNumber] range that both `fromNumber`
    // (used as a write-target by the shift's very first step) and
    // `toNumber` (used as a read/search-key by the shift's very last step,
    // in EITHER direction) get reused for an unrelated record's own move.
    // Doing the search-then-write before the shift collides with the
    // second; doing it after collides with the first (both found the hard
    // way, via a real 36MB file where the shifted range actually had
    // referenced records at both ends). Capturing identities up front and
    // applying the repoint by identity at the very end, after everything
    // else, sidesteps this entirely -- nothing else in this function ever
    // searches for "who references fromNumber" (the shift's own searches
    // all use i-1/i+1 for the current i, which never equals fromNumber),
    // so these captured slots are never touched by the shift in between.
    auto movingReferrers = findSetlistReferences(false, bank, fromNumber);

    // Same shift-the-intervening-range mechanic as reorderSong() (Set List
    // slots), applied to Combis -- see its own doc comment for the
    // direction reasoning. Each shifted record's own Set List referrers are
    // repointed right after it moves, so by the time this returns every
    // record in the range points wherever its own content actually ended
    // up, not just the one that was dragged.
    if (toNumber < fromNumber) {
        for (int i = fromNumber; i > toNumber; --i) {
            auto bytes = combiRecordBytes(bank, i - 1);
            if (!bytes) {
                result.error = "Couldn't read a Combi record while shifting";
                return result;
            }
            putCombiRecordBytes(bank, i, *bytes);
            result.setlistRefsRepointed += repointSetlistReferences(false, bank, i - 1, bank, i);
        }
    } else {
        for (int i = fromNumber; i < toNumber; ++i) {
            auto bytes = combiRecordBytes(bank, i + 1);
            if (!bytes) {
                result.error = "Couldn't read a Combi record while shifting";
                return result;
            }
            putCombiRecordBytes(bank, i, *bytes);
            result.setlistRefsRepointed += repointSetlistReferences(false, bank, i + 1, bank, i);
        }
    }

    putCombiRecordBytes(bank, toNumber, *movingBytes);

    // Apply the moving record's own repoint last, by the identities
    // captured up front -- see this function's own comment above for why.
    for (const auto& [setlistIndex, songIndex] : movingReferrers) {
        repointOneSetlistSlot(setlistIndex, songIndex, bank, toNumber);
        result.setlistRefsRepointed++;
    }

    result.ok = true;
    return result;
}

PcgFile::CombiRearrangeResult PcgFile::moveCombiToBank(int srcBank, int srcNumber, int dstBank, int dstNumber) {
    CombiRearrangeResult result;

    if (srcBank < 0 || srcBank >= static_cast<int>(combiBankLocations_.size()) || srcNumber < 0 ||
        static_cast<uint32_t>(srcNumber) >= combiBankLocations_[static_cast<size_t>(srcBank)].numRecords) {
        result.error = "No such source Combi";
        return result;
    }
    if (dstBank < 0 || dstBank >= static_cast<int>(combiBankLocations_.size()) || dstNumber < 0 ||
        static_cast<uint32_t>(dstNumber) >= combiBankLocations_[static_cast<size_t>(dstBank)].numRecords) {
        result.error = "No such destination Combi";
        return result;
    }
    if (srcBank == dstBank) {
        result.error = "Use moveCombiWithinBank() for a same-bank move";
        return result;
    }

    // Refuse rather than silently orphan/misdirect whoever referenced the
    // slot about to be overwritten -- see this method's own doc comment.
    for (const auto& setlist : setlists_) {
        for (const auto& song : setlist.songs) {
            if (song.params.found && !song.params.isProgram && song.params.bank == dstBank &&
                song.params.number == dstNumber) {
                result.error = "Can't overwrite -- the destination Combi is still referenced by at least one Set List slot";
                return result;
            }
        }
    }

    // Find a real "Init Combi" elsewhere in the SOURCE's own bank to fill
    // the vacated slot with -- see this method's own doc comment for why
    // this is sourced live rather than from a shipped template.
    std::optional<int> fillerNumber;
    for (const auto& combi : combis_) {
        if (combi.bank == srcBank && combi.number != srcNumber && combi.name == "Init Combi") {
            fillerNumber = combi.number;
            break;
        }
    }
    if (!fillerNumber.has_value()) {
        result.error = "Can't vacate -- no other \"Init Combi\" slot exists in this bank to fill it with";
        return result;
    }

    auto srcBytes = combiRecordBytes(srcBank, srcNumber);
    auto fillerBytes = combiRecordBytes(srcBank, *fillerNumber);
    if (!srcBytes || !fillerBytes) {
        result.error = "Couldn't read the source or filler Combi record";
        return result;
    }

    putCombiRecordBytes(dstBank, dstNumber, *srcBytes);

    // Patch the filler's own name field (offset 4, 24 bytes -- see
    // docs/content/format/index.md §5) to "- Init Combi -" before writing
    // it into the vacated slot, same visibility convention as the
    // Duplicates panel's Init Program templates -- makes a vacated slot
    // unmistakable rather than looking like Korg's own plain "Init Combi".
    constexpr size_t kNameOffset = 4;
    constexpr size_t kNameLength = 24;
    static const std::string kVacatedName = "- Init Combi -";
    for (size_t i = 0; i < kNameLength; ++i) {
        (*fillerBytes)[kNameOffset + i] = i < kVacatedName.size() ? static_cast<uint8_t>(kVacatedName[i]) : 0;
    }
    putCombiRecordBytes(srcBank, srcNumber, *fillerBytes);

    result.setlistRefsRepointed = repointSetlistReferences(false, srcBank, srcNumber, dstBank, dstNumber);
    result.ok = true;
    return result;
}

PcgFile::CombiRearrangeResult PcgFile::copyCombi(int srcBank, int srcNumber, int dstBank, int dstNumber) {
    CombiRearrangeResult result;

    if (srcBank < 0 || srcBank >= static_cast<int>(combiBankLocations_.size()) || srcNumber < 0 ||
        static_cast<uint32_t>(srcNumber) >= combiBankLocations_[static_cast<size_t>(srcBank)].numRecords) {
        result.error = "No such source Combi";
        return result;
    }
    if (dstBank < 0 || dstBank >= static_cast<int>(combiBankLocations_.size()) || dstNumber < 0 ||
        static_cast<uint32_t>(dstNumber) >= combiBankLocations_[static_cast<size_t>(dstBank)].numRecords) {
        result.error = "No such destination Combi";
        return result;
    }
    if (srcBank == dstBank && srcNumber == dstNumber) {
        result.error = "Source and destination are the same slot";
        return result;
    }

    // Find the destination's current name (from the already-decoded
    // combis_ cache, no need to re-decode) -- refuse unless it looks empty.
    // See looksLikeEmptyCombiName()'s own comment for why this is a
    // substring match, not exact-equals.
    const CombiInfo* dstCombi = nullptr;
    for (const auto& combi : combis_) {
        if (combi.bank == dstBank && combi.number == dstNumber) {
            dstCombi = &combi;
            break;
        }
    }
    if (!dstCombi || !looksLikeEmptyCombiName(dstCombi->name)) {
        result.error = "Can't copy here -- the destination isn't an empty (\"Init Combi\") slot. Drop directly onto a real Combi to swap instead.";
        return result;
    }

    // Same defensive reasoning as moveCombiToBank()'s own destination check
    // -- not expected to ever actually trigger for a genuinely empty slot,
    // but refuse rather than silently misdirect a real reference if it did.
    for (const auto& setlist : setlists_) {
        for (const auto& song : setlist.songs) {
            if (song.params.found && !song.params.isProgram && song.params.bank == dstBank &&
                song.params.number == dstNumber) {
                result.error = "Can't copy here -- the destination Combi is still referenced by at least one Set List slot";
                return result;
            }
        }
    }

    auto srcBytes = combiRecordBytes(srcBank, srcNumber);
    if (!srcBytes) {
        result.error = "Couldn't read the source Combi record";
        return result;
    }

    putCombiRecordBytes(dstBank, dstNumber, *srcBytes);

    // Source is deliberately left untouched -- see this method's own doc
    // comment. Nothing to repoint either: the source's own Set List
    // references still correctly point at it, and the destination had none
    // (it was empty) to carry over.
    result.ok = true;
    return result;
}

// Shared by analyzeCombiCrossDatasetCopy()/applyCombiCrossDatasetCopy() below --
// the same "is (dstBank, dstNumber) a safe copy target" check copyCombi() already
// runs, just factored out so both the read-only analysis and the actual write
// enforce it identically rather than risking the two drifting apart.
namespace {
std::string checkCombiCopyDestination(const PcgFile& dest, int dstBank, int dstNumber) {
    // `combis()` is a flat list across every bank, not indexed by bank -- a
    // direct existence search (rather than a separate bank-count bound
    // check, which would compare a bank INDEX against a flat TOTAL combi
    // count and mean nothing) is both correct and sufficient here.
    const CombiInfo* dstCombi = nullptr;
    for (const auto& combi : dest.combis()) {
        if (combi.bank == dstBank && combi.number == dstNumber) {
            dstCombi = &combi;
            break;
        }
    }
    if (!dstCombi) return "No such destination Combi";
    if (!looksLikeEmptyCombiName(dstCombi->name)) {
        return "Can't copy here -- the destination isn't an empty (\"Init Combi\") slot. Drop directly onto a real Combi to swap instead.";
    }
    for (const auto& setlist : dest.setlists()) {
        for (const auto& song : setlist.songs) {
            if (song.params.found && !song.params.isProgram && song.params.bank == dstBank &&
                song.params.number == dstNumber) {
                return "Can't copy here -- the destination Combi is still referenced by at least one Set List slot";
            }
        }
    }
    return "";
}
}  // namespace

PcgFile::CombiCrossDatasetAnalysis PcgFile::analyzeCombiCrossDatasetCopy(const PcgFile& src, int srcBank,
                                                                          int srcNumber, int dstBank,
                                                                          int dstNumber) const {
    CombiCrossDatasetAnalysis result;

    if (srcBank < 0 || srcBank >= static_cast<int>(src.combiBankLocations_.size()) || srcNumber < 0 ||
        static_cast<uint32_t>(srcNumber) >= src.combiBankLocations_[static_cast<size_t>(srcBank)].numRecords) {
        result.error = "No such source Combi";
        return result;
    }
    result.error = checkCombiCopyDestination(*this, dstBank, dstNumber);
    if (!result.error.empty()) return result;

    const CombiInfo* srcCombi = nullptr;
    for (const auto& combi : src.combis_) {
        if (combi.bank == srcBank && combi.number == srcNumber) {
            srcCombi = &combi;
            break;
        }
    }
    if (!srcCombi) {
        result.error = "Couldn't read the source Combi";
        return result;
    }

    // Dedup by (srcBank, srcNumber) -- several Timbres can reference the same
    // Program; each gets its own `dependencies` entry (so the UI can list
    // every Timbre), but only ONE `unresolved` entry per unique Program.
    std::vector<std::pair<int, int>> seenUnresolvedPrograms;
    for (int i = 0; i < static_cast<int>(srcCombi->timbres.size()); ++i) {
        const auto& t = srcCombi->timbres[static_cast<size_t>(i)];
        if (t.isDefault) continue;
        // GM/G(n)/g(n) (permanently indexless, built into every unit) or a
        // genuinely unidentified raw code -- neither is file-specific data
        // to resolve; the apply step copies these Timbres' raw bytes
        // through unchanged instead of listing them here. Only the latter
        // (timbreBankName() has no name for it either) is worth telling the
        // caller about -- GM/G(n)/g(n) are confirmed, universal, hardware-
        // builtin content, guaranteed meaningful in ANY destination file, so
        // there's nothing uncertain to warn about there.
        const int programBank = programBankForConfirmedTimbreCode(t.rawBankCode);
        if (programBank < 0) {
            if (timbreBankName(t.rawBankCode).empty()) {
                result.unmappableTimbres.push_back({i, t.rawBankCode, t.number});
            }
            continue;
        }

        const ProgramInfo* srcProgram = nullptr;
        for (const auto& p : src.programs_) {
            if (p.bank == programBank && p.number == t.number) {
                srcProgram = &p;
                break;
            }
        }
        if (!srcProgram) continue;  // shouldn't happen -- a confirmed bank/number should always have a real ProgramInfo

        TimbreProgramDependency dep;
        dep.timbreIndex = i;
        dep.srcBank = srcProgram->bank;
        dep.srcNumber = srcProgram->number;
        dep.name = srcProgram->name;
        dep.bankType = srcProgram->bankType;
        for (const auto& p : programs_) {
            if (p.contentHash == srcProgram->contentHash) {
                dep.found = true;
                dep.foundBank = p.bank;
                dep.foundNumber = p.number;
                break;
            }
        }
        result.dependencies.push_back(dep);

        if (!dep.found &&
            std::none_of(seenUnresolvedPrograms.begin(), seenUnresolvedPrograms.end(),
                         [&](const auto& bn) { return bn.first == dep.srcBank && bn.second == dep.srcNumber; })) {
            seenUnresolvedPrograms.push_back({dep.srcBank, dep.srcNumber});
        }
    }

    for (const auto& [b, n] : seenUnresolvedPrograms) {
        const ProgramInfo* srcProgram = nullptr;
        for (const auto& p : src.programs_) {
            if (p.bank == b && p.number == n) {
                srcProgram = &p;
                break;
            }
        }
        if (!srcProgram) continue;

        UnresolvedProgram unresolved;
        unresolved.srcBank = b;
        unresolved.srcNumber = n;
        unresolved.name = srcProgram->name;
        unresolved.bankType = srcProgram->bankType;
        for (size_t bankIdx = 0; bankIdx < programBankLocations_.size(); ++bankIdx) {
            if (programBankLocations_[bankIdx].bankType != srcProgram->bankType) continue;
            const bool hasFreeSlot = std::any_of(programs_.begin(), programs_.end(), [&](const ProgramInfo& p) {
                return p.bank == static_cast<int>(bankIdx) && looksLikeEmptyProgramName(p.name);
            });
            if (hasFreeSlot) unresolved.candidateBanks.push_back(static_cast<int>(bankIdx));
        }
        result.unresolved.push_back(std::move(unresolved));
    }

    result.ok = true;
    return result;
}

PcgFile::CombiRearrangeResult PcgFile::applyCombiCrossDatasetCopy(const PcgFile& src, int srcBank, int srcNumber,
                                                                    int dstBank, int dstNumber,
                                                                    const std::vector<ProgramPlacement>& placements) {
    CombiRearrangeResult result;

    if (srcBank < 0 || srcBank >= static_cast<int>(src.combiBankLocations_.size()) || srcNumber < 0 ||
        static_cast<uint32_t>(srcNumber) >= src.combiBankLocations_[static_cast<size_t>(srcBank)].numRecords) {
        result.error = "No such source Combi";
        return result;
    }
    result.error = checkCombiCopyDestination(*this, dstBank, dstNumber);
    if (!result.error.empty()) return result;

    const CombiInfo* srcCombi = nullptr;
    for (const auto& combi : src.combis_) {
        if (combi.bank == srcBank && combi.number == srcNumber) {
            srcCombi = &combi;
            break;
        }
    }
    if (!srcCombi) {
        result.error = "Couldn't read the source Combi";
        return result;
    }

    // Pass 1: resolve every real Program dependency WITHOUT writing anything
    // yet -- all-or-nothing, same discipline resolveDuplicates() uses for its
    // own template-size validation pass. Re-resolves "already exists" fresh
    // here rather than trusting an earlier analyzeCombiCrossDatasetCopy()
    // call, in case this file changed in the meantime (e.g. the opposite pane).
    struct Resolution {
        int timbreIndex;
        int dstProgramBank;
        int dstProgramNumber;
    };
    struct ResolvedProgram {
        int srcBank, srcNumber, dstBank, dstNumber;
        bool alreadyPresent;  // true = an existing match was reused, false = a fresh copy is needed in pass 2
    };
    std::vector<Resolution> resolutions;
    std::vector<ResolvedProgram> resolvedPrograms;

    for (int i = 0; i < static_cast<int>(srcCombi->timbres.size()); ++i) {
        const auto& t = srcCombi->timbres[static_cast<size_t>(i)];
        if (t.isDefault) continue;
        const int programBank = programBankForConfirmedTimbreCode(t.rawBankCode);
        if (programBank < 0) continue;  // GM/unidentified -- Timbre bytes pass through unchanged below

        const ProgramInfo* srcProgram = nullptr;
        for (const auto& p : src.programs_) {
            if (p.bank == programBank && p.number == t.number) {
                srcProgram = &p;
                break;
            }
        }
        if (!srcProgram) continue;

        // Already resolved this exact source Program earlier in this same loop?
        auto already = std::find_if(resolvedPrograms.begin(), resolvedPrograms.end(), [&](const ResolvedProgram& r) {
            return r.srcBank == srcProgram->bank && r.srcNumber == srcProgram->number;
        });
        if (already != resolvedPrograms.end()) {
            resolutions.push_back({i, already->dstBank, already->dstNumber});
            continue;
        }

        auto foundIt = std::find_if(programs_.begin(), programs_.end(),
                                     [&](const ProgramInfo& p) { return p.contentHash == srcProgram->contentHash; });
        if (foundIt != programs_.end()) {
            resolvedPrograms.push_back({srcProgram->bank, srcProgram->number, foundIt->bank, foundIt->number, true});
            resolutions.push_back({i, foundIt->bank, foundIt->number});
            continue;
        }

        auto placementIt = std::find_if(placements.begin(), placements.end(), [&](const ProgramPlacement& pl) {
            return pl.srcBank == srcProgram->bank && pl.srcNumber == srcProgram->number;
        });
        if (placementIt == placements.end()) {
            result.error = "No destination chosen for \"" + srcProgram->name + "\" (" + std::to_string(srcProgram->bank) +
                            "/" + std::to_string(srcProgram->number) + ")";
            return result;
        }

        int chosenNumber = -1;
        if (placementIt->dstNumber >= 0) {
            // An exact slot the user picked (e.g. from a per-bank dropdown of that
            // bank's own empty Program names) -- re-validated fresh here, not
            // trusted from whatever the UI last knew, in case it's been taken in
            // the meantime (e.g. by an earlier placement in this SAME apply() call
            // sharing the same bank, or a write from the opposite pane).
            const bool stillEmpty = std::any_of(programs_.begin(), programs_.end(), [&](const ProgramInfo& p) {
                return p.bank == placementIt->dstBank && p.number == placementIt->dstNumber && looksLikeEmptyProgramName(p.name);
            });
            if (!stillEmpty) {
                result.error = "The chosen destination slot for \"" + srcProgram->name + "\" is no longer free -- pick another.";
                return result;
            }
            chosenNumber = placementIt->dstNumber;
        } else {
            for (const auto& p : programs_) {
                if (p.bank == placementIt->dstBank && looksLikeEmptyProgramName(p.name) && (chosenNumber < 0 || p.number < chosenNumber)) {
                    chosenNumber = p.number;
                }
            }
            if (chosenNumber < 0) {
                result.error = "No free slot left in the chosen bank for \"" + srcProgram->name + "\"";
                return result;
            }
        }

        resolvedPrograms.push_back({srcProgram->bank, srcProgram->number, placementIt->dstBank, chosenNumber, false});
        resolutions.push_back({i, placementIt->dstBank, chosenNumber});
    }

    // Pass 2: copy each genuinely new Program (skip ones that reused an
    // existing match -- copyProgramFrom() would itself reject those as
    // DuplicateExists, so only the fresh ones need the call at all).
    for (const auto& rp : resolvedPrograms) {
        if (rp.alreadyPresent) continue;
        auto copyError = copyProgramFrom(src, rp.srcBank, rp.srcNumber, rp.dstBank, rp.dstNumber);
        if (copyError.has_value()) {
            result.error =
                "Couldn't copy Program " + std::to_string(rp.srcBank) + "/" + std::to_string(rp.srcNumber) + " into the destination";
            return result;
        }
    }

    // Rewrite a COPY of the source Combi's own bytes so every real Program-
    // referencing Timbre points at its resolved destination -- src itself is
    // never touched. GM/unidentified/default Timbres' bytes pass through
    // unchanged (never in `resolutions`).
    auto combiBytes = src.combiRecordBytes(srcBank, srcNumber);
    if (!combiBytes) {
        result.error = "Couldn't read the source Combi's raw bytes";
        return result;
    }
    for (const auto& res : resolutions) {
        const int rawCode = confirmedTimbreCodeForProgramBank(res.dstProgramBank);
        if (rawCode < 0) continue;  // shouldn't happen -- every dest bank here came from a confirmed source translation
        writeTimbreProgramRef(combiBytes->data(), combiBytes->size(), res.timbreIndex, res.dstProgramNumber, rawCode);
    }

    if (!putCombiRecordBytes(dstBank, dstNumber, *combiBytes)) {
        result.error = "Couldn't write the Combi into its destination slot (record size mismatch)";
        return result;
    }

    result.ok = true;
    return result;
}

std::vector<PcgFile::ProgramBankTypeEntry> PcgFile::programBankTypes() const {
    std::vector<ProgramBankTypeEntry> result;
    result.reserve(programBankLocations_.size());
    for (size_t bank = 0; bank < programBankLocations_.size(); ++bank) {
        result.push_back({static_cast<int>(bank), programBankLocations_[bank].bankType});
    }
    return result;
}

std::optional<ProgramBankType> PcgFile::programBankTypeAt(int bank) const {
    if (bank < 0 || bank >= static_cast<int>(programBankLocations_.size())) return std::nullopt;
    return programBankLocations_[static_cast<size_t>(bank)].bankType;
}

std::vector<std::string> PcgFile::topLevelChunkTags() const {
    std::vector<std::string> tags;
    if (data_.size() < 16) return tags;

    // Byte 16 is PCG1 itself (the whole-file root container, confirmed via
    // a real file: its own declared size exactly spans the rest of the
    // file), not its children -- DIV1/SLS1/PRG1/CMB1/etc. are one level
    // further in, at PCG1's own contentStart. "Top-level" here means
    // PCG1's direct children, which is what every other top-level tag in
    // this format (and the Internals pane) actually means.
    auto pcg1 = readChunk(data_, 16, data_.size());
    if (!pcg1) return tags;

    size_t pos = pcg1->contentStart;
    while (pos + 12 <= pcg1->contentEnd) {
        auto chunk = readChunk(data_, pos, pcg1->contentEnd);
        if (!chunk) break;
        tags.push_back(chunk->tag);
        pos = chunk->contentEnd + (chunk->contentEnd % 2);
    }
    return tags;
}

std::vector<PcgFile::ProgramBankInfo> PcgFile::programBankInfo() const {
    std::vector<ProgramBankInfo> result;
    result.reserve(programBankLocations_.size());
    for (size_t i = 0; i < programBankLocations_.size(); ++i) {
        const auto& loc = programBankLocations_[i];
        result.push_back({static_cast<int>(i), loc.bankType, static_cast<int>(loc.numRecords),
                           static_cast<int>(loc.bytesPerRecord)});
    }
    return result;
}

std::vector<PcgFile::CombiBankInfo> PcgFile::combiBankInfo() const {
    std::vector<CombiBankInfo> result;
    result.reserve(combiBankLocations_.size());
    for (size_t i = 0; i < combiBankLocations_.size(); ++i) {
        const auto& loc = combiBankLocations_[i];
        result.push_back({static_cast<int>(i), static_cast<int>(loc.numRecords), static_cast<int>(loc.bytesPerRecord)});
    }
    return result;
}

std::optional<ProgramInfo> PcgFile::decodeProgram(int bank, int number) const {
    if (bank < 0 || bank >= static_cast<int>(programBankLocations_.size())) return std::nullopt;
    const auto& loc = programBankLocations_[bank];
    if (number < 0 || static_cast<uint32_t>(number) >= loc.numRecords) return std::nullopt;

    size_t off = loc.recordsStart + static_cast<size_t>(number) * loc.bytesPerRecord;
    if (off + loc.bytesPerRecord > data_.size()) return std::nullopt;

    const uint8_t* record = &data_[off];
    ProgramFields fields = decodeProgramFields(record, loc.bytesPerRecord, bank, number);
    uint64_t hash = hashProgramRecord(record, loc.bytesPerRecord);
    return ProgramInfo{fields.bank, fields.number, fields.name, hash, loc.bankType, fields.exiAlgorithmType};
}

std::optional<PcgFile::ProgramCopyError> PcgFile::copyProgramFrom(const PcgFile& src, int srcBank, int srcNumber,
                                                                    int dstBank, int dstNumber) {
    // Bounds -- same pattern as decodeProgram()/decodeProgram(), just against
    // two different files' own bank tables.
    if (srcBank < 0 || srcBank >= static_cast<int>(src.programBankLocations_.size())) return ProgramCopyError::OutOfRange;
    const auto& srcLoc = src.programBankLocations_[static_cast<size_t>(srcBank)];
    if (srcNumber < 0 || static_cast<uint32_t>(srcNumber) >= srcLoc.numRecords) return ProgramCopyError::OutOfRange;

    if (dstBank < 0 || dstBank >= static_cast<int>(programBankLocations_.size())) return ProgramCopyError::OutOfRange;
    const auto& dstLoc = programBankLocations_[static_cast<size_t>(dstBank)];
    if (dstNumber < 0 || static_cast<uint32_t>(dstNumber) >= dstLoc.numRecords) return ProgramCopyError::OutOfRange;

    if (srcLoc.bankType != dstLoc.bankType) return ProgramCopyError::BankTypeMismatch;
    if (srcLoc.bytesPerRecord != dstLoc.bytesPerRecord) return ProgramCopyError::RecordSizeMismatch;

    const size_t srcOff = srcLoc.recordsStart + static_cast<size_t>(srcNumber) * srcLoc.bytesPerRecord;
    const size_t dstOff = dstLoc.recordsStart + static_cast<size_t>(dstNumber) * dstLoc.bytesPerRecord;
    if (srcOff + srcLoc.bytesPerRecord > src.data_.size()) return ProgramCopyError::OutOfRange;
    if (dstOff + dstLoc.bytesPerRecord > data_.size()) return ProgramCopyError::OutOfRange;

    // Target slot already holds a *different* Program -- reject rather than
    // silently overwrite. Re-dropping the exact same Program already sitting
    // there is caught by the DuplicateExists check below instead (it already
    // exists in this file, namely right here), not this one.
    for (const auto& p : programs_) {
        if (p.bank == dstBank && p.number == dstNumber && !looksLikeEmptyProgramName(p.name)) return ProgramCopyError::TargetSlotOccupied;
    }

    const uint8_t* srcRecord = &src.data_[srcOff];
    const uint64_t srcHash = hashProgramRecord(srcRecord, srcLoc.bytesPerRecord);
    const bool sameFile = &src == this;
    for (const auto& p : programs_) {
        // For a same-dataset copy, the source's own slot trivially has this
        // exact hash (it's the thing being copied) -- comparing against it
        // would reject every same-dataset copy as "a duplicate of itself".
        // Skip only that one specific slot, not its whole bank.
        if (sameFile && p.bank == srcBank && p.number == srcNumber) continue;
        if (p.contentHash == srcHash) return ProgramCopyError::DuplicateExists;
    }

    writeIntoData(dstOff, srcRecord, dstLoc.bytesPerRecord);
    refreshProgramInfo(dstBank, dstNumber);

    return std::nullopt;
}

PcgFile::ProgramSwapResult PcgFile::swapPrograms(int bankA, int numberA, int bankB, int numberB) {
    ProgramSwapResult result;

    if (bankA < 0 || bankA >= static_cast<int>(programBankLocations_.size()) || numberA < 0 ||
        static_cast<uint32_t>(numberA) >= programBankLocations_[static_cast<size_t>(bankA)].numRecords) {
        result.error = "No such Program at the first position";
        return result;
    }
    if (bankB < 0 || bankB >= static_cast<int>(programBankLocations_.size()) || numberB < 0 ||
        static_cast<uint32_t>(numberB) >= programBankLocations_[static_cast<size_t>(bankB)].numRecords) {
        result.error = "No such Program at the second position";
        return result;
    }
    if (bankA == bankB && numberA == numberB) {
        result.ok = true;  // no-op, same as swapCombis()'s own convention
        return result;
    }

    const auto& locA = programBankLocations_[static_cast<size_t>(bankA)];
    const auto& locB = programBankLocations_[static_cast<size_t>(bankB)];
    if (locA.bankType != locB.bankType) {
        result.error = "Can't swap: the two banks are different engine types (HD-1/EXi) -- "
                        "a Program can only be loaded into a bank of the matching type.";
        return result;
    }
    if (locA.bytesPerRecord != locB.bytesPerRecord) {
        result.error = "Can't swap: the two banks don't share the same record size.";
        return result;
    }

    auto bytesA = programRecordBytes(bankA, numberA);
    auto bytesB = programRecordBytes(bankB, numberB);
    if (!bytesA || !bytesB) {
        result.error = "Couldn't read one of the two Program records";
        return result;
    }

    putProgramRecordBytes(bankA, numberA, *bytesB);
    putProgramRecordBytes(bankB, numberB, *bytesA);

    // Set List repoint -- a single pass checking each song against BOTH
    // original positions at once, same reasoning as swapCombis()'s own
    // identical loop: two SEQUENTIAL repointSetlistReferences() calls (A->B
    // then B->A) would have the second call immediately re-catch the slots
    // the first one just wrote (they now legitimately reference B) and
    // swap them straight back to A.
    for (auto& setlist : setlists_) {
        for (auto& song : setlist.songs) {
            if (!song.params.found || !song.params.isProgram) continue;
            int toBank, toNumber;
            if (song.params.bank == bankA && song.params.number == numberA) {
                toBank = bankB;
                toNumber = numberB;
            } else if (song.params.bank == bankB && song.params.number == numberB) {
                toBank = bankA;
                toNumber = numberA;
            } else {
                continue;
            }

            auto bytes = songRecordBytes(setlist.index, song.index);
            if (!bytes) continue;  // shouldn't happen -- this song was just read from setlists_ itself
            (*bytes)[kSbkBankOffset] =
                static_cast<uint8_t>(((*bytes)[kSbkBankOffset] & ~kSbkBankMask) | (toBank & kSbkBankMask));
            (*bytes)[kSbkNumberOffset] = static_cast<uint8_t>(toNumber);
            putSongRecordBytes(setlist.index, song.index, *bytes);
            result.setlistRefsRepointed++;
        }
    }

    // Combi Timbre repoint -- same single-pass-both-directions shape, per
    // Timbre instead of per Set List slot. Gated per-direction on the
    // DESTINATION bank having a confirmed raw Timbre code to repoint INTO
    // (mirrors resolveDuplicates()'s own combiRefsSkipped reasoning) --
    // structurally can't happen today since all 20 Program banks are
    // confirmed, kept for the same defensive reason.
    const int rawCodeA = confirmedTimbreCodeForProgramBank(bankA);
    const int rawCodeB = confirmedTimbreCodeForProgramBank(bankB);
    for (const auto& combi : combis_) {
        struct Repoint {
            int timbreIndex;
            int newNumber;
            int newRawCode;
        };
        std::vector<Repoint> repoints;
        int skipped = 0;
        for (int i = 0; i < static_cast<int>(combi.timbres.size()); ++i) {
            const auto& t = combi.timbres[static_cast<size_t>(i)];
            if (t.isDefault) continue;
            if (rawCodeA >= 0 && t.rawBankCode == rawCodeA && t.number == numberA) {
                if (rawCodeB < 0) {
                    ++skipped;
                    continue;
                }
                repoints.push_back({i, numberB, rawCodeB});
            } else if (rawCodeB >= 0 && t.rawBankCode == rawCodeB && t.number == numberB) {
                if (rawCodeA < 0) {
                    ++skipped;
                    continue;
                }
                repoints.push_back({i, numberA, rawCodeA});
            }
        }
        result.combiRefsSkipped += skipped;
        if (repoints.empty()) continue;

        auto bytes = combiRecordBytes(combi.bank, combi.number);
        if (!bytes) continue;  // shouldn't happen -- this combi was just read from combis_ itself
        for (const auto& r : repoints) {
            writeTimbreProgramRef(bytes->data(), bytes->size(), r.timbreIndex, r.newNumber, r.newRawCode);
            result.combiRefsRepointed++;
        }
        putCombiRecordBytes(combi.bank, combi.number, *bytes);
    }

    result.ok = true;
    return result;
}

void PcgFile::refreshProgramInfo(int bank, int number) {
    const auto& loc = programBankLocations_[static_cast<size_t>(bank)];
    size_t off = loc.recordsStart + static_cast<size_t>(number) * loc.bytesPerRecord;
    const uint8_t* record = &data_[off];
    ProgramFields fields = decodeProgramFields(record, loc.bytesPerRecord, bank, number);
    uint64_t hash = hashProgramRecord(record, loc.bytesPerRecord);
    ProgramInfo updated{fields.bank, fields.number, fields.name, hash, loc.bankType, fields.exiAlgorithmType};

    auto it = std::find_if(programs_.begin(), programs_.end(),
                            [&](const ProgramInfo& p) { return p.bank == bank && p.number == number; });
    if (it != programs_.end()) {
        *it = updated;
    } else {
        programs_.push_back(updated);
    }
}

void PcgFile::refreshCombiInfo(int bank, int number) {
    const auto& loc = combiBankLocations_[static_cast<size_t>(bank)];
    size_t off = loc.recordsStart + static_cast<size_t>(number) * loc.bytesPerRecord;
    const uint8_t* record = &data_[off];
    CombiFields fields = decodeCombiFields(record, loc.bytesPerRecord, bank, number);
    CombiInfo updated{fields.bank, fields.number, fields.name, fields.timbres};

    auto it = std::find_if(combis_.begin(), combis_.end(),
                            [&](const CombiInfo& c) { return c.bank == bank && c.number == number; });
    if (it != combis_.end()) {
        *it = updated;
    } else {
        combis_.push_back(updated);
    }
}

std::optional<CombiInfo> PcgFile::decodeCombi(int bank, int number) const {
    if (bank < 0 || bank >= static_cast<int>(combiBankLocations_.size())) return std::nullopt;
    const auto& loc = combiBankLocations_[bank];
    if (number < 0 || static_cast<uint32_t>(number) >= loc.numRecords) return std::nullopt;

    size_t off = loc.recordsStart + static_cast<size_t>(number) * loc.bytesPerRecord;
    if (off + loc.bytesPerRecord > data_.size()) return std::nullopt;

    const uint8_t* record = &data_[off];
    CombiFields fields = decodeCombiFields(record, loc.bytesPerRecord, bank, number);
    return CombiInfo{fields.bank, fields.number, fields.name, fields.timbres};
}

std::optional<std::vector<uint8_t>> PcgFile::combiRecordBytes(int bank, int number) const {
    if (bank < 0 || bank >= static_cast<int>(combiBankLocations_.size())) return std::nullopt;
    const auto& loc = combiBankLocations_[static_cast<size_t>(bank)];
    if (number < 0 || static_cast<uint32_t>(number) >= loc.numRecords) return std::nullopt;

    size_t off = loc.recordsStart + static_cast<size_t>(number) * loc.bytesPerRecord;
    if (off + loc.bytesPerRecord > data_.size()) return std::nullopt;

    return std::vector<uint8_t>(data_.begin() + static_cast<long>(off),
                                 data_.begin() + static_cast<long>(off + loc.bytesPerRecord));
}

bool PcgFile::putCombiRecordBytes(int bank, int number, const std::vector<uint8_t>& bytes) {
    if (bank < 0 || bank >= static_cast<int>(combiBankLocations_.size())) return false;
    const auto& loc = combiBankLocations_[static_cast<size_t>(bank)];
    if (number < 0 || static_cast<uint32_t>(number) >= loc.numRecords) return false;
    if (bytes.size() != loc.bytesPerRecord) return false;

    size_t off = loc.recordsStart + static_cast<size_t>(number) * loc.bytesPerRecord;
    if (off + loc.bytesPerRecord > data_.size()) return false;

    writeIntoData(off, bytes.data(), bytes.size());
    refreshCombiInfo(bank, number);
    return true;
}

std::optional<std::vector<uint8_t>> PcgFile::programRecordBytes(int bank, int number) const {
    if (bank < 0 || bank >= static_cast<int>(programBankLocations_.size())) return std::nullopt;
    const auto& loc = programBankLocations_[static_cast<size_t>(bank)];
    if (number < 0 || static_cast<uint32_t>(number) >= loc.numRecords) return std::nullopt;

    size_t off = loc.recordsStart + static_cast<size_t>(number) * loc.bytesPerRecord;
    if (off + loc.bytesPerRecord > data_.size()) return std::nullopt;

    return std::vector<uint8_t>(data_.begin() + static_cast<long>(off),
                                 data_.begin() + static_cast<long>(off + loc.bytesPerRecord));
}

bool PcgFile::putProgramRecordBytes(int bank, int number, const std::vector<uint8_t>& bytes) {
    if (bank < 0 || bank >= static_cast<int>(programBankLocations_.size())) return false;
    const auto& loc = programBankLocations_[static_cast<size_t>(bank)];
    if (number < 0 || static_cast<uint32_t>(number) >= loc.numRecords) return false;
    if (bytes.size() != loc.bytesPerRecord) return false;

    size_t off = loc.recordsStart + static_cast<size_t>(number) * loc.bytesPerRecord;
    if (off + loc.bytesPerRecord > data_.size()) return false;

    writeIntoData(off, bytes.data(), bytes.size());
    refreshProgramInfo(bank, number);
    return true;
}

std::optional<std::vector<uint8_t>> PcgFile::songRecordBytes(int setlistIndex, int songIndex) const {
    if (setlistIndex < 0 || static_cast<size_t>(setlistIndex) >= sbkSongsStart_.size()) return std::nullopt;
    if (songIndex < 0 || static_cast<size_t>(songIndex) >= setlists_[static_cast<size_t>(setlistIndex)].songs.size())
        return std::nullopt;

    size_t start = sbkSongsStart_[static_cast<size_t>(setlistIndex)];
    if (start == static_cast<size_t>(-1)) return std::nullopt;

    size_t songOff = start + static_cast<size_t>(songIndex) * kSbkRecordSize;
    if (songOff + kSbkRecordSize > data_.size()) return std::nullopt;

    return std::vector<uint8_t>(data_.begin() + static_cast<long>(songOff),
                                 data_.begin() + static_cast<long>(songOff + kSbkRecordSize));
}

bool PcgFile::putSongRecordBytes(int setlistIndex, int songIndex, const std::vector<uint8_t>& bytes) {
    if (bytes.size() != kSbkRecordSize) return false;
    if (setlistIndex < 0 || static_cast<size_t>(setlistIndex) >= sbkSongsStart_.size()) return false;
    if (songIndex < 0 || static_cast<size_t>(songIndex) >= setlists_[static_cast<size_t>(setlistIndex)].songs.size())
        return false;

    size_t start = sbkSongsStart_[static_cast<size_t>(setlistIndex)];
    if (start == static_cast<size_t>(-1)) return false;

    size_t songOff = start + static_cast<size_t>(songIndex) * kSbkRecordSize;
    if (songOff + kSbkRecordSize > data_.size()) return false;

    writeIntoData(songOff, bytes.data(), bytes.size());

    Song& song = setlists_[static_cast<size_t>(setlistIndex)].songs[static_cast<size_t>(songIndex)];
    song.params = readSlotParams(data_.data(), songOff, data_.size());
    song.comment = readComment(data_.data(), songOff, data_.size());
    song.instrumentName = song.params.found
                               ? resolveInstrumentName(song.params.isProgram, song.params.bank, song.params.number)
                               : std::string();
    return true;
}

std::string PcgFile::resolveInstrumentName(bool isProgram, int bank, int number) const {
    if (isProgram) {
        for (const auto& p : programs_) {
            if (p.bank == bank && p.number == number) return p.name;
        }
    } else {
        for (const auto& c : combis_) {
            if (c.bank == bank && c.number == number) return c.name;
        }
    }
    return {};
}

std::optional<std::vector<uint8_t>> PcgFile::nameRecordBytes(int setlistIndex, int songIndex) const {
    if (setlistIndex < 0 || static_cast<size_t>(setlistIndex) >= sdbSongsStart_.size()) return std::nullopt;
    if (songIndex < 0 || static_cast<size_t>(songIndex) >= setlists_[static_cast<size_t>(setlistIndex)].songs.size())
        return std::nullopt;

    size_t start = sdbSongsStart_[static_cast<size_t>(setlistIndex)];
    if (start == static_cast<size_t>(-1)) return std::nullopt;

    size_t nameOff = start + static_cast<size_t>(songIndex) * kRecordSize;
    if (nameOff + kRecordSize > data_.size()) return std::nullopt;

    return std::vector<uint8_t>(data_.begin() + static_cast<long>(nameOff),
                                 data_.begin() + static_cast<long>(nameOff + kRecordSize));
}

bool PcgFile::putNameRecordBytes(int setlistIndex, int songIndex, const std::vector<uint8_t>& bytes) {
    if (bytes.size() != kRecordSize) return false;
    if (setlistIndex < 0 || static_cast<size_t>(setlistIndex) >= sdbSongsStart_.size()) return false;
    if (songIndex < 0 || static_cast<size_t>(songIndex) >= setlists_[static_cast<size_t>(setlistIndex)].songs.size())
        return false;

    size_t start = sdbSongsStart_[static_cast<size_t>(setlistIndex)];
    if (start == static_cast<size_t>(-1)) return false;

    size_t nameOff = start + static_cast<size_t>(songIndex) * kRecordSize;
    if (nameOff + kRecordSize > data_.size()) return false;

    writeIntoData(nameOff, bytes.data(), bytes.size());

    setlists_[static_cast<size_t>(setlistIndex)].songs[static_cast<size_t>(songIndex)].name =
        readRecordName(data_.data(), nameOff, data_.size());
    return true;
}

bool PcgFile::reorderSong(int setlistIndex, int fromIndex, int toIndex) {
    if (setlistIndex < 0 || static_cast<size_t>(setlistIndex) >= setlists_.size()) return false;
    const int count = static_cast<int>(setlists_[static_cast<size_t>(setlistIndex)].songs.size());
    if (fromIndex < 0 || fromIndex >= count || toIndex < 0 || toIndex >= count) return false;
    if (fromIndex == toIndex) return true;

    auto movingName = nameRecordBytes(setlistIndex, fromIndex);
    auto movingParams = songRecordBytes(setlistIndex, fromIndex);
    if (!movingName || !movingParams) return false;

    // Shift the intervening range by one to fill the gap fromIndex leaves,
    // then drop the moving slot's own original content into the now-free
    // toIndex -- e.g. fromIndex=10,toIndex=3 shifts [3..9] to [4..10]
    // (moving backward through the range so each write's source hasn't
    // been overwritten yet); fromIndex=3,toIndex=10 shifts [4..10] to
    // [3..9] (moving forward, same reasoning).
    if (toIndex < fromIndex) {
        for (int i = fromIndex; i > toIndex; --i) {
            auto name = nameRecordBytes(setlistIndex, i - 1);
            auto params = songRecordBytes(setlistIndex, i - 1);
            if (!name || !params) return false;
            putNameRecordBytes(setlistIndex, i, *name);
            putSongRecordBytes(setlistIndex, i, *params);
        }
    } else {
        for (int i = fromIndex; i < toIndex; ++i) {
            auto name = nameRecordBytes(setlistIndex, i + 1);
            auto params = songRecordBytes(setlistIndex, i + 1);
            if (!name || !params) return false;
            putNameRecordBytes(setlistIndex, i, *name);
            putSongRecordBytes(setlistIndex, i, *params);
        }
    }

    putNameRecordBytes(setlistIndex, toIndex, *movingName);
    putSongRecordBytes(setlistIndex, toIndex, *movingParams);
    return true;
}

bool PcgFile::copySetlist(int srcSetlistIndex, int dstSetlistIndex) {
    if (srcSetlistIndex < 0 || static_cast<size_t>(srcSetlistIndex) >= setlists_.size()) return false;
    if (dstSetlistIndex < 0 || static_cast<size_t>(dstSetlistIndex) >= setlists_.size()) return false;
    if (srcSetlistIndex == dstSetlistIndex) return true;

    const int count = static_cast<int>(setlists_[static_cast<size_t>(srcSetlistIndex)].songs.size());
    for (int i = 0; i < count; ++i) {
        auto name = nameRecordBytes(srcSetlistIndex, i);
        auto params = songRecordBytes(srcSetlistIndex, i);
        if (!name || !params) return false;
        if (!putNameRecordBytes(dstSetlistIndex, i, *name)) return false;
        if (!putSongRecordBytes(dstSetlistIndex, i, *params)) return false;
    }
    return true;
}

bool PcgFile::sortSetlist(int setlistIndex, bool ascending) {
    if (setlistIndex < 0 || static_cast<size_t>(setlistIndex) >= setlists_.size()) return false;

    const auto& songs = setlists_[static_cast<size_t>(setlistIndex)].songs;
    const int count = static_cast<int>(songs.size());

    std::vector<int> order(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) order[static_cast<size_t>(i)] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const bool aEmpty = songs[static_cast<size_t>(a)].name.empty();
        const bool bEmpty = songs[static_cast<size_t>(b)].name.empty();
        if (aEmpty != bEmpty) return !aEmpty;  // non-empty always sorts before empty
        if (aEmpty) return false;              // both empty -- keep their relative order
        return ascending ? songs[static_cast<size_t>(a)].name < songs[static_cast<size_t>(b)].name
                          : songs[static_cast<size_t>(a)].name > songs[static_cast<size_t>(b)].name;
    });

    // Snapshot every slot's raw bytes before writing any of them back --
    // this touches all 128 slots in a new, non-contiguous order (unlike
    // reorderSong()'s single-range shift), so there's no safe direction to
    // read-then-write in without a slot's own bytes getting overwritten
    // before something else has read them.
    std::vector<std::vector<uint8_t>> names(static_cast<size_t>(count));
    std::vector<std::vector<uint8_t>> params(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        auto name = nameRecordBytes(setlistIndex, i);
        auto p = songRecordBytes(setlistIndex, i);
        if (!name || !p) return false;
        names[static_cast<size_t>(i)] = std::move(*name);
        params[static_cast<size_t>(i)] = std::move(*p);
    }
    for (int i = 0; i < count; ++i) {
        const size_t from = static_cast<size_t>(order[static_cast<size_t>(i)]);
        if (!putNameRecordBytes(setlistIndex, i, names[from])) return false;
        if (!putSongRecordBytes(setlistIndex, i, params[from])) return false;
    }
    return true;
}

}  // namespace kronos
