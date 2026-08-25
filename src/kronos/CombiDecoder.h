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

}  // namespace kronos
