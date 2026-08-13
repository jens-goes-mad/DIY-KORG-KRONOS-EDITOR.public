#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kronos {

// Confirmed by diffing purpose-built test files (setlist_test.PCG, and
// later test_1.PCG for Font size/Transpose specifically) where each
// parameter was varied in isolation across known slots -- see
// docs/content/format/index.md's "SBK1" section (§4.3-4.4) for the byte-level
// derivation, including the bit-packing note: Font size and Transpose
// each share a byte with Color/Bank respectively, so reading (or
// writing) any of these four fields must mask to the bits it actually
// owns -- see PcgFile.cpp's readSlotParams() for the exact masks.
//
// 0=S (the true baseline -- zero extra bits set), 1=XS, 2=M, 3=L, 4=XL --
// not alphabetical/size order, that's just what the confirmed bit
// encoding produces (see docs/content/format/index.md §4.4).
enum class FontSize { S, XS, M, L, XL };

struct SlotParams {
    bool isProgram = true;  // true = Program, false = Combi
    int bank = 0;           // bank index (masked to this field's own 5 bits -- see docs/content/format/index.md §4.3)
    int number = 0;         // program/combi number within that bank (0-127)
    int color = 1;          // 1-based color index (1..16 -- masked to this field's own bits, see §4.3)
    int holdTime = 0;       // Hold Time value
    int volume = 127;       // 0-127, MIDI-style
    FontSize fontSize = FontSize::S;
    int transpose = 0;      // semitones, signed (confirmed range -24..+24; encoding supports -32..+31)
    bool found = false;     // false if this slot had no SBK1 record at all (e.g. SBK1 missing/malformed)
};

// One song/program slot within a Set List.
struct Song {
    int index = 0;       // 0-based slot position within its Set List (0..127)
    std::string name;     // may be empty -- an unused slot
    SlotParams params;    // from SBK1; params.found is false if SBK1 wasn't parseable
    std::string comment;  // free-text comment, may contain \r\n line breaks; may be empty
    // The actual Combi's own name, looked up from the CMB1/CBK1 instrument
    // bank by params.bank/params.number -- only populated when
    // params.isProgram is false. Program-side lookup (PRG1/PBK1/MBK1) is
    // not implemented yet -- its bank layout is more complex, see
    // README.md. Empty if not applicable/not found.
    std::string instrumentName;
};

// One of the Kronos's 128 Set Lists.
struct Setlist {
    int index = 0;                 // 0-based Set List number (0..127)
    std::string name;              // e.g. "Preload Set List", or "Set List 005" if never renamed
    std::vector<Song> songs;       // always 128 entries (a real Kronos Set List has exactly 128 slots)
};

// A Program bank's underlying storage/engine family. NOT fixed per bank
// index -- Kronos OS 3.0+ lets a user reassign INT Program Banks between
// HD-1 and EXi, so this is read per-file from data already parsed at load
// time (the bank's own chunk tag, cross-checked against its declared
// per-record byte stride), never a hardcoded per-bank-index table -- see
// src/kronos/ProgramDecoder.h's classifyProgramBankType() and
// docs/external/README.md for the sources this came from. Practically:
// a Program can only be loaded into a bank of the matching type, and a
// Combi's Timbre references only mean anything if the physical bank/number
// they point at actually holds a Program (of the right type) -- relevant
// to any future cross-dataset "move/merge patches" feature, not to
// anything built yet.
//
// NOT YET independently verified against a real Kronos backup by this
// project's own "no guessing" standard -- see docs/external/README.md's
// caveat before trusting this for anything beyond its own unit test.
//
// One thing IS confirmed directly (real hardware behavior, 2026-08-07,
// see docs/content/format/index.md §5.2): engine assignment is a global, per-BANK
// setting on the unit -- Programs within one bank can never mix engines,
// and a bank's storage is all-or-nothing (always exactly 128 slots, never
// partially saved). That's exactly why this type is tracked once per bank
// here, never per record -- confirmed behavior, even though the specific
// MBK1=EXi/PBK1=HD-1 tag mapping itself remains externally sourced only.
enum class ProgramBankType { Hd1, Exi };

// One Program's table row: `bank`/`name`/`number` are raw Kronos fields,
// read directly off PRG1's MBK1/PBK1 banks (see docs/content/format/index.md §5.2) by
// src/kronos/ProgramDecoder.h. `contentHash` is deliberately NOT a Kronos
// format field -- it's this project's own application-level bookkeeping
// (an FNV-1a hash of the record's raw bytes, for byte-exact duplicate
// detection), computed once and cached here rather than recomputed on
// every use. See docs/content/components/index.md for why this
// raw-field/derived-data split is kept explicit rather than blurred.
// `bankType` is likewise derived bookkeeping, not a per-record Kronos field
// -- see ProgramBankType's own doc comment above.
struct ProgramInfo {
    int bank = 0;
    int number = 0;
    std::string name;
    uint64_t contentHash = 0;
    ProgramBankType bankType = ProgramBankType::Hd1;
};

