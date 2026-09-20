#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "PcgFile.h"

namespace kronos {

// Second of the small, focused, independently testable per-record decoders
// -- see docs/content/components/index.md and STATE.md's "ARCHITECTURE:
// DECODER/ENCODER REFACTOR" for the rationale. Same shape as
// ProgramDecoder.h, extended to also decode each Combi's 16
// Timbre-to-Program references (Programs have no equivalent).
// hashCombiRecord() (byte-exact content hashing, originally requested for
// Programs only -- see CombiInfo's own doc comment in PcgFile.h for why
// Combi got it too later) is the one encoder-adjacent thing here; every
// other current use of Combi data is still read-only.

// Raw Kronos fields for one Combi record -- read directly off the bytes.
// bank/number are the record's own position among its siblings, not
// something stored in the record's bytes (same convention as
// ProgramFields) -- passed in by the caller, not decoded here.
struct CombiFields {
    int bank = 0;
    int number = 0;
    std::string name;
    std::vector<TimbreRef> timbres;  // always 16 entries, Timbre 1..16 in order
};

// `record` must point to exactly `recordSize` bytes -- one CBK1 record
// slice (see docs/content/format/index.md's "Combi Timbre references" section). Never
// throws or fails: a malformed/truncated slice yields an empty name and/or
// default (isDefault=true) TimbreRefs for whatever doesn't fit, matching
// this project's usual "degrade gracefully" convention for optional data.
CombiFields decodeCombiFields(const uint8_t* record, size_t recordSize, int bank, int number);

// Standard FNV-1a 64-bit over the raw record's own bytes -- identical
// algorithm to ProgramDecoder.h's hashProgramRecord(), kept as a separate
// function (not a shared helper) rather than calling that one on Combi
// bytes, matching this project's own convention of small duplication over
// a premature shared abstraction between the two otherwise-independent
// decoders. Used for content-based duplicate/collision detection the same
// way ProgramInfo::contentHash already is.
uint64_t hashCombiRecord(const uint8_t* record, size_t recordSize);

// Byte offset within a Combi record where Timbre `timbreIndex`'s (0-15) own
// 3-byte block starts (number, rawBankCode, status) -- see
// decodeCombiFields() above for the confirmed base offset/stride this
// computes from (docs/content/format/index.md's "Combi Timbre references"
// section). Exposed so writeTimbreProgramRef() below -- and any other
// future caller writing a Timbre reference -- doesn't need this project's
// only copy of that derivation duplicated a second time. Out-of-range
// timbreIndex still returns a computed (meaningless) value; callers
// bounds-check against the record's own size themselves, same convention
// as decodeCombiFields().
size_t timbreByteOffset(int timbreIndex);

// Patches ONE Timbre's number/rawBankCode bytes directly into `record`
// (which must be exactly `recordSize` bytes, the same raw slice
// decodeCombiFields() reads) -- the status byte (offset+2, see
// TimbreStatus in PcgFile.h) is left untouched, since repointing a
// reference to a different Program shouldn't silently flip a Timbre's
// Off/On state. A `timbreIndex` outside 0-15, or a record too short to
// hold this Timbre's own 3 bytes, is a no-op. First (and so far only) use:
// PcgFile::resolveDuplicates()'s Combi Timbre repointing.
void writeTimbreProgramRef(uint8_t* record, size_t recordSize, int timbreIndex, int number, int rawBankCode);

// "Total Effect > Master Volume" (docs/external/KORG/
// CombiAndSongTimbreSet.txt, SysEx offset 1188, `00~7F` -> `0~127`) --
// CONFIRMED via this project's own "+4 byte shift from Korg's own SysEx
// offset to this project's real CBK1 file-record offset" rule, independently
// re-derived TWICE already elsewhere in this project rather than assumed
// fresh here: kTimbreBaseOffset above (4806) is exactly Korg's own
// "Timbre1 > Program Number" SysEx offset (4802) + 4, and TimbreStatus's
// own offset+2 (see decodeTimbreStatus()) is exactly Korg's own
// "Timbre1 > Status" SysEx offset (4804) + 4 -- two independent hits on the
// same +4 shift, applied here a third time: file offset 1188 + 4 = 1192.
// Returns -1 if `record` is too short to contain this byte.
int decodeCombiMasterVolume(const uint8_t* record, size_t recordSize);

// One Timbre's own "Volume" (docs/external/KORG/CombiAndSongTimbreSet.txt:
// "Timbre1 > Volume", SysEx offset 4807, `00~7F` -> `0~127`) -- relative
// offset 5 from that Timbre's own start (4807-4802), added to
// timbreByteOffset(timbreIndex) above (which already applies the confirmed
// +4 shift). CONFIRMED directly against two real, independently-obtained
// Kronos backups (`K1_20260418.PCG`/`K2_20260401.PCG`, added 2026-09-20
// specifically to test this feature): every single one of that day's
// "Other section differs" Combi divergences -- 20 of them, across entirely
// different Combi names/banks -- turned out to be this EXACT byte
// (Timbre1, offset 4811) going from 111 to 103, confirmed to be a real,
// normally-distributed field (a histogram scan across all 1792 Combi
// records in both files shows a real spread of values with 127/max the
// common default, not a suspicious constant), not a coincidence or a
// mis-attributed bit. `timbreIndex` is 0-15. Returns -1 if `record` is too
// short to contain this Timbre's own Volume byte.
int decodeCombiTimbreVolume(const uint8_t* record, size_t recordSize, int timbreIndex);

// Human-readable, best-effort description of what changed between two
// Combi records at the SAME (bank, number) slot in two different files --
// built for the cross-dataset "Compare two files" tool (STATE.md entry 92),
// whose whole point was "impossible to resolve conflicts because the
// reason is unknown" (direct quote). Two tiers, per direct RFC decision
// (2026-09-20), REFINED the same day against two real backups (entry 93)
// once every "Other section differs" hit in that real data turned out to
// be the exact same previously-uncategorized field (Timbre Volume), and
// each entry's raw byte RANGES exposed alongside its description (entry
// 94) so a caller can actually RESOLVE one specific change (copy those
// exact bytes from one file's record into the other's) instead of only
// ever being able to describe it:
//   - NAMED, WITH VALUES: each Timbre's Program reference/on-off status
//     (already fully decoded via CombiInfo::timbres, zero new byte work),
//     "Master Volume", and each Timbre's own "Volume" (the two fields
//     decoded above).
//   - COARSE, NAME ONLY (no value shown): "IFX1".."IFX12" (each Insert
//     Effect slot's own wrapper settings -- Effect Type/Channel/Switch/
//     Panpot/Bus routing/Sends, NOT that effect's own internal parameters,
//     which aren't decoded anywhere in this project), "MFX" (Master
//     Effect 1+2 combined, including their shared Return/Chain settings --
//     deliberately NOT split by 1/2, per direct decision: a Combi's two
//     Master Effect slots are functionally paired, not independent facts
//     worth distinguishing here), "TFX" (Total Effect 1+2 combined, same
//     reasoning -- excludes the Master Volume byte just above, which is
//     already reported with its own value so it isn't ALSO folded into a
//     vaguer "TFX differs" line), "EQ" (each Timbre's own "(Track EQ)"
//     Trim/Bypass/Frequency/Gain bytes, pooled across all 16 Timbres into
//     one category -- fires if ANY Timbre's EQ bytes differ anywhere), and
//     "Mixer" (entry 93: every OTHER per-Timbre byte between the Timbre's
//     own 3-byte reference/status block and its EQ bytes -- Pan, Send1/2,
//     Bend Range, Transpose, Detune, Delay settings, Drum Kit Patch
//     IFX1-12, Bus Select/Rec Bus/Chord/Max-Notes bits, and the (Filter)
//     transmit toggles -- pooled the same way EQ is, EXCLUDING the Volume
//     byte just above so a Volume-only change isn't ALSO reported as a
//     vaguer "Mixer differs"; none of these individual fields are decoded
//     with their own name/value yet, but the byte RANGE they occupy is
//     confirmed, so a change there is still attributable to "this Timbre's
//     mixer/tuning/routing settings" instead of falling all the way
//     through to the catch-all).
//   - A catch-all "Other section differs" fires only if the two records'
//     contentHash actually differs but NONE of the above caught it --
//     this project's own "detect changes in raw data blocks we already
//     have detected, without parsing every parameter" mandate for this
//     pass: nothing is silently dropped, an uncategorized difference still
//     surfaces, just without a specific name. Entry 93 confirmed this
//     bucket can genuinely empty out in practice -- every real "Other"
//     hit in the two test backups moved to a named line once Timbre
//     Volume was decoded.
//   - Deliberately NOT included this pass: a "MIDI" category. Korg's own
//     reference table lists a per-Timbre "MIDI Channel" at the SAME byte
//     TimbreStatus already reads (SysEx offset 4804, bits 4~0) -- but
//     TimbreStatus's own doc comment in PcgFile.h already documents THIS
//     project's independently-confirmed finding that those exact bits are
//     a Timbre's own 0-based index (confirmed by watching it count 0..15
//     across a real Combi's 16 Timbres), not a MIDI channel. Real,
//     unresolved conflict between two sources -- flagged rather than
//     silently picking one, per this project's own no-guessing rule; a
//     "MIDI" category needs this settled first (a real Combi where the
//     Timbre index count DOESN'T hold, or an independent second reference,
//     would settle it either way).
// `recordA`/`recordB` are that slot's own raw CBK1 record bytes in each
// file (PcgFile::combiRecordBytes()); `a`/`b` are that slot's already-
// decoded CombiInfo (for the Timbre-reference tier, and the contentHash
// catch-all check) -- passed in rather than re-decoded here, since the
// caller (PcgFile::findDivergentCombisAcrossFiles()) already has both.
//
// A change's `description` is DIRECTIONAL -- built from (a, b) in that
// order ("111 -> 103" means a's value first) -- so a caller resolving one
// specific change by matching this exact string back (STATE.md entry 94's
// "->"/"<-" per-change resolve buttons) MUST always recompute this list in
// the SAME (a, b) order it was first displayed in, regardless of which
// direction the actual byte copy then runs -- the ranges themselves are
// direction-agnostic (plain offsets), only the DESCRIPTION TEXT depends on
// argument order.
//
// [{range.offset, range.length}] is exactly the set of bytes THAT ENTRY
// covers, no more/less than what its own detection above already scans --
// a caller resolving a coarse category (IFX/MFX/TFX/EQ/Mixer) by copying
// these ranges therefore copies the WHOLE declared section, not just
// whichever specific byte(s) happened to differ within it (there's no
// record of which -- these categories only ever detected "differs
// somewhere", never narrowed further, by design -- see this function's own
// per-category comments above). The catch-all "Other section differs"
// entry's own range is the ENTIRE record -- the only accurate answer when
// nothing more specific was ever identified.
struct CombiChangeRange {
    size_t offset = 0;
    size_t length = 0;
};
struct CombiChange {
    std::string description;
    std::vector<CombiChangeRange> ranges;
};
std::vector<CombiChange> describeCombiDivergence(const CombiInfo& a, const CombiInfo& b, const std::vector<uint8_t>& recordA,
                                                   const std::vector<uint8_t>& recordB);

}  // namespace kronos
