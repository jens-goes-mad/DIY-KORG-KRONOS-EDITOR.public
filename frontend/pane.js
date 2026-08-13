// One Norton-Commander-style pane: a thin shell (one dataset selector, one
// category navbar -- Setlist/Programs/Combis/Duplicates, Global later once
// GLB1 is ever parsed) around two content renderers -- createSetlistPanel()
// below, and library.js's createLibraryPanels() -- so a pane can show any
// category of any already-open dataset, independent of the other pane. Two
// shells are created by app.js ("A" and "B") so Setlist slots can be
// dragged between them.
// Korg's own bank naming, from a song slot's raw `bank` byte. Combi (14
// banks, 0-13) is mechanically confirmed -- cross-referencing CBK1 by this
// exact order reproduces known real Combi names exactly (see README.md).
// Program (bank 0-19) is now directly confirmed the same rigorous way
// (2026-08-11): the project owner checked every single position-0 Program
// name in the app against the real Kronos's own on-screen bank browser,
// for all 20 banks -- every one matched this order exactly. There is no
// stored "I-G" or "G(d)" bank at all (real hardware shows those as
// GM/g(d) content, not stored per-file, see docs/content/format/index.md
// §5.2/§5.4) -- USER-A starts right after INT-F, and USER-A..G is 7
// single-letter banks before the double-letter USER-AA..GG series starts.
// This array used to have "I-G"/"G(d)" as real entries at index 6/7,
// shifting everything below by 2 -- fixed the same day as
// PcgFile.cpp's kConfirmedTimbreBanks (which had the identical bug for the
// separate Combi-Timbre-raw-code lookup, see its own doc comment).
// Shortened form ("I-A"/"U-A") of Korg's own "INT-A"/"USER-A" naming --
// used everywhere in the UI to save column width (see STATE.md); the full
// "INT-"/"USER-" form is what's actually verified against ground truth
// (docs/content/format/index.md, PcgFile.cpp's confirmed Timbre bank codes), so only the
// display layer abbreviates, never the underlying data.
// Confirmed fixed count, real hardware and file format alike (docs/content/format/index.md
// §3.2/§4.2) -- every Set List always has exactly this many song slots, never
// more or fewer. Used by the drag-and-drop "insert after the last entry"
// gesture to clamp a target index to the last real slot.
const SETLIST_SONG_COUNT = 128;

const COMBI_BANK_NAMES = [
  "I-A", "I-B", "I-C", "I-D", "I-E", "I-F", "I-G",
  "U-A", "U-B", "U-C", "U-D", "U-E", "U-F", "U-G",
];

const PROGRAM_BANK_NAMES = [
  "I-A", "I-B", "I-C", "I-D", "I-E", "I-F",
  "U-A", "U-B", "U-C", "U-D", "U-E", "U-F", "U-G",
  "U-AA", "U-BB", "U-CC", "U-DD", "U-EE", "U-FF", "U-GG",
];

// Applies the same "INT-"/"USER-" -> "I-"/"U-" shortening to a full bank
// name coming straight from the bridge (e.g. a Timbre's confirmed
// `bankName`, see library.js's formatTimbreRef()) -- so a ground-truth name
// stored/logged in full still renders shortened, consistent with
// COMBI_BANK_NAMES/PROGRAM_BANK_NAMES above.
function abbreviateBankName(name) {
  return name.replace(/^INT-/, "I-").replace(/^USER-/, "U-");
}

// Real Kronos Set List slot color names AND hex values (16 total, matching
// SlotParams' 1-based `color` field -- see PcgFile.h §4.3) -- CONFIRMED
// 2026-08-06 against a real Kronos unit (see STATE.md), via
// tools/generate_setlist_test_matrix.{js,cpp}'s Group 5 (slots 030-045, one
// real color per slot). Both name AND order were substantially wrong before
// this: the earlier list ("Standard/Blue/Ivy/Gold/Rose/Azure/Red/Orange/
// Yellow/Green/Cyan/Purple/Magenta/Brown/Black/White") was an unconfirmed
// guess, and the real palette isn't generic named colors at all -- it's
// Korg's own curated, muted set, e.g. no Red/Green/Blue/Black/White exist
// as such. Hex values are the project owner's own on-screen reading, not a
// pixel-perfect sample -- close enough to use directly rather than
// substituting a "usual" hex for a similarly-named CSS color, since the
// whole point is showing what THIS hardware actually renders, not a
// generic "gold" or "navy."
const SETLIST_COLOR_NAMES = [
  "Default", "Charcoal", "Brick", "Burgundy", "Ivy", "Olive", "Gold", "Cacao",
  "Indigo", "Navy", "Rose", "Lavender", "Azure", "Denim", "Silver", "Slate",
];

// Same index (0-based here, color field is 1-based) as SETLIST_COLOR_NAMES.
const SETLIST_COLOR_HEX = [
  "#494c55", // Default
  "#282b31", // Charcoal
  "#af4350", // Brick
  "#661b27", // Burgundy
  "#929a33", // Ivy
  "#233519", // Olive
  "#aa8c3e", // Gold
  "#723d3f", // Cacao
  "#3759bf", // Indigo
  "#0410ab", // Navy
  "#9478c7", // Rose
  "#745ad2", // Lavender
  "#5588c2", // Azure
  "#385f9c", // Denim
  "#546180", // Silver
  "#2a3149", // Slate
];

// Purely cosmetic UI legibility boost -- NOT a hardware value, just a
// display-time brightening so the real palette's fairly dark/muted swatches
// (SETLIST_COLOR_HEX above, confirmed against real hardware) don't
// disappear into this app's own dark background. Per-channel multiply,
// clamped -- SETLIST_COLOR_HEX itself stays the literal confirmed value,
// only what's actually rendered gets adjusted.
function brightenHex(hex, factor = 1.3) {
  const r = parseInt(hex.slice(1, 3), 16);
  const g = parseInt(hex.slice(3, 5), 16);
  const b = parseInt(hex.slice(5, 7), 16);
  const clamp = (v) => Math.min(255, Math.round(v * factor));
  const toHex = (v) => v.toString(16).padStart(2, "0");
  return `#${toHex(clamp(r))}${toHex(clamp(g))}${toHex(clamp(b))}`;
}

// entry.color is 1-based -- falls back to Default rather than guessing if
// it's ever out of the confirmed 1..16 range. Returns the brightened
// display version (brightenHex above) -- every current caller is a UI
// swatch (the "#" cell background, the Color editor's 16 buttons), never a
// place that needs the literal unmodified hardware value.
function setlistColorHex(color) {
  return brightenHex(SETLIST_COLOR_HEX[color - 1] || SETLIST_COLOR_HEX[0]);
}

function setlistColorName(color) {
  return SETLIST_COLOR_NAMES[color - 1] || `Color ${color}`;
}

// Which of the three drag-and-drop gestures a Setlist row's dragover/drop
// applies, based on where within the row's own height the cursor currently
// is -- top 30% = "insert before this row", bottom 30% = "insert after this
// row", middle 40% = "copy over this row" (see app.js's onDropEntry and
// STATE.md's RFC for what each does). Recomputed on every dragover, not
// just once, since the cursor can move between zones while still hovering
// the same row.
function dropZoneForEvent(tr, ev) {
  const rect = tr.getBoundingClientRect();
  const relativeY = (ev.clientY - rect.top) / rect.height;
  if (relativeY < 0.3) return "before";
  if (relativeY > 0.7) return "after";
  return "on";
}

function kronosNumber(n) {
  return String(n).padStart(3, "0");
}

// `bankType` ("HD-1"/"EXi", see EditorBridge::getProgramBankTypes()) is
// optional and only ever shown for Programs -- Combis have no engine type of
// their own, so it's ignored whenever entry.isProgram is false, regardless
// of what's passed. Per-file data, not a hardcoded table -- see
// PcgFile.h's ProgramBankType doc comment: Kronos OS 3.0+ lets a user
// reassign INT Program Banks between HD-1 and EXi.
function formatBankNumber(entry, bankType) {
  const num = kronosNumber(entry.number);
  const names = entry.isProgram ? PROGRAM_BANK_NAMES : COMBI_BANK_NAMES;
  const label =
    entry.bank >= 0 && entry.bank < names.length
      ? `${names[entry.bank]} ${num}`
      : // Beyond the stored bank list -- almost certainly a GM/GM2 reference
        // (fixed content, not stored per-file) or corrupt data. Show the raw
        // index rather than guess at a label.
        `${entry.bank} ${num}`;
  return entry.isProgram && bankType ? `${label} (${bankType})` : label;
}

// Builds a <colgroup> from 12-based column-grid fractions -- Bulma's own
// grid (.column.is-1 .. is-12) is a 12-column system; this applies the same
// convention to table <col> widths, as percentages (e.g. [1,7,1,2,1] ->
// 8.33%/58.33%/8.33%/16.67%/8.33%, summing to 100%) so the table scales
// proportionally with its container instead of being pixel-locked. Paired
// with style.css's `table-layout: fixed` (shared by every real .table in
// this app -- Setlist here, Programs/Combis/Duplicates in library.js) --
// Bulma's .table component styles a real HTML table (colors/borders/
// hover), it doesn't do column-width layout at all, so this is the one
// genuinely irreducible bit of non-Bulma CSS a table still needs. `<col>`
// only accepts a handful of CSS properties (width chief among them), which
// is exactly what's needed here and nothing more.
//
// Fractions don't have to be whole numbers -- a plain float like `1.3`
// nudges a column a bit wider without disturbing the others' ratios. A
// previous version supported `{frac, extraPx}`, baking a flat pixel bump
// into the width via `calc(pct% + Npx)` -- reverted after that made every
// column except the flexible one effectively disappear. `<col>` elements
// have historically had weak, inconsistent cross-engine support for
// anything beyond a plain width value, and this correlates exactly with
// when the widths broke, so calc() on a <col> is being treated as unsafe
// here even though it's spec-legal -- not independently confirmed root-
// caused (no way to inspect the actual rendered layout in this
// environment), but not worth risking a second time either.
function colgroupHtml(fractions12) {
  return (
    "<colgroup>" +
    fractions12
      .map((f) => (f == null ? "<col>" : `<col style="width:${((f / 12) * 100).toFixed(4)}%">`))
      .join("") +
    "</colgroup>"
  );
}

