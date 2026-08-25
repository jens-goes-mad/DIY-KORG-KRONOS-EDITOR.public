// Scoped, fast C++ test target: depends only on PcgFile.cpp/ProgramDecoder.cpp
// (see CMakeLists.txt's pcg_file_test target) -- deliberately NOT main.cpp/
// EditorBridge.cpp/CHOC, so `ctest` builds and runs in seconds, no WebView
// toolchain required. Hand-rolled assertions (CHECK/CHECK_EQ below) rather
// than a pulled-in framework, matching this project's no-extra-dependencies
// convention (see CLAUDE.md).
//
// Real .PCG files are large and .gitignore'd (never committed), so this
// builds a small synthetic file in memory instead, byte-for-byte matching
// the confirmed chunk/record layout documented in docs/content/format/index.md -- enough
// to exercise PcgFile::loadFromMemory() end-to-end (SDB1 Set List names,
// SBK1 slot params incl. the Font size/Transpose bit-packing, PBK1 Program
// banks, cross-referencing, duplicate detection, and decodeProgram()'s
// on-demand re-decode) without needing a real backup on disk.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "kronos/CombiDecoder.h"
#include "kronos/PcgFile.h"
#include "kronos/ProgramDecoder.h"

namespace {

int g_failures = 0;

void reportCheck(bool pass, const char* exprText, const char* file, int line) {
    if (pass) return;
    g_failures++;
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, exprText);
}

template <typename A, typename B>
void reportCheckEq(bool pass, const A& actual, const B& expected, const char* label, const char* file, int line) {
    if (pass) return;
    g_failures++;
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, label);
}

#define CHECK(expr) reportCheck((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(actual, expected, label) reportCheckEq((actual) == (expected), actual, expected, label, __FILE__, __LINE__)

// --- Synthetic .PCG byte-builder helpers ------------------------------

void pushU32BE(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}

void pushZeros(std::vector<uint8_t>& v, size_t n) { v.insert(v.end(), n, uint8_t{0}); }

// A 4-byte-prefix + 24-byte space/NUL-padded name field, the shape shared by
// Set List/Song records (SDB1) and bank records (PBK1/CBK1) -- see
// docs/content/format/index.md. `prefix` is arbitrary/unused data before the name.
void pushNameRecord(std::vector<uint8_t>& v, const std::string& name, size_t totalSize) {
    size_t start = v.size();
    pushZeros(v, totalSize);
    for (size_t i = 0; i < name.size() && i + 4 < totalSize; ++i) v[start + 4 + i] = static_cast<uint8_t>(name[i]);
}

// One CBK1 Combi record: name at offset+4 (same shape as PBK1), plus a
// couple of Timbre-to-Program references at the confirmed fixed stride
// (docs/content/format/index.md's "Combi Timbre references" section) -- byte0=number,
// byte1=rawBankCode, byte2's top 3 bits=status. Timbres 0 and 1 are set
// here; every other Timbre (2..15) stays all-zero, matching a genuinely
// unassigned Timbre (isDefault=true).
constexpr size_t kTimbreBaseOffset = 4806;
constexpr size_t kTimbreStride = 188;

std::vector<uint8_t> makeCbkCombiRecord(const std::string& name, size_t totalSize) {
    std::vector<uint8_t> rec(totalSize, 0);
    for (size_t i = 0; i < name.size() && 4 + i < totalSize; ++i) rec[4 + i] = static_cast<uint8_t>(name[i]);
    rec[kTimbreBaseOffset] = 5;                                   // Timbre 0 -> Program number 5
    rec[kTimbreBaseOffset + 1] = 1;                               // Timbre 0 -> raw bank code 1 (INT-B)
    rec[kTimbreBaseOffset + 2] = static_cast<uint8_t>(1 << 5);     // status Internal
    // Timbre 1 -> raw bank code 20 (USER-D, PBK1 file-order index 9 --
    // see kConfirmedTimbreBanks in PcgFile.cpp) -- exercises the raw-code
    // <-> file-order-index translation for a bank where the two numbers
    // differ, not just the INT-A..D range where they happen to coincide.
    rec[kTimbreBaseOffset + kTimbreStride] = 7;                                // Timbre 1 -> Program number 7
    rec[kTimbreBaseOffset + kTimbreStride + 1] = 20;                           // Timbre 1 -> raw bank code 20 (USER-D)
    rec[kTimbreBaseOffset + kTimbreStride + 2] = static_cast<uint8_t>(1 << 5);  // status Internal
    return rec;
}

// Real chunk header shape (confirmed via
// docs/external/Synthify-Kronos-PCG-File-Structures.xlsx, see PcgFile.cpp's
// readChunk()): [4-char tag][u32be size][4-byte unknown "dwX"][content].
// `dwX` itself is arbitrary/unused data here -- nothing in the parser reads
// it, only skips past it -- but it must be PRESENT (12-byte header, not the
// old 8-byte tag+size-only shape) for this fixture to actually exercise the
// real on-disk layout.
void appendChunk(std::vector<uint8_t>& out, const char* tag, const std::vector<uint8_t>& content) {
    out.insert(out.end(), tag, tag + 4);
    pushU32BE(out, static_cast<uint32_t>(content.size()));
    pushU32BE(out, 0);  // dwX -- unknown, unused by the parser
    out.insert(out.end(), content.begin(), content.end());
}

// One 542-byte SBK1 song record with the given Program bank/number and
// Comment, plus Font size/Transpose encoded via the confirmed bit-packing
// (docs/content/format/index.md §4.4): Font size's low 2 bits and Type+Color share byte
// +12, Font size's high bit and Transpose's low 3 bits share byte +17,
// Transpose's high 3 bits share byte +13 with Bank. `colorField1based` and
// `garbageLow4` deliberately poke bits Font size/Transpose/Bank/Color do
// NOT own, to prove decoding only reads the bits it actually owns (mirrors
// setlist-editor-comment-and-font.test.js's bit-preservation check, but for the C++
// decoder instead of the JS encoder). Byte +12 has no spare bit left to
// poke this way anymore -- Type is confirmed 2 bits wide (bits0-1, not
// just bit0), so combined with Color (bits2-5) and Font size (bits6-7)
// every bit in this byte is now owned by something real.
std::vector<uint8_t> makeSbkSongRecord(bool isProgram, int bank, int number, int colorField1based, int holdTime,
                                        int volume, int fontSizeValue, int transpose, int garbageLow4,
                                        const std::string& comment) {
    std::vector<uint8_t> rec(542, 0);

    int unsigned6 = transpose >= 0 ? transpose : transpose + 64;
    int bankHigh3 = (unsigned6 >> 3) & 0x07;
    int fontLow3 = unsigned6 & 0x07;

    uint8_t typeColor = 0;
    typeColor |= isProgram ? 0x01 : 0x00;  // Type: bits0-1, 0=Combi/1=Program/2=Song -- see kSbkTypeMask in PcgFile.cpp
    typeColor |= static_cast<uint8_t>(((colorField1based - 1) & 0x0F) << 2);
    typeColor |= static_cast<uint8_t>((fontSizeValue & 0x02) ? 0x80 : 0x00);
    typeColor |= static_cast<uint8_t>((fontSizeValue & 0x01) ? 0x40 : 0x00);

    uint8_t bankByte = static_cast<uint8_t>((bank & 0x1F) | (bankHigh3 << 5));
    uint8_t fontTransposeByte =
        static_cast<uint8_t>((garbageLow4 & 0x0F) | ((fontSizeValue & 0x04) ? 0x10 : 0x00) | (fontLow3 << 5));

    rec[12] = typeColor;
    rec[13] = bankByte;
    rec[14] = static_cast<uint8_t>(number);
    rec[15] = static_cast<uint8_t>(holdTime + 1);
    rec[16] = static_cast<uint8_t>(volume);
    rec[17] = fontTransposeByte;

    for (size_t i = 0; i < comment.size() && 18 + i < rec.size() - 1; ++i) {
        rec[18 + i] = static_cast<uint8_t>(comment[i]);
    }
    return rec;
}

// Builds a whole synthetic KORG file: one Set List (SDB1), its slot params
// (SBK1), and two PBK1 Program banks -- bank 0 has an intentional byte-exact
// duplicate pair (records 0 and 1), bank 1 has two distinct records, one of
// which (bank 1, number 0) is what both crafted song slots reference, so
// programSetlistUsages() has something real to count.
std::vector<uint8_t> buildSyntheticPcgFile() {
    constexpr uint32_t kSongsPerSetlist = 128;
    constexpr size_t kRecordSize = 28;      // SDB1 name-record stride
    constexpr size_t kSbkHeaderSize = 40;
    constexpr size_t kSbkRecordSize = 542;
    constexpr size_t kBankRecordSize = 32;  // PBK1 record stride for this test (real files use ~4960)

    // SDB1: two Set Lists. Setlist 0 ("Test Setlist") has song 0/1 named --
    // the original fixture content, still exercised by every test below
    // that references setlists()[0]. Setlist 1 ("Gig Setlist") exists
    // purely for copySetlist()'s own test: one pre-existing song ("Old
    // Song") that a copy from setlist 0 must overwrite, proving the
    // operation actually writes into the destination rather than just
    // reading the source. Header is 2 u32be fields (numSetlists,
    // bytesPerSetlist) -- NOT 3; there is no separate leading "count"
    // field, see PcgFile.cpp's own note on this (confirmed against a real
    // 36MB backup, 2026-08-08).
    std::vector<uint8_t> sdb1;
    pushU32BE(sdb1, 2);                                     // numSetlists
    pushU32BE(sdb1, (kSongsPerSetlist + 1) * kRecordSize);  // bytesPerSetlist
    pushNameRecord(sdb1, "Test Setlist", kRecordSize);
    pushNameRecord(sdb1, "Song Zero", kRecordSize);
    pushNameRecord(sdb1, "Song One", kRecordSize);
    for (uint32_t k = 2; k < kSongsPerSetlist; ++k) pushZeros(sdb1, kRecordSize);
    pushNameRecord(sdb1, "Gig Setlist", kRecordSize);
    pushNameRecord(sdb1, "Old Song", kRecordSize);
    for (uint32_t k = 1; k < kSongsPerSetlist; ++k) pushZeros(sdb1, kRecordSize);

    // SBK1: same two Set Lists' slot params. Setlist 0, song 0 -> Program
    // bank1/number0, Font size L (3), transpose -5. Song 1 -> same Program
    // (bank1/number0, to exercise a 2-usage count), Font size XS (1),
    // transpose +20. Setlist 1, song 0 -> a different Program
    // (bank1/number1), distinct Comment -- copySetlist()'s test overwrites
    // this with setlist 0's song 0 and confirms the old content is gone.
    std::vector<uint8_t> sbk1;
    pushU32BE(sbk1, 2);  // numSetlists
    pushU32BE(sbk1, static_cast<uint32_t>(kSbkHeaderSize + kSongsPerSetlist * kSbkRecordSize));  // bytesPerSetlist
    pushZeros(sbk1, kSbkHeaderSize);
    auto song0 = makeSbkSongRecord(/*isProgram=*/true, /*bank=*/1, /*number=*/0, /*color=*/1, /*holdTime=*/4,
                                    /*volume=*/100, /*fontSizeValue=*/3, /*transpose=*/-5,
                                    /*garbageLow4=*/0x0B, "Hello test");
    auto song1 = makeSbkSongRecord(/*isProgram=*/true, /*bank=*/1, /*number=*/0, /*color=*/5, /*holdTime=*/9,
                                    /*volume=*/80, /*fontSizeValue=*/1, /*transpose=*/20,
                                    /*garbageLow4=*/0x05, "second");
    sbk1.insert(sbk1.end(), song0.begin(), song0.end());
    sbk1.insert(sbk1.end(), song1.begin(), song1.end());
    for (uint32_t k = 2; k < kSongsPerSetlist; ++k) pushZeros(sbk1, kSbkRecordSize);
    pushZeros(sbk1, kSbkHeaderSize);
    auto gigSong0 = makeSbkSongRecord(/*isProgram=*/true, /*bank=*/1, /*number=*/1, /*color=*/2, /*holdTime=*/2,
                                       /*volume=*/60, /*fontSizeValue=*/0, /*transpose=*/0,
                                       /*garbageLow4=*/0x00, "should be overwritten");
    sbk1.insert(sbk1.end(), gigSong0.begin(), gigSong0.end());
    for (uint32_t k = 1; k < kSongsPerSetlist; ++k) pushZeros(sbk1, kSbkRecordSize);

    // PBK1 bank 0: records 0 and 1 byte-identical (a duplicate pair), record
    // 2 unique -- exercises findDuplicatePrograms(). Records 3 and 4 are
    // left empty (all-zero -- an unassigned slot, same convention as an
    // unused Set List song slot) so testCopyProgramFrom() has genuinely
    // empty same-type targets to copy into.
    std::vector<uint8_t> pbk1BankA;
    pushU32BE(pbk1BankA, 5);  // numRecords
    pushU32BE(pbk1BankA, static_cast<uint32_t>(kBankRecordSize));  // bytesPerRecord
    pushNameRecord(pbk1BankA, "Test Program A", kBankRecordSize);
    pushNameRecord(pbk1BankA, "Test Program A", kBankRecordSize);  // byte-exact duplicate of the record above
    pushNameRecord(pbk1BankA, "Unique Program", kBankRecordSize);
    pushZeros(pbk1BankA, kBankRecordSize);  // record 3 -- empty
    size_t record4Start = pbk1BankA.size();
    pushZeros(pbk1BankA, kBankRecordSize);  // record 4 -- empty
    // Differ from record 3 in a byte outside the name field (offset 4..28,
    // see pushNameRecord()) -- both still decode to an empty name, but two
    // byte-IDENTICAL empty records would otherwise register as a spurious
    // second duplicate pair, breaking the "exactly one duplicate group"
    // check below.
    pbk1BankA[record4Start] = 0xFF;

    // PBK1 bank 1: two distinct records -- number 0 is what both song slots
    // above reference.
    std::vector<uint8_t> pbk1BankB;
    pushU32BE(pbk1BankB, 2);  // numRecords
    pushU32BE(pbk1BankB, static_cast<uint32_t>(kBankRecordSize));  // bytesPerRecord
    pushNameRecord(pbk1BankB, "Bank1 Program0", kBankRecordSize);
    pushNameRecord(pbk1BankB, "Bank1 Program1", kBankRecordSize);

    // CBK1 bank 0: one Combi record with a real Timbre 0 (Program bank1/
    // number5, status Internal) and 15 default/unassigned Timbres.
    const size_t kCombiRecordSize = kTimbreBaseOffset + kTimbreStride * 16;
    std::vector<uint8_t> cbk1BankA;
    pushU32BE(cbk1BankA, 1);  // numRecords
    pushU32BE(cbk1BankA, static_cast<uint32_t>(kCombiRecordSize));  // bytesPerRecord
    auto combi0 = makeCbkCombiRecord("Test Combi", kCombiRecordSize);
    cbk1BankA.insert(cbk1BankA.end(), combi0.begin(), combi0.end());

    // Real hierarchy (docs/content/format/index.md §2), not a flat sibling list: SDB1/SBK1
    // nest inside SLS1, PBK1/MBK1 inside PRG1, CBK1 inside CMB1 -- each
    // wrapping chunk's own declared `size` is exactly the sum of its
    // children's full sizes (header + content each), the invariant
    // readChunk()'s recursive walk depends on. A DIV1 sibling (content
    // deliberately empty -- this project doesn't decode it) sits before
    // SLS1, matching real file order and exercising a zero-size chunk.
    // Nesting this way is what actually exercises readChunk()'s 12-byte
    // header fix end-to-end -- a flat sibling list (the previous shape of
    // this fixture) still passed even with the old, wrong 8-byte-header
    // math, because nothing was nested deeply enough to drift out of sync.
    std::vector<uint8_t> sls1Content;
    appendChunk(sls1Content, "SDB1", sdb1);
    appendChunk(sls1Content, "SBK1", sbk1);

    std::vector<uint8_t> prg1Content;
    appendChunk(prg1Content, "PBK1", pbk1BankA);
    // Tagged MBK1 (EXi), not PBK1, so the synthetic fixture exercises both
    // classifyProgramBankType() paths end-to-end through loadFromMemory(),
    // not just the standalone unit test below. The toy kBankRecordSize (32)
    // deliberately doesn't match either real HD-1/EXi stride (4960/3706) --
    // that's fine, tagMatchesStride is expected to be false for both banks
    // in this synthetic fixture; only `type` (derived from the tag) matters
    // for the end-to-end assertions.
    appendChunk(prg1Content, "MBK1", pbk1BankB);

    std::vector<uint8_t> cmb1Content;
    appendChunk(cmb1Content, "CBK1", cbk1BankA);

    // PCG1 itself is a real chunk starting at byte 16 (confirmed against a
    // real 36MB backup, 2026-08-08: its own declared size exactly spans
    // the rest of the file) -- DIV1/SLS1/PRG1/CMB1 are its children, one
    // level in, not siblings alongside some 16-byte "file header" that
    // already consumes PCG1's own tag+size+dwX.
    std::vector<uint8_t> pcg1Content;
    appendChunk(pcg1Content, "DIV1", {});
    appendChunk(pcg1Content, "SLS1", sls1Content);
    appendChunk(pcg1Content, "PRG1", prg1Content);
    appendChunk(pcg1Content, "CMB1", cmb1Content);

    std::vector<uint8_t> data;
    data.insert(data.end(), {'K', 'O', 'R', 'G'});
    pushZeros(data, 12);  // pad to the 16-byte offset every chunk walk starts from
    appendChunk(data, "PCG1", pcg1Content);
    return data;
}

void testDecodeProgramFields() {
    std::vector<uint8_t> record(32, 0);
    const std::string name = "Padded Name";
    for (size_t i = 0; i < name.size(); ++i) record[4 + i] = static_cast<uint8_t>(name[i]);
    // bytes 4+name.size() .. 27 stay 0 (NUL padding) -- decodeProgramFields
    // must trim that padding, not include it in the returned name.

    kronos::ProgramFields fields = kronos::decodeProgramFields(record.data(), record.size(), 3, 7);
    CHECK_EQ(fields.bank, 3, "decodeProgramFields keeps the caller-supplied bank");
    CHECK_EQ(fields.number, 7, "decodeProgramFields keeps the caller-supplied number");
    CHECK_EQ(fields.name, name, "decodeProgramFields trims trailing NUL padding from the name");

    // Truncated record (shorter than the name field needs) degrades to an
    // empty name rather than reading out of bounds.
    std::vector<uint8_t> tooShort(10, 0);
    kronos::ProgramFields shortFields = kronos::decodeProgramFields(tooShort.data(), tooShort.size(), 0, 0);
    CHECK_EQ(shortFields.name, std::string(), "decodeProgramFields on a truncated record yields an empty name");
    CHECK_EQ(shortFields.exiAlgorithmType, 0, "a too-short record leaves exiAlgorithmType at its default (0/Off)");

    // exiAlgorithmType reads a plain byte at file offset 2861 (confirmed --
    // see ProgramFields::exiAlgorithmType's own doc comment) -- a synthetic
    // check here that the right BYTE is read, real-file ground truth (the
    // checked-in Init-Program-*.raw templates) covered separately below in
    // testExiAlgorithmTypeRealTemplates().
    std::vector<uint8_t> withAlgo(2862, 0);
    withAlgo[2861] = 2;  // AL-1
    kronos::ProgramFields algoFields = kronos::decodeProgramFields(withAlgo.data(), withAlgo.size(), 0, 0);
    CHECK_EQ(algoFields.exiAlgorithmType, 2, "decodeProgramFields reads Algorithm Type from file offset 2861");
}