// A Timbre's on/off + source-engine status, read from the byte immediately
// after its [number][bank] pair (byte offset +2 within the Timbre block):
// the top 3 bits ((byte >> 5) & 0x07) give this status, confirmed against
// an independent external reference (DaBlick/PCG-Tools' "PCG Structure
// Kronos.txt", see docs/content/format/index.md) and cross-checked against this project's
// own real Combi samples. `Off` is what every genuinely-unassigned Timbre
// slot shows; the lower 5 bits of the same byte are NOT part of this --
// they hold the Timbre's own 0-based index, a redundant field unrelated to
// on/off state (confirmed by watching it count 0..15 across a real Combi's
// 16 Timbres regardless of status).
enum class TimbreStatus { Off, Internal, External, Ex2, Unknown };

// One Combi Timbre's Program reference, read directly from the Combi's raw
// record bytes at a fixed stride (see docs/content/format/index.md's "Combi Timbre
// references" section for how this was derived from real Combi samples the
// project owner provided directly, and independently cross-checked against
// DaBlick/PCG-Tools' reference doc). Encoding: byte 0 = Program number, byte
// 1 = a raw bank code -- confirmed NOT to be the same index space
// ProgramInfo::bank/SlotParams::bank use (those are PBK1 file order; this
// is some other, absolute Kronos-internal numbering). Only a handful of
// codes are confirmed to a named bank so far (see kronos::timbreBankName);
// every other code is real but not yet identified.
struct TimbreRef {
    int number = 0;
    int rawBankCode = 0;
    TimbreStatus status = TimbreStatus::Off;
    // true when number==0 && rawBankCode==0 -- this Timbre slot has no real
    // Program reference stored at all (as opposed to having one that's
    // just currently switched off -- see `status`). Deliberately NOT tied
    // to status: a Timbre can hold a genuine, non-zero bank/number while
    // status is Off (e.g. temporarily disabled without clearing its
    // assignment), and that should still count as "this Combi references
    // that Program" for anything safety-related (e.g. deciding whether a
    // Program is safe to delete) -- only isDefault means "nothing here."
    bool isDefault = true;
};

// Returns the confirmed bank name for a raw Combi Timbre bank code (e.g.
// "USER-D"), or an empty string if this code hasn't been identified yet.
// NOT the same lookup as the Program/Combi bank arrays used elsewhere --
// see TimbreRef's comment.
std::string timbreBankName(int rawBankCode);

// Whether `programBank` (this project's PBK1 file-order Program bank
// index, see ProgramInfo::bank) has an independently-confirmed Combi
// Timbre raw bank code (TimbreRef::rawBankCode) -- true for the 8 banks in
// PcgFile.cpp's kConfirmedTimbreBanks table (INT-A..D, USER-A/D/F/AA, see
// docs/content/format/index.md §6.2). The two numbering schemes coincide for INT-A..D
// (both use 0..3) but diverge for the other 4 (e.g. USER-D is file-order
// index 11 but Timbre code 20) -- combiUsagesForProgram()/
// combiUsageCounts() translate between the two via that same table rather
// than assuming they're numerically equal, so Combi usage counting is
// correct for all 8, not just the range where the numbers happen to
// match. Every other Program bank has no confirmed Timbre code at all yet
// -- counting Combi usage for those would be a guess, so it's not
// attempted.
bool isConfirmedTimbreProgramBank(int programBank);

// One Combi, from CMB1's CBK1 banks (see docs/content/format/index.md §5.1). No
// contentHash -- duplicate detection was only requested for Programs.
struct CombiInfo {
    int bank = 0;
    int number = 0;
    std::string name;
    std::vector<TimbreRef> timbres;  // always 16 entries, Timbre 1..16 in order
};

// One Set List slot that directly references a given Program (as opposed
// to referencing it indirectly through a Combi -- Combi-internal
// references aren't parsed yet, see docs/content/format/index.md's Phase 2 roadmap).
struct SetlistUsage {
    int setlistIndex = 0;
    std::string setlistName;
    int songIndex = 0;
};

