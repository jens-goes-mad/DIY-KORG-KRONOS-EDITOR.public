// Set List marker slots -- an app convention, not Korg's. A marker is an
// ordinary slot whose NAME has a special meaning, so it lives on the Kronos
// itself and survives every save/load (and shows on the Kronos screen
// during a gig). Written by the Setlist row menu's "Reset to SET BREAK…" /
// "Reset to GIG…" (pane-setlist-editor.js), read by the print export
// (setlist-print.js). Both names fit the 24-byte slot name field
// (setlist-editor-name.js's NAME_MAX_LENGTH).
//
// Neither counts as an empty slot (pane.js's looksLikeEmptySetlistName()
// only matches blank and "- Init Setlist -"), so drag-and-drop's "copy
// over" refuses to overwrite one, same as a real song.

export const SET_BREAK_NAME = "---- SET BREAK ----";

// "2025-10-28" -> "---- GIG 2025-10-28 ----" (exactly 24 characters).
export function gigMarkerName(isoDate) {
  return `---- GIG ${isoDate} ----`;
}

// "GIG 2025-10-28" anywhere in the text (slot name or Set List name) ->
// "2025-10-28", else null. The surrounding dashes are decoration only.
export function parseGigDate(text) {
  const m = /\bGIG\s+(\d{4}-\d{2}-\d{2})\b/i.exec(text || "");
  return m ? m[1] : null;
}

export function isSetBreakName(name) {
  return /^[\s-]*SET BREAK[\s-]*$/i.test(name || "");
}