void testClassifyProgramBankType() {
    // Tag is the primary signal -- PBK1=Hd1, MBK1=Exi -- independent of stride.
    auto hd1Match = kronos::classifyProgramBankType("PBK1", 4960);
    CHECK(hd1Match.type == kronos::ProgramBankType::Hd1);
    CHECK_EQ(hd1Match.tagMatchesStride, true, "PBK1 tag with the expected HD-1 stride (4960) matches");

    // EXi's own expected stride was corrected 2026-08-16 (docs/content/
    // format/index.md §5.5): both bank types are really 4960 bytes, not
    // 3706 -- confirmed against two real files AND, independently, Korg's
    // own Prog_EXi_Common.txt ("EXi Program Size: 4960 byte").
    auto exiMatch = kronos::classifyProgramBankType("MBK1", 4960);
    CHECK(exiMatch.type == kronos::ProgramBankType::Exi);
    CHECK_EQ(exiMatch.tagMatchesStride, true, "MBK1 tag with the expected EXi stride (4960) matches");

    // A stride that doesn't match the tag's expected value is a genuine
    // anomaly worth flagging, not silently ignored -- `type` still follows
    // the tag either way (the more authoritative signal), but the mismatch
    // flag must go false.
    auto hd1Mismatch = kronos::classifyProgramBankType("PBK1", 3706);
    CHECK(hd1Mismatch.type == kronos::ProgramBankType::Hd1);
    CHECK_EQ(hd1Mismatch.tagMatchesStride, false, "PBK1 tag with a wrong stride is flagged as a mismatch");

    auto exiMismatch = kronos::classifyProgramBankType("MBK1", 3706);
    CHECK(exiMismatch.type == kronos::ProgramBankType::Exi);
    CHECK_EQ(exiMismatch.tagMatchesStride, false, "MBK1 tag with the old (now-corrected) wrong stride is flagged as a mismatch");

    // Any tag other than MBK1 defaults to Hd1 -- PBK1 is the only real
    // HD-1 tag this format uses, but this keeps classification total rather
    // than needing a third "unknown" state for a tag that shouldn't occur.
    auto unknownTag = kronos::classifyProgramBankType("XBK1", 1234);
    CHECK(unknownTag.type == kronos::ProgramBankType::Hd1);
}