// One Combi whose Timbres reference a given Program. `active` is true if
// *any* matching Timbre's status isn't Off -- a Combi can reference a
// Program only through an Off Timbre (e.g. a stale/disabled assignment),
// which still counts as a reference (see TimbreRef::isDefault's comment)
// but is worth distinguishing in the UI.
struct CombiUsage {
    int bank = 0;
    int number = 0;
    std::string name;
    bool active = false;
};

// Parses a Korg Kronos .PCG/.SNG backup file and extracts all 128 Set Lists
// from its SDB1 ("Set List database") chunk. Loads the whole file into
// memory -- fine for desktop use at the ~50-70MB sizes these files run.
//
// Chunk format and the SDB1 record layout are documented in README.md.
class PcgFile {
public:
    // Returns false and fills `error` on failure (bad magic, missing chunk,
    // truncated/malformed data). Does not throw.
    bool load(const std::string& path, std::string& error);

    // Same as load(), but from bytes already in memory (e.g. a file dropped
    // onto the UI and read via the browser's File API, which has no
    // filesystem path to give us -- see README.md's "Open File" section).
    bool loadFromMemory(std::vector<uint8_t> data, std::string& error);

    // Writes the retained raw bytes (data_) straight to `path`, verbatim --
    // no re-serialization, no re-deriving anything. This is deliberately
    // simple: every edit this app makes (copyProgramFrom(), putSongRecordBytes())
    // already writes directly into data_ the moment it happens (see
    // STATE.md's "ARCHITECTURE" section), so by the time save() is called
    // data_ already IS the fully up-to-date file -- there is no separate
    // in-memory model to serialize. Returns false and fills `error` if the
    // path can't be opened for writing, or if no file is loaded (data_
    // empty). Does not throw.
    bool save(const std::string& path, std::string& error) const;

    const std::vector<Setlist>& setlists() const { return setlists_; }
    std::vector<Setlist>& setlists() { return setlists_; }

    const std::vector<ProgramInfo>& programs() const { return programs_; }
    const std::vector<CombiInfo>& combis() const { return combis_; }

    // One entry per Program bank actually present in this file, giving its
    // classified engine type without needing that bank's ~128 individual
    // Program records (programs(), which every ProgramInfo already carries
    // bankType on) -- for UI that wants to label a *bank*, not a specific
    // Program row (bank-filter buttons, a Set List slot's Bank-jump button,
    // neither of which has a specific Program row in hand). See
    // ProgramBankType's own doc comment: derived per-file, never a
    // hardcoded table.
    struct ProgramBankTypeEntry {
        int bank = 0;
        ProgramBankType bankType = ProgramBankType::Hd1;
    };
    std::vector<ProgramBankTypeEntry> programBankTypes() const;

    // Single-bank lookup version of programBankTypes(), for a caller that
    // already knows the one bank it cares about. nullopt if `bank` is out of
    // range for this file.
    std::optional<ProgramBankType> programBankTypeAt(int bank) const;

    // Every top-level chunk tag actually found directly under PCG1 (DIV1,
    // SLS1, PRG1, CMB1, DKT1, WSQ1, GLB1, DPI1 are the ones this format is
    // known to use -- see docs/content/format/index.md), in file order, duplicates
    // included if a tag genuinely appears more than once. For "Internals"-
    // style diagnostics: a real backup can apparently be saved with only a
    // subset of this data included (the project owner's own observation),
    // so this is how a caller can tell "was there ever a DKT1/WSQ1/GLB1
    // chunk in this file at all" -- something the rest of this class never
    // needed to ask before now, since it only ever looked for chunks it
    // already knew how to parse.
    std::vector<std::string> topLevelChunkTags() const;

    // One entry per Program/Combi bank actually present in this file --
    // richer than programBankTypes()/ProgramBankTypeEntry (adds record
    // count/stride), for an "Internals" view that wants to show what's
    // actually in a bank, not just its engine type.
    //
    // IMPORTANT, NOT YET RESOLVED: `index` is this bank's POSITION among
    // however many PRG1 sub-bank chunks were found, in file order -- NOT a
    // confirmed-stable bank identity. This project has always assumed a
    // real file contains all 20 Program banks / 14 Combi banks in one
    // fixed canonical order (see PROGRAM_BANK_NAMES/COMBI_BANK_NAMES in
    // frontend/pane.js), so position-equals-identity has been silently
    // correct so far -- but if a real backup can genuinely omit banks (see
    // topLevelChunkTags()'s own comment), that assumption breaks: a file
    // missing, say, canonical bank 4 would have this method return only
    // 19 entries, and its index-5-onward entries would actually BE
    // canonical banks 5-19's data, silently mislabeled as 4-18 by every
    // caller that assumes index-equals-canonical-position (ProgramInfo::
    // bank, the whole Programs/Combis table, Timbre cross-referencing,
    // copyProgramFrom()'s destination checks -- all of it). Each PRG1/CBK1
    // sub-bank chunk's own first 4 bytes (currently read and discarded,
    // "meaning not understood yet" -- see PcgFile.cpp) are a plausible
    // candidate for a real per-chunk bank-identity field that would let
    // this be fixed properly; not yet investigated with real test data
    // that's actually missing a known bank. See STATE.md.
    struct ProgramBankInfo {
        int index = 0;
        ProgramBankType bankType = ProgramBankType::Hd1;
        int numRecords = 0;
        int bytesPerRecord = 0;
    };
    std::vector<ProgramBankInfo> programBankInfo() const;