const NO_DATASET_MESSAGE = "No dataset selected -- use the Open... button above, or pick an already-open dataset from the selector.";

// Shared across every pane's Setlist rows during a same-page row drag --
// lets a row being dragged OVER (not just dropped on) know which dataset
// the drag actually originated from, so a cross-dataset drop can be shown
// as rejected during hover, not just after release. HTML5's DataTransfer
// payload isn't readable during dragover for security reasons, but since
// this is a same-page drag (not a file dragged in from Finder/Explorer), a
// plain shared variable works fine as a side channel. The actual block
// (app.js's onDropEntry) doesn't depend on this -- it's purely a visual
// hint, see that function's own doc comment for why cross-dataset Setlist
// copies are rejected at all.
let draggedFromDatasetId = null;

// Lazily loads the two pure-JS SBK1 record codecs (frontend/components/
// kronos/setlist-comment.js, setlist-slot-params.js) the Setlist row
// editors below use to read-modify-write raw bytes -- a dynamic import()
// expression works from inside this plain (non-module) script without
// converting index.html's scripts to type="module", see STATE.md. Cached
// in one shared promise so opening a second editor (same pane or the
// other one) doesn't re-trigger a second import; the browser's own module
// cache would dedupe the actual fetch anyway, but this also dedupes the
// in-flight Promise for callers that race each other. `resolvedSlotCodecs`
// holds the already-settled value so row-builders (called synchronously
// from renderRows(), see openSection()'s own comment for why that's safe)
// can use the codecs without themselves being async.
let slotCodecsPromise = null;
let resolvedSlotCodecs = null;
function loadSlotCodecs() {
  if (!slotCodecsPromise) {
    slotCodecsPromise = Promise.all([
      import("./components/kronos/setlist-comment.js"),
      import("./components/kronos/setlist-slot-params.js"),
      import("./components/kronos/setlist-slot-name.js"),
    ]).then(([comment, slotParams, slotName]) => {
      resolvedSlotCodecs = { ...comment, ...slotParams, ...slotName };
      return resolvedSlotCodecs;
    });
  }
  return slotCodecsPromise;
}

// Shared across every pane instance (both "A" and "B" are separate
// createSetlistPanel() closures) -- keyed by "datasetId:setlistIndex:
// songIndex", value is the paneId currently holding at least one editor
// open on that exact slot. Two panes CAN legitimately show the same Set
// List at once (both pointed at the same dataset -- a real, supported
// setup, see the Datasets architecture in STATE.md); if they did and both
// opened an editor on the same slot, each pane's raw-bytes cache below is
// its own independent copy of those 542 bytes, so whichever pane committed
// second would silently overwrite whatever the other had just written --
// the same class of bug multi-open already had to solve for within one
// pane (see commitSlotBytes()'s own comment), just across panes instead of
// across editor types within a row. Blocking a second pane from opening
// the SAME slot at all avoids the race outright, rather than trying to
// detect or merge a conflicting write after the fact.
const openSlotEditors = new Map();
function slotEditorKey(datasetId, setlistIndex, songIndex) {
  return `${datasetId}:${setlistIndex}:${songIndex}`;
}

