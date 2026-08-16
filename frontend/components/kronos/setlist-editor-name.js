// SetlistEditorName: name codec for one SDB1 Set List slot NAME record --
// renamed 2026-08-16 (was setlist-slot-name.js), part of the wider
// "setlist-editor-*" family rename (siblings: setlist-editor-comment-and-
// font.js, setlist-editor-color.js, setlist-editor-volume.js). Distinct
// from those siblings' SBK1 records: a slot's name lives in an entirely
// separate chunk, at different byte offsets and strides (see
// src/kronos/PcgFile.h's own nameRecordBytes() doc comment for why). Same
// masked-read-modify-write discipline as its siblings, kept as its own
// file for the same "one concern per file" reason they're all split apart.
// Deliberately standalone -- no dependency on the rest of this app, choc,
// or a native build.
//
// Record shape CONFIRMED -- see docs/content/format/index.md §3.2 and
// src/kronos/PcgFile.cpp's readRecordName() for the C++ side of the same
// derivation: a 4-byte marker (meaning not understood yet, preserved
// untouched on every write) followed by a 24-byte ASCII name field,
// NUL-padded if shorter than 24 bytes, un-terminated if exactly 24.
export const NAME_RECORD_SIZE = 28; // SDB1 name record stride (docs/content/format/index.md §3.2)
const NAME_OFFSET = 4; // bytes 0-3 are the marker
export const NAME_MAX_LENGTH = 24; // the remaining bytes

function checkLength(bytes, fnName) {
  if (bytes.length !== NAME_RECORD_SIZE) {
    throw new Error(`${fnName}: expected ${NAME_RECORD_SIZE} bytes, got ${bytes.length}`);
  }
}

// Returns the decoded name, trimmed at the first NUL byte (or the full 24
// bytes if there isn't one) -- matches PcgFile.cpp's own readRecordName().
export function decodeSlotName(bytes) {
  checkLength(bytes, "decodeSlotName");
  let end = NAME_OFFSET;
  while (end < NAME_OFFSET + NAME_MAX_LENGTH && bytes[end] !== 0) end++;
  return String.fromCharCode(...bytes.slice(NAME_OFFSET, end));
}

// Returns a NEW 28-byte Uint8Array: the 4-byte marker preserved byte-
// identical from the input, the 24-byte name field replaced with `name`
// (truncated to NAME_MAX_LENGTH characters, NUL-padded if shorter) -- same
// charCodeAt-and-mask encoding setlist-editor-comment-and-font.js's encoder uses.
export function encodeSlotName(bytes, name) {
  checkLength(bytes, "encodeSlotName");
  const out = Uint8Array.from(bytes);
  const truncated = String(name).slice(0, NAME_MAX_LENGTH);
  for (let i = 0; i < NAME_MAX_LENGTH; i++) {
    out[NAME_OFFSET + i] = i < truncated.length ? truncated.charCodeAt(i) & 0xff : 0;
  }
  return out;
}