    // Same caveat as ProgramBankInfo -- see its own doc comment.
    struct CombiBankInfo {
        int index = 0;
        int numRecords = 0;
        int bytesPerRecord = 0;
    };
    std::vector<CombiBankInfo> combiBankInfo() const;

    // Why a copy can be rejected -- see copyProgramFrom()'s own doc comment.
    // Kept as a plain enum (not an exception) so EditorBridge can map each
    // reason to a specific user-facing message without a try/catch.
    enum class ProgramCopyError { BankTypeMismatch, RecordSizeMismatch, OutOfRange, TargetSlotOccupied, DuplicateExists };

    // Copies one Program record's raw bytes from `src` (pass *this for a
    // same-dataset copy -- src and dst never alias, since distinct
    // (bank, number) pairs always map to distinct byte ranges) at
    // (srcBank, srcNumber) into THIS file's (dstBank, dstNumber) slot, then
    // re-decodes this file's programs() entry for that slot from the
    // freshly-written bytes -- a cached field (name/contentHash/bankType)
    // must never go stale after a direct data_ write. This is this
    // project's first operation that writes directly into the retained raw
    // buffer rather than only mutating in-memory bookkeeping (setlists()'s
    // Songs) -- see STATE.md.
    //
    // Returns nullopt on success. Rejects (writes nothing) on:
    //  - OutOfRange: either bank/number pair doesn't exist in its file.
    //  - BankTypeMismatch: source and destination banks are different
    //    engine types (HD-1 vs EXi) -- see ProgramBankType's own doc
    //    comment for why a Program can only be loaded into a matching bank.
    //  - RecordSizeMismatch: defensive second check on top of the above --
    //    should already be implied by matching bank type. CORRECTED
    //    2026-08-13: this comment used to claim HD-1=4960 bytes, EXi=3706 --
    //    that EXi figure was never actually confirmed against real bytes.
    //    Checked directly against two independent real backup files
    //    (programBankInfo() over both): every one of the 20 PRG1 sub-banks,
    //    HD-1 or EXi alike, uses 4960-byte records. Kept as a belt-and-
    //    suspenders check regardless, since ProgramBankTypeResult::
    //    tagMatchesStride can still be false for real data this project
    //    hasn't independently verified yet, and a third file could yet
    //    show a real EXi/HD-1 stride difference this hasn't hit.
    //  - TargetSlotOccupied: the destination's *current* name is non-empty
    //    (a different Program already lives there -- re-dropping the exact
    //    same Program already at that slot is caught by DuplicateExists
    //    instead, not this).
    //  - DuplicateExists: a byte-identical Program (matching contentHash)
    //    already exists anywhere in this file.
    std::optional<ProgramCopyError> copyProgramFrom(const PcgFile& src, int srcBank, int srcNumber,
                                                      int dstBank, int dstNumber);

    // Every Program-type Set List slot that directly references this
    // bank/number. Does NOT include usage from inside a Combi's Timbres --
    // that part of the format isn't parsed yet (see docs/content/format/index.md).
    //
    // Caveat: bank 0 / number 0 is also the all-zero byte value, so it
    // over-counts -- a slot that was never actually assigned a Program
    // still reads as "bank 0, number 0" (confirmed: this returns 16000+
    // "usages" for 0/0 on a real backup, vs. a handful for any other
    // bank/number). There's no known flag distinguishing "really assigned
    // to bank 0/number 0" from "never touched" -- treat 0/0 usage counts
    // with that in mind; every other bank/number has been spot-checked as
    // accurate.
    std::vector<SetlistUsage> programSetlistUsages(int bank, int number) const;

    // Every Combi-type Set List slot that directly references this
    // bank/number. Same bank-0/number-0 caveat as programSetlistUsages()
    // applies here too.
    std::vector<SetlistUsage> combiSetlistUsages(int bank, int number) const;