// Ground truth for ProgramFields::exiAlgorithmType's confirmed offset/enum,
// against the two REAL byte-extracted Program record templates already
// checked into resources/ (docs/content/format/index.md §5.5 -- both
// cross-verified against two independent real backup files when they were
// first extracted) -- not synthetic data, per this project's "real Kronos
// data makes tests worth trusting" convention.
void testExiAlgorithmTypeRealTemplates() {
    auto readResource = [](const char* relativePath) -> std::vector<uint8_t> {
        std::ifstream file(std::string(EDITOR_RESOURCES_DIR) + "/" + relativePath, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    };

    auto hd1 = readResource("Init-Program-HD1.raw");
    CHECK_EQ(static_cast<int>(hd1.size()), 4960, "Init-Program-HD1.raw is a real 4960-byte Program record");
    if (hd1.size() == 4960) {
        auto fields = kronos::decodeProgramFields(hd1.data(), hd1.size(), 0, 0);
        CHECK_EQ(fields.exiAlgorithmType, 0, "an HD-1 Program's EXi1 Algorithm Type reads Off -- no EXi engine active");
    }

    auto exi = readResource("Init-Program-EXi.raw");
    CHECK_EQ(static_cast<int>(exi.size()), 4960, "Init-Program-EXi.raw is a real 4960-byte Program record");
    if (exi.size() == 4960) {
        auto fields = kronos::decodeProgramFields(exi.data(), exi.size(), 0, 0);
        CHECK_EQ(fields.exiAlgorithmType, 2, "the factory Init EXi Program defaults to AL-1 (2)");
    }
}

void testDecodeCombiFields() {
    std::vector<uint8_t> record(kTimbreBaseOffset + kTimbreStride * 16, 0);
    const std::string name = "Combi Name";
    for (size_t i = 0; i < name.size(); ++i) record[4 + i] = static_cast<uint8_t>(name[i]);
    record[kTimbreBaseOffset] = 42;                             // Timbre 0 -> Program number 42
    record[kTimbreBaseOffset + 1] = 3;                          // Timbre 0 -> raw bank code 3
    record[kTimbreBaseOffset + 2] = static_cast<uint8_t>(3 << 5);  // status External

    kronos::CombiFields fields = kronos::decodeCombiFields(record.data(), record.size(), 2, 9);
    CHECK_EQ(fields.bank, 2, "decodeCombiFields keeps the caller-supplied bank");
    CHECK_EQ(fields.number, 9, "decodeCombiFields keeps the caller-supplied number");
    CHECK_EQ(fields.name, name, "decodeCombiFields trims trailing NUL padding from the name");
    CHECK_EQ(fields.timbres.size(), static_cast<size_t>(16), "always 16 Timbre entries");
    CHECK_EQ(fields.timbres[0].number, 42, "Timbre 0 number");
    CHECK_EQ(fields.timbres[0].rawBankCode, 3, "Timbre 0 rawBankCode");
    CHECK(fields.timbres[0].status == kronos::TimbreStatus::External);
    CHECK(!fields.timbres[0].isDefault);
    CHECK(fields.timbres[1].isDefault);  // untouched -- genuinely unassigned

    // Truncated record: shorter than even the Timbre area needs -- every
    // Timbre degrades to a default (isDefault=true) rather than reading OOB.
    std::vector<uint8_t> tooShort(100, 0);
    kronos::CombiFields shortFields = kronos::decodeCombiFields(tooShort.data(), tooShort.size(), 0, 0);
    CHECK_EQ(shortFields.name, std::string(), "decodeCombiFields on a truncated record yields an empty name");
    CHECK(shortFields.timbres[0].isDefault);
}

void testHashProgramRecord() {
    std::vector<uint8_t> a = {1, 2, 3, 4, 5};
    std::vector<uint8_t> aCopy = a;
    std::vector<uint8_t> b = {1, 2, 3, 4, 6};

    CHECK_EQ(kronos::hashProgramRecord(a.data(), a.size()), kronos::hashProgramRecord(aCopy.data(), aCopy.size()),
             "hashProgramRecord is deterministic for identical bytes");
    CHECK(kronos::hashProgramRecord(a.data(), a.size()) != kronos::hashProgramRecord(b.data(), b.size()));
}

// Real third-party PCG sound-bank distributions (donated for testing:
// HALEN-SPLIT.PCG, JMJ KRONOS 2.PCG) contain zero SLS1/SDB1/SBK1 anywhere
// in the hierarchy -- just PRG1/CBK1 (one also had WSQ1/DPI1 Drum Sample
// data, not modeled here since nothing reads it yet). Both failed to load
// entirely before this fixture's fix (loadFromMemory() treated a missing
// SDB1 as fatal) -- Set Lists are just one of several categories the
// Kronos's own backup dialog lets you include/exclude, not something every
// real PCG is guaranteed to have.
std::vector<uint8_t> buildNoSetlistsPcgFile() {
    constexpr size_t kBankRecordSize = 32;

    std::vector<uint8_t> pbk1BankA;
    pushU32BE(pbk1BankA, 2);  // numRecords
    pushU32BE(pbk1BankA, static_cast<uint32_t>(kBankRecordSize));  // bytesPerRecord
    pushNameRecord(pbk1BankA, "No Setlist Program 0", kBankRecordSize);
    pushNameRecord(pbk1BankA, "No Setlist Program 1", kBankRecordSize);

    const size_t kCombiRecordSize = kTimbreBaseOffset + kTimbreStride * 16;
    std::vector<uint8_t> cbk1BankA;
    pushU32BE(cbk1BankA, 1);  // numRecords
    pushU32BE(cbk1BankA, static_cast<uint32_t>(kCombiRecordSize));  // bytesPerRecord
    auto combi0 = makeCbkCombiRecord("No Setlist Combi", kCombiRecordSize);
    cbk1BankA.insert(cbk1BankA.end(), combi0.begin(), combi0.end());

    std::vector<uint8_t> prg1Content;
    appendChunk(prg1Content, "PBK1", pbk1BankA);

    std::vector<uint8_t> cmb1Content;
    appendChunk(cmb1Content, "CBK1", cbk1BankA);

    // No SLS1 sibling at all -- matches the real donated files exactly,
    // rather than an SLS1 wrapper with empty SDB1/SBK1 children (a weaker
    // test: real files omit the wrapper chunk itself, not just its content).
    std::vector<uint8_t> pcg1Content;
    appendChunk(pcg1Content, "DIV1", {});
    appendChunk(pcg1Content, "PRG1", prg1Content);
    appendChunk(pcg1Content, "CMB1", cmb1Content);

    std::vector<uint8_t> data;
    data.insert(data.end(), {'K', 'O', 'R', 'G'});
    pushZeros(data, 12);
    appendChunk(data, "PCG1", pcg1Content);
    return data;
}

void testPcgFileNoSetlists() {
    kronos::PcgFile pcg;
    std::string error;
    std::vector<uint8_t> data = buildNoSetlistsPcgFile();
    bool ok = pcg.loadFromMemory(data, error);

    CHECK(ok);
    CHECK_EQ(error, std::string(""), "no error message on a file with no Set Lists at all");
    CHECK_EQ(pcg.setlists().size(), static_cast<size_t>(0), "zero Set Lists, not a load failure");
    CHECK_EQ(pcg.programs().size(), static_cast<size_t>(2), "Programs still load fine without SDB1");
    CHECK_EQ(pcg.combis().size(), static_cast<size_t>(1), "Combis still load fine without SDB1");
}

void testPcgFileEndToEnd() {
    kronos::PcgFile pcg;
    std::string error;
    std::vector<uint8_t> data = buildSyntheticPcgFile();
    bool loaded = pcg.loadFromMemory(std::move(data), error);
    CHECK(loaded);
    if (!loaded) {
        std::fprintf(stderr, "  loadFromMemory error: %s\n", error.c_str());
        return;
    }

    CHECK_EQ(pcg.setlists().size(), static_cast<size_t>(2), "two synthetic Set Lists loaded");
    const auto& setlist = pcg.setlists()[0];
    CHECK_EQ(setlist.name, std::string("Test Setlist"), "Set List name read from SDB1");
    CHECK_EQ(setlist.songs.size(), static_cast<size_t>(128), "every Set List has 128 song slots");
    CHECK_EQ(setlist.songs[0].name, std::string("Song Zero"), "song 0 name read from SDB1");

    // Setlist 1 ("Gig Setlist") -- exists purely for copySetlist()'s own
    // test further down; sanity-check its baseline content loaded correctly
    // before that test starts overwriting it.
    CHECK_EQ(pcg.setlists()[1].name, std::string("Gig Setlist"), "second Set List's own name read from SDB1");
    CHECK_EQ(pcg.setlists()[1].songs[0].name, std::string("Old Song"), "second Set List's song 0 name");
    CHECK_EQ(pcg.setlists()[1].songs[0].comment, std::string("should be overwritten"),
             "second Set List's song 0 comment");

    // Song 0: Font size L, transpose -5, despite garbage bits in bytes+12/17
    // that Font size/Transpose/Color/Bank don't own (proves readSlotParams()
    // masks instead of reading raw bytes).
    const auto& p0 = setlist.songs[0].params;
    CHECK(p0.found);
    CHECK(p0.isProgram);
    CHECK_EQ(p0.bank, 1, "song 0 Program bank");
    CHECK_EQ(p0.number, 0, "song 0 Program number");
    CHECK_EQ(p0.color, 1, "song 0 color unaffected by garbage bit1");
    CHECK_EQ(p0.holdTime, 4, "song 0 Hold Time (stored value - 1)");
    CHECK_EQ(p0.volume, 100, "song 0 Volume");
    CHECK(p0.fontSize == kronos::FontSize::L);
    CHECK_EQ(p0.transpose, -5, "song 0 Transpose (signed, despite garbage low bits in byte+17)");
    CHECK_EQ(setlist.songs[0].comment, std::string("Hello test"), "song 0 Comment");
    CHECK_EQ(setlist.songs[0].instrumentName, std::string("Bank1 Program0"),
             "song 0 cross-referenced to its Program's real name");

    // Song 1: same Program, different Font size/Transpose/garbage bits.
    const auto& p1 = setlist.songs[1].params;
    CHECK(p1.fontSize == kronos::FontSize::XS);
    CHECK_EQ(p1.transpose, 20, "song 1 Transpose");
    CHECK_EQ(p1.color, 5, "song 1 color");
    CHECK_EQ(setlist.songs[1].comment, std::string("second"), "song 1 Comment");
    CHECK_EQ(setlist.songs[1].instrumentName, std::string("Bank1 Program0"), "song 1 resolves to the same Program");

    // Programs table: 5 (bank 0, incl. 2 empty trailing records) + 2 (bank 1) = 7 rows.
    CHECK_EQ(pcg.programs().size(), static_cast<size_t>(7), "programs() has one row per PBK1 record");

    // Bank type is classified from each bank's own chunk tag: bank 0 is
    // tagged PBK1 (Hd1), bank 1 is tagged MBK1 (Exi) in this fixture.
    CHECK(pcg.programs()[0].bankType == kronos::ProgramBankType::Hd1);
    CHECK(pcg.programs()[5].bankType == kronos::ProgramBankType::Exi);

    // programBankTypes() gives the same classification per-bank, without
    // needing a specific Program row -- one entry per bank actually present
    // (2 in this fixture), matching programs()'s own per-row bankType.
    auto bankTypes = pcg.programBankTypes();
    CHECK_EQ(bankTypes.size(), static_cast<size_t>(2), "programBankTypes() has one entry per PBK1/MBK1 bank");
    if (bankTypes.size() == 2) {
        CHECK_EQ(bankTypes[0].bank, 0, "programBankTypes()[0] is bank 0");
        CHECK(bankTypes[0].bankType == kronos::ProgramBankType::Hd1);
        CHECK_EQ(bankTypes[1].bank, 1, "programBankTypes()[1] is bank 1");
        CHECK(bankTypes[1].bankType == kronos::ProgramBankType::Exi);
    }

    // Duplicate detection: exactly one group, bank0/number0 + bank0/number1.
    auto dupGroups = pcg.findDuplicatePrograms();
    CHECK_EQ(dupGroups.size(), static_cast<size_t>(1), "exactly one duplicate group found");
    if (dupGroups.size() == 1) {
        CHECK_EQ(dupGroups[0].size(), static_cast<size_t>(2), "the duplicate group has 2 byte-exact members");
        CHECK_EQ(dupGroups[0][0].bank, 0, "duplicate group member 0 bank");
        CHECK_EQ(dupGroups[0][0].number, 0, "duplicate group member 0 number");
        CHECK_EQ(dupGroups[0][1].bank, 0, "duplicate group member 1 bank");
        CHECK_EQ(dupGroups[0][1].number, 1, "duplicate group member 1 number");
    }

    // Set-List usage: both song 0 and song 1 reference bank1/number0.
    auto usages = pcg.programSetlistUsages(1, 0);
    CHECK_EQ(usages.size(), static_cast<size_t>(2), "bank1/number0 is used by exactly 2 Set List slots");

    // decodeProgram() re-decodes straight from the retained raw bytes,
    // independently of the programs_ table built once at load time.
    auto redecoded = pcg.decodeProgram(1, 0);
    CHECK(redecoded.has_value());
    if (redecoded) {
        CHECK_EQ(redecoded->name, std::string("Bank1 Program0"), "decodeProgram() re-decodes the right record");
        CHECK_EQ(redecoded->contentHash, pcg.programs()[5].contentHash,
                 "decodeProgram()'s hash matches the same record's cached table entry");
        CHECK(redecoded->bankType == kronos::ProgramBankType::Exi);
    }
    CHECK(!pcg.decodeProgram(99, 0).has_value());  // out-of-range bank
    CHECK(!pcg.decodeProgram(1, 99).has_value());  // out-of-range number

    // copyProgramFrom(): same-dataset copy (pcg passed as both src and dst,
    // exactly the documented "pass *this" case) exercising a real write plus
    // every rejection guard. Bank 0 (Hd1) records 3/4 were left empty
    // specifically for this. Order matters below -- each subtest targets a
    // slot untouched by the previous ones, so earlier writes don't change
    // later expectations.
    {
        // Successful copy: "Unique Program" (bank0/number2) into the empty
        // bank0/number3.
        auto ok = pcg.copyProgramFrom(pcg, 0, 2, 0, 3);
        CHECK(!ok.has_value());  // nullopt == success
        auto copied = pcg.decodeProgram(0, 3);
        CHECK(copied.has_value());
        if (copied) {
            CHECK_EQ(copied->name, std::string("Unique Program"), "copyProgramFrom() actually wrote the source's bytes");
            auto source = pcg.decodeProgram(0, 2);
            CHECK(source.has_value());
            if (source) CHECK_EQ(copied->contentHash, source->contentHash, "copied record hashes identically to its source");
        }
        // programs() cache must reflect the fresh write, not go stale.
        auto cacheEntry = std::find_if(pcg.programs().begin(), pcg.programs().end(),
                                        [](const kronos::ProgramInfo& p) { return p.bank == 0 && p.number == 3; });
        CHECK(cacheEntry != pcg.programs().end());
        if (cacheEntry != pcg.programs().end()) {
            CHECK_EQ(cacheEntry->name, std::string("Unique Program"), "programs() cache updated after copyProgramFrom()");
        }

        // Rejected: engine type mismatch (bank0=Hd1 -> bank1=Exi).
        auto typeMismatch = pcg.copyProgramFrom(pcg, 0, 2, 1, 0);
        CHECK(typeMismatch.has_value());
        if (typeMismatch) CHECK(*typeMismatch == kronos::PcgFile::ProgramCopyError::BankTypeMismatch);
        auto untouchedDst = pcg.decodeProgram(1, 0);
        CHECK(untouchedDst.has_value());
        if (untouchedDst) {
            CHECK_EQ(untouchedDst->name, std::string("Bank1 Program0"), "rejected type-mismatch copy leaves the destination untouched");
        }

        // Rejected: target slot already holds a *different* Program.
        auto occupied = pcg.copyProgramFrom(pcg, 0, 2, 0, 0);
        CHECK(occupied.has_value());
        if (occupied) CHECK(*occupied == kronos::PcgFile::ProgramCopyError::TargetSlotOccupied);
        auto stillOriginal = pcg.decodeProgram(0, 0);
        CHECK(stillOriginal.has_value());
        if (stillOriginal) {
            CHECK_EQ(stillOriginal->name, std::string("Test Program A"), "rejected occupied-slot copy leaves the destination untouched");
        }

        // Rejected: byte-identical Program already exists elsewhere in the
        // file ("Test Program A" already lives at bank0/number0 and number1).
        auto duplicate = pcg.copyProgramFrom(pcg, 0, 0, 0, 4);
        CHECK(duplicate.has_value());
        if (duplicate) CHECK(*duplicate == kronos::PcgFile::ProgramCopyError::DuplicateExists);
        auto stillEmpty = pcg.decodeProgram(0, 4);
        CHECK(stillEmpty.has_value());
        if (stillEmpty) {
            CHECK_EQ(stillEmpty->name, std::string(), "rejected duplicate copy leaves the empty destination untouched");
        }

        // Rejected: out-of-range bank/number on either side.
        auto outOfRange = pcg.copyProgramFrom(pcg, 99, 0, 0, 3);
        CHECK(outOfRange.has_value());
        if (outOfRange) CHECK(*outOfRange == kronos::PcgFile::ProgramCopyError::OutOfRange);
    }

    // Combis: one synthetic CBK1 record with real Timbres 0/1 and 14
    // default/unassigned Timbres.
    CHECK_EQ(pcg.combis().size(), static_cast<size_t>(1), "combis() has one row for the synthetic CBK1 record");
    const auto& combi0 = pcg.combis()[0];
    CHECK_EQ(combi0.bank, 0, "Combi bank");
    CHECK_EQ(combi0.number, 0, "Combi number");
    CHECK_EQ(combi0.name, std::string("Test Combi"), "Combi name decoded");
    CHECK_EQ(combi0.timbres.size(), static_cast<size_t>(16), "Combi always has 16 Timbre entries");
    CHECK_EQ(combi0.timbres[0].number, 5, "Combi Timbre 0 number");
    CHECK_EQ(combi0.timbres[0].rawBankCode, 1, "Combi Timbre 0 rawBankCode");
    CHECK(combi0.timbres[0].status == kronos::TimbreStatus::Internal);
    CHECK(!combi0.timbres[0].isDefault);
    CHECK_EQ(combi0.timbres[1].number, 7, "Combi Timbre 1 number");
    CHECK_EQ(combi0.timbres[1].rawBankCode, 20, "Combi Timbre 1 rawBankCode (USER-D)");
    CHECK(!combi0.timbres[1].isDefault);
    CHECK(combi0.timbres[2].isDefault);  // untouched -- genuinely unassigned

    auto redecodedCombi = pcg.decodeCombi(0, 0);
    CHECK(redecodedCombi.has_value());
    if (redecodedCombi) {
        CHECK_EQ(redecodedCombi->name, std::string("Test Combi"), "decodeCombi() re-decodes the right record");
        CHECK_EQ(redecodedCombi->timbres[0].number, 5, "decodeCombi()'s Timbre 0 matches combis()'s cached entry");
    }
    CHECK(!pcg.decodeCombi(99, 0).has_value());  // out-of-range bank
    CHECK(!pcg.decodeCombi(0, 99).has_value());  // out-of-range number

    // isConfirmedTimbreProgramBank()/timbreBankName()/combiUsagesForProgram()/
    // combiUsageCounts() -- the raw-code <-> file-order-index translation
    // (kConfirmedTimbreBanks in PcgFile.cpp) that lets Combi-usage counting
    // cover all 20 Program bank indices now (INT-A..F, USER-A..G/AA..GG,
    // fully confirmed as of 2026-08-14), not just INT-A..D where the two
    // number spaces happen to coincide.
    {
        CHECK(kronos::isConfirmedTimbreProgramBank(1));  // INT-B -- coincides with its own raw code
        CHECK(kronos::isConfirmedTimbreProgramBank(9));  // USER-D -- file-order index 9, raw code 20
        CHECK(!kronos::isConfirmedTimbreProgramBank(99));  // out-of-range index -- all 20 real ones are
                                                            // confirmed now (2026-08-14), see below
        CHECK(kronos::isConfirmedTimbreProgramBank(12));  // USER-G -- file-order index 12, raw code 23
        CHECK(kronos::isConfirmedTimbreProgramBank(19));  // USER-GG -- file-order index 19, raw code 30
        // Promoted 2026-08-11 -- were name-only confirmed (kConfirmedTimbreBankNamesOnly,
        // now removed), gained a confirmed index once §5.2's full 20-bank
        // order was confirmed against real hardware.
        CHECK(kronos::isConfirmedTimbreProgramBank(5));   // INT-F -- file-order index 5, raw code 5
        CHECK(kronos::isConfirmedTimbreProgramBank(7));   // USER-B -- file-order index 7, raw code 18
        CHECK(kronos::isConfirmedTimbreProgramBank(8));   // USER-C -- file-order index 8, raw code 19
        CHECK(kronos::isConfirmedTimbreProgramBank(15));  // USER-CC -- file-order index 15, raw code 26
        CHECK(kronos::isConfirmedTimbreProgramBank(16));  // USER-DD -- file-order index 16, raw code 27
        // CORRECTED 2026-08-14, retracting a 2026-08-11 misreading: this
        // project briefly had raw code 4 = USER-E (index 10), reported as
        // "a genuine surprise" -- the project owner re-checked the exact
        // same real Combi and confirmed real hardware actually shows
        // INT-E for that reference, not USER-E. INT-E is raw code 4
        // (coincides with its own index, same as INT-A..D/F -- no anomaly
        // after all); USER-E is raw code 21 (confirmed separately via a
        // different real Combi), also exactly the "obvious" gap in
        // USER-A..G's 17-23 block. See kConfirmedTimbreBanks's own doc
        // comment in PcgFile.cpp for the full story.
        CHECK(kronos::isConfirmedTimbreProgramBank(4));   // INT-E -- file-order index 4, raw code 4
        CHECK(kronos::isConfirmedTimbreProgramBank(10));  // USER-E -- file-order index 10, raw code 21
        CHECK(kronos::isConfirmedTimbreProgramBank(14));  // USER-BB -- file-order index 14, raw code 25
        CHECK(kronos::isConfirmedTimbreProgramBank(17));  // USER-EE -- file-order index 17, raw code 28
        // USER-FF -- confirmed 2026-08-14, the last of the 20 Program bank
        // indices to get a confirmed raw code. Every one is covered now.
        CHECK(kronos::isConfirmedTimbreProgramBank(18));  // USER-FF -- file-order index 18, raw code 29
        // timbreBankName() returns "" for anything with a confirmed index
        // (kConfirmedTimbreBanks has no `name` field, 2026-08-11, see its
        // own doc comment): the frontend derives the name from
        // PROGRAM_BANK_NAMES[programBankIndex] instead, so there's exactly
        // one place ("index 6 = USER-A") that fact is spelled out, not two
        // that could drift apart again.
        CHECK_EQ(kronos::timbreBankName(20), std::string(""), "timbreBankName() leaves confirmed-index codes for the frontend to resolve");
        CHECK_EQ(kronos::timbreBankName(5), std::string(""), "timbreBankName() leaves confirmed-index codes for the frontend to resolve");
        CHECK_EQ(kronos::timbreBankName(999), std::string(""), "timbreBankName() returns \"\" for a genuinely unconfirmed code");
        // GM (raw code 6) is the real counterexample kConfirmedTimbreBanks'
        // dedup comment anticipated -- permanently indexless (not one of
        // the 20 stored PBK1/MBK1 banks), confirmed 2026-08-12, see
        // kConfirmedTimbreBankNamesOnly's own doc comment.
        CHECK_EQ(kronos::timbreBankName(6), std::string("GM"), "timbreBankName() for the permanently indexless GM raw code");
        // NOT `isConfirmedTimbreProgramBank(6)` here -- that takes a PBK1
        // file-order INDEX, a different number space (index 6 = USER-A,
        // genuinely confirmed), not GM's raw CODE 6. kConfirmedTimbreBanks
        // has no entry pairing index-anything with raw code 6, by design --
        // there's no direct C++-level assertion for "this raw code has no
        // confirmed index" without exposing an internal helper, so this
        // relies on kConfirmedTimbreBanks' own array contents (reviewed by
        // hand, see its doc comment) plus timbreBankName()'s check above.
        // G(1)..G(4) (codes 7-10) -- confirmed 2026-08-12 the same
        // permanently-indexless way as GM, sitting right after it.
        CHECK_EQ(kronos::timbreBankName(7), std::string("G(1)"), "timbreBankName() for the permanently indexless G(1) raw code");
        CHECK_EQ(kronos::timbreBankName(8), std::string("G(2)"), "timbreBankName() for the permanently indexless G(2) raw code");
        CHECK_EQ(kronos::timbreBankName(9), std::string("G(3)"), "timbreBankName() for the permanently indexless G(3) raw code");
        CHECK_EQ(kronos::timbreBankName(10), std::string("G(4)"), "timbreBankName() for the permanently indexless G(4) raw code");
        // g(5)/g(6)/g(7)/g(9) (codes 11/12/13/15) -- confirmed 2026-08-13,
        // same contiguous block right past G(4). g(8) (code 14) is
        // deliberately NOT checked here -- not confirmed yet.
        CHECK_EQ(kronos::timbreBankName(11), std::string("g(5)"), "timbreBankName() for the permanently indexless g(5) raw code");
        CHECK_EQ(kronos::timbreBankName(12), std::string("g(6)"), "timbreBankName() for the permanently indexless g(6) raw code");
        CHECK_EQ(kronos::timbreBankName(13), std::string("g(7)"), "timbreBankName() for the permanently indexless g(7) raw code");
        CHECK_EQ(kronos::timbreBankName(15), std::string("g(9)"), "timbreBankName() for the permanently indexless g(9) raw code");
        CHECK_EQ(kronos::timbreBankName(14), std::string(""), "timbreBankName() returns \"\" for g(8) -- not yet confirmed");

        auto usersD = pcg.combiUsagesForProgram(9, 7);  // USER-D file-order index, Timbre 1's number
        CHECK_EQ(usersD.size(), static_cast<size_t>(1), "combiUsagesForProgram() finds Timbre 1 via the translated raw code");
        if (usersD.size() == 1) {
            CHECK_EQ(usersD[0].bank, 0, "combiUsagesForProgram() result's Combi bank");
            CHECK_EQ(usersD[0].number, 0, "combiUsagesForProgram() result's Combi number");
        }
        CHECK_EQ(pcg.combiUsagesForProgram(99, 7).size(), static_cast<size_t>(0),
                 "combiUsagesForProgram() returns nothing for an out-of-range bank rather than guessing");

        auto counts = pcg.combiUsageCounts();
        CHECK(counts.size() > 9 && counts[9].size() > 7 && counts[9][7] == 1);
    }

    // Internals accessors (topLevelChunkTags()/programBankInfo()/
    // combiBankInfo()) -- the synthetic fixture's real top-level chunks are
    // DIV1, SLS1 (wrapping SDB1/SBK1), PRG1 (wrapping PBK1 bank0/MBK1
    // bank1), CMB1 (wrapping CBK1 bank0), in that order -- topLevelChunkTags()
    // must NOT descend into any of them (that's what actually exercises
    // readChunk()'s 12-byte-header fix: getting this wrong desyncs the
    // whole-file walk after the first nested chunk, exactly the real bug
    // this fixture reshape was built to catch, see STATE.md).
    {
        auto tags = pcg.topLevelChunkTags();
        std::vector<std::string> expectedTags = {"DIV1", "SLS1", "PRG1", "CMB1"};
        CHECK_EQ(tags.size(), expectedTags.size(), "topLevelChunkTags() finds every top-level chunk, not its nested children");
        if (tags.size() == expectedTags.size()) {
            for (size_t i = 0; i < tags.size(); ++i) {
                CHECK_EQ(tags[i], expectedTags[i], "topLevelChunkTags() preserves file order");
            }
        }

        auto progBanks = pcg.programBankInfo();
        CHECK_EQ(progBanks.size(), static_cast<size_t>(2), "programBankInfo() has one entry per PRG1 sub-bank");
        if (progBanks.size() == 2) {
            CHECK_EQ(progBanks[0].index, 0, "programBankInfo()[0].index");
            CHECK(progBanks[0].bankType == kronos::ProgramBankType::Hd1);
            CHECK_EQ(progBanks[0].numRecords, 5, "programBankInfo()[0].numRecords (bank 0 has 5 records)");
            CHECK_EQ(progBanks[1].index, 1, "programBankInfo()[1].index");
            CHECK(progBanks[1].bankType == kronos::ProgramBankType::Exi);
            CHECK_EQ(progBanks[1].numRecords, 2, "programBankInfo()[1].numRecords (bank 1 has 2 records)");
        }

        auto combiBanks = pcg.combiBankInfo();
        CHECK_EQ(combiBanks.size(), static_cast<size_t>(1), "combiBankInfo() has one entry per CBK1 sub-bank");
        if (combiBanks.size() == 1) {
            CHECK_EQ(combiBanks[0].index, 0, "combiBankInfo()[0].index");
            CHECK_EQ(combiBanks[0].numRecords, 1, "combiBankInfo()[0].numRecords (one synthetic Combi record)");
        }
    }

    // songRecordBytes()/putSongRecordBytes(): the raw-byte read/write path
    // the Setlist Color/Volume/Comment row editors use (frontend/pane-
    // setlist-editor.js + frontend/components/kronos/setlist-editor-
    // color.js/setlist-editor-volume.js). Exercises success,
    // the re-derive-cached-fields discipline (mirrors copyProgramFrom()),
    // and every rejection guard.
    {
        auto bytes0 = pcg.songRecordBytes(0, 0);
        CHECK(bytes0.has_value());
        if (bytes0) {
            CHECK_EQ(bytes0->size(), static_cast<size_t>(542), "songRecordBytes() returns one full 542-byte record");
            CHECK_EQ((*bytes0)[16], static_cast<uint8_t>(100), "songRecordBytes() byte+16 is song 0's Volume (100)");
            CHECK_EQ(std::string(reinterpret_cast<const char*>(bytes0->data() + 18)), std::string("Hello test"),
                     "songRecordBytes() byte+18.. is song 0's Comment");

            // Round-trip: flip the raw Volume byte only, leave everything
            // else (incl. Comment) untouched, write it back.
            auto edited = *bytes0;
            edited[16] = 42;
            bool wrote = pcg.putSongRecordBytes(0, 0, edited);
            CHECK(wrote);
            CHECK_EQ(pcg.setlists()[0].songs[0].params.volume, 42,
                     "putSongRecordBytes() re-derives params.volume from the freshly-written bytes");
            CHECK_EQ(pcg.setlists()[0].songs[0].comment, std::string("Hello test"),
                     "putSongRecordBytes() re-derives comment too, unaffected by the Volume-only edit");
            CHECK_EQ(pcg.setlists()[0].songs[0].params.bank, 1,
                     "putSongRecordBytes() leaves untouched fields (bank) alone");

            // Neighbor record (song 1) must be untouched by song 0's write.
            CHECK_EQ(pcg.setlists()[0].songs[1].comment, std::string("second"),
                     "writing song 0's record doesn't disturb song 1's");

            // instrumentName is a separate cached cross-reference (Song::
            // instrumentName, resolved once at load) -- must follow a
            // bank/number change through putSongRecordBytes() too, not
            // just params. RESOLVED 2026-08-13: this used to be a real,
            // reproducible stale-name bug (surfaced by
            // PcgFile::resolveDuplicates(), the first caller that ever
            // repoints bank/number through this same method) -- see
            // putSongRecordBytes()'s own doc comment. bank1/number0
            // ("Bank1 Program0") and bank1/number1 ("Bank1 Program1") are
            // two DISTINCT real Programs, not duplicates, so this actually
            // discriminates stale-vs-fresh instead of coincidentally
            // passing either way.
            CHECK_EQ(pcg.setlists()[0].songs[0].instrumentName, std::string("Bank1 Program0"),
                     "instrumentName resolves correctly before any repoint");
            auto repointed = *bytes0;
            repointed[14] = 1;  // number -> 1 ("Bank1 Program1"), bank stays 1
            CHECK(pcg.putSongRecordBytes(0, 0, repointed));
            CHECK_EQ(pcg.setlists()[0].songs[0].instrumentName, std::string("Bank1 Program1"),
                     "putSongRecordBytes() re-resolves instrumentName after a bank/number change");

            // Restore song 0's original bytes so nothing downstream in this
            // test (there is nothing after this block, but for hygiene) sees
            // a mutated fixture.
            CHECK(pcg.putSongRecordBytes(0, 0, *bytes0));
            CHECK_EQ(pcg.setlists()[0].songs[0].params.volume, 100, "putSongRecordBytes() restore round-trips cleanly");
        }

        // Rejected: wrong byte count.
        std::vector<uint8_t> wrongSize(541, 0);
        CHECK(!pcg.putSongRecordBytes(0, 0, wrongSize));

        // Rejected: out-of-range setlist/song index, both directions.
        CHECK(!pcg.songRecordBytes(99, 0).has_value());
        CHECK(!pcg.songRecordBytes(0, 999).has_value());
        if (bytes0) CHECK(!pcg.putSongRecordBytes(99, 0, *bytes0));
    }

    // nameRecordBytes()/putNameRecordBytes(): the SDB1 counterpart to
    // songRecordBytes()/putSongRecordBytes() -- a slot's name lives in a
    // completely separate chunk/stride from its params, see PcgFile.h's
    // own doc comment on why a reorder must move both together.
    {
        auto name0 = pcg.nameRecordBytes(0, 0);
        CHECK(name0.has_value());
        if (name0) {
            CHECK_EQ(name0->size(), static_cast<size_t>(28), "nameRecordBytes() returns one full 28-byte record");
            CHECK_EQ(std::string(reinterpret_cast<const char*>(name0->data() + 4)), std::string("Song Zero"),
                     "nameRecordBytes() byte+4.. is the record's name");
        }
        auto name1 = pcg.nameRecordBytes(0, 1);
        CHECK(name1.has_value());
        if (name1) {
            CHECK_EQ(std::string(reinterpret_cast<const char*>(name1->data() + 4)), std::string("Song One"),
                     "nameRecordBytes() for song 1");
        }

        // Rejected: wrong byte count, out-of-range indices both directions.
        std::vector<uint8_t> wrongSize(27, 0);
        CHECK(!pcg.putNameRecordBytes(0, 0, wrongSize));
        CHECK(!pcg.nameRecordBytes(99, 0).has_value());
        CHECK(!pcg.nameRecordBytes(0, 999).has_value());
        if (name0) CHECK(!pcg.putNameRecordBytes(99, 0, *name0));
    }

    // reorderSong(): relocates a slot's name AND params together, shifting
    // the intervening range -- the operation the Setlist drag-and-drop
    // "insert between two entries" gesture is built on (STATE.md). Only
    // slots 0 ("Song Zero") and 1 ("Song One") carry real data in this
    // fixture; slots 2-5 are empty, giving a clean way to see the shift
    // happen without needing more populated fixture data.
    {
        // No-op: same index, returns true, nothing changes.
        CHECK(pcg.reorderSong(0, 3, 3));

        // Rejected: out-of-range setlist/song index, both directions.
        CHECK(!pcg.reorderSong(99, 0, 1));
        CHECK(!pcg.reorderSong(0, 0, 999));

        // Forward move: slot 0 ("Song Zero") relocates to slot 5, shifting
        // slots [1..5] back to [0..4].
        CHECK(pcg.reorderSong(0, 0, 5));
        CHECK_EQ(pcg.setlists()[0].songs[0].name, std::string("Song One"),
                 "reorderSong() forward: slot 1's content shifted into slot 0");
        CHECK_EQ(pcg.setlists()[0].songs[0].comment, std::string("second"),
                 "reorderSong() forward: slot 0's params are now slot 1's original params");
        CHECK(pcg.setlists()[0].songs[1].name.empty());  // was slot 2's (empty) content, shifted back one
        CHECK_EQ(pcg.setlists()[0].songs[5].name, std::string("Song Zero"),
                 "reorderSong() forward: the moved slot's own content lands at the target index");
        CHECK_EQ(pcg.setlists()[0].songs[5].comment, std::string("Hello test"),
                 "reorderSong() forward: the moved slot's params traveled with its name");

        // Backward move: put it back (slot 5 -> slot 0), restoring the
        // original order -- proves the shift works symmetrically in the
        // other direction, not just coincidentally for this one case.
        CHECK(pcg.reorderSong(0, 5, 0));
        CHECK_EQ(pcg.setlists()[0].songs[0].name, std::string("Song Zero"),
                 "reorderSong() backward: restores the original order");
        CHECK_EQ(pcg.setlists()[0].songs[0].comment, std::string("Hello test"), "reorderSong() backward: song 0 restored");
        CHECK_EQ(pcg.setlists()[0].songs[1].name, std::string("Song One"), "reorderSong() backward: song 1 restored");
        CHECK_EQ(pcg.setlists()[0].songs[1].comment, std::string("second"), "reorderSong() backward: song 1's params restored");
        CHECK(pcg.setlists()[0].songs[5].name.empty());  // shifted back out to its original empty state
    }

    // copySetlist(): overwrites every song slot in the destination Set List
    // with the source's -- the "copy all to opposite" pane-header button
    // (STATE.md). Setlist 0 ("Test Setlist") is the source, still in its
    // original restored state from the reorderSong() block above; setlist 1
    // ("Gig Setlist") is the destination, pre-loaded with distinct content
    // (song 0 = "Old Song") specifically so this test can tell a real
    // overwrite apart from a no-op.
    {
        // No-op: same index, returns true, changes nothing.
        CHECK(pcg.copySetlist(0, 0));
        CHECK_EQ(pcg.setlists()[0].songs[0].name, std::string("Song Zero"), "copySetlist() same-index no-op leaves the source alone");

        // Rejected: out-of-range setlist index, both directions.
        CHECK(!pcg.copySetlist(99, 1));
        CHECK(!pcg.copySetlist(0, 99));

        CHECK(pcg.copySetlist(0, 1));
        CHECK_EQ(pcg.setlists()[1].songs[0].name, std::string("Song Zero"),
                 "copySetlist(): destination song 0's name overwritten from the source");
        CHECK_EQ(pcg.setlists()[1].songs[0].comment, std::string("Hello test"),
                 "copySetlist(): destination song 0's params overwritten from the source");
        CHECK_EQ(pcg.setlists()[1].songs[1].name, std::string("Song One"),
                 "copySetlist(): destination song 1 also overwritten (source's song 1)");
        CHECK(pcg.setlists()[1].songs[2].name.empty());  // was never populated in either Set List
        CHECK_EQ(pcg.setlists()[1].name, std::string("Gig Setlist"),
                 "copySetlist() leaves the destination Set List's OWN name untouched -- only song slots move");
        CHECK_EQ(pcg.setlists()[0].songs[0].name, std::string("Song Zero"),
                 "copySetlist() doesn't disturb the source Set List");

        // Restore setlist 1's original content so nothing downstream sees a
        // mutated fixture (hygiene, matching this file's existing pattern).
        std::vector<uint8_t> restoreName(28, 0);
        const std::string oldSongName = "Old Song";
        std::copy(oldSongName.begin(), oldSongName.end(), restoreName.begin() + 4);
        CHECK(pcg.putNameRecordBytes(1, 0, restoreName));
        auto restoredParams = makeSbkSongRecord(/*isProgram=*/true, /*bank=*/1, /*number=*/1, /*color=*/2,
                                                  /*holdTime=*/2, /*volume=*/60, /*fontSizeValue=*/0, /*transpose=*/0,
                                                  /*garbageLow4=*/0x00, "should be overwritten");
        CHECK(pcg.putSongRecordBytes(1, 0, restoredParams));
        std::vector<uint8_t> emptyName(28, 0);
        CHECK(pcg.putNameRecordBytes(1, 1, emptyName));
        std::vector<uint8_t> emptyParams(542, 0);
        CHECK(pcg.putSongRecordBytes(1, 1, emptyParams));
        CHECK_EQ(pcg.setlists()[1].songs[0].name, std::string("Old Song"), "copySetlist() restore round-trips cleanly");
    }

    // sortSetlist(): a REAL, immediate, whole-Set-List rewrite -- not a
    // display-only convenience, see its own doc comment for why (no
    // separate "sorted view" a Kronos can show independent of a slot's
    // actual record position). Setlist 0 is back in its original,
    // restored state from the reorderSong() block above: songs[0]="Song
    // Zero" ("Hello test"), songs[1]="Song One" ("second"), songs[2..127]
    // empty.
    {
        // Rejected: out-of-range setlist index.
        CHECK(!pcg.sortSetlist(99, true));

        // Ascending: "Song One" ('O' < 'Z') sorts before "Song Zero";
        // empty slots trail after both regardless of direction.
        CHECK(pcg.sortSetlist(0, /*ascending=*/true));
        CHECK_EQ(pcg.setlists()[0].songs[0].name, std::string("Song One"),
                 "sortSetlist() ascending: 'Song One' sorts before 'Song Zero'");
        CHECK_EQ(pcg.setlists()[0].songs[0].comment, std::string("second"),
                 "sortSetlist() ascending: song 0's params traveled with its name");
        CHECK_EQ(pcg.setlists()[0].songs[1].name, std::string("Song Zero"),
                 "sortSetlist() ascending: 'Song Zero' sorts second");
        CHECK_EQ(pcg.setlists()[0].songs[1].comment, std::string("Hello test"),
                 "sortSetlist() ascending: song 1's params traveled with its name");
        CHECK(pcg.setlists()[0].songs[2].name.empty());  // empty slots stay trailing, not interleaved

        // Descending, applied to the now-ascending state: restores the
        // original order (Song Zero, Song One) since there were only two
        // named slots -- also doubles as this block's own cleanup, no
        // separate restore step needed.
        CHECK(pcg.sortSetlist(0, /*ascending=*/false));
        CHECK_EQ(pcg.setlists()[0].songs[0].name, std::string("Song Zero"),
                 "sortSetlist() descending: restores the original order");
        CHECK_EQ(pcg.setlists()[0].songs[0].comment, std::string("Hello test"), "sortSetlist() descending: song 0 restored");
        CHECK_EQ(pcg.setlists()[0].songs[1].name, std::string("Song One"), "sortSetlist() descending: song 1 restored");
        CHECK_EQ(pcg.setlists()[0].songs[1].comment, std::string("second"), "sortSetlist() descending: song 1's params restored");
    }
}

// save() is deliberately trivial (a verbatim write of data_, see its own doc
// comment) -- this test isn't about the write logic itself, it's proof that
// a save()+load() round-trip actually reproduces the same file: catches
// anything that would corrupt data_ before this point without needing to
// byte-compare the whole buffer by hand.
void testSaveRoundTrip() {
    kronos::PcgFile pcg;
    std::string error;
    std::vector<uint8_t> data = buildSyntheticPcgFile();
    bool loaded = pcg.loadFromMemory(std::move(data), error);
    CHECK(loaded);
    if (!loaded) return;

    const char* path = "pcg_file_test_save_output.tmp";
    bool saved = pcg.save(path, error);
    CHECK(saved);
    if (!saved) {
        std::fprintf(stderr, "  save() error: %s\n", error.c_str());
        return;
    }

    kronos::PcgFile reloaded;
    std::string reloadError;
    bool reloadedOk = reloaded.load(path, reloadError);
    CHECK(reloadedOk);
    if (reloadedOk) {
        CHECK_EQ(reloaded.setlists().size(), pcg.setlists().size(),
                 "save()+load() round-trips the same number of Set Lists");
        if (!reloaded.setlists().empty() && !pcg.setlists().empty()) {
            CHECK_EQ(reloaded.setlists()[0].name, pcg.setlists()[0].name,
                     "save()+load() round-trips the Set List name");
            CHECK_EQ(reloaded.setlists()[0].songs[0].comment, pcg.setlists()[0].songs[0].comment,
                     "save()+load() round-trips song 0's Comment");
        }
        CHECK_EQ(reloaded.programs().size(), pcg.programs().size(),
                 "save()+load() round-trips the same number of Programs");
    }

    std::remove(path);

    // Rejected: nothing loaded (data_ empty) -- save() has nothing to write.
    kronos::PcgFile empty;
    std::string emptyError;
    CHECK(!empty.save(path, emptyError));
}

// PcgFile::isDirty() (2026-08-15) -- every raw-byte write funnels through
// ONE private helper (writeIntoData(), PcgFile.h) that flips this flag, so
// this test covers each of the 5 call sites that reach it, plus the two
// edges that matter for the app's own "Unload" confirmation: a REJECTED
// write must never dirty the file, and a successful save() must clear the
// flag. One fresh PcgFile per block, same isolation reasoning as
// testResolveDuplicates() below.
void testDirtyTracking() {
    // --- A freshly loaded file is never dirty -----------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        CHECK(pcg.loadFromMemory(buildSyntheticPcgFile(), error));
        CHECK(!pcg.isDirty());
    }

    // --- putSongRecordBytes(): a rejected write (wrong byte length) leaves
    // isDirty() false; a real one sets it -----------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        CHECK(pcg.loadFromMemory(buildSyntheticPcgFile(), error));

        CHECK(!pcg.putSongRecordBytes(0, 0, std::vector<uint8_t>(10, 0)));  // wrong size -- rejected
        CHECK(!pcg.isDirty());

        auto bytes = pcg.songRecordBytes(0, 0);
        CHECK(bytes.has_value());
        if (bytes) {
            CHECK(pcg.putSongRecordBytes(0, 0, *bytes));
            CHECK(pcg.isDirty());
        }
    }

    // --- putNameRecordBytes() -----------------------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        CHECK(pcg.loadFromMemory(buildSyntheticPcgFile(), error));
        CHECK(!pcg.isDirty());
        auto bytes = pcg.nameRecordBytes(0, 0);
        CHECK(bytes.has_value());
        if (bytes) {
            CHECK(pcg.putNameRecordBytes(0, 0, *bytes));
            CHECK(pcg.isDirty());
        }
    }

    // --- putCombiRecordBytes() -----------------------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        CHECK(pcg.loadFromMemory(buildSyntheticPcgFile(), error));
        CHECK(!pcg.isDirty());
        auto bytes = pcg.combiRecordBytes(0, 0);
        CHECK(bytes.has_value());
        if (bytes) {
            CHECK(pcg.putCombiRecordBytes(0, 0, *bytes));
            CHECK(pcg.isDirty());
        }
    }

    // --- putProgramRecordBytes() -----------------------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        CHECK(pcg.loadFromMemory(buildSyntheticPcgFile(), error));
        CHECK(!pcg.isDirty());
        auto bytes = pcg.programRecordBytes(0, 2);
        CHECK(bytes.has_value());
        if (bytes) {
            CHECK(pcg.putProgramRecordBytes(0, 2, *bytes));
            CHECK(pcg.isDirty());
        }
    }

    // --- copyProgramFrom() dirties the destination -------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        CHECK(pcg.loadFromMemory(buildSyntheticPcgFile(), error));
        CHECK(!pcg.isDirty());
        auto copyError = pcg.copyProgramFrom(pcg, 0, 2, 0, 3);  // bank0/number3 is empty in the base fixture
        CHECK(!copyError.has_value());
        CHECK(pcg.isDirty());
    }

    // --- save() clears the dirty flag on success ----------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        CHECK(pcg.loadFromMemory(buildSyntheticPcgFile(), error));
        auto bytes = pcg.songRecordBytes(0, 0);
        CHECK(bytes.has_value());
        if (bytes) CHECK(pcg.putSongRecordBytes(0, 0, *bytes));
        CHECK(pcg.isDirty());

        const char* path = "pcg_file_test_dirty_output.tmp";
        CHECK(pcg.save(path, error));
        CHECK(!pcg.isDirty());
        std::remove(path);
    }
}

