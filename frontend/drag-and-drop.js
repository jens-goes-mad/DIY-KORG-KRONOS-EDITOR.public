// The one shared drag-and-drop engine for every draggable table row in this
// app -- Set List slots (pane-setlist-editor.js), Programs (pane-program-
// editor.js) and Combis (pane-combi-editor.js). Each of those used to
// hand-roll its own dragstart/dragover/dragleave/dragend/drop wiring:
// near-identical skeletons that had quietly drifted apart -- only Combis set
// a "copy" cursor, only Set List showed an insert line, only Programs
// rejected an engine-type mismatch during hover -- and one real off-by-one
// (a downward "insert before/after" drop landing one slot too far) that had
// to be fixed in two places at once. This collapses the skeleton into one
// place and leaves each caller only the parts that genuinely differ:
//
//   getPayload()  -- what identifies this row as a drag SOURCE
//   classify(...) -- is a given hovered row a valid target right now, and
//                    would a drop there COPY or MOVE? (the only judgement
//                    the hover feedback needs; the caller re-derives the
//                    full operation itself in onDrop)
//   onDrop(...)   -- actually perform the drop
//
// Loaded before the pane editors in index.html; everything here is a plain
// global, same as pane.js's shared helpers (this app uses no module system).

// The 3-zone split for a row that supports before/after insertion: top 30%
// = "insert before this row", bottom 30% = "insert after", middle 40% =
// "onto this row". Recomputed on every dragover (the cursor can cross a
// zone boundary while still hovering the same row), not cached. A caller
// without any before/after gesture at all would pass `zones: false` to
// always get "on" -- none of this app's three tables currently do (Programs
// gained its own move-between-rows gesture 2026-09-04), but the option
// stays for a future table that genuinely has none.
function dropZoneForEvent(tr, ev) {
  const rect = tr.getBoundingClientRect();
  const relativeY = (ev.clientY - rect.top) / rect.height;
  if (relativeY < 0.3) return "before";
  if (relativeY > 0.7) return "after";
  return "on";
}

// Converts a before/after drop's zone + the target row's own position into
// the FINAL resting index a shift-based move expects -- shared by every
// caller of one (Set List's reorderSongEntry, Combi's
// moveCombiWithinBank(), Programs' moveProgramWithinBank(), one copy of
// this math instead of three). All of those take the moving record's FINAL
// index, not an "insert position": each shifts the intervening range
// first, so if the source sits ABOVE the target, pulling it out slides the
// target (and everything between) down by one, and a raw `targetPosition`/
// `targetPosition + 1` overshoots by one -- the record lands one past
// where the before/after line indicated. Originally found and fixed twice
// independently (Combi, then Setlist) before landing here once.
function finalIndexForInsert(zone, targetPosition, sourcePosition) {
  const insertPos = zone === "before" ? targetPosition : targetPosition + 1;
  return sourcePosition < insertPos ? insertPos - 1 : insertPos;
}

// Every class a row can carry mid-drag to show what a drop right now would
// do. Exactly one is ever set at a time; cleared together on
// dragleave/dragend/drop and re-set on the next dragover.
//   .drop-copy    -- the drop will COPY into this slot (green + the OS "+"
//                    cursor badge, which dropEffect = "copy" asks for)
//   .drop-target  -- the drop will MOVE or SWAP with this occupied slot
//                    (blue, the long-standing shared style)
//   .drop-before  -- insert above this row (a line along its top edge)
//   .drop-after   -- insert below this row (a line along its bottom edge)
// See style.css for the rules -- all four span every draggable table now
// that Programs supports before/after too (2026-09-04).
const DRAG_DROP_CLASSES = ["drop-target", "drop-copy", "drop-before", "drop-after"];

function clearDropClasses(tr) {
  tr.classList.remove(...DRAG_DROP_CLASSES);
}

// Which hover class illustrates this verdict: the before/after zones show
// an insert line regardless of effect (the "+" cursor still carries
// copy-vs-move); an "onto" drop shows green for copy, blue for move/swap.
function dropHoverClass(effect, zone) {
  if (zone === "before") return "drop-before";
  if (zone === "after") return "drop-after";
  return effect === "copy" ? "drop-copy" : "drop-target";
}

// The payload of the row currently being dragged, or null. Set on
// dragstart, cleared on dragend -- readable during dragover, where the
// HTML5 DataTransfer payload itself deliberately is not (a security
// restriction aimed at files dragged in from the OS; a same-page row drag
// can use a plain shared variable as a side channel). Module-level, not
// per-pane: a drag that starts in one pane and hovers a row in the OTHER
// pane (both showing the same dataset) must still see it.
let draggedRow = null;