    // Set-List-slot reference counts for every (bank, number) at once,
    // indexed `[bank][number]` -- built in one pass over all Set Lists
    // rather than calling programSetlistUsages()/combiSetlistUsages() once
    // per Program/Combi (which would be O(programs x songs) instead of
    // O(songs)). Used to attach a reference count to every row of a
    // Programs/Combis listing without it being slow at ~2500/~1800 rows.
    std::vector<std::vector<int>> setlistUsageCounts(bool isProgram) const;

    // Every Combi whose Timbres reference this bank/number, regardless of
    // on/off status (see CombiUsage::active). Only meaningful when
    // isConfirmedTimbreProgramBank(bank) is true -- returns an empty list
    // otherwise, same as if there were genuinely no usages, since this
    // project can't yet tell the difference for unconfirmed banks.
    std::vector<CombiUsage> combiUsagesForProgram(int bank, int number) const;

    // Combi-usage counts for every (bank, number) at once, indexed
    // `[bank][number]` -- same one-pass-instead-of-per-row idea as
    // setlistUsageCounts(). Only populated for banks where
    // isConfirmedTimbreProgramBank() is true; every other bank has no
    // entry at all (callers must check isConfirmedTimbreProgramBank()
    // themselves to tell "zero real usages" apart from "not computed").
    std::vector<std::vector<int>> combiUsageCounts() const;

    // Groups of 2+ Programs sharing an identical contentHash (byte-exact
    // duplicates). Programs with a unique hash are omitted entirely.
    std::vector<std::vector<ProgramInfo>> findDuplicatePrograms() const;

    // Re-decodes one Program directly from the retained raw file bytes,
    // independently of programs() (which was built once during load) --
    // proof that the decoder is a real, reusable, on-demand operation
    // rather than something only ever run once. Returns nullopt if
    // bank/number is out of range, or no file is loaded.
    //
    // This is the first piece of the architecture direction described in
    // docs/content/components/index.md and STATE.md's "ARCHITECTURE:
    // DECODER/ENCODER REFACTOR" section: raw bytes are retained as the
    // one canonical copy (see data_ below) instead of being discarded
    // after an eager parse, and small per-record decoders
    // (src/kronos/ProgramDecoder.h so far) compute structure from them on
    // demand. Combi and Set List slot decoders are the planned next steps
    // once this is proven out -- programs_/combis_/setlists_ below still
    // reflect the older eager-parse shape for everything else.
    std::optional<ProgramInfo> decodeProgram(int bank, int number) const;

    // Same as decodeProgram(), for Combis -- see src/kronos/CombiDecoder.h,
    // the second per-record decoder built this way.
    std::optional<CombiInfo> decodeCombi(int bank, int number) const;

    // One Program's raw PBK1/MBK1 record, straight from the retained file
    // bytes -- same offset math as copyProgramFrom()'s source-side lookup,
    // exposed as its own read so a caller doesn't need a whole second
    // PcgFile just to pull one record's bytes out. First use: extracting a
    // real "Init Program"/"Init EXi Program" slot's bytes as this app's own
    // known-good template for clearing a Program slot, rather than
    // guessing what an empty/init record's bytes should be (see
    // resources/Init-Program-HD1.raw / Init-Program-EXi.raw, and STATE.md).
    //
    // RESOLVED 2026-08-13: while extracting those two templates, bytes
    // 2632-2633 turned out to differ consistently across banks between
    // "Init Program" copies (e.g. bank 12 vs bank 17, both HD-1) --
    // suspected at first as a per-bank identity tag baked into the record.
    // Checked against Korg's own official parameter reference
    // (docs/external/KORG/Prog_HD-1.txt and Prog_EXi_Common.txt, identical
    // entry in both): it's "Tone Adjust" / "Switch8 On Value", a real
    // Program parameter, not anything bank- or identity-related. Still an
    // open practical question for a future cross-bank template write (the
    // Duplicates-resolution feature this was built for -- see STATE.md): a
    // factory Init Program's Tone Adjust value isn't identical across
    // every bank, so writing one bank's template into a different bank
    // would carry over whichever value that SOURCE bank's Init Program
    // happened to have -- not yet checked whether that reads as harmless
    // (an inactive/default switch state regardless of its exact bits) on
    // real hardware.
    // Returns nullopt if the (bank, number) pair is out of range.
    std::optional<std::vector<uint8_t>> programRecordBytes(int bank, int number) const;