// Each block below builds its own fresh PcgFile from buildSyntheticPcgFile()
// -- same "one function, one independent instance" pattern as
// testSaveRoundTrip() above, so a write in one block (this is a real write
// path -- resolveDuplicates() mutates data_) can never leak into another.
void testResolveDuplicates() {
    // --- Happy path: clear + repoint both a Set List slot and a Combi
    // Timbre -----------------------------------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildSyntheticPcgFile(), error);
        CHECK(loaded);
        if (!loaded) return;

        // The fixture already has a real duplicate pair: PBK1 bank 0,
        // numbers 0 and 1, both "Test Program A" (see
        // buildSyntheticPcgFile()'s own comment). Point Setlist 0's slot 2
        // (empty in the base fixture -- unused by any other test, each of
        // which builds its own separate instance) and Combi 0's Timbre 2
        // (also unassigned in the base fixture) at bank0/number1, the
        // duplicate about to be cleared -- so this exercises real
        // repointing, not just the Program-record clear.
        {
            auto bytes = pcg.songRecordBytes(0, 2);
            CHECK(bytes.has_value());
            if (bytes) {
                (*bytes)[12] |= 0x01;  // Type bits0-1 = 1 (Program) -- see docs/content/format/index.md §4.3
                (*bytes)[13] = 0;      // bank 0 (low 5 bits of byte+13)
                (*bytes)[14] = 1;      // number 1
                CHECK(pcg.putSongRecordBytes(0, 2, *bytes));
            }
        }
        {
            auto bytes = pcg.combiRecordBytes(0, 0);
            CHECK(bytes.has_value());
            if (bytes) {
                kronos::writeTimbreProgramRef(bytes->data(), bytes->size(), /*timbreIndex=*/2, /*number=*/1,
                                               /*rawBankCode=*/0);  // bank 0 = INT-A, confirmed raw code 0
                // writeTimbreProgramRef() deliberately leaves the status
                // byte alone -- set it directly here so this Timbre reads
                // as a real active reference (Internal), not the all-zero
                // Off state it started in.
                size_t statusOff = kronos::timbreByteOffset(2) + 2;
                (*bytes)[statusOff] = static_cast<uint8_t>(1 << 5);
                CHECK(pcg.putCombiRecordBytes(0, 0, *bytes));
            }
        }

        // Confirm the setup actually landed before testing resolveDuplicates() itself.
        CHECK_EQ(pcg.setlists()[0].songs[2].params.bank, 0, "setup: song 2 references bank 0");
        CHECK_EQ(pcg.setlists()[0].songs[2].params.number, 1, "setup: song 2 references number 1");
        {
            auto combi = pcg.decodeCombi(0, 0);
            CHECK(combi.has_value());
            if (combi) {
                CHECK(!combi->timbres[2].isDefault);
                CHECK_EQ(combi->timbres[2].number, 1, "setup: Timbre 2 references number 1");
                CHECK_EQ(combi->timbres[2].rawBankCode, 0, "setup: Timbre 2 references raw code 0");
            }
        }

        // bank0/number2 ("Unique Program") is exactly bank 0's own record
        // size (kBankRecordSize=32 in this fixture) with a name distinct
        // from "Test Program A" -- reused as this test's own stand-in
        // "Init Program" template so a successful clear is easy to tell
        // apart from the original duplicate content.
        const auto hd1Template = pcg.programRecordBytes(0, 2);
        CHECK(hd1Template.has_value());
        if (!hd1Template) return;

        auto result = pcg.resolveDuplicates(/*keepBank=*/0, /*keepNumber=*/0, /*targets=*/{{0, 1}},
                                             /*requireByteExactMatch=*/true, *hd1Template, /*exiInitBytes=*/{});
        CHECK(result.ok);
        if (!result.ok) {
            std::fprintf(stderr, "  resolveDuplicates() error: %s\n", result.error.c_str());
            return;
        }
        CHECK_EQ(result.clearedPrograms, 1, "exactly one duplicate (bank0/number1) existed");
        CHECK_EQ(result.setlistRefsRepointed, 1, "exactly one Set List slot referenced it");
        CHECK_EQ(result.combiRefsRepointed, 1, "exactly one Combi Timbre referenced it");
        CHECK_EQ(result.combiRefsSkipped, 0, "bank 0's raw Timbre code (INT-A=0) is confirmed");

        auto cleared = pcg.decodeProgram(0, 1);
        CHECK(cleared.has_value());
        if (cleared) {
            CHECK_EQ(cleared->name, std::string("Unique Program"), "cleared duplicate now holds the template's bytes");
        }

        auto kept = pcg.decodeProgram(0, 0);
        CHECK(kept.has_value());
        if (kept) CHECK_EQ(kept->name, std::string("Test Program A"), "kept slot's own bytes are untouched");

        CHECK_EQ(pcg.setlists()[0].songs[2].params.bank, 0, "Set List slot repointed: bank (already 0)");
        CHECK_EQ(pcg.setlists()[0].songs[2].params.number, 0, "Set List slot repointed: number now 0");
        // Not a discriminating check on its own -- the kept and cleared
        // slots share this exact name pre-resolve (that's what makes them
        // duplicates), so this would read the same whether or not
        // instrumentName actually re-resolved. See the dedicated
        // putSongRecordBytes() test above (bank1/number0 -> number1, two
        // DISTINCT Programs) for the real regression coverage of that fix.
        CHECK_EQ(pcg.setlists()[0].songs[2].instrumentName, std::string("Test Program A"),
                 "Set List slot repointed: instrumentName resolves to the kept slot's name");

        auto combiAfter = pcg.decodeCombi(0, 0);
        CHECK(combiAfter.has_value());
        if (combiAfter) {
            CHECK_EQ(combiAfter->timbres[2].number, 0, "Combi Timbre repointed: number now 0");
            CHECK_EQ(combiAfter->timbres[2].rawBankCode, 0, "Combi Timbre repointed: raw code (already 0)");
            CHECK(combiAfter->timbres[2].status == kronos::TimbreStatus::Internal);  // status byte untouched by the repoint
        }
    }

    // --- Rejected: no such Program to keep -----------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildSyntheticPcgFile(), error);
        CHECK(loaded);
        if (!loaded) return;

        auto result = pcg.resolveDuplicates(99, 0, {}, /*requireByteExactMatch=*/true, {}, {});
        CHECK(!result.ok);
    }

    // --- Rejected: template size doesn't match the duplicate's own
    // bank's record size -- all-or-nothing, writes nothing -------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildSyntheticPcgFile(), error);
        CHECK(loaded);
        if (!loaded) return;

        const std::vector<uint8_t> wrongSize(4, 0);  // real bank 0 record size in this fixture is 32
        auto result = pcg.resolveDuplicates(0, 0, {{0, 1}}, /*requireByteExactMatch=*/true, wrongSize, wrongSize);
        CHECK(!result.ok);

        auto stillDuplicate = pcg.decodeProgram(0, 1);
        CHECK(stillDuplicate.has_value());
        if (stillDuplicate) {
            CHECK_EQ(stillDuplicate->name, std::string("Test Program A"),
                     "size-mismatch rejection writes nothing -- duplicate untouched");
        }
    }

    // --- Rejected: a named target isn't actually byte-identical to the
    // kept copy -- the real trust boundary against the JS frontend's own
    // resolve-picker sidebar (2026-08-25), which is expected to only ever
    // offer targets drawn from findDuplicatePrograms()'s own grouping, but
    // is verified here independently rather than trusted blindly. All-or-
    // nothing: bank0/number1 IS a real duplicate, but since it's bundled
    // in the same call as the bad target (bank0/number2, "Unique Program",
    // a different hash entirely), NEITHER gets touched. -------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildSyntheticPcgFile(), error);
        CHECK(loaded);
        if (!loaded) return;

        const auto hd1Template = pcg.programRecordBytes(0, 2);
        CHECK(hd1Template.has_value());
        if (!hd1Template) return;

        auto result = pcg.resolveDuplicates(0, 0, {{0, 1}, {0, 2}}, /*requireByteExactMatch=*/true, *hd1Template, {});
        CHECK(!result.ok);

        auto stillDuplicate = pcg.decodeProgram(0, 1);
        CHECK(stillDuplicate.has_value());
        if (stillDuplicate) {
            CHECK_EQ(stillDuplicate->name, std::string("Test Program A"),
                     "hash-mismatch rejection writes nothing -- the OTHER, genuinely-matching target is untouched too");
        }
        auto stillUnique = pcg.decodeProgram(0, 2);
        CHECK(stillUnique.has_value());
        if (stillUnique) CHECK_EQ(stillUnique->name, std::string("Unique Program"), "the mismatched target itself is untouched");
    }
}

// requireByteExactMatch=false -- the Duplicates panel's "Same name,
// different content" consolidate flow (2026-08-25, per direct decision:
// destroying a genuinely-different Program's real content isn't
// acceptable, so this mode ONLY ever repoints references, never clears
// bytes -- see resolveDuplicates()'s own doc comment in PcgFile.h). Reuses
// buildSyntheticPcgFile()'s bank0/number2 ("Unique Program"), which is
// GENUINELY different content from bank0/number0/1 ("Test Program A") --
// the exact case testResolveDuplicates() above proves gets REJECTED when
// requireByteExactMatch=true; this proves it's ACCEPTED (and left
// untouched) when false.
void testResolveDuplicatesConsolidateDifferentContent() {
    kronos::PcgFile pcg;
    std::string error;
    bool loaded = pcg.loadFromMemory(buildSyntheticPcgFile(), error);
    CHECK(loaded);
    if (!loaded) return;

    // Same setup shape as testResolveDuplicates()'s own happy path, just
    // pointed at bank0/number2 (genuinely different content) instead of
    // the byte-identical bank0/number1.
    {
        auto bytes = pcg.songRecordBytes(0, 2);
        CHECK(bytes.has_value());
        if (bytes) {
            (*bytes)[12] |= 0x01;  // Type bits0-1 = 1 (Program)
            (*bytes)[13] = 0;      // bank 0
            (*bytes)[14] = 2;      // number 2
            CHECK(pcg.putSongRecordBytes(0, 2, *bytes));
        }
    }
    {
        auto bytes = pcg.combiRecordBytes(0, 0);
        CHECK(bytes.has_value());
        if (bytes) {
            kronos::writeTimbreProgramRef(bytes->data(), bytes->size(), /*timbreIndex=*/2, /*number=*/2, /*rawBankCode=*/0);
            size_t statusOff = kronos::timbreByteOffset(2) + 2;
            (*bytes)[statusOff] = static_cast<uint8_t>(1 << 5);
            CHECK(pcg.putCombiRecordBytes(0, 0, *bytes));
        }
    }

    auto result = pcg.resolveDuplicates(/*keepBank=*/0, /*keepNumber=*/0, /*targets=*/{{0, 2}},
                                         /*requireByteExactMatch=*/false, /*hd1InitBytes=*/{}, /*exiInitBytes=*/{});
    CHECK(result.ok);
    if (!result.ok) {
        std::fprintf(stderr, "  resolveDuplicates() error: %s\n", result.error.c_str());
        return;
    }
    CHECK_EQ(result.clearedPrograms, 0, "requireByteExactMatch=false never clears a target's bytes");
    CHECK_EQ(result.setlistRefsRepointed, 1, "the Set List slot referencing number 2 was repointed");
    CHECK_EQ(result.combiRefsRepointed, 1, "the Combi Timbre referencing number 2 was repointed");

    CHECK_EQ(pcg.setlists()[0].songs[2].params.number, 0, "Set List slot repointed to the kept number 0");
    {
        auto combi = pcg.decodeCombi(0, 0);
        CHECK(combi.has_value());
        if (combi) CHECK_EQ(combi->timbres[2].number, 0, "Combi Timbre repointed to the kept number 0");
    }

    // The real point of this whole mode: bank0/number2's own content is
    // untouched -- still "Unique Program", not overwritten with anything.
    auto stillUnique = pcg.decodeProgram(0, 2);
    CHECK(stillUnique.has_value());
    if (stillUnique) {
        CHECK_EQ(stillUnique->name, std::string("Unique Program"),
                 "requireByteExactMatch=false leaves the target's genuinely-different content untouched");
    }
}