// The Setlist category's own content: a Set List picker + a filterable,
// searchable, drag-and-drop-able table of that Set List's 128 song slots,
// plus the per-slot Comment editor. Extracted out of the pane shell so it's
// a peer of library.js's createLibraryPanels() -- both are just "renderers"
// the shell mounts/hides depending on which category button is active.
// Reads the dataset to show via getDatasetId() (owned by the shell) rather
// than tracking it itself, matching createLibraryPanels()'s same contract.
function createSetlistPanel(
  container,
  { paneId, log, showToast, onDropEntry, onCopySetlist, getDatasetId, getOpposite, getProgramBankType, onJumpToInstrument }
) {
  container.innerHTML = `
    <div class="select is-fullwidth is-small">
      <select class="setlist-select" disabled></select>
    </div>
    <div class="setlist-info-row">
      <div class="setlist-info help">${NO_DATASET_MESSAGE}</div>
      <button class="button is-small copy-setlist-button" type="button" disabled>Copy all to opposite</button>
    </div>
    <div class="filter-row">
      <input class="filter-input input is-small" type="text" placeholder="Filter / search..." disabled />
      <div class="buttons has-addons sort-buttons">
        <button class="button is-small sort-asc-button" type="button" disabled title="Physically reorders all 128 slots of this Set List by name, A to Z -- writes real bytes immediately, just like drag-and-drop. Empty slots move to the end. No undo.">A&#8594;Z</button>
        <button class="button is-small sort-desc-button" type="button" disabled title="Physically reorders all 128 slots of this Set List by name, Z to A -- writes real bytes immediately, just like drag-and-drop. Empty slots move to the end. No undo.">Z&#8594;A</button>
      </div>
    </div>
    <div class="entries-scroll">
      <table class="table is-fullwidth is-hoverable is-narrow setlist-table">
        ${colgroupHtml([
          1.3,   // #
          null,  // Song -- flexible
          1.3,   // Type
          3.5,   // Bank -- room for "I-C 000" plus an optional "(HD-1)"/"(EXi)" suffix (wraps onto a 2nd line if it still doesn't fit, see .bank-jump-button)
          1.3,   // Vol
        ])}
        <thead>
          <tr>
            <th title="Slot number -- background shows the slot's Color">#</th>
            <th class="col-song">Song</th>
            <th title="Program or Combi">Type</th>
            <th title="Bank / number within bank">Bank</th>
            <th>Vol</th>
          </tr>
        </thead>
        <tbody></tbody>
      </table>
      <div class="drop-indicator" hidden></div>
    </div>
  `;

  const setlistSelect = container.querySelector(".setlist-select");
  const infoEl = container.querySelector(".setlist-info");
  const copySetlistButton = container.querySelector(".copy-setlist-button");
  const filterInput = container.querySelector(".filter-input");
  const sortAscButton = container.querySelector(".sort-asc-button");
  const sortDescButton = container.querySelector(".sort-desc-button");
  const tbody = container.querySelector("tbody");
  const dropIndicator = container.querySelector(".drop-indicator");

  // The "insert before/after this row" drag gesture (dropZoneForEvent()
  // below) originally showed a `box-shadow` on the target `<tr>` itself --
  // it never actually rendered. Bulma's `.table` sets `border-collapse:
  // collapse`, and collapsed table rows don't paint a box-shadow at all in
  // any current browser (a well-known table-rendering limitation, not a
  // specificity/z-index bug -- the "on" gesture's `.drop-target` class
  // works fine because it uses `background`/`outline`, neither of which has
  // this limitation). Rather than fight `<tr>` styling further, this is one
  // floating `<div>` per pane, absolutely positioned against
  // `.entries-scroll` (`position: relative`, see style.css) and moved to
  // straddle the boundary above/below whichever row is currently the
  // insert target -- a real "this is where it lands" line, unaffected by
  // table layout quirks, and it naturally scrolls with the table since it's
  // a child of the same scrolling element rather than the table itself.
  function showDropIndicator(tr, zone) {
    dropIndicator.style.top = `${zone === "before" ? tr.offsetTop : tr.offsetTop + tr.offsetHeight}px`;
    dropIndicator.hidden = false;
  }
  function hideDropIndicator() {
    dropIndicator.hidden = true;
  }

  let setlists = [];        // [{index, name}], as returned by listSetlists()
  let currentSetlistIndex = -1;
  let entries = [];         // [{index, label}], as returned by getEntries()
  let filterText = "";
  // Which slots currently show the editor panel at all -- a slot's panel,
  // once open, stays open (showing all three Color/Comment/Volume accordion
  // headers) until explicitly dismissed via its own Close button, regardless
  // of how many of its sections are individually expanded/collapsed. This
  // split (panel-open vs. section-expanded, below) exists specifically so
  // "close everything" is one click on one button, not clicking every
  // column that happens to have something expanded -- the original design
  // (one <tr> per open type, closed by re-clicking the same column) read as
  // counterintuitive once there was no single place to close "all of it."
  let openPanels = new Set();
  // Multi-select for a future bulk drag-and-drop iteration (STATE.md's
  // RFC) -- ctrl/cmd+click toggles a row in/out of this set, visual only
  // for now, no bulk action wired up yet. Deliberately a SEPARATE concept
  // from openPanels above (which row's editor is open) -- a row can be
  // multi-selected without its editor being open, and vice versa.
  const multiSelected = new Set();
  // Which of an OPEN panel's two accordion sections are currently expanded
  // (showing content, not just a header) -- songIndex -> Set of
  // "general"|"comment". A type absent here (or the whole songIndex
  // missing) shows as a collapsed header, not as absent -- once a panel is
  // open both headers are always shown, see buildEditorRow(). Both
  // can be expanded on the SAME panel at once (per explicit request), and
  // independently across every OTHER row/pane too -- except the exact same
  // slot in another pane showing the same Set List of the same dataset,
  // which openSlotEditors below deliberately blocks (a real, not
  // hypothetical, setup -- see its own comment).
  let expandedSections = new Map();
  // One shared raw-bytes cache per row with an open panel, keyed by
  // songIndex -- Comment/Color/Volume sections on the same panel all
  // read/write through the same cached Uint8Array (see commitSlotBytes())
  // rather than each fetching (and each other's writes silently discarding)
  // their own copy -- see STATE.md for why Comment was retrofitted onto
  // this same raw-byte path once multi-open made that a real correctness
  // risk, not just a style choice. Populated lazily in getSlotBytes(),
  // dropped once the panel closes.
  let slotBytesCache = new Map();
  // Same idea as slotBytesCache, but for a slot's separate 28-byte SDB1
  // NAME record (General section's name input) -- a completely different
  // chunk/stride from the 542-byte SBK1 params record above, see
  // PcgFile.h's nameRecordBytes() doc comment for why they need independent
  // caches/fetches rather than being one buffer.
  let nameBytesCache = new Map();

  // Fetches (once per row) and caches this row's raw 542-byte SBK1 record.
  async function getSlotBytes(entry) {
    if (slotBytesCache.has(entry.index)) return slotBytesCache.get(entry.index);
    const result = await window.getSongRecordBytes(getDatasetId(), currentSetlistIndex, entry.index);
    if (!result.ok) {
      log(`[Pane ${paneId}] ${result.error}`);
      return null;
    }
    const bytes = Uint8Array.from(result.bytes);
    slotBytesCache.set(entry.index, bytes);
    return bytes;
  }

  // Fetches (once per row) and caches this row's raw 28-byte SDB1 name record.
  async function getNameBytes(entry) {
    if (nameBytesCache.has(entry.index)) return nameBytesCache.get(entry.index);
    const result = await window.getNameRecordBytes(getDatasetId(), currentSetlistIndex, entry.index);
    if (!result.ok) {
      log(`[Pane ${paneId}] ${result.error}`);
      return null;
    }
    const bytes = Uint8Array.from(result.bytes);
    nameBytesCache.set(entry.index, bytes);
    return bytes;
  }

  // Opens a slot's editor panel if it isn't already open (fetching bytes/
  // codecs and acquiring the cross-pane lock first -- see openSlotEditors'
  // own comment for the race that guards against), then toggles ONE
  // section's own expanded state -- called both by a column click (#/Vol/
  // Song-Type on the main row) and by clicking a section's own accordion
  // header once the panel is already open; both are exactly the same
  // action; "make sure the panel is open, then toggle this one section."
  // Does NOT close the panel even if this collapses its last expanded
  // section -- see closePanel() for the one explicit way to do that.
  async function openSection(entry, type) {
    const key = slotEditorKey(getDatasetId(), currentSetlistIndex, entry.index);

    if (!openPanels.has(entry.index)) {
      // Refuse to open if another pane already has this exact slot open --
      // see openSlotEditors' own comment for the race this avoids. A toast
      // (not just the persistent status bar) because this is a click that
      // visibly did nothing -- without an in-the-moment popup, it just
      // looks broken rather than deliberately blocked.
      const owner = openSlotEditors.get(key);
      if (owner != null && owner !== paneId) {
        showToast(`This Set List slot is already being edited in Pane ${owner}.`);
        return;
      }

      const [bytes, nameBytes] = await Promise.all([getSlotBytes(entry), getNameBytes(entry), loadSlotCodecs()]);
      if (bytes == null || nameBytes == null) return;  // already logged the error

      openPanels.add(entry.index);
      openSlotEditors.set(key, paneId);
    }

    const sections = expandedSections.get(entry.index) || new Set();
    if (sections.has(type)) sections.delete(type);
    else sections.add(type);
    expandedSections.set(entry.index, sections);
    renderRows();
  }

  // The one explicit way to fully dismiss a slot's editor panel -- its own
  // Close button (buildEditorRow()), never a column click. Drops every bit
  // of per-slot editor state (expanded sections, cached bytes, cross-pane
  // lock) in one go.
  function closePanel(entry) {
    const key = slotEditorKey(getDatasetId(), currentSetlistIndex, entry.index);
    openPanels.delete(entry.index);
    expandedSections.delete(entry.index);
    slotBytesCache.delete(entry.index);
    nameBytesCache.delete(entry.index);
    if (openSlotEditors.get(key) === paneId) openSlotEditors.delete(key);
    renderRows();
  }

  // Releases every cross-pane edit-lock THIS pane currently holds (see
  // openSlotEditors above) -- called anywhere openPanels is about to be
  // cleared wholesale (Set List switch, dataset switch), so a lock never
  // outlives the editor state it was protecting. Brute-force scan is fine
  // here: at most a couple of entries are ever locked at once.
  function releaseAllSlotLocks() {
    for (const [key, owner] of openSlotEditors) {
      if (owner === paneId) openSlotEditors.delete(key);
    }
  }

  // Writes `newBytes` back via the bridge, updates the shared cache and
  // this row's own already-in-memory display fields (comment/color/volume)
  // straight from the codecs (no refetch needed), then re-renders -- the
  // single write path every editor below (Comment/Color/Volume) commits
  // through, so none of them can silently discard another's in-flight edit
  // on the same row.
  async function commitSlotBytes(entry, newBytes) {
    const result = await window.putSongRecordBytes(getDatasetId(), currentSetlistIndex, entry.index, Array.from(newBytes));
    if (!result.ok) {
      log(`[Pane ${paneId}] ${result.error}`);
      return;
    }
    slotBytesCache.set(entry.index, newBytes);
    const codecs = await loadSlotCodecs();
    const decodedComment = codecs.decodeSetlistComment(newBytes);
    entry.comment = decodedComment.comment;
    entry.fontSize = decodedComment.fontSize;
    entry.color = codecs.decodeSlotColor(newBytes);
    entry.volume = codecs.decodeSlotVolume(newBytes);
    renderRows();
  }

  // Same idea as commitSlotBytes() above, but for the slot's separate
  // 28-byte SDB1 name record (General section's name input) -- its own
  // write path since it's a completely different record/bridge call, see
  // nameBytesCache's own comment.
  async function commitNameBytes(entry, newBytes) {
    const result = await window.putNameRecordBytes(getDatasetId(), currentSetlistIndex, entry.index, Array.from(newBytes));
    if (!result.ok) {
      log(`[Pane ${paneId}] ${result.error}`);
      return;
    }
    nameBytesCache.set(entry.index, newBytes);
    const codecs = await loadSlotCodecs();
    entry.label = codecs.decodeSlotName(newBytes);
    renderRows();
  }

  // ONE editor <tr> per slot with an open panel (not one per open section)
  // -- a single "you're now editing this slot" panel inside one
  // <td colSpan=5> (a real <table> again, so a plain colSpan instead of a
  // grid-column hack), containing both General/Comment accordion headers
  // (buildAccordionSection() below) plus one Close button that's the only
  // way to fully dismiss it (closePanel()). Two sections, not three --
  // General bundles Name/Color/Volume together (per explicit request: they
  // were three separate look-alike accordion items, General groups the
  // slot's quick-glance identity/appearance fields into one). Column
  // clicks (#/Vol -> General, Song/Type -> Comment) still expand/collapse
  // their own section in place (openSection()) -- same as clicking that
  // section's own header once the panel is already open -- they just don't
  // close the panel itself. Nothing to clean up on close: a closed slot's
  // <tr> simply isn't rebuilt into the next renderRows() pass
  // (tbody.innerHTML = "" there), so it's fully gone from the DOM the
  // moment it leaves openPanels -- no separate removal step.
  function buildEditorRow(entry, sections) {
    const editorTr = document.createElement("tr");
    editorTr.classList.add("editor-row");
    const td = document.createElement("td");
    td.colSpan = 5;  // #, Song, Type, Bank, Vol

    const panel = document.createElement("div");
    panel.className = "editor-panel";

    const closeBtn = document.createElement("button");
    closeBtn.type = "button";
    closeBtn.className = "editor-panel-close";
    closeBtn.textContent = "✕";
    closeBtn.title = "Close";
    closeBtn.addEventListener("click", () => closePanel(entry));
    panel.appendChild(closeBtn);

    const stack = document.createElement("div");
    stack.className = "editor-row-stack";
    stack.appendChild(buildAccordionSection(entry, "general", "General", sections.has("general"), buildGeneralSection));
    stack.appendChild(buildAccordionSection(entry, "comment", "Comment", sections.has("comment"), buildCommentSection));
    panel.appendChild(stack);

    td.appendChild(panel);
    editorTr.appendChild(td);
    return editorTr;
  }

  // One collapsible accordion item: a clickable header (chevron + title,
  // title suffixed with the current value for General/Comment so a
  // collapsed section still shows a useful at-a-glance summary) that
  // toggles this one section via openSection(), and -- only when `isOpen`
  // -- its content (`buildContent(entry)`, one of the section-builders
  // below).
  function buildAccordionSection(entry, type, title, isOpen, buildContent) {
    const section = document.createElement("div");
    section.className = "accordion-section" + (isOpen ? " is-open" : "");

    const header = document.createElement("button");
    header.type = "button";
    header.className = "accordion-header";
    header.addEventListener("click", () => openSection(entry, type));

    const chevron = document.createElement("span");
    chevron.className = "accordion-chevron";
    chevron.textContent = isOpen ? "▾" : "▸";

    const titleEl = document.createElement("span");
    titleEl.className = "accordion-title";
    titleEl.textContent = title + accordionSummary(type, entry);

    header.append(chevron, titleEl);
    section.appendChild(header);

    if (isOpen) {
      const content = document.createElement("div");
      content.className = "accordion-content";
      content.appendChild(buildContent(entry));
      section.appendChild(content);
    }

    return section;
  }

  // At-a-glance summary appended to a (possibly collapsed) accordion
  // header -- Comment's own text has no short summary worth showing (could
  // be long/multi-line), but its Font size is a quick, useful hint.
  function accordionSummary(type, entry) {
    if (type === "general") return ` — ${setlistColorName(entry.color)}, Vol ${entry.volume}`;
    if (type === "comment") return entry.fontSize ? ` — Font ${entry.fontSize}` : "";
    return "";
  }

  const COMMENT_EDITOR_MIN_ROWS = 10;

  // Grows the textarea to fit its content (no internal scrollbar) but never
  // shrinks below COMMENT_EDITOR_MIN_ROWS worth of height -- self-contained
  // and safe to call at any time, in any order: resetting to `height: auto`
  // first falls back to the <textarea rows=N> attribute's own intrinsic
  // sizing (browser-native, and correctly reflects whatever font-size is
  // CURRENTLY applied, since `rows` is a text-metric-relative attribute,
  // not a fixed pixel count) -- so `clientHeight` measured right after that
  // reset already IS "N rows at the current font-size," fresh every call,
  // no separately cached floor to go stale when font-size changes later.
  function autoSizeCommentEditor(textarea) {
    textarea.style.height = "auto";
    const naturalRowsHeight = textarea.clientHeight;
    textarea.style.height = `${Math.max(textarea.scrollHeight, naturalRowsHeight)}px`;
  }

  // Real per-Font-size relative character-width ratios, confirmed against
  // actual Kronos hardware 2026-08-06 (STATE.md's Group 4 word-wrap probe --
  // tokens/line at each size, using a fixed "NN " 3-char token: XS=40,
  // S=35, M=30, L=19, XL=8). Expressed here as a font-size SCALE FACTOR
  // relative to S (the byte encoding's own true baseline/default value),
  // not as an absolute pixels-per-line target -- this pane's own width is
  // user-resizable (stretching the main window resizes it, unlike the
  // Kronos's fixed physical screen), so there's no single "characters per
  // line" this editor could match exactly regardless of window size. A
  // font-size RATIO stays correct at any pane width: CSS text reflow
  // naturally re-wraps at whatever width happens to be available, and the
  // RELATIVE size difference between Kronos Font sizes (XL always renders
  // ~4.4x wider per character than S, on any screen) is what this
  // reproduces -- not literal Kronos pixel/character counts. Also worth
  // remembering: the confirmed ratio came from one specific numbered-token
  // test string, not general prose, so real Comment text won't wrap at
  // identical character positions, just at a proportionally similar rate.
  const KRONOS_FONT_SIZE_TOKENS_PER_LINE = { XS: 40, S: 35, M: 30, L: 19, XL: 8 };
  // The project owner confirmed S at 13px reads "nearly identical" to a
  // real Kronos specifically when the Comment textarea itself is ~600px
  // wide -- since the Kronos's own screen is a FIXED physical width but
  // this pane's isn't, that 13px is only correct AT that one reference
  // width. commentEditorFontSizePx() below scales it by the textarea's
  // CURRENT width relative to this reference, so resizing the window keeps
  // the preview calibrated instead of freezing at whatever the confirmed
  // match happened to be measured at.
  const COMMENT_EDITOR_BASE_PX = 13;
  const COMMENT_EDITOR_REFERENCE_WIDTH_PX = 600;

  function commentEditorFontSizePx(fontSize, containerWidthPx) {
    const tokens = KRONOS_FONT_SIZE_TOKENS_PER_LINE[fontSize] || KRONOS_FONT_SIZE_TOKENS_PER_LINE.S;
    const baseSize = COMMENT_EDITOR_BASE_PX * (KRONOS_FONT_SIZE_TOKENS_PER_LINE.S / tokens);
    const widthScale = (containerWidthPx || COMMENT_EDITOR_REFERENCE_WIDTH_PX) / COMMENT_EDITOR_REFERENCE_WIDTH_PX;
    return baseSize * widthScale;
  }

  // Applies the Font-size-driven, width-scaled font-size (see
  // commentEditorFontSizePx()) AND re-measures height (a larger font wraps
  // into more lines even with unchanged text) -- called on a Font size
  // button click AND on every resize (see the ResizeObserver in
  // buildCommentSection() below), so the preview updates live before
  // Apply and stays accurate as the window/pane is stretched, not just at
  // whatever width the editor happened to have when it was opened.
  function applyCommentEditorFontSize(textarea, fontSize) {
    textarea.style.fontSize = `${commentEditorFontSizePx(fontSize, textarea.clientWidth)}px`;
    autoSizeCommentEditor(textarea);
  }

  // Comment editor -- click a row's Song/Type cell to expand a multiline-
  // editable panel below it (same click-to-expand-inline interaction as
  // DIY-MIDI-METRONOME's EDITOR trigger list), plus a Font size button bar
  // (XS/S/M/L/XL) above it -- brought back 2026-08-06 after being dropped
  // when Comment editing moved from the old standalone setlist-comment.js
  // component (which had both) into this accordion section (which
  // initially only ported the textarea). Both share one Apply button --
  // unlike Color/Volume, Font size does NOT commit on click. It's packed
  // into the SAME bytes/codec call as the Comment text (setlist-comment.js
  // encodes both together), and this section's free text needs a deliberate
  // confirm step (unlike Color/Volume's discrete immediate-apply options);
  // committing Font size immediately would trigger commitSlotBytes()'s
  // renderRows(), which rebuilds this whole section from the just-written
  // bytes -- silently discarding whatever comment text was typed but not
  // yet Applied. Font size clicks instead just update local `pendingFontSize`
  // and the buttons' own active state, exactly like the textarea's own
  // uncommitted draft, until Apply writes both together.
  //
  // Apply reads/writes through the shared raw-bytes cache via setlist-
  // comment.js's real codec (see commitSlotBytes()) -- retrofitted off the
  // old struct-only setComment() bridge call once Color/Volume being
  // independently open on the same row made that a real data-loss risk,
  // not just an inconsistency (see STATE.md). A plain <textarea> is used
  // (not contenteditable) so \r\n line breaks Just Work via normal
  // textarea semantics. Sized to show at least 10 lines, growing to fit
  // longer content instead of scrolling.
  function buildCommentSection(entry) {
    const bytes = slotBytesCache.get(entry.index);
    const decoded = resolvedSlotCodecs.decodeSetlistComment(bytes);
    let pendingFontSize = decoded.fontSize;

    const textarea = document.createElement("textarea");
    textarea.className = "comment-editor";
    textarea.rows = COMMENT_EDITOR_MIN_ROWS;
    textarea.value = decoded.comment;
    textarea.placeholder = "Comment (supports line breaks)...";
    textarea.addEventListener("input", () => autoSizeCommentEditor(textarea));

    const fontBar = document.createElement("div");
    fontBar.className = "comment-fontsize-bar";
    const fontButtons = {};
    for (const size of resolvedSlotCodecs.FONT_SIZES) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "button is-small fontsize-button" + (size === pendingFontSize ? " is-link" : "");
      btn.textContent = size;
      btn.title = `Font size ${size}`;
      btn.addEventListener("click", () => {
        pendingFontSize = size;
        for (const [otherSize, otherBtn] of Object.entries(fontButtons)) {
          otherBtn.classList.toggle("is-link", otherSize === size);
        }
        // Live preview -- updates the textarea's own font-size (and
        // re-wraps/re-measures its height) immediately on click, same as
        // Color's immediate-apply elsewhere, even though the byte write
        // itself still waits for Apply (see this function's own doc
        // comment for why).
        applyCommentEditorFontSize(textarea, size);
      });
      fontButtons[size] = btn;
      fontBar.appendChild(btn);
    }

    const applyBtn = document.createElement("button");
    applyBtn.type = "button";
    applyBtn.className = "button is-small is-link";
    applyBtn.textContent = "Apply";
    applyBtn.addEventListener("click", async () => {
      const current = slotBytesCache.get(entry.index);
      const encoded = resolvedSlotCodecs.encodeSetlistComment(current, {
        comment: textarea.value,
        fontSize: pendingFontSize,
      });
      await commitSlotBytes(entry, encoded);
    });

    const textRow = document.createElement("div");
    textRow.className = "comment-editor-text-row";
    textRow.append(textarea, applyBtn);

    // Deliberately observes textRow (a flex row whose WIDTH is driven by
    // the pane's own layout), not the textarea itself -- observing the
    // textarea directly would create a feedback loop, since
    // applyCommentEditorFontSize() changes the textarea's own font-size
    // AND height, which would immediately re-trigger the very observer
    // watching it. textRow's width never changes as a side effect of the
    // textarea's height growing, so no such loop here. Also covers the
    // very first layout (ResizeObserver fires once automatically as soon
    // as an observed element gets its initial layout box), so no separate
    // "wait for paint" step is needed the way the old minHeightPx capture
    // once required.
    const commentResizeObserver = new ResizeObserver(() => {
      applyCommentEditorFontSize(textarea, pendingFontSize);
    });
    commentResizeObserver.observe(textRow);

    const section = document.createElement("div");
    section.className = "comment-editor-section";
    section.append(fontBar, textRow);
    return section;
  }

  // General section -- click a row's # or Vol cell to expand it. Bundles
  // three quick-glance fields that used to be three separate accordion
  // items (per explicit request: Name/Color/Volume all changed together
  // often enough that three look-alike headers just added clicks). Name
  // first (per explicit request), then Color, then Volume.
  function buildGeneralSection(entry) {
    const section = document.createElement("div");
    section.className = "general-editor-section";
    section.append(buildNameField(entry), buildColorSwatches(entry), buildVolumeSlider(entry));
    return section;
  }

  // Name editor -- a slot's name lives in its own separate 28-byte SDB1
  // record (nameBytesCache/commitNameBytes), not the SBK1 params record
  // Color/Volume below write into. Commits on blur or Enter, not on every
  // keystroke (matches Volume's release-not-drag commit point below) --
  // a text field mid-edit isn't a "confirmed" value the way a color click
  // or a slider release is. `maxLength` gives immediate, native browser-
  // level validation against the format's real 24-byte cap
  // (setlist-slot-name.js's NAME_MAX_LENGTH) -- backed up by the encoder's
  // own truncation either way, so an oversized paste can't slip through.
  function buildNameField(entry) {
    const labelEl = document.createElement("label");
    labelEl.className = "name-label";
    labelEl.textContent = "Name";

    const input = document.createElement("input");
    input.type = "text";
    input.className = "input is-small name-input";
    input.maxLength = resolvedSlotCodecs.NAME_MAX_LENGTH;
    input.value = entry.label || "";
    input.placeholder = "(empty)";

    const counter = document.createElement("span");
    counter.className = "name-length-counter";
    const updateCounter = () => {
      counter.textContent = `${input.value.length}/${resolvedSlotCodecs.NAME_MAX_LENGTH}`;
    };
    updateCounter();
    input.addEventListener("input", updateCounter);

    async function commitIfChanged() {
      if (input.value === (entry.label || "")) return;  // nothing to write
      const current = nameBytesCache.get(entry.index);
      const encoded = resolvedSlotCodecs.encodeSlotName(current, input.value);
      await commitNameBytes(entry, encoded);
    }
    input.addEventListener("blur", commitIfChanged);
    input.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter") input.blur();  // triggers commitIfChanged() above
    });

    const row = document.createElement("div");
    row.className = "name-editor-row";
    row.append(labelEl, input, counter);
    return row;
  }

  // Color editor -- one of 16 real Kronos Set List colors
  // (SETLIST_COLOR_NAMES/_HEX above). Immediate-apply, no Apply button (per
  // explicit request): each click decodes/re-encodes through
  // setlist-slot-params.js's masked Color codec and commits straight away.
  function buildColorSwatches(entry) {
    const section = document.createElement("div");
    section.className = "color-editor-section";
    for (let color = 1; color <= 16; color++) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "button is-small color-swatch-button" + (color === entry.color ? " is-link" : "");
      btn.style.background = setlistColorHex(color);
      btn.title = setlistColorName(color);
      btn.addEventListener("click", async () => {
        const current = slotBytesCache.get(entry.index);
        const encoded = resolvedSlotCodecs.encodeSlotColor(current, color);
        await commitSlotBytes(entry, encoded);
      });
      section.appendChild(btn);
    }
    return section;
  }

  // Volume editor -- a 0-127 slider. Immediate-apply on release (per
  // explicit request: "color on press and slider on release") -- `change`
  // fires on release/blur, not on every `input` tick during drag, so this
  // doesn't spam a write per pixel of drag movement. The live numeric label
  // still updates on every `input` tick for feedback, it just doesn't
  // commit until `change`.
  function buildVolumeSlider(entry) {
    const labelEl = document.createElement("label");
    labelEl.className = "volume-label";
    labelEl.textContent = "Volume";

    const slider = document.createElement("input");
    slider.type = "range";
    slider.min = "0";
    slider.max = "127";
    slider.value = String(entry.volume);
    slider.className = "volume-slider";

    const valueEl = document.createElement("span");
    valueEl.className = "volume-value";
    valueEl.textContent = String(entry.volume);

    slider.addEventListener("input", () => {
      valueEl.textContent = slider.value;
    });
    slider.addEventListener("change", async () => {
      const current = slotBytesCache.get(entry.index);
      const encoded = resolvedSlotCodecs.encodeSlotVolume(current, Number(slider.value));
      await commitSlotBytes(entry, encoded);
    });

    const section = document.createElement("div");
    section.className = "volume-editor-section";
    section.append(labelEl, slider, valueEl);
    return section;
  }

  // Shared by every per-cell click handler below (row fallback, Bank/Vol/#
  // cells) -- ctrl/cmd+click toggles multi-select and pre-empts whatever
  // that cell would normally do, so building a selection never has the
  // side effect of also opening an editor. Returns true if it handled the
  // click (caller should stop there, not run its own action).
  function handleMultiSelectClick(ev, songIndex) {
    if (!ev.ctrlKey && !ev.metaKey) return false;
    if (multiSelected.has(songIndex)) multiSelected.delete(songIndex);
    else multiSelected.add(songIndex);
    renderRows();
    return true;
  }

  // Same technique as library.js's own scrollRowBelowHeader() -- this
  // table has a sticky <thead> too, so a plain scrollIntoView({block:
  // "center"}) can leave a row still partly hidden under it, especially
  // near the top of the list. Kept as its own small copy here rather than
  // shared with library.js -- these two files don't otherwise share
  // rendering helpers, and per CLAUDE.md's duplicate-code norm this is
  // flagged to the project rather than unilaterally factored out.
  function scrollRowBelowHeader(row) {
    const scrollBox = row.closest(".entries-scroll");
    if (!scrollBox) return;
    const table = row.closest("table");
    const header = table ? table.querySelector("thead") : null;
    const headerHeight = header ? header.getBoundingClientRect().height : 0;
    const rowRect = row.getBoundingClientRect();
    const boxRect = scrollBox.getBoundingClientRect();
    const currentOffset = rowRect.top - boxRect.top;
    const desiredOffset = headerHeight + 8;  // a small gap below the header
    scrollBox.scrollTo({ top: scrollBox.scrollTop + currentOffset - desiredOffset, behavior: "smooth" });
  }

  function renderRows() {
    const needle = filterText.trim().toLowerCase();
    // Filter is the only remaining display-only view state here -- sorting
    // used to be applied the same way (a client-side .sort() on top of
    // this), but that's gone: a Kronos has no notion of "sorted"
    // independent of a slot's actual record position (see PcgFile::
    // sortSetlist()'s own doc comment), so the A-Z/Z-A buttons below now
    // commit a REAL reorder through the bridge and this just re-renders
    // whatever refreshEntries() reads back afterward, in physical slot
    // order like always.
    const visible = needle ? entries.filter((e) => e.label.toLowerCase().includes(needle)) : entries;

    tbody.innerHTML = "";
    for (const entry of visible) {
      const tr = document.createElement("tr");
      tr.draggable = true;
      tr.dataset.index = String(entry.index);
      // Bulma's own `tr.is-selected` highlight (real Bulma table state, not
      // a hand-rolled class) marks whether this row's editor panel is open
      // at all -- stays highlighted even if every section inside it is
      // individually collapsed, since the panel (and its Close button)
      // are still showing.
      if (openPanels.has(entry.index)) tr.classList.add("is-selected");
      if (multiSelected.has(entry.index)) tr.classList.add("multi-selected");

      // Row-level fallback: Song/Type cells (and anywhere else not handled
      // by a cell's own listener below) open/toggle the Comment section,
      // same as before this row grew per-column routing. # and Vol get
      // their own listeners (with stopPropagation) further down, inside
      // the paramsFound block -- there's nothing meaningful to toggle for
      // either without real slot params. ctrl/cmd+click is intercepted
      // first (both modifiers, for cross-platform muscle memory) to toggle
      // multi-select instead -- no editor-opening side effect while
      // building a selection.
      tr.addEventListener("click", (ev) => {
        if (handleMultiSelectClick(ev, entry.index)) return;
        openSection(entry, "comment");
      });

      tr.addEventListener("dragstart", (ev) => {
        draggedFromDatasetId = getDatasetId();
        ev.dataTransfer.setData(
          "application/json",
          JSON.stringify({ datasetId: getDatasetId(), setlistIndex: currentSetlistIndex, index: entry.index })
        );
        ev.dataTransfer.effectAllowed = "copyMove";
      });
      tr.addEventListener("dragend", () => {
        draggedFromDatasetId = null;
        hideDropIndicator();
      });
      tr.addEventListener("dragover", (ev) => {
        // Cross-dataset Setlist copies are rejected outright (see app.js's
        // onDropEntry) -- don't even show this row as a valid drop target,
        // and don't preventDefault() so the browser's own "not allowed"
        // cursor takes over and no `drop` event fires here at all.
        if (draggedFromDatasetId != null && draggedFromDatasetId !== getDatasetId()) {
          tr.classList.remove("drop-target");
          hideDropIndicator();
          return;
        }
        ev.preventDefault();
        ev.dataTransfer.dropEffect = "move";
        const zone = dropZoneForEvent(tr, ev);
        tr.classList.toggle("drop-target", zone === "on");
        if (zone === "on") hideDropIndicator();
        else showDropIndicator(tr, zone);
      });
      tr.addEventListener("dragleave", () => {
        tr.classList.remove("drop-target");
        hideDropIndicator();
      });
      tr.addEventListener("drop", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();  // don't also trigger the pane's file-drop handler
        const zone = dropZoneForEvent(tr, ev);
        tr.classList.remove("drop-target");
        hideDropIndicator();
        const raw = ev.dataTransfer.getData("application/json");
        if (!raw) return;
        onDropEntry(JSON.parse(raw), {
          datasetId: getDatasetId(),
          setlistIndex: currentSetlistIndex,
          index: entry.index,
          zone,
        });
      });

      const idxTd = document.createElement("td");
      idxTd.textContent = kronosNumber(entry.index);  // 0-based, matching the Kronos's own 000-127 numbering

      const labelTd = document.createElement("td");
      labelTd.className = "col-song";
      labelTd.textContent = entry.label || "(empty)";
      if (entry.comment) {
        labelTd.classList.add("has-comment");
        labelTd.title = entry.comment;
      }
      // The Combi/Program's OWN name (cross-referenced from the instrument
      // bank, see README.md) -- shown always, even when it matches the
      // slot's own label, so it's visible that the lookup actually found
      // something (per explicit request, not just shown on mismatch).
      if (entry.instrumentName) {
        const instrumentEl = document.createElement("div");
        instrumentEl.className = "instrument-name";
        instrumentEl.textContent = entry.instrumentName;
        labelTd.appendChild(instrumentEl);
      } else if (entry.paramsFound && !entry.instrumentName && entry.label) {
        // A real assignment exists (bank/number), but nothing in this
        // file's instrument banks matched it -- almost always a GM/GM2
        // reference (fixed content the file doesn't store, bank >=20 for
        // Program / >=14 for Combi -- see README.md), occasionally a
        // corrupt bank value in that one slot. Shown explicitly rather
        // than silently blank, so it doesn't read as "lookup is broken."
        const instrumentEl = document.createElement("div");
        instrumentEl.className = "instrument-name instrument-name-unknown";
        instrumentEl.textContent = `(${formatBankNumber(entry)} not in this file -- likely GM)`;
        labelTd.appendChild(instrumentEl);
      }

      const typeTd = document.createElement("td");
      const bankTd = document.createElement("td");
      const volTd = document.createElement("td");

      if (entry.paramsFound) {
        typeTd.textContent = entry.isProgram ? "Prog" : "Combi";
        // A button, not plain text: clicking it jumps to this exact
        // Program/Combi in the pane's own Programs/Combis category instead
        // of toggling this row's Comment editor (stopPropagation below
        // keeps the row's own click handler from also firing).
        const bankType = entry.isProgram ? getProgramBankType(entry.bank) : undefined;
        const bankButton = document.createElement("button");
        bankButton.type = "button";
        bankButton.className = "button is-small bank-jump-button";  // Bulma button, same look as the topbar's Open button
        bankButton.textContent = formatBankNumber(entry, bankType);
        bankButton.title = `Show ${entry.isProgram ? "Program" : "Combi"} ${formatBankNumber(entry, bankType)} in this pane's Programs/Combis view`;
        bankButton.addEventListener("click", (ev) => {
          ev.stopPropagation();
          if (handleMultiSelectClick(ev, entry.index)) return;
          onJumpToInstrument({
            isProgram: entry.isProgram,
            bank: entry.bank,
            number: entry.number,
            from: { kind: "setlist", setlistIndex: currentSetlistIndex, songIndex: entry.index },
          });
        });
        bankTd.appendChild(bankButton);
        volTd.textContent = String(entry.volume);
        // Vol cell opens the General editor (Name/Color/Volume) instead of
        // the row's own Comment-editor fallback -- stopPropagation keeps
        // the row-level listener above from also firing (same pattern as
        // bankButton).
        volTd.addEventListener("click", (ev) => {
          ev.stopPropagation();
          if (handleMultiSelectClick(ev, entry.index)) return;
          openSection(entry, "general");
        });
        // Color used to be its own swatch column -- now shown as the "#"
        // cell's own background instead, freeing up a whole column. Real
        // Kronos color names now (SETLIST_COLOR_NAMES/_HEX above), not the
        // old synthetic hue-spread placeholder -- ordering still unconfirmed
        // against real hardware, see that array's own doc comment.
        idxTd.style.background = setlistColorHex(entry.color);
        idxTd.title = `Color ${entry.color} (${setlistColorName(entry.color)})`;
        // # cell opens the General editor too -- same stopPropagation
        // pattern as Vol.
        idxTd.addEventListener("click", (ev) => {
          ev.stopPropagation();
          if (handleMultiSelectClick(ev, entry.index)) return;
          openSection(entry, "general");
        });
      }

      // Hold Time dropped from this row (2026-08-04) -- moving to the
      // Comment editor panel later rather than showing it here; entry.holdTime
      // itself is untouched (still fetched/returned by getEntries()), just
      // not rendered as its own column for now.
      tr.append(idxTd, labelTd, typeTd, bankTd, volTd);
      tbody.appendChild(tr);

      // The editor panel, if this slot's is open -- see buildEditorRow()'s
      // own comment. Shows all three accordion headers regardless of which
      // are individually expanded, so an empty Set (every section
      // collapsed, panel still open pending its Close button) is valid.
      if (openPanels.has(entry.index)) {
        const sections = expandedSections.get(entry.index) || new Set();
        tbody.appendChild(buildEditorRow(entry, sections));
      }
    }
  }

  async function refreshEntries() {
    const datasetId = getDatasetId();
    if (datasetId == null || currentSetlistIndex < 0) {
      entries = [];
    } else {
      entries = await window.getEntries(datasetId, currentSetlistIndex);
    }
    renderRows();
  }

  // "Copy all to opposite" (setlist-info row) -- only meaningful once both
  // panes are pointed at the SAME dataset but showing two DIFFERENT Set
  // Lists (per direct request); copying a Set List onto itself is a no-op,
  // and cross-dataset copying raises the same physical bank/number
  // portability question app.js's onDropEntry() already documents for
  // single-slot copies. Recomputed whenever EITHER pane's selection changes
  // (onPaneSelectionChanged() below, datasets.js), not just this one's --
  // the opposite pane switching its own Set List must also flip this
  // button's state even though nothing here changed.
  function updateCopyButtonState() {
    const datasetId = getDatasetId();
    const opposite = getOpposite();
    const oppositeDatasetId = opposite ? opposite.getCurrentDatasetId() : null;
    const oppositeSetlistIndex = opposite ? opposite.getCurrentSetlistIndex() : -1;
    const eligible =
      datasetId != null &&
      oppositeDatasetId === datasetId &&
      currentSetlistIndex >= 0 &&
      oppositeSetlistIndex >= 0 &&
      currentSetlistIndex !== oppositeSetlistIndex;
    copySetlistButton.disabled = !eligible;
    copySetlistButton.title = eligible
      ? `Copy all 128 slots of this Set List onto the opposite pane's Set List ${kronosNumber(oppositeSetlistIndex)}, overwriting it.`
      : "Copy every slot of this Set List onto the opposite pane's -- only available when both panes show the same dataset with two different Set Lists selected.";
  }

  copySetlistButton.addEventListener("click", async () => {
    const datasetId = getDatasetId();
    const opposite = getOpposite();
    if (datasetId == null || !opposite) return;
    const oppositeSetlistIndex = opposite.getCurrentSetlistIndex();
    if (oppositeSetlistIndex < 0 || oppositeSetlistIndex === currentSetlistIndex) return;
    await onCopySetlist({ datasetId, setlistIndex: currentSetlistIndex }, { datasetId, setlistIndex: oppositeSetlistIndex });
  });

  onPaneSelectionChanged(updateCopyButtonState);

  function populateSetlistSelect() {
    setlistSelect.innerHTML = "";
    for (const s of setlists) {
      const opt = document.createElement("option");
      opt.value = String(s.index);
      opt.textContent = `${kronosNumber(s.index)}  ${s.name}`;
      setlistSelect.appendChild(opt);
    }
    setlistSelect.disabled = setlists.length === 0;
  }

  // Shared by the dropdown's own change handler and jumpToEntry() below --
  // both mean "show a different Set List in this pane," with the same
  // stale-state resets (an open editor panel/expanded section/cached bytes
  // belong to the OLD Set List's song at that index, not whatever now
  // shares that index in the new one).
  async function selectSetlist(setlistIndex) {
    currentSetlistIndex = setlistIndex;
    setlistSelect.value = String(setlistIndex);
    filterText = "";
    filterInput.value = "";
    openPanels.clear();  // don't carry an open editor panel over to a different Set List's song at the same index
    expandedSections.clear();
    slotBytesCache.clear();
    releaseAllSlotLocks();
    notifyPaneSelectionChanged();
    await refreshEntries();
  }

  setlistSelect.addEventListener("change", () => selectSetlist(Number(setlistSelect.value)));

  // Called by the shell (jumpToInstrument() in createPane(), after already
  // switching this pane to its "setlist" category) when a Program's usage-
  // row Set List reference (library.js's buildUsageRow()) is clicked --
  // selects the target Set List first if this pane isn't already showing
  // it, then opens+scrolls to that exact slot, same as clicking the row
  // directly. Mirrors library.js's own jumpToEntry(), the Program/Combi
  // equivalent of this same "jump and land on the row" pattern.
  async function jumpToEntry(setlistIndex, songIndex) {
    if (currentSetlistIndex !== setlistIndex) {
      await selectSetlist(setlistIndex);
    } else if (filterText) {
      filterText = "";
      filterInput.value = "";
      renderRows();
    }
    const entry = entries.find((e) => e.index === songIndex);
    if (!entry) return;
    await openSection(entry, "comment");
    const row = tbody.querySelector(`[data-index="${songIndex}"]`);
    if (row) scrollRowBelowHeader(row);
  }

  filterInput.addEventListener("input", () => {
    filterText = filterInput.value;
    renderRows();
  });

  // A-Z/Z-A: a REAL, immediate reorder of every one of this Set List's 128
  // slots -- not a display-only convenience. A Kronos has no notion of
  // "sorted" independent of a slot's own record position (confirmed
  // against Korg's own SetList.txt, see docs/content/format/index.md §3.2 and
  // PcgFile::sortSetlist()'s own doc comment), so there's no lighter-
  // weight thing to change here than the actual bytes -- same "writes
  // immediately, no undo" contract as drag-and-drop and Copy all to
  // opposite. `sortSetlistEntries()` does the whole 128-slot rewrite in
  // one native call; refreshEntries() afterward just re-reads the file's
  // now-actually-different physical order, same as any other edit.
  async function sortSetlistNow(ascending) {
    const datasetId = getDatasetId();
    if (datasetId == null || currentSetlistIndex < 0) return;
    const result = await window.sortSetlistEntries(datasetId, currentSetlistIndex, ascending);
    if (!result.ok) {
      log(result.error);
      return;
    }
    await refreshEntries();
    showToast(`Set List sorted ${ascending ? "A-Z" : "Z-A"} -- slot order rewritten.`);
  }
  sortAscButton.addEventListener("click", () => sortSetlistNow(true));
  sortDescButton.addEventListener("click", () => sortSetlistNow(false));

  // Called by the shell whenever its shared dataset selection changes --
  // either a fresh dataset (displayName given) or the dataset this pane was
  // showing having been closed elsewhere (displayName omitted/getDatasetId()
  // already null by the time this runs).
  async function onDatasetChanged(displayName) {
    const datasetId = getDatasetId();
    if (datasetId == null) {
      setlists = [];
      currentSetlistIndex = -1;
      populateSetlistSelect();
      entries = [];
      filterText = "";
      filterInput.value = "";
      filterInput.disabled = true;
      sortAscButton.disabled = true;
      sortDescButton.disabled = true;
      openPanels.clear();
      expandedSections.clear();
      slotBytesCache.clear();
      releaseAllSlotLocks();
      infoEl.textContent = NO_DATASET_MESSAGE;
      renderRows();
      notifyPaneSelectionChanged();
      return;
    }

    setlists = await window.listSetlists(datasetId);
    populateSetlistSelect();
    currentSetlistIndex = setlists.length > 0 ? setlists[0].index : -1;
    if (currentSetlistIndex >= 0) setlistSelect.value = String(currentSetlistIndex);

    // Switching a pane to a different dataset must not leak state from
    // whatever was showing before -- a stale filter could hide entries in
    // the new dataset entirely, and a stale open panel would reopen on
    // whatever song now happens to share that index (and its raw-bytes
    // cache would be for the WRONG dataset's slot entirely).
    filterText = "";
    filterInput.value = "";
    openPanels.clear();
    expandedSections.clear();
    slotBytesCache.clear();
    releaseAllSlotLocks();

    infoEl.textContent = `Showing ${displayName}`;
    filterInput.disabled = setlists.length === 0;
    sortAscButton.disabled = setlists.length === 0;
    sortDescButton.disabled = setlists.length === 0;
    notifyPaneSelectionChanged();
    await refreshEntries();
    log(`[Pane ${paneId}] Showing ${displayName}`);
  }

  return { refreshEntries, onDatasetChanged, getCurrentSetlistIndex: () => currentSetlistIndex, jumpToEntry };
}