    // Writes `bytes` straight into this file's retained data_ for one
    // Program slot, then re-decodes programs()'s entry for that slot from
    // the freshly-written bytes (refreshProgramInfo(), shared with
    // copyProgramFrom()) -- same discipline as putSongRecordBytes(): a
    // cached decoded field must never go stale after a direct data_ write.
    // Returns false (writes nothing) if the (bank, number) pair is out of
    // range, or `bytes.size()` doesn't match that bank's own record size.
    bool putProgramRecordBytes(int bank, int number, const std::vector<uint8_t>& bytes);

    // Same idea as programRecordBytes()/putProgramRecordBytes(), for one
    // Combi's raw CBK1 record -- CombiDecoder.h's own doc comment used to
    // note "no encoder yet, every current use of Combi data is read-only";
    // this is the first one, needed to repoint a Combi Timbre's Program
    // reference (see resolveDuplicates() below) without guessing at the
    // Timbre byte layout a second time -- see CombiDecoder.h's
    // writeTimbreProgramRef(), the actual byte-level writer these two
    // read/write raw bytes for. Returns nullopt/false (write) if the
    // (bank, number) pair is out of range, or (write only) `bytes.size()`
    // doesn't match that bank's own record size.
    std::optional<std::vector<uint8_t>> combiRecordBytes(int bank, int number) const;
    bool putCombiRecordBytes(int bank, int number, const std::vector<uint8_t>& bytes);

    // Why a duplicate-resolution attempt can be rejected -- see
    // resolveDuplicates()'s own doc comment. Kept as a plain result struct
    // (not an exception/optional-with-outparam) so EditorBridge can surface
    // both the error and, on success, the three counts in one return.
    struct ResolveDuplicatesResult {
        bool ok = false;
        std::string error;               // only set when !ok
        int clearedPrograms = 0;         // OTHER duplicate slots overwritten with an Init Program template
        int setlistRefsRepointed = 0;    // Set List Program slots repointed to (keepBank, keepNumber)
        int combiRefsRepointed = 0;      // Combi Timbre references repointed
        int combiRefsSkipped = 0;        // Combi Timbre references left alone -- see own doc comment below
    };

    // Makes (keepBank, keepNumber) "the" copy of its byte-exact duplicate
    // group (see findDuplicatePrograms()): every OTHER Program in this file
    // sharing its contentHash gets its raw record overwritten with
    // `hd1InitBytes`/`exiInitBytes` (whichever matches THAT duplicate's own
    // bank type, not the kept slot's -- see resources/Init-Program-HD1.raw/
    // Init-Program-EXi.raw and programRecordBytes()'s own doc comment for
    // where these come from and the still-open Tone Adjust caveat), and
    // every Set List slot / Combi Timbre that referenced any of those
    // now-cleared duplicates gets repointed to (keepBank, keepNumber)
    // instead.
    //
    // All-or-nothing: every duplicate's own bank's record size is checked
    // against the matching template BEFORE any write happens -- a size
    // mismatch fails the whole call (ok=false, nothing written), never a
    // partial clear. This app has no undo/rollback machinery anywhere else
    // either, so "don't start a write that might not finish" is the only
    // safety available.
    //
    // Combi Timbre repointing needs to translate a PBK1 file-order bank
    // index to its raw Timbre bank code (see kConfirmedTimbreBanks in
    // PcgFile.cpp) -- if a duplicate's own bank, or keepBank itself, has no
    // confirmed code, those specific Combi Timbre references are left
    // untouched (counted in combiRefsSkipped) rather than writing a
    // guessed code. The Program clear and Set List repointing for that same
    // duplicate still happen regardless -- only the Combi side depends on
    // the translation being confirmed.
    //
    // Returns ok=false with `error` set if (keepBank, keepNumber) doesn't
    // exist in this file, or on the record-size mismatch above.
    ResolveDuplicatesResult resolveDuplicates(int keepBank, int keepNumber,
                                               const std::vector<uint8_t>& hd1InitBytes,
                                               const std::vector<uint8_t>& exiInitBytes);

    // Raw 542-byte SBK1 record for one Set List slot, straight from the
    // retained file bytes -- the same two-tier data-flow idea as
    // decodeProgram()/decodeCombi(), applied to Set List slots: a detail
    // editor (Color/Volume/Comment row) requests exactly this chunk,
    // decodes/encodes it entirely in JS via frontend/components/kronos/
    // setlist-comment.js and setlist-slot-params.js, and writes it back via
    // putSongRecordBytes() -- see STATE.md. Returns nullopt if the indices
    // are out of range or this file's SBK1 data wasn't parseable at load.
    std::optional<std::vector<uint8_t>> songRecordBytes(int setlistIndex, int songIndex) const;