// resetProgram() (2026-08-20) -- the single-slot "reset entry" half of
// resolveDuplicates() above, with none of its repointing: a Set List slot
// or Combi Timbre already referencing the reset slot must keep pointing at
// it (it'll just show the reset content now), unlike resolveDuplicates()
// which repoints everything AWAY from the slot it clears.
void testResetProgram() {
    // --- Happy path: bank0/number0 reset to a distinct template, existing
    // references left pointing at it (not repointed) --------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildSyntheticPcgFile(), error);
        CHECK(loaded);
        if (!loaded) return;

        // Point Setlist 0's slot 2 and Combi 0's Timbre 2 at bank0/number0
        // (the slot about to be reset) -- same setup shape as
        // testResolveDuplicates() above, but targeting the KEPT slot this
        // time, since resetProgram() has no "kept" slot at all.
        {
            auto bytes = pcg.songRecordBytes(0, 2);
            CHECK(bytes.has_value());
            if (bytes) {
                (*bytes)[12] |= 0x01;  // Type bits0-1 = 1 (Program) -- see docs/content/format/index.md §4.3
                (*bytes)[13] = 0;      // bank 0
                (*bytes)[14] = 0;      // number 0
                CHECK(pcg.putSongRecordBytes(0, 2, *bytes));
            }
        }
        {
            auto bytes = pcg.combiRecordBytes(0, 0);
            CHECK(bytes.has_value());
            if (bytes) {
                kronos::writeTimbreProgramRef(bytes->data(), bytes->size(), /*timbreIndex=*/2, /*number=*/0,
                                               /*rawBankCode=*/0);  // bank 0 = INT-A, confirmed raw code 0
                size_t statusOff = kronos::timbreByteOffset(2) + 2;
                (*bytes)[statusOff] = static_cast<uint8_t>(1 << 5);  // Internal, not the all-zero Off state
                CHECK(pcg.putCombiRecordBytes(0, 0, *bytes));
            }
        }

        // bank0/number2 ("Unique Program") stands in as this test's own
        // "Init Program" template, same trick as testResolveDuplicates()
        // above -- distinct name makes a successful reset easy to tell
        // apart from bank0/number0's original "Test Program A" content.
        const auto hd1Template = pcg.programRecordBytes(0, 2);
        CHECK(hd1Template.has_value());
        if (!hd1Template) return;

        auto result = pcg.resetProgram(/*bank=*/0, /*number=*/0, *hd1Template, /*exiInitBytes=*/{});
        CHECK(result.ok);
        if (!result.ok) {
            std::fprintf(stderr, "  resetProgram() error: %s\n", result.error.c_str());
            return;
        }

        auto reset = pcg.decodeProgram(0, 0);
        CHECK(reset.has_value());
        if (reset) CHECK_EQ(reset->name, std::string("Unique Program"), "reset slot now holds the template's bytes");

        auto untouched = pcg.decodeProgram(0, 1);
        CHECK(untouched.has_value());
        if (untouched) CHECK_EQ(untouched->name, std::string("Test Program A"), "the OTHER duplicate is untouched");

        CHECK_EQ(pcg.setlists()[0].songs[2].params.bank, 0, "Set List slot NOT repointed: still bank 0");
        CHECK_EQ(pcg.setlists()[0].songs[2].params.number, 0, "Set List slot NOT repointed: still number 0");
        CHECK_EQ(pcg.setlists()[0].songs[2].instrumentName, std::string("Unique Program"),
                 "Set List slot's instrumentName now resolves to the reset content");

        auto combiAfter = pcg.decodeCombi(0, 0);
        CHECK(combiAfter.has_value());
        if (combiAfter) {
            CHECK_EQ(combiAfter->timbres[2].number, 0, "Combi Timbre NOT repointed: still number 0");
            CHECK_EQ(combiAfter->timbres[2].rawBankCode, 0, "Combi Timbre NOT repointed: still raw code 0");
        }
    }

    // --- Rejected: no such Program bank ---------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildSyntheticPcgFile(), error);
        CHECK(loaded);
        if (!loaded) return;

        auto result = pcg.resetProgram(99, 0, {}, {});
        CHECK(!result.ok);
    }

    // --- Rejected: no such Program slot (number out of range) ----------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildSyntheticPcgFile(), error);
        CHECK(loaded);
        if (!loaded) return;

        const auto hd1Template = pcg.programRecordBytes(0, 0);
        CHECK(hd1Template.has_value());
        if (!hd1Template) return;

        auto result = pcg.resetProgram(0, 99, *hd1Template, {});
        CHECK(!result.ok);
    }

    // --- Rejected: template size doesn't match this bank's own record
    // size -- writes nothing ---------------------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildSyntheticPcgFile(), error);
        CHECK(loaded);
        if (!loaded) return;

        const std::vector<uint8_t> wrongSize(4, 0);  // real bank 0 record size in this fixture is 32
        auto result = pcg.resetProgram(0, 0, wrongSize, wrongSize);
        CHECK(!result.ok);

        auto stillOriginal = pcg.decodeProgram(0, 0);
        CHECK(stillOriginal.has_value());
        if (stillOriginal) {
            CHECK_EQ(stillOriginal->name, std::string("Test Program A"),
                     "size-mismatch rejection writes nothing -- slot untouched");
        }
    }
}

// swapPrograms() (2026-08-15) -- built so a plain drag-and-drop copy
// between two slots that are BOTH genuinely empty ("Init Program") doesn't
// have to fight copyProgramFrom()'s own DuplicateExists guard (every Init
// Program is byte-identical to every other one, so copying one onto
// another always trips it). One fresh PcgFile per block, same isolation
// reasoning as testResolveDuplicates() above.
void testProgramSwap() {
    // --- Happy path: bank0/number0 ("Test Program A") <-> bank0/number2
    // ("Unique Program"), each referenced by a DIFFERENT kind of pointer
    // (a Set List slot vs. a Combi Timbre) so both repoint directions get
    // exercised in one pass ------------------------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildSyntheticPcgFile(), error);
        CHECK(loaded);
        if (!loaded) return;

        // Setlist 0's song 2 (empty in the base fixture) -> bank0/number0.
        {
            auto bytes = pcg.songRecordBytes(0, 2);
            CHECK(bytes.has_value());
            if (bytes) {
                (*bytes)[12] |= 0x01;  // Type bits0-1 = 1 (Program)
                (*bytes)[13] = 0;      // bank 0
                (*bytes)[14] = 0;      // number 0
                CHECK(pcg.putSongRecordBytes(0, 2, *bytes));
            }
        }
        // Combi 0's Timbre 2 (default in the base fixture) -> bank0/number2
        // (rawBankCode 0 = INT-A, confirmed).
        {
            auto bytes = pcg.combiRecordBytes(0, 0);
            CHECK(bytes.has_value());
            if (bytes) {
                kronos::writeTimbreProgramRef(bytes->data(), bytes->size(), /*timbreIndex=*/2, /*number=*/2,
                                               /*rawBankCode=*/0);
                size_t statusOff = kronos::timbreByteOffset(2) + 2;
                (*bytes)[statusOff] = static_cast<uint8_t>(1 << 5);  // Internal, not the all-zero Off default
                CHECK(pcg.putCombiRecordBytes(0, 0, *bytes));
            }
        }

        auto result = pcg.swapPrograms(0, 0, 0, 2);
        CHECK(result.ok);
        if (!result.ok) std::fprintf(stderr, "  swapPrograms() error: %s\n", result.error.c_str());
        CHECK_EQ(result.setlistRefsRepointed, 1, "exactly one Set List slot referenced either position");
        CHECK_EQ(result.combiRefsRepointed, 1, "exactly one Combi Timbre referenced either position");
        CHECK_EQ(result.combiRefsSkipped, 0, "bank 0's raw Timbre code (INT-A=0) is confirmed");

        auto atZero = pcg.decodeProgram(0, 0);
        CHECK(atZero.has_value());
        if (atZero) CHECK_EQ(atZero->name, std::string("Unique Program"), "bank0/number0 now holds what number2 held");

        auto atTwo = pcg.decodeProgram(0, 2);
        CHECK(atTwo.has_value());
        if (atTwo) CHECK_EQ(atTwo->name, std::string("Test Program A"), "bank0/number2 now holds what number0 held");

        // The Set List slot followed "Test Program A" to its new position.
        CHECK_EQ(pcg.setlists()[0].songs[2].params.bank, 0, "Set List slot repointed: bank");
        CHECK_EQ(pcg.setlists()[0].songs[2].params.number, 2, "Set List slot repointed: number now 2");
        CHECK_EQ(pcg.setlists()[0].songs[2].instrumentName, std::string("Test Program A"),
                 "Set List slot's instrumentName resolves to the content it actually followed");

        // The Combi Timbre followed "Unique Program" to its new position.
        auto combiAfter = pcg.decodeCombi(0, 0);
        CHECK(combiAfter.has_value());
        if (combiAfter) {
            CHECK_EQ(combiAfter->timbres[2].number, 0, "Combi Timbre repointed: number now 0");
            CHECK_EQ(combiAfter->timbres[2].rawBankCode, 0, "Combi Timbre repointed: raw code (already 0)");
            CHECK(combiAfter->timbres[2].status == kronos::TimbreStatus::Internal);  // status byte untouched
        }
    }

    // --- No-op: same slot twice -------------------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        CHECK(pcg.loadFromMemory(buildSyntheticPcgFile(), error));
        auto result = pcg.swapPrograms(0, 0, 0, 0);
        CHECK(result.ok);
        CHECK_EQ(result.setlistRefsRepointed, 0, "no-op -- nothing to repoint");
    }

    // --- Rejected: different engine types (bank0=Hd1, bank1=Exi) ----------
    {
        kronos::PcgFile pcg;
        std::string error;
        CHECK(pcg.loadFromMemory(buildSyntheticPcgFile(), error));
        auto result = pcg.swapPrograms(0, 0, 1, 0);
        CHECK(!result.ok);
        auto stillAtBank0 = pcg.decodeProgram(0, 0);
        CHECK(stillAtBank0.has_value());
        if (stillAtBank0) CHECK_EQ(stillAtBank0->name, std::string("Test Program A"), "rejected swap writes nothing");
    }

    // --- Rejected: out-of-range position ------------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        CHECK(pcg.loadFromMemory(buildSyntheticPcgFile(), error));
        auto result = pcg.swapPrograms(0, 0, 99, 0);
        CHECK(!result.ok);
    }
}

// A small, dedicated fixture -- NOT buildSyntheticPcgFile() (that one's
// shared by testPcgFileEndToEnd()'s many sequential sub-tests and has a
// hard `CHECK_EQ(pcg.combis().size(), 1, ...)` assertion elsewhere, so
// adding more Combi records there would break it). Two Combi banks: bank 0
// has a deliberately-unreferenced "Unused" at number 0 (an all-zero Set
// List slot ALSO decodes as isProgram=false/bank=0/number=0 -- see
// docs/content/format/index.md §5.4's own caveat -- so nothing here ever
// uses (bank 0, number 0) as a REAL reference target, or every one of the
// 125 genuinely-empty slots below would spuriously match too), then
// "Combi A"/"Combi B"/"Combi C" at 1-3, plus an "Init Combi" filler at 4.
// Bank 1 has "Other Bank Combi" at 0 (safe -- the collision is specifically
// with bank 0) and an unreferenced "Empty Target" at 1. One Set List's
// first three slots reference bank0/1, bank0/2, and bank1/0 respectively,
// giving swap/move/move-to-bank real Set List referrers to repoint.
std::vector<uint8_t> buildCombiRearrangeFixture() {
    constexpr uint32_t kSongsPerSetlist = 128;
    constexpr size_t kRecordSize = 28;
    constexpr size_t kSbkHeaderSize = 40;
    constexpr size_t kSbkRecordSize = 542;
    constexpr size_t kCombiRecordSize = 40;  // small synthetic stride, same idea as kBankRecordSize=32 elsewhere

    std::vector<uint8_t> sdb1;
    pushU32BE(sdb1, 1);
    pushU32BE(sdb1, (kSongsPerSetlist + 1) * kRecordSize);
    pushNameRecord(sdb1, "Test Setlist", kRecordSize);
    pushNameRecord(sdb1, "Song A", kRecordSize);
    pushNameRecord(sdb1, "Song B", kRecordSize);
    pushNameRecord(sdb1, "Song C", kRecordSize);
    pushNameRecord(sdb1, "Song D", kRecordSize);
    for (uint32_t k = 4; k < kSongsPerSetlist; ++k) pushZeros(sdb1, kRecordSize);

    std::vector<uint8_t> sbk1;
    pushU32BE(sbk1, 1);
    pushU32BE(sbk1, static_cast<uint32_t>(kSbkHeaderSize + kSongsPerSetlist * kSbkRecordSize));
    pushZeros(sbk1, kSbkHeaderSize);
    auto songA = makeSbkSongRecord(/*isProgram=*/false, /*bank=*/0, /*number=*/1, 1, 0, 100, 0, 0, 0, "");
    auto songB = makeSbkSongRecord(/*isProgram=*/false, /*bank=*/0, /*number=*/2, 1, 0, 100, 0, 0, 0, "");
    auto songC = makeSbkSongRecord(/*isProgram=*/false, /*bank=*/1, /*number=*/0, 1, 0, 100, 0, 0, 0, "");
    // References bank0/4 ("Init Combi") directly -- exists specifically so
    // copyCombi()'s destination-referenced refusal has a real referenced
    // "Init Combi"-looking slot to test against, see testCombiRearrange().
    auto songD = makeSbkSongRecord(/*isProgram=*/false, /*bank=*/0, /*number=*/4, 1, 0, 100, 0, 0, 0, "");
    sbk1.insert(sbk1.end(), songA.begin(), songA.end());
    sbk1.insert(sbk1.end(), songB.begin(), songB.end());
    sbk1.insert(sbk1.end(), songC.begin(), songC.end());
    sbk1.insert(sbk1.end(), songD.begin(), songD.end());
    for (uint32_t k = 4; k < kSongsPerSetlist; ++k) pushZeros(sbk1, kSbkRecordSize);

    std::vector<uint8_t> cbk1BankA;
    pushU32BE(cbk1BankA, 5);
    pushU32BE(cbk1BankA, static_cast<uint32_t>(kCombiRecordSize));
    pushNameRecord(cbk1BankA, "Unused", kCombiRecordSize);  // number 0 -- deliberately never a real reference target
    pushNameRecord(cbk1BankA, "Combi A", kCombiRecordSize);
    pushNameRecord(cbk1BankA, "Combi B", kCombiRecordSize);
    pushNameRecord(cbk1BankA, "Combi C", kCombiRecordSize);
    pushNameRecord(cbk1BankA, "Init Combi", kCombiRecordSize);

    std::vector<uint8_t> cbk1BankB;
    pushU32BE(cbk1BankB, 3);
    pushU32BE(cbk1BankB, static_cast<uint32_t>(kCombiRecordSize));
    pushNameRecord(cbk1BankB, "Other Bank Combi", kCombiRecordSize);
    pushNameRecord(cbk1BankB, "Empty Target", kCombiRecordSize);  // unreferenced -- a real destination for moveCombiToBank()
    // Mixed-case, dash-wrapped, and unreferenced -- exercises both
    // copyCombi()'s case-insensitive "init combi" substring match AND a
    // cross-bank copy in the same fixture entry.
    pushNameRecord(cbk1BankB, "- iNit COMBI -", kCombiRecordSize);

    std::vector<uint8_t> sls1Content;
    appendChunk(sls1Content, "SDB1", sdb1);
    appendChunk(sls1Content, "SBK1", sbk1);

    std::vector<uint8_t> cmb1Content;
    appendChunk(cmb1Content, "CBK1", cbk1BankA);
    appendChunk(cmb1Content, "CBK1", cbk1BankB);

    std::vector<uint8_t> pcg1Content;
    appendChunk(pcg1Content, "SLS1", sls1Content);
    appendChunk(pcg1Content, "CMB1", cmb1Content);

    std::vector<uint8_t> data;
    data.insert(data.end(), {'K', 'O', 'R', 'G'});
    pushZeros(data, 12);
    appendChunk(data, "PCG1", pcg1Content);
    return data;
}

// Dedicated minimal fixture for testFindNameCollisions() -- a Program bank
// and a Combi bank, each with the same shape: two byte-identical records
// sharing a name (a plain duplicate, NOT a collision -- see
// findProgramNameCollisions()/findCombiNameCollisions()'s own doc comment),
// a third record under the SAME name but with a byte changed outside the
// name field (the real collision: 2 variants under one name), a
// uniquely-named record (never a collision), and an empty/placeholder-named
// record ("" for Program -- looksLikeEmptyProgramName() treats a blank name
// as empty on its own, no literal text needed; "Init Combi" for Combi,
// which has no such special case) that must be filtered out entirely even
// though a real file could have many placeholder-named slots that would
// otherwise register as their own spurious "collision."
// The MBK1 (EXi) bank below (2026-08-25) exercises the fix for a real
// reported issue: an HD-1 Program and an EXi Program sharing a name is
// coincidence, not "these are probably the same sound gone astray" (two
// entirely different synth engines can't be a "minor modification" of each
// other) -- see findProgramNameCollisions()'s own doc comment in PcgFile.h.
// "Lead" appears in BOTH banks here specifically so a regression (grouping
// by name alone, ignoring bank type) would have merged them into one
// 3-variant group instead of the correct outcome: the HD-1 "Lead" collision
// stays exactly 2 variants, and the EXi "Lead" (alone in its own bank type)
// forms no collision of its own at all.
std::vector<uint8_t> buildNameCollisionFixture() {
    constexpr size_t kProgramRecordSize = 32;
    constexpr size_t kCombiRecordSize = 40;

    std::vector<uint8_t> pbk1BankA;
    pushU32BE(pbk1BankA, 5);
    pushU32BE(pbk1BankA, static_cast<uint32_t>(kProgramRecordSize));
    pushNameRecord(pbk1BankA, "Lead", kProgramRecordSize);   // number 0
    pushNameRecord(pbk1BankA, "Lead", kProgramRecordSize);   // number 1 -- byte-identical to 0
    size_t programVariantStart = pbk1BankA.size();
    pushNameRecord(pbk1BankA, "Lead", kProgramRecordSize);   // number 2 -- same name, different bytes
    pbk1BankA[programVariantStart] = 0xFF;                   // outside the name field (offset 4+), same trick as buildSyntheticPcgFile()
    pushNameRecord(pbk1BankA, "Piano", kProgramRecordSize);  // number 3 -- unique name, never a collision
    pushZeros(pbk1BankA, kProgramRecordSize);                // number 4 -- empty name, filtered out

    std::vector<uint8_t> mbk1BankB;
    pushU32BE(mbk1BankB, 1);
    pushU32BE(mbk1BankB, static_cast<uint32_t>(kProgramRecordSize));
    pushNameRecord(mbk1BankB, "Lead", kProgramRecordSize);  // number 0 -- same NAME as bank A, different engine (EXi)

    std::vector<uint8_t> cbk1BankA;
    pushU32BE(cbk1BankA, 5);
    pushU32BE(cbk1BankA, static_cast<uint32_t>(kCombiRecordSize));
    pushNameRecord(cbk1BankA, "Lead", kCombiRecordSize);   // number 0
    pushNameRecord(cbk1BankA, "Lead", kCombiRecordSize);   // number 1 -- byte-identical to 0
    size_t combiVariantStart = cbk1BankA.size();
    pushNameRecord(cbk1BankA, "Lead", kCombiRecordSize);   // number 2 -- same name, different bytes
    cbk1BankA[combiVariantStart] = 0xFF;
    pushNameRecord(cbk1BankA, "Piano", kCombiRecordSize);       // number 3 -- unique name, never a collision
    pushNameRecord(cbk1BankA, "Init Combi", kCombiRecordSize);  // number 4 -- placeholder name, filtered out

    std::vector<uint8_t> prg1Content;
    appendChunk(prg1Content, "PBK1", pbk1BankA);
    appendChunk(prg1Content, "MBK1", mbk1BankB);

    std::vector<uint8_t> cmb1Content;
    appendChunk(cmb1Content, "CBK1", cbk1BankA);

    std::vector<uint8_t> pcg1Content;
    appendChunk(pcg1Content, "PRG1", prg1Content);
    appendChunk(pcg1Content, "CMB1", cmb1Content);

    std::vector<uint8_t> data;
    data.insert(data.end(), {'K', 'O', 'R', 'G'});
    pushZeros(data, 12);
    appendChunk(data, "PCG1", pcg1Content);
    return data;
}