function createPane(paneId, root, { onDropEntry, onDropProgram, onCopySetlist, getOpposite, log, showToast }) {
  root.innerHTML = `
    <div class="pane-header">
      <div class="pane-header-row dataset-select-row">
        <div class="select is-small is-fullwidth dataset-select-wrap">
          <select class="dataset-select"></select>
        </div>
        <button class="button is-small save-file-button" type="button" disabled title="Save this pane's dataset -- its current in-memory bytes, including any unsaved edits -- to a .PCG/.SNG file via a native Save dialog">Save As...</button>
      </div>
      <div class="pane-header-row tabs-row">
        <div class="tabs is-boxed is-small pane-category-tabs">
          <ul>
            <li class="is-active" data-category="setlist"><a>Setlist</a></li>
            <li data-category="programs"><a>Programs</a></li>
            <li data-category="combis"><a>Combis</a></li>
            <li data-category="duplicates"><a>Duplicates</a></li>
            <li data-category="internals"><a>Internals</a></li>
          </ul>
        </div>
        <div class="buttons has-addons nav-history-buttons">
          <button class="button is-small nav-back-button" type="button" disabled title="Back to the previous jump target">&#8592;</button>
          <button class="button is-small nav-forward-button" type="button" disabled title="Forward to the next jump target">&#8594;</button>
        </div>
      </div>
    </div>
    <div class="pane-category-content" data-category-panel="setlist"></div>
    <div class="pane-category-content" data-category-panel="library" hidden></div>
    <div class="pane-category-content" data-category-panel="internals" hidden></div>
  `;

  const datasetSelect = root.querySelector(".dataset-select");
  const saveFileButton = root.querySelector(".save-file-button");
  const navBackButton = root.querySelector(".nav-back-button");
  const navForwardButton = root.querySelector(".nav-forward-button");
  const categoryTabs = root.querySelectorAll(".pane-category-tabs li");
  const setlistContainer = root.querySelector('[data-category-panel="setlist"]');
  const libraryContainer = root.querySelector('[data-category-panel="library"]');
  const internalsContainer = root.querySelector('[data-category-panel="internals"]');

  let currentDatasetId = null;  // which loaded dataset this pane is showing, if any -- decoupled from paneId
  let knownDatasets = [];       // last list from onDatasetsChanged(), so the dataset-select's change handler can resolve a displayName without a bridge round-trip
  let currentCategory = "setlist";  // "setlist" | "programs" | "combis" | "duplicates" | "internals"

  function getCurrentDatasetId() {
    return currentDatasetId;
  }

  // bank -> "HD-1"/"EXi", refreshed on every dataset change -- shared by
  // both content renderers (Setlist's Bank-jump button, the Programs
  // bank-filter row) so the small getProgramBankTypes() bridge call happens
  // once per dataset load, not once per renderer. Per-file data, never a
  // hardcoded table -- see PcgFile.h's ProgramBankType doc comment.
  let programBankTypes = new Map();

  function getProgramBankType(bank) {
    return programBankTypes.get(bank);
  }

  async function refreshProgramBankTypes() {
    programBankTypes = new Map();
    if (currentDatasetId == null) return;
    const entries = await window.getProgramBankTypes(currentDatasetId);
    for (const entry of entries) programBankTypes.set(entry.bank, entry.bankType);
  }

  // Category switching just toggles which container is visible -- no data
  // reload needed on its own, since both renderers already hold current
  // data from the last dataset change (see onDatasetChanged() below).
  // Shared between category-tab clicks and jumpToInstrument() below, so
  // there's one place that keeps the tab-active state and container
  // visibility in sync.
  function switchCategory(category) {
    // Bulma's tabs component puts "active" state on the <li> as `is-active`
    // (not a plain custom class), see style.css's dark-theme override.
    categoryTabs.forEach((t) => t.classList.toggle("is-active", t.dataset.category === category));
    currentCategory = category;

    const isSetlist = category === "setlist";
    const isInternals = category === "internals";
    const isLibrary = !isSetlist && !isInternals;

    setlistContainer.hidden = !isSetlist;
    libraryContainer.hidden = !isLibrary;
    internalsContainer.hidden = !isInternals;
    if (isLibrary) libraryPanels.showPanel(category);
  }

  categoryTabs.forEach((tab) => {
    tab.addEventListener("click", () => switchCategory(tab.dataset.category));
  });

  // Applies one jump target without touching the nav-history stack below --
  // the shared "actually do the jump" step used both by a fresh jump
  // (jumpToInstrument/jumpToSetlistEntry, which push afterward) and by the
  // Back/Forward buttons replaying a past entry (which must NOT push, or
  // Back would immediately shove its own destination back onto the front
  // of Forward).
  function applyNavEntry(entry) {
    if (entry.kind === "instrument") {
      switchCategory(entry.isProgram ? "programs" : "combis");
      libraryPanels.jumpToEntry(entry.isProgram, entry.bank, entry.number);
    } else {
      switchCategory("setlist");
      setlistPanel.jumpToEntry(entry.setlistIndex, entry.songIndex);
    }
  }

  // Per-pane jump history for the Back/Forward buttons beside the category
  // tabs -- every jumpToInstrument()/jumpToSetlistEntry() call below (a
  // Setlist Bank button, a Combi Timbre bank-jump button, or a Program
  // usage-row Set List/Combi reference) pushes both where the click
  // happened (its own `from`, supplied by the caller -- see each of their
  // own call sites in library.js/this file) AND the destination it jumped
  // to. Recording `from` explicitly (rather than just switching category)
  // is what makes Back land back on the EXACT originating row -- e.g. the
  // Combi a Timbre's bank-jump button was clicked from -- not just its
  // category tab. Capped at 10 entries total (per explicit request) --
  // oldest entries drop off the front past that, same idea as a real
  // browser's history not growing unbounded. A fresh jump made after going
  // Back truncates whatever was still ahead, same as a real browser:
  // there's no redoing a "forward" branch a new jump just replaced.
  const NAV_HISTORY_LIMIT = 10;
  let navHistory = [];
  let navIndex = -1;

  function sameLocation(a, b) {
    if (!a || !b || a.kind !== b.kind) return false;
    return a.kind === "instrument"
      ? a.isProgram === b.isProgram && a.bank === b.bank && a.number === b.number
      : a.setlistIndex === b.setlistIndex && a.songIndex === b.songIndex;
  }

  function updateNavButtons() {
    navBackButton.disabled = navIndex <= 0;
    navForwardButton.disabled = navIndex < 0 || navIndex >= navHistory.length - 1;
  }

  function pushHistoryEntry(entry) {
    navHistory.push(entry);
    if (navHistory.length > NAV_HISTORY_LIMIT) navHistory.shift();
    navIndex = navHistory.length - 1;
  }

  // `from` is omitted only when there's nowhere meaningful to record it
  // (there isn't a call site like that today, but stays optional rather
  // than assumed). Skips re-recording `from` if it's already exactly where
  // the stack's current top sits -- avoids a redundant duplicate entry
  // when jumping again from the same row without ever clicking Back.
  function pushNavHistory(from, to) {
    navHistory = navHistory.slice(0, navIndex + 1);
    const top = navHistory[navHistory.length - 1];
    if (from && !sameLocation(top, from)) pushHistoryEntry(from);
    pushHistoryEntry(to);
    updateNavButtons();
  }

  // Called on every dataset switch (loadDataset()/resetToEmpty() below) --
  // a bank/number or setlist/song reference from one dataset means nothing
  // in another, so past jump targets can't survive the switch.
  function resetNavHistory() {
    navHistory = [];
    navIndex = -1;
    updateNavButtons();
  }

  navBackButton.addEventListener("click", () => {
    if (navIndex <= 0) return;
    navIndex--;
    applyNavEntry(navHistory[navIndex]);
    updateNavButtons();
  });
  navForwardButton.addEventListener("click", () => {
    if (navIndex < 0 || navIndex >= navHistory.length - 1) return;
    navIndex++;
    applyNavEntry(navHistory[navIndex]);
    updateNavButtons();
  });

  // Called when a Setlist row's Bank button, OR a Combi Timbre row's own
  // bank-jump button (library.js's buildTimbreRow(), only shown for a
  // confirmed Timbre bank code -- see formatTimbreRef()), is clicked --
  // switches this pane to its Programs/Combis category and expands+scrolls
  // to that exact entry, instead of just showing a bank/number label.
  // Always this SAME pane, never the opposite one, by construction --
  // `jumpToInstrument` is a closure over this one createPane() call's own
  // `switchCategory`/`libraryPanels`, and is handed to both
  // createSetlistPanel() and createLibraryPanels() as their own
  // `onJumpToInstrument` prop. `from` is the location the click itself
  // happened at (each call site knows its own containing row -- e.g. the
  // Setlist song, or the Combi a Timbre reference lives on -- see their own
  // call sites), recorded into nav history so Back returns to that EXACT
  // row rather than just its category.
  function jumpToInstrument({ isProgram, bank, number, from }) {
    const to = { kind: "instrument", isProgram, bank, number };
    applyNavEntry(to);
    pushNavHistory(from, to);
  }

  // The reverse direction: a Program's usage-row Set List reference
  // (library.js's buildUsageRow(), only shown once the row's "Set List
  // usage" list is expanded) jumping to its Setlist entry -- switches this
  // pane to its Setlist category and hands off to setlistPanel's own
  // jumpToEntry(), same "always this SAME pane" guarantee as
  // jumpToInstrument() above. Same `from`-recording as jumpToInstrument().
  function jumpToSetlistEntry({ setlistIndex, songIndex, from }) {
    const to = { kind: "setlist", setlistIndex, songIndex };
    applyNavEntry(to);
    pushNavHistory(from, to);
  }

  const setlistPanel = createSetlistPanel(setlistContainer, {
    paneId,
    log,
    showToast,
    onDropEntry,
    onCopySetlist,
    getDatasetId: getCurrentDatasetId,
    getOpposite,
    getProgramBankType,
    onJumpToInstrument: jumpToInstrument,
  });
  const libraryPanels = createLibraryPanels(libraryContainer, {
    log,
    getDatasetId: getCurrentDatasetId,
    getProgramBankType,
    onDropProgram,
    onJumpToInstrument: jumpToInstrument,
    onJumpToSetlist: jumpToSetlistEntry,
  });
  const internalsPanel = createInternalsPanel(internalsContainer, {
    getDatasetId: getCurrentDatasetId,
    log,
  });

  // Displays an already-open dataset in this pane -- called both right after
  // a fresh file drop (a new dataset) and when the dataset-select's change
  // handler switches to a dataset another pane already opened. Notifies
  // ALL THREE content renderers regardless of which category is currently
  // visible, so switching back to a hidden category later still shows
  // fresh data instead of whatever was last loaded.
  async function loadDataset(datasetId, displayName) {
    currentDatasetId = datasetId;
    datasetSelect.value = String(datasetId);
    saveFileButton.disabled = false;
    resetNavHistory();
    await refreshProgramBankTypes();
    await setlistPanel.onDatasetChanged(displayName);
    await libraryPanels.onDatasetChanged();
    await internalsPanel.onDatasetChanged();
  }

  // Back to the "nothing selected" state -- used both for the dataset-select's
  // own placeholder option and when the dataset this pane was showing gets
  // closed from elsewhere (another pane).
  async function resetToEmpty() {
    currentDatasetId = null;
    saveFileButton.disabled = true;
    resetNavHistory();
    await refreshProgramBankTypes();
    await setlistPanel.onDatasetChanged();
    await libraryPanels.onDatasetChanged();
    await internalsPanel.onDatasetChanged();
  }

  // "Save As..." (pane header, beside the dataset selector) -- writes this
  // pane's dataset's CURRENT in-memory bytes (including any edits made this
  // session: Setlist reorders/copy-overs, Color/Volume/Comment writes,
  // Program copies, ...) to a file via a native Save dialog. Built
  // specifically to let a sorted/reordered Set List actually be tested on
  // real Kronos hardware -- the app's own A-Z/Z-A sort buttons are display-
  // only (STATE.md/docs/content/format/index.md §3.2), so seeing a REAL reorder reflected
  // on the hardware needs an actual drag-and-drop move/copy (which does
  // write real bytes) followed by saving those bytes out to a file the unit
  // can load. `PcgFile::save()`'s own doc comment covers why no separate
  // serialization step is needed -- every edit already lands directly in
  // the retained buffer this writes out verbatim.
  saveFileButton.addEventListener("click", async () => {
    if (currentDatasetId == null) return;
    const result = await window.saveFileDialog(currentDatasetId);
    if (result.cancelled) return;  // user closed the dialog -- not an error, nothing to log
    if (!result.ok) {
      log(result.error);
      return;
    }
    showToast(`Saved to ${result.path}`);
    log(`[Pane ${paneId}] Saved to ${result.path}`);
  });

  datasetSelect.addEventListener("change", async () => {
    const value = datasetSelect.value;
    if (!value) {
      await resetToEmpty();
      return;
    }
    const datasetId = Number(value);
    const dataset = knownDatasets.find((d) => d.datasetId === datasetId);
    await loadDataset(datasetId, dataset ? dataset.displayName : "");
  });

  // Fires immediately with whatever's already cached, and again whenever any
  // pane (or another pane's Library categories) opens/closes a dataset --
  // keeps this pane's selector (and its own currently-shown dataset, if it
  // just got closed elsewhere) in sync without needing a bespoke pub/sub
  // per action.
  onDatasetsChanged((datasets) => {
    knownDatasets = datasets;
    populateDatasetSelect(datasetSelect, datasets, currentDatasetId != null ? String(currentDatasetId) : "");
    const stillOpen = currentDatasetId != null && datasets.some((d) => d.datasetId === currentDatasetId);
    if (currentDatasetId != null && !stillOpen) resetToEmpty();
  });

  // Exposed so app.js's onDropEntry knows which pane(s) are currently
  // showing an affected dataset after a move/copy, and need refreshing --
  // could be 0, 1, or both panes, e.g. when both point at the same dataset.
  // `loadDataset`/`isEmpty` are exposed for app.js's single, global Open
  // button (see index.html's topbar) to pick a pane to land a newly-opened
  // dataset in, now that opening is no longer a per-pane action.
  return {
    refreshEntries: setlistPanel.refreshEntries,
    // Exposed so app.js's onDropProgram can re-fetch this pane's Programs
    // table after a Program copy lands in it -- same "which pane(s) need
    // refreshing" need as refreshEntries above, just for the library view.
    refreshLibrary: libraryPanels.refresh,
    getCurrentDatasetId,
    // Exposed so the OPPOSITE pane's "copy all to opposite" button
    // (createSetlistPanel's getOpposite() callback, app.js wires it) can
    // read which Set List this pane currently shows, without this pane
    // needing any reference back to the shell that created it.
    getCurrentSetlistIndex: setlistPanel.getCurrentSetlistIndex,
    loadDataset,
    isEmpty: () => currentDatasetId == null,
  };
}