// Whether Shift is currently held. WebKit (WKWebView on macOS -- what CHOC
// uses there) does NOT populate modifier flags on drag events:
// `dragover.shiftKey` / `drop.shiftKey` are always false there, so a
// Shift-changes-the-drop's-effect gesture can't be read off the event.
// Track it from real keydown/keyup instead -- those fire normally, and by
// the time a drag's own modal loop starts suppressing keyboard events this
// is already set from the keydown that preceded the mousedown (so the rule
// becomes "hold Shift before you start dragging"; releasing mid-drag is not
// seen, which is fine). Chromium (WebView2 on Windows) reports it on the
// event too -- shiftHeld() ORs the two so either path works. `capture: true`
// so a stopPropagation somewhere in the tree can't hide it; window blur
// clears a hold that ends while we're not focused.
//
// STILL UNVERIFIED ON REAL HARDWARE, reliability now genuinely in question
// (2026-09-04): this was built specifically to fix Combi's own Shift-to-copy
// gesture not registering, and was reported STILL not registering afterward
// -- tried held-before-the-drag (this function's own documented rule),
// pressed-mid-drag, AND Option/Alt instead of Shift, none of it worked.
// Combi's own onto-an-empty-slot gesture no longer uses shiftKey/Shift AT
// ALL as a result (pane-combi-editor.js, see its own comment) -- dropping
// onto an empty slot is unconditionally a copy now, and moving a Combi INTO
// an empty slot instead uses the direction-reversed swap gesture (drag the
// EMPTY slot onto the USED one), which needs no modifier. Programs'
// Shift-to-swap (pane-program-editor.js) is the only remaining caller of
// this function -- it almost certainly has the identical failure and
// hasn't been fixed only because it wasn't what was reported; flagged, not
// silently left, in STATE.md.
let shiftKeyDown = false;
document.addEventListener("keydown", (ev) => { if (ev.key === "Shift") shiftKeyDown = true; }, true);
document.addEventListener("keyup", (ev) => { if (ev.key === "Shift") shiftKeyDown = false; }, true);
window.addEventListener("blur", () => { shiftKeyDown = false; });
function shiftHeld(ev) {
  return (ev && ev.shiftKey) || shiftKeyDown;
}

// Wire one draggable row. Call once per <tr> as it is built.
//
//   tr          the row element
//   zones       true if this row supports the before/after insert gesture
//               -- all three of this app's tables do (Set List, Combis,
//               Programs)
//   getPayload  () => a plain JSON-serialisable object identifying this row
//               as a drag source. Called on dragstart.
//   classify    ({ dragged, zone, shiftKey }) => null | { effect, zone? }
//               Called on every dragover with `dragged` = the live
//               draggedRow and `shiftKey` = whether Shift is held (tracked
//               via keydown, not the event -- see shiftHeld() above for
//               why). Return null to reject this row (no drop,
//               browser shows the "not allowed" cursor). Otherwise return
//               the drop `effect` ("copy" | "move") for the cursor hint and
//               hover class, and optionally a coerced `zone` when the
//               gesture forces one regardless of cursor position (e.g. a
//               cross-dataset Combi copy is always "onto"). The last
//               dragover's verdict is what the following drop acts on, so
//               keep this a pure function of its arguments and the caller's
//               closure.
//   onDrop      ({ source, zone, shiftKey }) => void. `source` is the
//               payload parsed back out of the DataTransfer, `zone` is the
//               last dragover's (coerced) zone -- exactly what the hover
//               showed. Called once on a real drop on this row.
function makeRowDraggable(tr, { zones = false, getPayload, classify, onDrop }) {
  tr.draggable = true;

  // The zone and classify() verdict from the most recent dragover on this
  // row. `drop` acts on THESE rather than recomputing from the drop event:
  // the two must agree (a drop has to do exactly what the hover just
  // promised), and a dragover event's cursor coordinates are always present
  // where a drop event's have been seen to arrive as 0 in some WebViews --
  // which silently turned every drop into a top-edge "before". Reset on
  // dragend (and left alone by the spurious dragleave/dragenter pair that
  // fires while the cursor crosses this row's own cell boundaries).
  let hoverZone = "on";
  let hoverVerdict = null;

  tr.addEventListener("dragstart", (ev) => {
    draggedRow = getPayload();
    ev.dataTransfer.setData("application/json", JSON.stringify(draggedRow));
    // "copyMove", not "move": the browser only honours a later
    // `dropEffect = "copy"` (the "+" cursor badge) if dragstart's
    // effectAllowed permitted copy.
    ev.dataTransfer.effectAllowed = "copyMove";
  });

  tr.addEventListener("dragend", () => {
    draggedRow = null;
    hoverVerdict = null;
    clearDropClasses(tr);
  });

  tr.addEventListener("dragover", (ev) => {
    // Not one of our row drags (e.g. a file dragged in from the OS) -- stay
    // out of it entirely: no preventDefault, no hover feedback.
    if (draggedRow == null) return;
    hoverZone = zones ? dropZoneForEvent(tr, ev) : "on";
    hoverVerdict = classify({ dragged: draggedRow, zone: hoverZone, shiftKey: shiftHeld(ev) });
    if (!hoverVerdict) {
      clearDropClasses(tr);
      return;  // reject: no preventDefault, so no `drop` fires here
    }
    ev.preventDefault();
    ev.dataTransfer.dropEffect = hoverVerdict.effect;
    clearDropClasses(tr);
    tr.classList.add(dropHoverClass(hoverVerdict.effect, hoverVerdict.zone ?? hoverZone));
  });

  tr.addEventListener("dragleave", () => clearDropClasses(tr));

  tr.addEventListener("drop", (ev) => {
    ev.preventDefault();
    ev.stopPropagation();  // don't also bubble to a pane/document file-drop handler
    clearDropClasses(tr);
    const raw = ev.dataTransfer.getData("application/json");
    if (!raw) return;
    const source = JSON.parse(raw);
    // Act on the last dragover's verdict; fall back to a fresh classify only
    // if somehow no dragover ran (shouldn't happen -- a drop is always
    // preceded by at least one).
    const verdict = hoverVerdict ?? classify({ dragged: source, zone: hoverZone, shiftKey: shiftHeld(ev) });
    if (!verdict) return;
    onDrop({ source, zone: verdict.zone ?? hoverZone, shiftKey: shiftHeld(ev) });
  });
}