void testFindNameCollisions() {
    kronos::PcgFile pcg;
    std::string error;
    bool loaded = pcg.loadFromMemory(buildNameCollisionFixture(), error);
    CHECK(loaded);
    if (!loaded) return;

    auto programGroups = pcg.findProgramNameCollisions();
    CHECK_EQ(programGroups.size(), static_cast<size_t>(1),
             "exactly one Program name collision (\"Piano\", the empty name, and the cross-engine EXi \"Lead\" are "
             "not collisions)");
    if (programGroups.size() == 1) {
        CHECK_EQ(programGroups[0].name, std::string("Lead"), "the collision is under \"Lead\"");
        CHECK_EQ(programGroups[0].bankType, 0, "the collision is the HD-1 bank's own group (ProgramBankType::Hd1 = 0)");
        // The real regression check: the EXi bank's own "Lead" (same name,
        // different engine) must NOT have been folded into this group --
        // still exactly the original 2 variants from the HD-1 bank alone,
        // not 3.
        CHECK_EQ(programGroups[0].variants.size(), static_cast<size_t>(2), "two distinct variants: {0,1} identical, {2} different");
        if (programGroups[0].variants.size() == 2) {
            CHECK_EQ(programGroups[0].variants[0].members.size(), static_cast<size_t>(2), "first variant has the two byte-identical records");
            CHECK_EQ(programGroups[0].variants[1].members.size(), static_cast<size_t>(1), "second variant has the one differing record");
            if (programGroups[0].variants[0].members.size() == 2) {
                CHECK_EQ(programGroups[0].variants[0].members[0].second, 0, "variant 0's first member is number 0");
                CHECK_EQ(programGroups[0].variants[0].members[1].second, 1, "variant 0's second member is number 1");
            }
            if (programGroups[0].variants[1].members.size() == 1) {
                CHECK_EQ(programGroups[0].variants[1].members[0].second, 2, "variant 1's member is number 2");
            }
        }
    }

    auto combiGroups = pcg.findCombiNameCollisions();
    CHECK_EQ(combiGroups.size(), static_cast<size_t>(1), "exactly one Combi name collision (\"Piano\" and \"Init Combi\" are not collisions)");
    if (combiGroups.size() == 1) {
        CHECK_EQ(combiGroups[0].name, std::string("Lead"), "the collision is under \"Lead\"");
        CHECK_EQ(combiGroups[0].variants.size(), static_cast<size_t>(2), "two distinct variants: {0,1} identical, {2} different");
    }
}

// Dedicated fixture for testFindAndResolveDuplicateCombis() -- two
// byte-identical Combis ("Twin", built via the real makeCbkCombiRecord()
// so this exercises hashCombiRecord() against a realistic record, not just
// a flat pushNameRecord() one), a third unique one ("Solo"), and two Set
// List slots -- one already referencing the copy that'll be kept (number
// 1), one referencing the one that'll be superseded (number 0) -- so
// resolveDuplicateCombis() has a real repoint to do AND a real "already
// correct, leave alone" case to not double-count.
std::vector<uint8_t> buildCombiDuplicateFixture() {
    constexpr uint32_t kSongsPerSetlist = 128;
    constexpr size_t kRecordSize = 28;
    constexpr size_t kSbkHeaderSize = 40;
    constexpr size_t kSbkRecordSize = 542;
    constexpr size_t kCombiRecordSize = kTimbreBaseOffset + kTimbreStride * 16;

    std::vector<uint8_t> sdb1;
    pushU32BE(sdb1, 1);
    pushU32BE(sdb1, (kSongsPerSetlist + 1) * kRecordSize);
    pushNameRecord(sdb1, "Test Setlist", kRecordSize);
    pushNameRecord(sdb1, "Song A", kRecordSize);
    pushNameRecord(sdb1, "Song B", kRecordSize);
    for (uint32_t k = 2; k < kSongsPerSetlist; ++k) pushZeros(sdb1, kRecordSize);

    std::vector<uint8_t> sbk1;
    pushU32BE(sbk1, 1);
    pushU32BE(sbk1, static_cast<uint32_t>(kSbkHeaderSize + kSongsPerSetlist * kSbkRecordSize));
    pushZeros(sbk1, kSbkHeaderSize);
    // Neither references number 0 -- an all-zero (empty/padding) song slot
    // decodes as isProgram=false/bank=0/number=0 too (Type/bank/number are
    // all zero-byte defaults), genuinely indistinguishable at the raw-byte
    // level from a real reference there. Every other fixture in this file
    // already avoids bank0/number0 for a real reference target for exactly
    // this reason -- confirmed the hard way here (a first version of this
    // fixture DID use number 0 and saw 127 "repoints", one per zero-filled
    // padding slot beyond the two real ones, not the expected 1).
    auto songA = makeSbkSongRecord(/*isProgram=*/false, /*bank=*/0, /*number=*/1, 1, 0, 100, 0, 0, 0, "");
    auto songB = makeSbkSongRecord(/*isProgram=*/false, /*bank=*/0, /*number=*/2, 1, 0, 100, 0, 0, 0, "");
    sbk1.insert(sbk1.end(), songA.begin(), songA.end());
    sbk1.insert(sbk1.end(), songB.begin(), songB.end());
    for (uint32_t k = 2; k < kSongsPerSetlist; ++k) pushZeros(sbk1, kSbkRecordSize);

    std::vector<uint8_t> cbk1BankA;
    pushU32BE(cbk1BankA, 3);
    pushU32BE(cbk1BankA, static_cast<uint32_t>(kCombiRecordSize));
    auto solo = makeCbkCombiRecord("Solo", kCombiRecordSize);
    cbk1BankA.insert(cbk1BankA.end(), solo.begin(), solo.end());  // number 0 -- unique, never referenced
    auto twin = makeCbkCombiRecord("Twin", kCombiRecordSize);
    cbk1BankA.insert(cbk1BankA.end(), twin.begin(), twin.end());  // number 1
    cbk1BankA.insert(cbk1BankA.end(), twin.begin(), twin.end());  // number 2 -- byte-identical to number 1

    std::vector<uint8_t> sls1Content;
    appendChunk(sls1Content, "SDB1", sdb1);
    appendChunk(sls1Content, "SBK1", sbk1);

    std::vector<uint8_t> cmb1Content;
    appendChunk(cmb1Content, "CBK1", cbk1BankA);

    std::vector<uint8_t> pcg1Content;
    appendChunk(pcg1Content, "SLS1", sls1Content);
    appendChunk(pcg1Content, "CMB1", cmb1Content);

    std::vector<uint8_t> data;
    data.insert(data.end(), {'K', 'O', 'R', 'G'});
    pushZeros(data, 12);
    appendChunk(data, "PCG1", pcg1Content);
    return data;
}

void testFindAndResolveDuplicateCombis() {
    // --- findDuplicateCombis(): exactly one group, {1,2} -----------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildCombiDuplicateFixture(), error);
        CHECK(loaded);
        if (!loaded) return;

        auto groups = pcg.findDuplicateCombis();
        CHECK_EQ(groups.size(), static_cast<size_t>(1), "exactly one duplicate Combi group (\"Solo\" is unique)");
        if (groups.size() == 1) {
            CHECK_EQ(groups[0].size(), static_cast<size_t>(2), "two byte-identical copies");
            if (groups[0].size() == 2) {
                CHECK_EQ(groups[0][0].number, 1, "first copy is number 1");
                CHECK_EQ(groups[0][1].number, 2, "second copy is number 2");
            }
        }
    }

    // --- resolveDuplicateCombis(): keep number 2, repoints number 1's Set
    // List reference, leaves number 1's own bytes untouched --------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildCombiDuplicateFixture(), error);
        CHECK(loaded);
        if (!loaded) return;

        CHECK_EQ(pcg.setlists()[0].songs[0].params.number, 1, "setup: Song A references number 1");

        auto result =
            pcg.resolveDuplicateCombis(/*keepBank=*/0, /*keepNumber=*/2, /*targets=*/{{0, 1}}, /*requireByteExactMatch=*/true);
        CHECK(result.ok);
        if (!result.ok) {
            std::fprintf(stderr, "  resolveDuplicateCombis() error: %s\n", result.error.c_str());
            return;
        }
        CHECK_EQ(result.setlistRefsRepointed, 1, "exactly one Set List slot referenced the duplicate (number 1)");

        CHECK_EQ(pcg.setlists()[0].songs[0].params.number, 2, "Song A repointed to number 2");
        CHECK_EQ(pcg.setlists()[0].songs[1].params.number, 2, "Song B still references number 2 (already correct)");

        // The duplicate's OWN bytes are untouched -- still there, still
        // named "Twin", per this method's own doc comment: no Init Combi
        // template exists yet to clear it to.
        auto stillThere = pcg.decodeCombi(0, 1);
        CHECK(stillThere.has_value());
        if (stillThere) CHECK_EQ(stillThere->name, std::string("Twin"), "the duplicate's own content is untouched");
    }

    // --- Rejected: no such Combi to keep ---------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildCombiDuplicateFixture(), error);
        CHECK(loaded);
        if (!loaded) return;

        auto result = pcg.resolveDuplicateCombis(0, 99, {{0, 1}}, /*requireByteExactMatch=*/true);
        CHECK(!result.ok);
    }
}

// requireByteExactMatch=false -- the Combi side of the same "Same name,
// different content" consolidate flow tested for Programs above (see
// testResolveDuplicatesConsolidateDifferentContent()'s own comment).
// "Solo" (number 0) and "Twin" (number 1) in buildCombiDuplicateFixture()
// are genuinely different content -- the exact pair testFindAndResolve-
// DuplicateCombis() above proves gets REJECTED when requireByteExactMatch=
// true; this proves it's ACCEPTED when false. Combis never clear bytes in
// EITHER mode (PcgFile.h's own doc comment on resolveDuplicateCombis()), so
// unlike the Program version there's no clearedPrograms-style count to
// check -- "Solo"'s own content simply staying "Solo" IS the assertion.
void testResolveDuplicateCombisConsolidateDifferentContent() {
    kronos::PcgFile pcg;
    std::string error;
    bool loaded = pcg.loadFromMemory(buildCombiDuplicateFixture(), error);
    CHECK(loaded);
    if (!loaded) return;

    auto result = pcg.resolveDuplicateCombis(/*keepBank=*/0, /*keepNumber=*/1, /*targets=*/{{0, 0}},
                                              /*requireByteExactMatch=*/false);
    CHECK(result.ok);
    if (!result.ok) {
        std::fprintf(stderr, "  resolveDuplicateCombis() error: %s\n", result.error.c_str());
        return;
    }

    auto stillSolo = pcg.decodeCombi(0, 0);
    CHECK(stillSolo.has_value());
    if (stillSolo) {
        CHECK_EQ(stillSolo->name, std::string("Solo"),
                 "requireByteExactMatch=false leaves the target's genuinely-different content untouched");
    }
}

// Dedicated fixture for testResolveDuplicateCombisSelective() below -- THREE
// byte-identical Combis ("Twin", numbers 1/2/3) plus one unique ("Solo",
// number 0), and two Set List slots each referencing a different Twin copy
// (2/3). buildCombiDuplicateFixture() above only has room for two Twins,
// which is enough to prove resolveDuplicateCombis() repoints a duplicate,
// but not enough to prove it LEAVES AN UNNAMED one alone -- the whole point
// of the 2026-08-25 explicit-targets change (Duplicates panel's resolve-
// picker sidebar lets the user fold in only SOME of a group's duplicates).
std::vector<uint8_t> buildCombiDuplicateTrioFixture() {
    constexpr uint32_t kSongsPerSetlist = 128;
    constexpr size_t kRecordSize = 28;
    constexpr size_t kSbkHeaderSize = 40;
    constexpr size_t kSbkRecordSize = 542;
    constexpr size_t kCombiRecordSize = kTimbreBaseOffset + kTimbreStride * 16;

    std::vector<uint8_t> sdb1;
    pushU32BE(sdb1, 1);
    pushU32BE(sdb1, (kSongsPerSetlist + 1) * kRecordSize);
    pushNameRecord(sdb1, "Test Setlist", kRecordSize);
    pushNameRecord(sdb1, "Song A", kRecordSize);
    pushNameRecord(sdb1, "Song B", kRecordSize);
    for (uint32_t k = 2; k < kSongsPerSetlist; ++k) pushZeros(sdb1, kRecordSize);

    std::vector<uint8_t> sbk1;
    pushU32BE(sbk1, 1);
    pushU32BE(sbk1, static_cast<uint32_t>(kSbkHeaderSize + kSongsPerSetlist * kSbkRecordSize));
    pushZeros(sbk1, kSbkHeaderSize);
    // Neither song references number 0 -- see buildCombiDuplicateFixture()'s
    // own comment above for why (an all-zero Set List slot decodes as
    // bank=0/number=0 too, indistinguishable from a real reference there).
    auto songA = makeSbkSongRecord(/*isProgram=*/false, /*bank=*/0, /*number=*/2, 1, 0, 100, 0, 0, 0, "");
    auto songB = makeSbkSongRecord(/*isProgram=*/false, /*bank=*/0, /*number=*/3, 1, 0, 100, 0, 0, 0, "");
    sbk1.insert(sbk1.end(), songA.begin(), songA.end());
    sbk1.insert(sbk1.end(), songB.begin(), songB.end());
    for (uint32_t k = 2; k < kSongsPerSetlist; ++k) pushZeros(sbk1, kSbkRecordSize);

    std::vector<uint8_t> cbk1BankA;
    pushU32BE(cbk1BankA, 4);
    pushU32BE(cbk1BankA, static_cast<uint32_t>(kCombiRecordSize));
    auto solo = makeCbkCombiRecord("Solo", kCombiRecordSize);
    cbk1BankA.insert(cbk1BankA.end(), solo.begin(), solo.end());  // number 0 -- unique, mismatch-rejection target
    auto twin = makeCbkCombiRecord("Twin", kCombiRecordSize);
    cbk1BankA.insert(cbk1BankA.end(), twin.begin(), twin.end());  // number 1 -- kept
    cbk1BankA.insert(cbk1BankA.end(), twin.begin(), twin.end());  // number 2 -- duplicate A, referenced by Song A
    cbk1BankA.insert(cbk1BankA.end(), twin.begin(), twin.end());  // number 3 -- duplicate B, referenced by Song B

    std::vector<uint8_t> sls1Content;
    appendChunk(sls1Content, "SDB1", sdb1);
    appendChunk(sls1Content, "SBK1", sbk1);

    std::vector<uint8_t> cmb1Content;
    appendChunk(cmb1Content, "CBK1", cbk1BankA);

    std::vector<uint8_t> pcg1Content;
    appendChunk(pcg1Content, "SLS1", sls1Content);
    appendChunk(pcg1Content, "CMB1", cmb1Content);

    std::vector<uint8_t> data;
    data.insert(data.end(), {'K', 'O', 'R', 'G'});
    pushZeros(data, 12);
    appendChunk(data, "PCG1", pcg1Content);
    return data;
}

void testResolveDuplicateCombisSelective() {
    // --- Fold in only duplicate A (number 2), leave duplicate B (number 3)
    // completely alone -- the core selective-resolve guarantee. -----------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildCombiDuplicateTrioFixture(), error);
        CHECK(loaded);
        if (!loaded) return;

        auto result =
            pcg.resolveDuplicateCombis(/*keepBank=*/0, /*keepNumber=*/1, /*targets=*/{{0, 2}}, /*requireByteExactMatch=*/true);
        CHECK(result.ok);
        if (!result.ok) {
            std::fprintf(stderr, "  resolveDuplicateCombis() error: %s\n", result.error.c_str());
            return;
        }
        CHECK_EQ(result.setlistRefsRepointed, 1, "only Song A's reference (to the targeted number 2) was repointed");

        CHECK_EQ(pcg.setlists()[0].songs[0].params.number, 1, "Song A repointed to the kept number 1");
        CHECK_EQ(pcg.setlists()[0].songs[1].params.number, 3, "Song B untouched -- number 3 was never a target");

        auto untouchedDup = pcg.decodeCombi(0, 3);
        CHECK(untouchedDup.has_value());
        if (untouchedDup) CHECK_EQ(untouchedDup->name, std::string("Twin"), "the un-targeted duplicate's content is untouched");

        // Still findable as the SAME 3-member duplicate group as before --
        // unlike the Program version, resolving a Combi duplicate never
        // clears its bytes (this method's own doc comment), so number 2 is
        // still byte-identical to numbers 1 and 3 even though its Set List
        // reference already moved. Confirms resolving one member doesn't
        // make it invisible to a follow-up resolve round (the sidebar's own
        // "stays open" model) -- there's still a real reason to come back.
        auto groups = pcg.findDuplicateCombis();
        CHECK_EQ(groups.size(), static_cast<size_t>(1), "numbers 1, 2, and 3 still form one byte-exact group");
        if (groups.size() == 1) CHECK_EQ(groups[0].size(), static_cast<size_t>(3), "content hashing is unaffected by resolve -- all three copies remain");
    }

    // --- Rejected: one target (number 0, "Solo") doesn't share the kept
    // copy's contentHash -- all-or-nothing, so the OTHER, genuinely-
    // matching target in the same call is left untouched too. ------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildCombiDuplicateTrioFixture(), error);
        CHECK(loaded);
        if (!loaded) return;

        auto result = pcg.resolveDuplicateCombis(0, 1, {{0, 2}, {0, 0}}, /*requireByteExactMatch=*/true);
        CHECK(!result.ok);

        CHECK_EQ(pcg.setlists()[0].songs[0].params.number, 2, "hash-mismatch rejection writes nothing -- Song A still references number 2");
        CHECK_EQ(pcg.setlists()[0].songs[1].params.number, 3, "Song B untouched");
    }
}