    // Writes `bytes` (must be exactly 542 bytes) straight into this file's
    // retained data_ for the given slot, then re-derives setlists()[setlistIndex]
    // .songs[songIndex]'s params/comment/instrumentName from the freshly-
    // written bytes via the same readSlotParams()/readComment() the initial
    // load used -- a cached decoded field must never go stale after a
    // direct data_ write, same discipline as copyProgramFrom(). instrumentName
    // re-resolution was ADDED 2026-08-13 (RESOLVED a real bug): this doc
    // comment used to say "does NOT re-resolve instrumentName -- out of
    // scope, since every editor using this path so far (Color/Volume/
    // Comment) never touches bank/number" -- true until
    // PcgFile::resolveDuplicates() became the first caller that DOES
    // repoint bank/number through this same method, at which point the
    // gap was a real, reproducible stale-name bug, not a hypothetical one.
    // Returns false (writes nothing) if `bytes` isn't exactly 542 bytes, or
    // the indices are out of range.
    bool putSongRecordBytes(int setlistIndex, int songIndex, const std::vector<uint8_t>& bytes);

    // A Set List slot's raw 28-byte NAME record (4-byte marker + 24-byte
    // ASCII name) from SDB1 -- a completely separate chunk, at a different
    // byte offset and stride, from this same slot's SBK1 params
    // (songRecordBytes() above). A slot's displayed name and its
    // bank/number/comment/etc. are NOT stored together, so any operation
    // that relocates or duplicates a slot (see reorderSong() below, and
    // the drag-and-drop "copy over" gesture) must move/copy both records
    // together or the result mismatches a slot's name against its actual
    // content. Returns nullopt if the indices are out of range or this
    // file's SDB1 data wasn't parseable at load.
    std::optional<std::vector<uint8_t>> nameRecordBytes(int setlistIndex, int songIndex) const;

    // Writes `bytes` (must be exactly 28 bytes) straight into data_ for the
    // given slot's SDB1 name record, then re-derives
    // setlists()[setlistIndex].songs[songIndex].name from it -- same
    // "never leave a cached field stale after a direct data_ write"
    // discipline as putSongRecordBytes(). Returns false (writes nothing)
    // if `bytes` isn't exactly 28 bytes, or the indices are out of range.
    bool putNameRecordBytes(int setlistIndex, int songIndex, const std::vector<uint8_t>& bytes);

    // Relocates the song at `fromIndex` to `toIndex` within one Set List --
    // both its SDB1 name record and its SBK1 params record -- shifting the
    // intervening range by one position to fill the gap left behind (e.g.
    // fromIndex=10,toIndex=3 shifts slots [3..9] to [4..10], then places
    // slot 10's original content at 3). A pure rearrangement of the same
    // 128 slots -- nothing added or removed -- done as a single call so a
    // caller never has to issue one bridge round-trip per shifted slot.
    // Returns false (writes nothing) if either index is out of range or
    // this Set List has no SBK1/SDB1 data; a no-op (returns true, writes
    // nothing) if fromIndex == toIndex.
    bool reorderSong(int setlistIndex, int fromIndex, int toIndex);

    // Overwrites every song slot (0..127, both its SDB1 name record and its
    // SBK1 params record) in `dstSetlistIndex` with the corresponding slot's
    // content from `srcSetlistIndex` -- "copy all to opposite" (frontend/
    // pane.js), for copying a whole prepared Set List into a gig's slot and
    // then tweaking a few entries in place, rather than rebuilding it from
    // scratch. Same file only (both indices are within this one PcgFile) --
    // the two-panes-must-share-one-dataset requirement is enforced by the
    // caller (EditorBridge::copySetlistEntries()), not here. Does NOT touch
    // either Set List's own name (Setlist::name, e.g. "Preload Set List") --
    // only the 128 song slots. One native call rather than 128 bridge round-
    // trips, same reasoning as reorderSong() above. Returns false (writes
    // nothing) if either index is out of range; a no-op (returns true) if
    // srcSetlistIndex == dstSetlistIndex.
    bool copySetlist(int srcSetlistIndex, int dstSetlistIndex);

