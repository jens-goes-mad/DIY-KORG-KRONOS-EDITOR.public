// SetlistEditorColor: Color codec for one SBK1 Set List song record.
// Split out from the old combined setlist-slot-params.js (2026-08-16, per
// direct request) into one file per field -- Color and Volume happened to
// be packed into the same file originally only because both are small and
// share the same 542-byte record, not because they're related fields; kept
// separate now on purpose, anticipating more SBK1/record fields (and,
// eventually, other records entirely -- EXi Program params) needing their
// own codecs, where "one file per concern" scales better than one growing
// grab-bag file. Sibling to setlist-editor-comment-and-font.js (same
// 542-byte record, same masked-read-modify-write discipline). Deliberately
// standalone -- no dependency on the rest of this app, choc, or a native
// build. Open setlist-editor-color.test.html directly (via a static file
// server, see setlist-editor-comment-and-font.test.html's own comment for
// why) to develop/test this file in isolation.
//
// CONFIRMED -- see docs/content/format/index.md §4.3 and
// src/kronos/PcgFile.cpp's readSlotParams()/kSbk* constants for the C++
// side of the same derivation.
//
// Color is a 4-bit field (1-based, 1..16) packed into byte +12's bits 2-5 --
// that byte is ALSO bit0 (isProgram) and bits6-7 (Font size's low 2 bits,
// see setlist-editor-comment-and-font.js). Every read/write below masks to
// exactly the bits Color owns, so this never clobbers isProgram, Font size,
// or the still-unexplained bit1.
export const RECORD_SIZE = 542; // SBK1 song record stride (docs/content/format/index.md §4.2), same as setlist-editor-comment-and-font.js
const TYPE_COLOR_OFFSET = 12; // docs/content/format/index.md §4.3 -- bits2-5 are Color's own bits
const COLOR_MASK = 0x3c; // bits 2-5 of +12

function checkLength(bytes, fnName) {
  if (bytes.length !== RECORD_SIZE) {
    throw new Error(`${fnName}: expected ${RECORD_SIZE} bytes, got ${bytes.length}`);
  }
}

// Returns the 1-based Color index (1..16).
export function decodeSlotColor(bytes) {
  checkLength(bytes, "decodeSlotColor");
  return ((bytes[TYPE_COLOR_OFFSET] & COLOR_MASK) >> 2) + 1;
}

// Returns a NEW 542-byte Uint8Array: Color's own bits (byte +12, bits2-5)
// set to `color` (1-based, clamped to 1..16), every other bit of that byte
// (isProgram, Font size, the unexplained bit1) and every other byte
// byte-identical to the input.
export function encodeSlotColor(bytes, color) {
  checkLength(bytes, "encodeSlotColor");
  const out = Uint8Array.from(bytes);
  const clamped = Math.max(1, Math.min(16, Math.round(color)));
  const fieldBits = ((clamped - 1) << 2) & COLOR_MASK;
  // Clear only Color's own bits first (& ~mask), THEN OR in the new value --
  // never a plain assignment, same discipline as setlist-editor-comment-
  // and-font.js's Font size write, for the same reason (this byte is
  // shared with other fields).
  out[TYPE_COLOR_OFFSET] = (bytes[TYPE_COLOR_OFFSET] & ~COLOR_MASK) | fieldBits;
  return out;
}