void testCombiRearrange() {
    // --- swapCombis(): bank0/1 ("Combi A") <-> bank1/0 ("Other Bank
    // Combi"), both referenced ------------------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildCombiRearrangeFixture(), error);
        CHECK(loaded);
        if (!loaded) return;

        auto result = pcg.swapCombis(0, 1, 1, 0);
        CHECK(result.ok);
        if (!result.ok) std::fprintf(stderr, "  swapCombis() error: %s\n", result.error.c_str());
        CHECK_EQ(result.setlistRefsRepointed, 2, "both referenced slots (Song A, Song C) moved");

        auto atBank0 = pcg.decodeCombi(0, 1);
        auto atBank1 = pcg.decodeCombi(1, 0);
        CHECK(atBank0.has_value() && atBank1.has_value());
        if (atBank0 && atBank1) {
            CHECK_EQ(atBank0->name, std::string("Other Bank Combi"), "bank0/1 now holds what was at bank1/0");
            CHECK_EQ(atBank1->name, std::string("Combi A"), "bank1/0 now holds what was at bank0/1");
        }
        CHECK_EQ(pcg.setlists()[0].songs[0].params.bank, 1, "Song A followed Combi A to bank 1");
        CHECK_EQ(pcg.setlists()[0].songs[0].params.number, 0, "Song A followed Combi A to number 0");
        CHECK_EQ(pcg.setlists()[0].songs[2].params.bank, 0, "Song C followed Other Bank Combi to bank 0");
        CHECK_EQ(pcg.setlists()[0].songs[2].params.number, 1, "Song C followed Other Bank Combi to number 1");
        // Song B (bank0/2, "Combi B") was never part of the swap -- must be untouched.
        CHECK_EQ(pcg.setlists()[0].songs[1].params.bank, 0, "Song B untouched by an unrelated swap");
        CHECK_EQ(pcg.setlists()[0].songs[1].params.number, 2, "Song B untouched by an unrelated swap");

        // No-op: swapping a slot with itself.
        auto noop = pcg.swapCombis(0, 2, 0, 2);
        CHECK(noop.ok);
        CHECK_EQ(noop.setlistRefsRepointed, 0, "self-swap is a no-op");

        // Rejected: out-of-range position.
        auto bad = pcg.swapCombis(99, 0, 0, 1);
        CHECK(!bad.ok);
    }

    // --- moveCombiWithinBank(): shift bank0's Combi C (number 3) to
    // number 1, shifting A and B up by one -------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildCombiRearrangeFixture(), error);
        CHECK(loaded);
        if (!loaded) return;

        auto result = pcg.moveCombiWithinBank(0, 3, 1);
        CHECK(result.ok);
        if (!result.ok) std::fprintf(stderr, "  moveCombiWithinBank() error: %s\n", result.error.c_str());
        // Combi C itself moved (was unreferenced, so 0 for it) -- but
        // shifting A (referenced by Song A) and B (referenced by Song B)
        // out of the way repoints both: 2 total.
        CHECK_EQ(result.setlistRefsRepointed, 2, "both shifted-out records' referrers followed them");

        auto n1 = pcg.decodeCombi(0, 1);
        auto n2 = pcg.decodeCombi(0, 2);
        auto n3 = pcg.decodeCombi(0, 3);
        CHECK(n1.has_value() && n2.has_value() && n3.has_value());
        if (n1 && n2 && n3) {
            CHECK_EQ(n1->name, std::string("Combi C"), "Combi C moved to number 1");
            CHECK_EQ(n2->name, std::string("Combi A"), "Combi A shifted to number 2");
            CHECK_EQ(n3->name, std::string("Combi B"), "Combi B shifted to number 3");
        }
        CHECK_EQ(pcg.setlists()[0].songs[0].params.number, 2, "Song A followed Combi A to number 2");
        CHECK_EQ(pcg.setlists()[0].songs[1].params.number, 3, "Song B followed Combi B to number 3");

        // Rejected: out-of-range index.
        auto bad = pcg.moveCombiWithinBank(0, 0, 99);
        CHECK(!bad.ok);
    }

    // --- moveCombiToBank(): happy path, dest-referenced refusal, no-
    // filler refusal -----------------------------------------------------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildCombiRearrangeFixture(), error);
        CHECK(loaded);
        if (!loaded) return;

        // bank1/0 ("Other Bank Combi") IS referenced (Song C) -- refuse.
        auto refused = pcg.moveCombiToBank(0, 1, 1, 0);
        CHECK(!refused.ok);
        auto stillThere = pcg.decodeCombi(1, 0);
        CHECK(stillThere.has_value());
        if (stillThere) CHECK_EQ(stillThere->name, std::string("Other Bank Combi"), "refused move writes nothing");

        // Same-bank rejected outright (use moveCombiWithinBank() instead).
        auto sameBank = pcg.moveCombiToBank(0, 1, 0, 2);
        CHECK(!sameBank.ok);

        // Happy path: move Combi B (bank0/2, referenced by Song B) into
        // bank1/1 ("Empty Target", unreferenced).
        auto moved = pcg.moveCombiToBank(0, 2, 1, 1);
        CHECK(moved.ok);
        if (!moved.ok) std::fprintf(stderr, "  moveCombiToBank() error: %s\n", moved.error.c_str());
        CHECK_EQ(moved.setlistRefsRepointed, 1, "Song B (the only referrer) followed Combi B to its new home");

        auto atDest = pcg.decodeCombi(1, 1);
        CHECK(atDest.has_value());
        if (atDest) CHECK_EQ(atDest->name, std::string("Combi B"), "destination now holds the moved Combi's content");

        auto vacated = pcg.decodeCombi(0, 2);
        CHECK(vacated.has_value());
        if (vacated) {
            CHECK_EQ(vacated->name, std::string("- Init Combi -"),
                     "vacated source renamed to the visibility-customized filler name");
        }

        CHECK_EQ(pcg.setlists()[0].songs[1].params.bank, 1, "Song B repointed to the new bank");
        CHECK_EQ(pcg.setlists()[0].songs[1].params.number, 1, "Song B repointed to the new number");

        // Rejected: bank 1 (the source bank here) has no "Init Combi" of
        // its own to vacate with -- source=bank1/0 ("Other Bank Combi",
        // still referenced by Song C, which is fine -- only the
        // DESTINATION's references block a move), destination=bank0/3
        // ("Combi C", unreferenced -- passes that check cleanly) -- so this
        // isolates the "no filler" refusal specifically, not the
        // destination-referenced one.
        auto noFiller = pcg.moveCombiToBank(1, 0, 0, 3);
        CHECK(!noFiller.ok);
        auto stillC = pcg.decodeCombi(0, 3);
        CHECK(stillC.has_value());
        if (stillC) CHECK_EQ(stillC->name, std::string("Combi C"), "rejected move writes nothing");
    }

    // --- copyCombi(): happy path (cross-bank, case-insensitive "init
    // combi" match), source untouched, and three refusal paths -----------
    {
        kronos::PcgFile pcg;
        std::string error;
        bool loaded = pcg.loadFromMemory(buildCombiRearrangeFixture(), error);
        CHECK(loaded);
        if (!loaded) return;

        // Happy path: copy Combi A (bank0/1, referenced by Song A) onto
        // bank1/2 ("- iNit COMBI -" -- mixed case, dash-wrapped, and a
        // DIFFERENT bank, exercising both the case-insensitive substring
        // match and that a copy doesn't care about same/different bank the
        // way moveCombiWithinBank()/moveCombiToBank() do).
        auto copied = pcg.copyCombi(0, 1, 1, 2);
        CHECK(copied.ok);
        if (!copied.ok) std::fprintf(stderr, "  copyCombi() error: %s\n", copied.error.c_str());
        CHECK_EQ(copied.setlistRefsRepointed, 0, "a copy never repoints anything");

        auto atDest = pcg.decodeCombi(1, 2);
        CHECK(atDest.has_value());
        if (atDest) CHECK_EQ(atDest->name, std::string("Combi A"), "destination now holds a copy of the source's content");

        auto sourceStill = pcg.decodeCombi(0, 1);
        CHECK(sourceStill.has_value());
        if (sourceStill) CHECK_EQ(sourceStill->name, std::string("Combi A"), "source is left completely untouched by a copy");
        CHECK_EQ(pcg.setlists()[0].songs[0].params.bank, 0, "Song A's reference to the SOURCE is untouched");
        CHECK_EQ(pcg.setlists()[0].songs[0].params.number, 1, "Song A's reference to the SOURCE is untouched");

        // Rejected: destination isn't empty (a real, named Combi already
        // lives there) -- writes nothing.
        auto occupied = pcg.copyCombi(0, 1, 0, 2);
        CHECK(!occupied.ok);
        auto stillB = pcg.decodeCombi(0, 2);
        CHECK(stillB.has_value());
        if (stillB) CHECK_EQ(stillB->name, std::string("Combi B"), "rejected copy writes nothing");

        // Rejected: destination LOOKS empty ("Init Combi") but is still
        // referenced (bank0/4, referenced by Song D) -- same defensive
        // reasoning as moveCombiToBank()'s own destination check.
        auto referenced = pcg.copyCombi(0, 1, 0, 4);
        CHECK(!referenced.ok);

        // Rejected: source and destination are the same slot.
        auto sameSlot = pcg.copyCombi(0, 1, 0, 1);
        CHECK(!sameSlot.ok);

        // Rejected: out-of-range destination bank.
        auto outOfRange = pcg.copyCombi(0, 1, 99, 0);
        CHECK(!outOfRange.ok);
    }
}

// --- Cross-dataset Combi copy: two independent files (not one shared
// fixture -- this feature is inherently cross-file, unlike every other
// Combi test above) -----------------------------------------------------
//
// The SOURCE file's one Combi has:
//  - Timbre 0 (rawBankCode=0/INT-A, number=2) -> "Shared Lead", a Program
//    that will exist BYTE-IDENTICAL in the destination but at a DIFFERENT
//    (bank, number) -- proves matching is by contentHash, not position.
//  - Timbre 1 (rawBankCode=1/INT-B, number=3) -> "Unique Brass", a Program
//    that does NOT exist in the destination at all -- needs a placement.
//  - Timbre 2 (rawBankCode=6, GM) -- permanently indexless, must be copied
//    through unchanged and never listed as a dependency.
//  - Timbres 3-15: default (all-zero) -- must be skipped entirely.
constexpr size_t kCrossDsTimbreBase = 4806;
constexpr size_t kCrossDsTimbreStride = 188;
constexpr size_t kCrossDsCombiRecordSize = kCrossDsTimbreBase + kCrossDsTimbreStride * 16;
constexpr size_t kCrossDsProgramRecordSize = 64;  // small synthetic stride, real files use ~4960

std::vector<uint8_t> makeCrossDsSourceCombiRecord(const std::string& name) {
    std::vector<uint8_t> rec(kCrossDsCombiRecordSize, 0);
    for (size_t i = 0; i < name.size() && 4 + i < rec.size(); ++i) rec[4 + i] = static_cast<uint8_t>(name[i]);
    auto setTimbre = [&](int index, int number, int rawBankCode) {
        size_t h = kCrossDsTimbreBase + static_cast<size_t>(index) * kCrossDsTimbreStride;
        rec[h] = static_cast<uint8_t>(number);
        rec[h + 1] = static_cast<uint8_t>(rawBankCode);
        rec[h + 2] = static_cast<uint8_t>(1 << 5);  // status Internal
    };
    setTimbre(0, 2, 0);    // -> INT-A/2 "Shared Lead"
    setTimbre(1, 3, 1);    // -> INT-B/3 "Unique Brass"
    setTimbre(2, 91, 6);   // -> GM (number is arbitrary -- GM has no real Program lookup)
    setTimbre(3, 12, 16);  // -> raw code 16, a real gap this project hasn't identified at
                            // all (neither kConfirmedTimbreBanks nor
                            // kConfirmedTimbreBankNamesOnly has an entry for it) --
                            // exercises analyzeCombiCrossDatasetCopy()'s unmappableTimbres
    return rec;
}

// One PBK1 bank's worth of records from (number, name) pairs -- a number
// not listed within [0, max explicit number] decodes to name="" (a "free"
// slot, this fixture's own stand-in for copyProgramFrom()'s real
// name.empty() check).
std::vector<uint8_t> buildCrossDsProgramBank(const std::vector<std::pair<int, std::string>>& entries) {
    int maxNumber = 0;
    for (const auto& entry : entries) maxNumber = std::max(maxNumber, entry.first);
    std::vector<std::string> names(static_cast<size_t>(maxNumber) + 1);
    for (const auto& entry : entries) names[static_cast<size_t>(entry.first)] = entry.second;

    std::vector<uint8_t> bank;
    pushU32BE(bank, static_cast<uint32_t>(names.size()));
    pushU32BE(bank, static_cast<uint32_t>(kCrossDsProgramRecordSize));
    for (const auto& name : names) pushNameRecord(bank, name, kCrossDsProgramRecordSize);
    return bank;
}

// Shared skeleton for both cross-dataset fixtures below: one Set List (128
// slots, slot 0 optionally referencing a Combi -- `referencedCombi`), two
// PBK1 Program banks (INT-A/INT-B, both HD-1 -- MBK1/EXi isn't needed to
// exercise this feature), and one CBK1 Combi bank built from
// `combiRecordBytes` directly (letting each fixture supply already-built
// per-slot records: the source needs one real-Timbre record
// (makeCrossDsSourceCombiRecord()), the destination needs several plain
// name-only ones, see pushNameRecord()).
std::vector<uint8_t> buildCrossDatasetFixture(const std::vector<uint8_t>& combiBankRecords, int combiCount,
                                               const std::vector<std::pair<int, std::string>>& programBank0,
                                               const std::vector<std::pair<int, std::string>>& programBank1,
                                               std::optional<std::pair<int, int>> referencedCombi) {
    constexpr uint32_t kSongsPerSetlist = 128;
    constexpr size_t kRecordSize = 28;
    constexpr size_t kSbkHeaderSize = 40;
    constexpr size_t kSbkRecordSize = 542;

    std::vector<uint8_t> sdb1;
    pushU32BE(sdb1, 1);
    pushU32BE(sdb1, (kSongsPerSetlist + 1) * kRecordSize);
    pushNameRecord(sdb1, "Test Setlist", kRecordSize);
    pushNameRecord(sdb1, "Song A", kRecordSize);
    for (uint32_t k = 1; k < kSongsPerSetlist; ++k) pushZeros(sdb1, kRecordSize);

    std::vector<uint8_t> sbk1;
    pushU32BE(sbk1, 1);
    pushU32BE(sbk1, static_cast<uint32_t>(kSbkHeaderSize + kSongsPerSetlist * kSbkRecordSize));
    pushZeros(sbk1, kSbkHeaderSize);
    if (referencedCombi.has_value()) {
        auto songA = makeSbkSongRecord(/*isProgram=*/false, referencedCombi->first, referencedCombi->second, 1, 0,
                                        100, 0, 0, 0, "");
        sbk1.insert(sbk1.end(), songA.begin(), songA.end());
    } else {
        pushZeros(sbk1, kSbkRecordSize);
    }
    for (uint32_t k = 1; k < kSongsPerSetlist; ++k) pushZeros(sbk1, kSbkRecordSize);

    std::vector<uint8_t> cbk1;
    pushU32BE(cbk1, static_cast<uint32_t>(combiCount));
    pushU32BE(cbk1, static_cast<uint32_t>(kCrossDsCombiRecordSize));
    cbk1.insert(cbk1.end(), combiBankRecords.begin(), combiBankRecords.end());

    std::vector<uint8_t> sls1Content;
    appendChunk(sls1Content, "SDB1", sdb1);
    appendChunk(sls1Content, "SBK1", sbk1);

    std::vector<uint8_t> prg1Content;
    appendChunk(prg1Content, "PBK1", buildCrossDsProgramBank(programBank0));
    appendChunk(prg1Content, "PBK1", buildCrossDsProgramBank(programBank1));

    std::vector<uint8_t> cmb1Content;
    appendChunk(cmb1Content, "CBK1", cbk1);

    std::vector<uint8_t> pcg1Content;
    appendChunk(pcg1Content, "SLS1", sls1Content);
    appendChunk(pcg1Content, "PRG1", prg1Content);
    appendChunk(pcg1Content, "CMB1", cmb1Content);

    std::vector<uint8_t> data;
    data.insert(data.end(), {'K', 'O', 'R', 'G'});
    pushZeros(data, 12);
    appendChunk(data, "PCG1", pcg1Content);
    return data;
}

std::vector<uint8_t> buildCrossDatasetSrcFixture() {
    auto combi0 = makeCrossDsSourceCombiRecord("Source Combi");
    return buildCrossDatasetFixture(combi0, 1, {{2, "Shared Lead"}}, {{3, "Unique Brass"}}, std::nullopt);
}

// Destination CBK1 bank 0: number0="Unused" -- deliberately never a real
// target. Every genuinely-unused Set List slot in this fixture ALSO decodes
// as a reference to (bank=0, number=0) (the documented all-zero collision,
// docs/content/format/index.md §5.4 -- an unassigned slot is
// byte-indistinguishable from a real reference to bank 0/number 0), so a
// real target ever placed at (0, 0) would spuriously read as
// Set-List-referenced by all 127 of this fixture's own unused slots. Same
// fix buildCombiRearrangeFixture() already uses for the identical reason.
// number1="Init Combi" (unreferenced -- the happy-path target),
// number2="Occupied Combi" (not empty -- "target not empty" refusal),
// number3="Init Combi" too, but Set List slot 0 references it
// (Set-List-referenced refusal). PBK1 bank0 (INT-A) already holds "Shared
// Lead" at a DIFFERENT number (5, not 2) than the source -- the
// contentHash match must still find it. PBK1 bank1 (INT-B) is entirely
// empty (a single free slot, number 0) -- "Unique Brass" isn't there, but
// there's exactly one candidate slot to place it into.
std::vector<uint8_t> buildCrossDatasetDstFixture() {
    std::vector<uint8_t> combiBank;
    pushNameRecord(combiBank, "Unused", kCrossDsCombiRecordSize);
    pushNameRecord(combiBank, "Init Combi", kCrossDsCombiRecordSize);
    pushNameRecord(combiBank, "Occupied Combi", kCrossDsCombiRecordSize);
    pushNameRecord(combiBank, "Init Combi", kCrossDsCombiRecordSize);
    return buildCrossDatasetFixture(combiBank, 4, {{5, "Shared Lead"}}, {}, std::make_pair(0, 3));
}