    // Physically reorders every one of a Set List's 128 slots (name AND
    // params together) into alphabetical-by-name order -- `ascending` true
    // for A-Z, false for Z-A. Empty slots (no name) always sort to the end
    // regardless of direction, matching frontend/pane.js's own sort
    // convention -- there's no real name to compare, and a byte-order
    // comparison would otherwise put every unused slot before any named
    // song. This is a REAL, immediate, whole-Set-List rewrite -- there is
    // no separate "display order" a Kronos can show independent of a
    // slot's actual record position (confirmed against
    // docs/external/KORG/SetList.txt, see docs/content/format/index.md §3.2), so
    // "sorted" only ever means "physically rearranged." Every slot's raw
    // bytes are snapshotted up front before any write, so reading a slot
    // that's about to be overwritten never races its own move (unlike
    // reorderSong()'s shift, this touches every slot at once, not a
    // contiguous range, so there's no safe "direction" to iterate in
    // without snapshotting first). Comparison is a plain byte-wise
    // std::string comparison, not locale-aware -- ASCII-range Kronos names
    // sort the same way either way in practice, but see std::string::
    // operator< if two names ever meaningfully disagree on this. Returns
    // false (writes nothing) if setlistIndex is out of range or this Set
    // List has no SBK1/SDB1 data.
    bool sortSetlist(int setlistIndex, bool ascending);

private:
    // Re-decodes programs_'s/combis_'s entry for one slot from data_ as it
    // stands right now -- the shared tail end of copyProgramFrom() and
    // putProgramRecordBytes() (Program side) / putCombiRecordBytes() (Combi
    // side), factored out so a cached decoded field being re-derived after
    // a raw write is written in exactly one place, not copy-pasted per
    // caller. Assumes the (bank, number) pair is already known in range --
    // callers check bounds themselves before writing, same as everywhere
    // else in this file.
    void refreshProgramInfo(int bank, int number);
    void refreshCombiInfo(int bank, int number);

    // The Set List instrument-name cross-reference (§5 in docs/content/
    // format/index.md), resolved on demand from programs_/combis_ rather
    // than the load-time-only lookup table loadFromMemory() builds and
    // discards -- used by putSongRecordBytes() to keep Song::instrumentName
    // from going stale after a bank/number change. A linear scan, not a
    // table lookup: only ever called for one slot at a time (an occasional
    // single write, never the whole-file bulk load), so this is never
    // hot. Returns "" if no Program/Combi exists at (bank, number), same
    // as the load-time lookup's own out-of-range behavior.
    std::string resolveInstrumentName(bool isProgram, int bank, int number) const;

    // Where one PRG1 sub-bank's (MBK1 or PBK1) records live within data_
    // -- retained so decodeProgram() can locate and re-decode a specific
    // record on demand, without re-scanning the whole file's chunk
    // hierarchy every time.
    struct ProgramBankLocation {
        size_t recordsStart = 0;
        uint32_t numRecords = 0;
        uint32_t bytesPerRecord = 0;
        ProgramBankType bankType = ProgramBankType::Hd1;  // classified once at load, see ProgramBankType's doc comment
    };

    // Same as ProgramBankLocation, for one CBK1 sub-bank -- retained so
    // decodeCombi() can locate and re-decode a specific record on demand.
    struct CombiBankLocation {
        size_t recordsStart = 0;
        uint32_t numRecords = 0;
        uint32_t bytesPerRecord = 0;
    };

    std::vector<Setlist> setlists_;
    std::vector<ProgramInfo> programs_;
    std::vector<CombiInfo> combis_;
    std::vector<uint8_t> data_;                          // the whole file's raw bytes, retained after load
    std::vector<ProgramBankLocation> programBankLocations_;  // index into data_, one entry per PRG1 sub-bank
    std::vector<CombiBankLocation> combiBankLocations_;      // index into data_, one entry per CBK1 sub-bank

    // data_ offset of setlists_[i]'s first song record (i.e. right after
    // that Set List's own 40-byte SBK1 header) -- one entry per setlists_
    // index, the exact same value loadFromMemory() already computes as
    // `songsStart` while populating songs[k].params/comment, kept around so
    // songRecordBytes()/putSongRecordBytes() can locate a slot's raw bytes
    // without re-scanning the file's chunk hierarchy. SIZE_MAX means "no
    // SBK1 data for this Set List" (SBK1 missing/malformed, or a
    // non-matching chunk among multiple SBK1 chunks -- see loadFromMemory()).
    std::vector<size_t> sbkSongsStart_;

    // data_ offset of setlists_[i]'s first song NAME record within SDB1
    // (i.e. right after that Set List's own name record, record 0) -- the
    // same idea as sbkSongsStart_ above, but for the separate SDB1 chunk;
    // see nameRecordBytes()/putNameRecordBytes()'s own doc comments.
    // SIZE_MAX means "no SDB1 data for this Set List".
    std::vector<size_t> sdbSongsStart_;
};

}  // namespace kronos
