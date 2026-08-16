// SetlistEditorVolume: Volume codec for one SBK1 Set List song record.
// Split out from the old combined setlist-slot-params.js (2026-08-16, per
// direct request) into one file per field -- see setlist-editor-color.js's
// own doc comment for the full reasoning. Sibling to setlist-editor-
// comment-and-font.js/setlist-editor-color.js (same 542-byte record, same
// masked-read-modify-write discipline). Deliberately standalone -- no
// dependency on the rest of this app, choc, or a native build. Open
// setlist-editor-volume.test.html directly (via a static file server, see
// setlist-editor-comment-and-font.test.html's own comment for why) to
// develop/test this file in isolation.
//
// CONFIRMED -- see docs/content/format/index.md §4.3 and
// src/kronos/PcgFile.cpp's readSlotParams()/kSbk* constants for the C++
// side of the same derivation.
//
// Volume, unlike Color/Font size/Transpose, is a clean unpacked byte at
// +16 -- no masking needed, but still routed through the same
// decode/encode shape as this file's siblings for a consistent editor
// interface.
export const RECORD_SIZE = 542; // SBK1 song record stride (docs/content/format/index.md §4.2), same as setlist-editor-comment-and-font.js
const VOLUME_OFFSET = 16; // docs/content/format/index.md §4.3 -- a plain byte, 0-127 MIDI-style

function checkLength(bytes, fnName) {
  if (bytes.length !== RECORD_SIZE) {
    throw new Error(`${fnName}: expected ${RECORD_SIZE} bytes, got ${bytes.length}`);
  }
}

// Returns the raw Volume byte (0-127, MIDI-style).
export function decodeSlotVolume(bytes) {
  checkLength(bytes, "decodeSlotVolume");
  return bytes[VOLUME_OFFSET];
}

// Returns a NEW 542-byte Uint8Array: byte +16 set to `volume` (clamped to
// 0..127), every other byte byte-identical to the input -- no masking
// needed, this byte isn't shared with any other field.
export function encodeSlotVolume(bytes, volume) {
  checkLength(bytes, "encodeSlotVolume");
  const out = Uint8Array.from(bytes);
  out[VOLUME_OFFSET] = Math.max(0, Math.min(127, Math.round(volume)));
  return out;
}