void testCombiCrossDatasetCopy() {
    kronos::PcgFile src;
    kronos::PcgFile dst;
    std::string error;
    bool srcLoaded = src.loadFromMemory(buildCrossDatasetSrcFixture(), error);
    CHECK(srcLoaded);
    if (!srcLoaded) { std::fprintf(stderr, "  source fixture load error: %s\n", error.c_str()); return; }
    bool dstLoaded = dst.loadFromMemory(buildCrossDatasetDstFixture(), error);
    CHECK(dstLoaded);
    if (!dstLoaded) { std::fprintf(stderr, "  destination fixture load error: %s\n", error.c_str()); return; }

    // --- analyzeCombiCrossDatasetCopy(): happy-path destination ----------
    {
        auto analysis = dst.analyzeCombiCrossDatasetCopy(src, 0, 0, 0, 1);
        CHECK(analysis.ok);
        if (!analysis.ok) std::fprintf(stderr, "  analyze() error: %s\n", analysis.error.c_str());

        // Exactly 2 dependencies -- Timbre 0 (found) and Timbre 1 (unresolved).
        // GM (Timbre 2), the unmappable raw code (Timbre 3), and every
        // default Timbre are never listed as dependencies.
        CHECK_EQ(static_cast<int>(analysis.dependencies.size()), 2,
                 "GM, the unmappable Timbre, and default Timbres aren't dependencies");
        bool sawFound = false, sawUnresolved = false;
        for (const auto& dep : analysis.dependencies) {
            if (dep.timbreIndex == 0) {
                sawFound = true;
                CHECK_EQ(dep.name, std::string("Shared Lead"), "Timbre 0's dependency is Shared Lead");
                CHECK(dep.found);
                CHECK_EQ(dep.foundBank, 0, "found at dest bank 0");
                CHECK_EQ(dep.foundNumber, 5, "found at dest number 5 -- a DIFFERENT position than the source's 2");
            } else if (dep.timbreIndex == 1) {
                sawUnresolved = true;
                CHECK_EQ(dep.name, std::string("Unique Brass"), "Timbre 1's dependency is Unique Brass");
                CHECK(!dep.found);
            }
        }
        CHECK(sawFound);
        CHECK(sawUnresolved);

        // Timbre 3's raw code (16) is neither a confirmed Program bank nor a
        // confirmed permanently-indexless code (GM/G(n)/g(n)) -- it lands in
        // unmappableTimbres so the caller can warn about it, distinctly from
        // both `dependencies` (nothing there resolves it) and `unresolved`
        // (there's no Program to place -- there's nothing to place at all).
        CHECK_EQ(static_cast<int>(analysis.unmappableTimbres.size()), 1, "exactly one unmappable Timbre");
        if (!analysis.unmappableTimbres.empty()) {
            const auto& u = analysis.unmappableTimbres[0];
            CHECK_EQ(u.timbreIndex, 3, "Timbre 3 is the unmappable one");
            CHECK_EQ(u.rawBankCode, 16, "its raw bank code is preserved for the UI to report");
            CHECK_EQ(u.rawNumber, 12, "its raw number is preserved too");
        }

        CHECK_EQ(static_cast<int>(analysis.unresolved.size()), 1, "exactly one UNIQUE unresolved Program");
        if (!analysis.unresolved.empty()) {
            const auto& u = analysis.unresolved[0];
            CHECK_EQ(u.srcBank, 1, "Unique Brass's own source bank");
            CHECK_EQ(u.srcNumber, 3, "Unique Brass's own source number");
            // Both dest banks qualify -- bank 0 (INT-A) has free slots too
            // (only number 5 is filled, "Shared Lead"), not just bank 1.
            CHECK_EQ(static_cast<int>(u.candidateBanks.size()), 2, "both dest banks have a free slot of the matching type");
            if (u.candidateBanks.size() == 2) {
                CHECK_EQ(u.candidateBanks[0], 0, "candidate banks come back sorted ascending");
                CHECK_EQ(u.candidateBanks[1], 1, "candidate banks come back sorted ascending");
            }
        }
    }

    // --- analyzeCombiCrossDatasetCopy(): destination refusals -------------
    {
        auto occupied = dst.analyzeCombiCrossDatasetCopy(src, 0, 0, 0, 2);
        CHECK(!occupied.ok);
        auto referenced = dst.analyzeCombiCrossDatasetCopy(src, 0, 0, 0, 3);
        CHECK(!referenced.ok);
    }

    // --- applyCombiCrossDatasetCopy(): refused without a placement for the
    // unresolved Program -- writes nothing ---------------------------------
    {
        auto missingPlacement = dst.applyCombiCrossDatasetCopy(src, 0, 0, 0, 1, {});
        CHECK(!missingPlacement.ok);
        auto stillEmpty = dst.decodeCombi(0, 1);
        CHECK(stillEmpty.has_value());
        if (stillEmpty) CHECK_EQ(stillEmpty->name, std::string("Init Combi"), "refused apply writes nothing");
    }

    // --- applyCombiCrossDatasetCopy(): happy path -------------------------
    {
        kronos::PcgFile::ProgramPlacement placement;
        placement.srcBank = 1;
        placement.srcNumber = 3;
        placement.dstBank = 1;
        auto result = dst.applyCombiCrossDatasetCopy(src, 0, 0, 0, 1, {placement});
        CHECK(result.ok);
        if (!result.ok) std::fprintf(stderr, "  apply() error: %s\n", result.error.c_str());
        CHECK_EQ(result.setlistRefsRepointed, 0, "a copy never repoints anything");

        auto copiedCombi = dst.decodeCombi(0, 1);
        CHECK(copiedCombi.has_value());
        if (copiedCombi) {
            CHECK_EQ(copiedCombi->name, std::string("Source Combi"), "destination now holds the source Combi's content");
            CHECK(!copiedCombi->timbres[0].isDefault);
            CHECK_EQ(copiedCombi->timbres[0].number, 5, "Timbre 0 rewritten to the FOUND Program's dest number");
            CHECK_EQ(copiedCombi->timbres[0].rawBankCode, 0, "Timbre 0 still points at INT-A (raw code 0)");
            CHECK(!copiedCombi->timbres[1].isDefault);
            CHECK_EQ(copiedCombi->timbres[1].number, 0, "Timbre 1 rewritten to the newly-copied Program's dest number (dest PBK1 bank 1's only slot)");
            CHECK_EQ(copiedCombi->timbres[1].rawBankCode, 1, "Timbre 1 still points at INT-B (raw code 1)");
            CHECK(!copiedCombi->timbres[2].isDefault);
            CHECK_EQ(copiedCombi->timbres[2].number, 91, "GM Timbre's bytes pass through UNCHANGED");
            CHECK_EQ(copiedCombi->timbres[2].rawBankCode, 6, "GM Timbre's raw bank code is untouched");
            CHECK(!copiedCombi->timbres[3].isDefault);
            CHECK_EQ(copiedCombi->timbres[3].number, 12, "unmappable Timbre's bytes pass through UNCHANGED too");
            CHECK_EQ(copiedCombi->timbres[3].rawBankCode, 16, "unmappable Timbre's raw bank code is untouched");
            CHECK(copiedCombi->timbres[4].isDefault);  // a default Timbre stays default
        }

        auto newProgram = dst.decodeProgram(1, 0);
        CHECK(newProgram.has_value());
        if (newProgram) CHECK_EQ(newProgram->name, std::string("Unique Brass"), "Unique Brass really got copied into dest");

        // Source is COMPLETELY untouched by a copy -- re-decode it fresh and
        // compare against what it held before this whole test ran.
        auto sourceStill = src.decodeCombi(0, 0);
        CHECK(sourceStill.has_value());
        if (sourceStill) {
            CHECK_EQ(sourceStill->name, std::string("Source Combi"), "source Combi itself is untouched");
            CHECK_EQ(sourceStill->timbres[1].number, 3, "source Timbre 1 still points at its OWN original Program");
        }
        auto sourceProgramStill = src.decodeProgram(1, 3);
        CHECK(sourceProgramStill.has_value());
        if (sourceProgramStill) CHECK_EQ(sourceProgramStill->name, std::string("Unique Brass"), "source's own Program copy is untouched");
    }

    // --- Zero candidate banks reports an empty list, not a guess ----------
    {
        kronos::PcgFile srcFresh;
        kronos::PcgFile dstNoFreeSlot;
        std::string loadError;
        CHECK(srcFresh.loadFromMemory(buildCrossDatasetSrcFixture(), loadError));
        // Same destination shape, but EVERY slot in BOTH PBK1 banks is
        // filled -- not just bank 1 (INT-B) but also bank 0 (INT-A, which
        // in the main fixture above has plenty of free slots at 0-4) --
        // Unique Brass genuinely has nowhere left to go anywhere. number0 is
        // the same "Unused" dummy buildCrossDatasetDstFixture() uses, for
        // the same (bank=0, number=0) all-zero-collision reason (see that
        // function's own comment) -- every unused Set List slot in this
        // fixture ALSO decodes as referencing (0, 0).
        std::vector<uint8_t> combiBank;
        pushNameRecord(combiBank, "Unused", kCrossDsCombiRecordSize);
        pushNameRecord(combiBank, "Init Combi", kCrossDsCombiRecordSize);
        auto fixture = buildCrossDatasetFixture(
            combiBank, 2,
            {{0, "Filler A"}, {1, "Filler B"}, {2, "Filler C"}, {3, "Filler D"}, {4, "Filler E"}, {5, "Shared Lead"}},
            {{0, "Already Full"}}, std::nullopt);
        CHECK(dstNoFreeSlot.loadFromMemory(fixture, loadError));

        auto analysis = dstNoFreeSlot.analyzeCombiCrossDatasetCopy(srcFresh, 0, 0, 0, 1);
        CHECK(analysis.ok);
        CHECK_EQ(static_cast<int>(analysis.unresolved.size()), 1, "still one unresolved Program");
        if (!analysis.unresolved.empty()) {
            CHECK_EQ(static_cast<int>(analysis.unresolved[0].candidateBanks.size()), 0,
                     "no bank has a free slot -- reported honestly, not guessed");
        }

        // Applying with no valid placement available is refused the same way.
        auto applied = dstNoFreeSlot.applyCombiCrossDatasetCopy(srcFresh, 0, 0, 0, 1, {});
        CHECK(!applied.ok);
    }
}

// A Combi with exactly ONE real Timbre (index 0, INT-A/`number`) -- unlike
// makeCrossDsSourceCombiRecord() above (which hardcodes 3 Timbres for that
// test's own scenario), this is for testCombiCrossDatasetCopyExactSlot()'s
// own single-dependency fixture.
std::vector<uint8_t> makeCrossDsSingleTimbreCombiRecord(const std::string& name, int number) {
    std::vector<uint8_t> rec(kCrossDsCombiRecordSize, 0);
    for (size_t i = 0; i < name.size() && 4 + i < rec.size(); ++i) rec[4 + i] = static_cast<uint8_t>(name[i]);
    rec[kCrossDsTimbreBase] = static_cast<uint8_t>(number);
    rec[kCrossDsTimbreBase + 1] = 0;              // rawBankCode 0 -> INT-A
    rec[kCrossDsTimbreBase + 2] = 1 << 5;          // status Internal
    return rec;
}

// ProgramPlacement::dstNumber (2026-08-15): a caller can now pin an exact
// destination slot instead of leaving apply() to auto-pick the first free
// one -- built for the cross-dataset panel's own per-bank slot dropdown
// (frontend/combi-cross-dataset-panel.js), which lets the user choose a
// SPECIFIC empty Program by name/number, not just a bank. A dedicated small
// fixture pair, separate from testCombiCrossDatasetCopy()'s shared src/dst
// above -- those two files' state evolves across that test's own sub-tests
// (its one unresolved Program gets resolved partway through), so reusing
// them here would mean this test's outcome depends on running after (and
// not disturbing) that one, which is exactly the kind of order-coupling
// this project avoids in its fixtures.
void testCombiCrossDatasetCopyExactSlot() {
    // Destination PBK1 bank 0 (INT-A): numbers 0-2 free, number 3 filled --
    // 3 real candidate slots so picking a NON-lowest one (2) actually proves
    // the exact choice is honored, not just "first free slot" by coincidence.
    std::vector<uint8_t> combiBank;
    pushNameRecord(combiBank, "Unused", kCrossDsCombiRecordSize);      // number 0 -- (0,0)-collision dummy, see buildCrossDatasetDstFixture()'s own comment
    pushNameRecord(combiBank, "Init Combi", kCrossDsCombiRecordSize);  // number 1 -- the copy target
    auto dstFixture = buildCrossDatasetFixture(combiBank, 2, {{3, "Filler"}}, {}, std::nullopt);

    auto srcCombi = makeCrossDsSingleTimbreCombiRecord("Slot Test Source", /*number=*/9);
    auto srcFixture = buildCrossDatasetFixture(srcCombi, 1, {{9, "Needs Placement"}}, {}, std::nullopt);

    kronos::PcgFile src, dst;
    std::string error;
    CHECK(src.loadFromMemory(srcFixture, error));
    CHECK(dst.loadFromMemory(dstFixture, error));

    auto analysis = dst.analyzeCombiCrossDatasetCopy(src, 0, 0, 0, 1);
    CHECK(analysis.ok);
    CHECK_EQ(static_cast<int>(analysis.unresolved.size()), 1, "one unresolved Program");
    if (!analysis.unresolved.empty()) {
        // Both dest PBK1 banks are HD-1 typed in this fixture skeleton --
        // bank 1 always has its own single free slot too (programBank1={}
        // still builds one empty record), same "both banks qualify" shape
        // testCombiCrossDatasetCopy()'s own happy-path sub-test already
        // established.
        CHECK_EQ(static_cast<int>(analysis.unresolved[0].candidateBanks.size()), 2, "both dest banks have a free slot");
    }

    // --- Happy path: pin an exact, non-lowest free slot -------------------
    {
        kronos::PcgFile::ProgramPlacement placement;
        placement.srcBank = 0;
        placement.srcNumber = 9;
        placement.dstBank = 0;
        placement.dstNumber = 2;
        auto result = dst.applyCombiCrossDatasetCopy(src, 0, 0, 0, 1, {placement});
        CHECK(result.ok);
        if (!result.ok) std::fprintf(stderr, "  apply() error: %s\n", result.error.c_str());

        auto copiedCombi = dst.decodeCombi(0, 1);
        CHECK(copiedCombi.has_value());
        if (copiedCombi) {
            CHECK(!copiedCombi->timbres[0].isDefault);
            CHECK_EQ(copiedCombi->timbres[0].number, 2, "Timbre rewritten to the EXACT chosen slot, not the lowest free one");
        }

        auto placedProgram = dst.decodeProgram(0, 2);
        CHECK(placedProgram.has_value());
        if (placedProgram) CHECK_EQ(placedProgram->name, std::string("Needs Placement"), "the Program landed exactly where chosen");

        // Slots 0 and 1 -- both free and LOWER than the chosen 2 -- must stay
        // untouched, proving apply() didn't quietly fall back to "first free".
        auto slot0 = dst.decodeProgram(0, 0);
        CHECK(slot0.has_value());
        if (slot0) CHECK(slot0->name.empty());
        auto slot1 = dst.decodeProgram(0, 1);
        CHECK(slot1.has_value());
        if (slot1) CHECK(slot1->name.empty());
    }

    // --- Refused: the chosen exact slot is no longer free at apply time
    // (e.g. a write from the opposite pane between analyze() and apply()) --
    // a FRESH destination file, not the one just mutated above, with number
    // 2 already occupied. -----------------------------------------------
    {
        std::vector<uint8_t> staleCombiBank;
        pushNameRecord(staleCombiBank, "Unused", kCrossDsCombiRecordSize);
        pushNameRecord(staleCombiBank, "Init Combi", kCrossDsCombiRecordSize);
        auto staleFixture = buildCrossDatasetFixture(staleCombiBank, 2, {{2, "Already Taken"}}, {}, std::nullopt);
        kronos::PcgFile staleDst;
        CHECK(staleDst.loadFromMemory(staleFixture, error));

        kronos::PcgFile::ProgramPlacement stalePlacement;
        stalePlacement.srcBank = 0;
        stalePlacement.srcNumber = 9;
        stalePlacement.dstBank = 0;
        stalePlacement.dstNumber = 2;
        auto refused = staleDst.applyCombiCrossDatasetCopy(src, 0, 0, 0, 1, {stalePlacement});
        CHECK(!refused.ok);

        auto stillInit = staleDst.decodeCombi(0, 1);
        CHECK(stillInit.has_value());
        if (stillInit) CHECK_EQ(stillInit->name, std::string("Init Combi"), "refused apply writes nothing");
    }
}

// looksLikeEmptyProgramName() (2026-08-15): a genuinely untouched Program
// slot on real Kronos hardware is named Korg's own factory
// "Init Program"/"Init EXi Program", NOT a blank string -- confirmed
// against two independent real backup files (docs/content/format/index.md
// §5.5). Every "is this slot free" check used to test name.empty() only,
// which every one of this project's OWN synthetic fixtures happens to
// satisfy (a blank-name empty record), masking the bug -- reported
// directly against a real personal Kronos backup, where cross-dataset
// Combi copy found ZERO free destination banks anywhere despite the
// dataset genuinely having room. This test deliberately uses "Init
// Program"/"Init EXi Program" as the ONLY "free" slots in its destination
// bank (no blank-named slot at all), so it can only pass if the real
// factory name is actually recognized, not by accidentally also matching
// name.empty() as a fallback.
void testProgramCopyRecognizesRealFactoryEmptyNames() {
    // --- copyProgramFrom(): a real-factory-named slot is a valid copy target,
    // not TargetSlotOccupied -----------------------------------------------
    {
        std::vector<uint8_t> dummyCombiBank;
        pushNameRecord(dummyCombiBank, "Unused", kCrossDsCombiRecordSize);
        // Two DISTINCT source Programs (0, 3) -- copying the SAME source into
        // both target slots would trip DuplicateExists on the second copy
        // (an identical Program would already exist elsewhere by then), which
        // isn't what this test is checking.
        auto fixture = buildCrossDatasetFixture(
            dummyCombiBank, 1, {{0, "Real Program"}, {1, "Init Program"}, {2, "Init EXi Program"}, {3, "Second Program"}}, {},
            std::nullopt);
        kronos::PcgFile pcg;
        std::string error;
        CHECK(pcg.loadFromMemory(fixture, error));

        auto intoInitProgram = pcg.copyProgramFrom(pcg, 0, 0, 0, 1);
        CHECK(!intoInitProgram.has_value());  // nullopt == success
        auto slot1 = pcg.decodeProgram(0, 1);
        CHECK(slot1.has_value());
        if (slot1) CHECK_EQ(slot1->name, std::string("Real Program"), "copied INTO a real-factory \"Init Program\"-named slot");

        auto intoInitExiProgram = pcg.copyProgramFrom(pcg, 0, 3, 0, 2);
        CHECK(!intoInitExiProgram.has_value());  // nullopt == success
        auto slot2 = pcg.decodeProgram(0, 2);
        CHECK(slot2.has_value());
        if (slot2) CHECK_EQ(slot2->name, std::string("Second Program"), "copied INTO a real-factory \"Init EXi Program\"-named slot");
    }

    // --- analyzeCombiCrossDatasetCopy(): a destination bank whose ONLY free
    // slots are real-factory-named (no blank-name slot at all) still counts
    // as a candidate -- the exact scenario reported. -----------------------
    {
        auto srcCombi = makeCrossDsSingleTimbreCombiRecord("Factory Name Test Source", /*number=*/9);
        auto srcFixture = buildCrossDatasetFixture(srcCombi, 1, {{9, "Needs Placement"}}, {}, std::nullopt);

        std::vector<uint8_t> dstCombiBank;
        pushNameRecord(dstCombiBank, "Unused", kCrossDsCombiRecordSize);
        pushNameRecord(dstCombiBank, "Init Combi", kCrossDsCombiRecordSize);
        // Bank 0: number 0 occupied by a real Program, numbers 1-2 are
        // real-factory-named "empty" slots -- deliberately NO blank-name slot
        // anywhere, so a pass here can't be accidental.
        auto dstFixture =
            buildCrossDatasetFixture(dstCombiBank, 2, {{0, "Occupies Slot 0"}, {1, "Init Program"}, {2, "Init Program"}}, {}, std::nullopt);

        kronos::PcgFile src, dst;
        std::string error;
        CHECK(src.loadFromMemory(srcFixture, error));
        CHECK(dst.loadFromMemory(dstFixture, error));

        auto analysis = dst.analyzeCombiCrossDatasetCopy(src, 0, 0, 0, 1);
        CHECK(analysis.ok);
        CHECK_EQ(static_cast<int>(analysis.unresolved.size()), 1, "one unresolved Program");
        if (!analysis.unresolved.empty()) {
            CHECK(!analysis.unresolved[0].candidateBanks.empty());  // bank 0 counts as a candidate via its real-factory-named slots
        }

        kronos::PcgFile::ProgramPlacement placement;
        placement.srcBank = 0;
        placement.srcNumber = 9;
        placement.dstBank = 0;
        // No dstNumber -- auto-pick must land on 1 (the first real-factory
        // "Init Program"-named slot), not refuse with "no free slot".
        auto result = dst.applyCombiCrossDatasetCopy(src, 0, 0, 0, 1, {placement});
        CHECK(result.ok);
        if (!result.ok) std::fprintf(stderr, "  apply() error: %s\n", result.error.c_str());
        auto placed = dst.decodeProgram(0, 1);
        CHECK(placed.has_value());
        if (placed) CHECK_EQ(placed->name, std::string("Needs Placement"), "auto-pick landed in the real-factory-named slot");
    }
}

}  // namespace

int main() {
    testDecodeProgramFields();
    testClassifyProgramBankType();
    testExiAlgorithmTypeRealTemplates();
    testDecodeCombiFields();
    testHashProgramRecord();
    testPcgFileEndToEnd();
    testPcgFileNoSetlists();
    testSaveRoundTrip();
    testDirtyTracking();
    testResolveDuplicates();
    testResolveDuplicatesConsolidateDifferentContent();
    testFindNameCollisions();
    testFindAndResolveDuplicateCombis();
    testResolveDuplicateCombisSelective();
    testResolveDuplicateCombisConsolidateDifferentContent();
    testResetProgram();
    testProgramSwap();
    testCombiRearrange();
    testCombiCrossDatasetCopy();
    testCombiCrossDatasetCopyExactSlot();
    testProgramCopyRecognizesRealFactoryEmptyNames();

    if (g_failures > 0) {
        std::fprintf(stderr, "\n%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All checks passed\n");
    return 0;
}
