// One Norton-Commander-style pane: a thin shell (one dataset selector, one
// category navbar -- Setlist/Programs/Combis/Duplicates, Global later once
// GLB1 is ever parsed) around three content renderers -- pane-setlist-
// editor.js's createSetlistPanel(), createLibraryPanels() below (the shared
// Programs/Combis/Duplicates tab coordinator: filter input, bank-filter/
// select-control skeleton, load()/onDatasetChanged() orchestration across
// pane-program-editor.js's createProgramsPanel()/createDuplicatesPanel() and
// pane-combi-editor.js's createCombisPanel()), and internals.js's
// createInternalsPanel() -- so a pane can show any category of any already-
// open dataset, independent of the other pane. Two shells are created by
// app.js ("A" and "B") so Setlist slots can be dragged between them.
//
// This file also holds the utilities every category renderer shares -- bank
// name tables, formatBankNumber()/kronosNumber()/
// colgroupHtml()/NO_DATASET_MESSAGE, and (2026-08-14, moved here when
// Programs/Duplicates split off what was then library.js)
// renderBankFilterRow()/createSelectControlRow()/scrollRowBelowHeader()/
// filterByName()/bankCell()/refCell()/menuCell()/showRowContextMenu(), needed
// by both pane-program-editor.js and pane-combi-editor.js. Setlist-editor-only pieces (Set List slot Color
// palette, the raw-record codec loader, createSetlistPanel() itself) moved
// out to pane-setlist-editor.js the same day pane.js itself first split off
// createPane() from this file's old, much larger everything-in-one-file
// shape -- pane-combi-editor.js (renamed from library.js once nothing
// Program-related was left in it) completes the same direction.
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
// `bankName`, see pane-combi-editor.js's formatTimbreRef()) -- so a ground-truth name
// stored/logged in full still renders shortened, consistent with
// COMBI_BANK_NAMES/PROGRAM_BANK_NAMES above.
function abbreviateBankName(name) {
  return name.replace(/^INT-/, "I-").replace(/^USER-/, "U-");
}

function kronosNumber(n) {
  return String(n).padStart(3, "0");
}

// Mirrors kronos::ProgramBankType (PcgFile.h: `enum class ProgramBankType {
// Hd1, Exi }`) -- indexed by the RAW enum value the bridge now sends
// (EditorBridge stopped formatting this to a string 2026-08-15; naming a
// value for display is a JS/encoder-layer job, not native C++'s -- see
// STATE.md). Kept as a plain array, not a switch, matching
// PROGRAM_BANK_NAMES/COMBI_BANK_NAMES's own convention just above.
const PROGRAM_BANK_TYPE_NAMES = ["HD-1", "EXi"];

function programBankTypeName(bankType) {
  return PROGRAM_BANK_TYPE_NAMES[bankType] ?? String(bankType);
}

// Indexed by ProgramInfo::exiAlgorithmType's raw 0-9 value (EditorBridge's
// programToValue()) -- Korg's own explicit legend, confirmed directly
// against docs/external/KORG/Prog_EXi.txt's opening lines and byte-verified
// against the real Init-Program-HD1.raw/-EXi.raw templates (2026-08-16, see
// ProgramFields::exiAlgorithmType's doc comment in ProgramDecoder.h for the
// full derivation). Same "C++ decodes the raw byte, JS names it" split as
// PROGRAM_BANK_TYPE_NAMES above -- only meaningful when a Program's own
// bankType is Exi; a caller checks that before using this.
const EXI_ALGORITHM_NAMES = [
  "Off", "HD-1", "AL-1", "CX-3", "STR-1", "MS-20EX", "PolysixEX", "MOD-7", "SGX-2", "EP-1",
];

function exiEngineName(algorithmType) {
  return EXI_ALGORITHM_NAMES[algorithmType] ?? String(algorithmType);
}

// Mirrors PcgFile.cpp's looksLikeEmptyCombiName() -- case-insensitive
// substring match, catching both Korg's own literal "Init Combi" and this
// app's own vacated-slot rename ("- Init Combi -", see
// window.moveCombiToBank()'s own backend doc comment). The single shared
// definition (2026-08-15) -- previously inlined independently at 5 call
// sites across pane-combi-editor.js/mock_bridge.js (a plain regex in one
// file, a `.toLowerCase().includes(...)` in the other), all doing the same
// check slightly differently.
function looksLikeEmptyCombiName(name) {
  return /init combi/i.test(name || "");
}

// Mirrors PcgFile.cpp's looksLikeEmptyProgramName() -- a genuinely
// untouched Program slot on real Kronos hardware is named Korg's own
// factory "Init Program"/"Init EXi Program", not a blank string (docs/
// content/format/index.md §5.5). The single shared definition (2026-08-15)
// -- previously defined independently, with an identical body, in both
// combi-cross-dataset-panel.js and mock_bridge.js.
function looksLikeEmptyProgramName(name) {
  if (!name) return true;
  const lower = name.toLowerCase();
  return lower === "init exi program" || lower.includes("init program");
}

// A Set List slot, unlike a Program or Combi, genuinely has NO factory
// placeholder name to match against -- a real, never-touched slot's SDB1
// name record is a blank string, CONFIRMED end to end against a real
// 47.9MB backup (docs/content/format/index.md §3.2: "Unpopulated song
// slots are empty strings"). So this only needs the blank check PLUS this
// app's own "- Init Setlist -" reset marker (pane-setlist-editor.js's
// resetEntry()) -- there's no Korg-authored text to also recognize the way
// looksLikeEmptyProgramName()/looksLikeEmptyCombiName() above do. Used by
// the drag-and-drop "copy over" gesture (app.js's onDropEntry, this row's
// own classify() in pane-setlist-editor.js) to refuse overwriting a slot
// that's actually in use.
function looksLikeEmptySetlistName(name) {
  return !name || /init setlist/i.test(name);
}

// `bankType` (a raw kronos::ProgramBankType value, see
// PROGRAM_BANK_TYPE_NAMES above) is optional and only ever shown for
// Programs -- Combis have no engine type of their own, so it's ignored
// whenever entry.isProgram is false, regardless of what's passed. Checked
// with `!= null`, not truthiness -- bankType can legitimately be 0 (Hd1),
// which is falsy in JS.
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
  return entry.isProgram && bankType != null ? `${label} (${programBankTypeName(bankType)})` : label;
}

// Builds a <colgroup> from 12-based column-grid fractions -- Bulma's own
// grid (.column.is-1 .. is-12) is a 12-column system; this applies the same
// convention to table <col> widths, as percentages (e.g. [1,7,1,2,1] ->
// 8.33%/58.33%/8.33%/16.67%/8.33%, summing to 100%) so the table scales
// proportionally with its container instead of being pixel-locked. Paired
// with style.css's `table-layout: fixed` (shared by every real .table in
// this app -- Setlist here, Programs/Duplicates in pane-program-editor.js,
// Combis in pane-combi-editor.js) --
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

// scrollIntoView({block:"center"}) can leave a row still partly hidden under
// a table's sticky <thead> (especially rows near the top of the list, which
// can't be centered past the header at all) -- this instead computes the
// exact scroll position so the row lands just below the header, using
// getBoundingClientRect() (robust regardless of the table/tbody nesting
// between the row and its scrolling ancestor). Shared by pane-program-
// editor.js and pane-combi-editor.js's jumpToEntry() methods --
// pane-setlist-editor.js
// keeps its own small copy rather than using this one (per CLAUDE.md's
// duplicate-code norm, flagged rather than unilaterally merged: it predates
// this shared version and scrolls a DIFFERENT container class, `.entries-
// scroll` vs `.library-body`).
function scrollRowBelowHeader(row) {
  const scrollBox = row.closest(".library-body");
  if (!scrollBox) return;
  // row.closest("table"), not scrollBox.querySelector("thead") -- Programs'
  // and Combis' tables can both exist in the DOM at once (one just
  // hidden), so querying from scrollBox could grab the wrong table's
  // header height entirely.
  const table = row.closest("table");
  const header = table ? table.querySelector("thead") : null;
  const headerHeight = header ? header.getBoundingClientRect().height : 0;
  const rowRect = row.getBoundingClientRect();
  const boxRect = scrollBox.getBoundingClientRect();
  const currentOffset = rowRect.top - boxRect.top;
  const desiredOffset = headerHeight + 8;  // a small gap below the header
  scrollBox.scrollTo({ top: scrollBox.scrollTop + currentOffset - desiredOffset, behavior: "smooth" });
}

// A small "local menu" for a table row -- one Bulma dropdown-content
// positioned at the click point, mounted on document.body (outlives the
// row's own re-renders) and torn down on the next click/Escape/scroll, same
// dismiss model a native context menu has. Shared by the Programs and
// Combis tables' own "Reset entry" actions -- originally hand-rolled once in
// pane-program-editor.js as a right-click-only openRowMenu(), extracted
// here once Combis needed the identical scaffolding (2026-09-04), then
// called from menuCell()'s own button click too once right-click/Ctrl+click
// alone turned out not to be reliably discoverable (same day -- see
// menuCell()'s own comment).
//
//   ev     the triggering event (contextmenu OR a plain button click both
//          work -- preventDefault()ed here, harmless either way)
//   items  [{ label, onSelect }], one per menu entry, in order
//
// Module-level `rowContextMenuEl` -- only one such menu is ever open at a
// time; opening a second closes the first, same as a real context menu.
let rowContextMenuEl = null;
function closeRowContextMenu() {
  if (!rowContextMenuEl) return;
  rowContextMenuEl.remove();
  rowContextMenuEl = null;
  document.removeEventListener("click", closeRowContextMenu);
  document.removeEventListener("keydown", onRowContextMenuKeydown);
  document.removeEventListener("scroll", closeRowContextMenu, true);
}
function onRowContextMenuKeydown(ev) {
  if (ev.key === "Escape") closeRowContextMenu();
}
function showRowContextMenu(ev, items) {
  ev.preventDefault();
  closeRowContextMenu();
  rowContextMenuEl = document.createElement("div");
  rowContextMenuEl.className = "dropdown-menu row-context-menu";
  // Bulma's real .dropdown-menu rule is `display: none; position: absolute;
  // top: 100%; ...` by default -- it only becomes visible as a DESCENDANT
  // of `.dropdown.is-active`/`.dropdown.is-hoverable:hover` (see
  // vendor/bulma.min.css). This menu isn't wrapped in that structure (it's
  // positioned freely at the click point, not anchored under a trigger
  // button), so `display`/`position` must be set inline here to override
  // the stylesheet -- found by right-clicking with the Web Inspector
  // attached: the event fired and the element existed in the DOM, just
  // invisible. `dropdown-menu`/`dropdown-content`/`dropdown-item` are kept
  // purely for Bulma's box/padding/hover styling, not its active-state
  // machinery.
  rowContextMenuEl.style.display = "block";
  rowContextMenuEl.style.position = "fixed";
  rowContextMenuEl.style.zIndex = "1000";
  const content = document.createElement("div");
  content.className = "dropdown-content";
  for (const { label, onSelect } of items) {
    const item = document.createElement("a");
    item.className = "dropdown-item";
    item.href = "#";
    item.textContent = label;
    item.addEventListener("click", (clickEv) => {
      clickEv.preventDefault();
      closeRowContextMenu();
      onSelect();
    });
    content.appendChild(item);
  }
  rowContextMenuEl.appendChild(content);
  document.body.appendChild(rowContextMenuEl);
  // Positioned AFTER appending, so its real rendered size is known --
  // `left: ev.clientX` alone (the original approach) grows the menu
  // rightward/downward from the click point with no regard for the
  // viewport edge, which reliably ran off the right side of the window once
  // the "⋯" trigger moved into the table's own rightmost column (reported
  // directly). Clamp so the whole menu stays inside the viewport instead --
  // opens normally near the left edge, opens leftward/upward near the
  // right/bottom edge.
  const margin = 8;
  const rect = rowContextMenuEl.getBoundingClientRect();
  const left = Math.max(margin, Math.min(ev.clientX, window.innerWidth - rect.width - margin));
  const top = Math.max(margin, Math.min(ev.clientY, window.innerHeight - rect.height - margin));
  rowContextMenuEl.style.left = `${left}px`;
  rowContextMenuEl.style.top = `${top}px`;
  // Deferred so this SAME contextmenu event's own bubble-up doesn't
  // immediately trigger the outside-click listener it just registered.
  setTimeout(() => {
    document.addEventListener("click", closeRowContextMenu);
    document.addEventListener("keydown", onRowContextMenuKeydown);
    document.addEventListener("scroll", closeRowContextMenu, true);
  }, 0);
}

// Draws one category's bank-filter button row: one toggle per bank name,
// enabled only if that bank actually has entries in the current dataset
// (`present`), pressed (Bulma's `is-link` -- there's no dedicated "toggle
// button" component, this is the idiomatic way to show a button as active)
// if currently in `filterSet`. Pure rendering -- only the click handler
// mutates `filterSet`, so calling this again (e.g. to reflect a
// programmatic change from jumpToEntry()) never resets a user's existing
// choices on its own. `getBankType(bank)` is optional -- only Programs has
// one (Combis have no engine type of their own, see PcgFile.h's
// ProgramBankType doc comment), so pane-combi-editor.js's own refreshBankButtons()
// just omits it and buttons stay plain.
function renderBankFilterRow(container, bankNames, present, filterSet, onToggle, getBankType) {
  container.innerHTML = "";
  bankNames.forEach((name, bank) => {
    const isPresent = present.has(bank);
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "button is-small bank-filter-button";
    const bankType = getBankType && getBankType(bank);
    btn.textContent = bankType != null ? `${name} (${programBankTypeName(bankType)})` : name;
    btn.disabled = !isPresent;
    if (isPresent && filterSet.has(bank)) btn.classList.add("is-link");
    btn.addEventListener("click", () => {
      if (filterSet.has(bank)) filterSet.delete(bank);
      else filterSet.add(bank);
      btn.classList.toggle("is-link");
      onToggle();
    });
    container.appendChild(btn);
  });
}

// "Select: None/All/Invert" -- a small reusable component (Programs and
// Combis both need one, hence a shared function rather than writing it
// twice) that bulk-mutates a bank-filter Set instead of toggling one bank at
// a time. Wired up ONCE per category (unlike renderBankFilterRow, which
// re-renders on every filter change to reflect each bank's pressed/
// unpressed state) -- these three buttons have no state of their own to
// reflect, so there's nothing to redraw after a click, only the bank
// buttons and table need refreshing (`onChange`).
//
// `getFilterSet`/`getPresent` are passed as functions, not the Set values
// directly -- each category's own refresh()/onDatasetChanged() REASSIGNS its
// bank-filter/present Sets to a brand-new Set on every dataset load rather
// than mutating the existing one in place, so a plain captured reference
// taken once at setup time would silently start operating on a stale,
// disconnected Set after the very first load.
function createSelectControlRow(container, { getPresent, getFilterSet, onChange }) {
  const actions = [
    ["None", (filterSet) => filterSet.clear()],
    [
      "All",
      (filterSet, present) => {
        filterSet.clear();
        present.forEach((bank) => filterSet.add(bank));
      },
    ],
    [
      "Invert",
      (filterSet, present) => {
        present.forEach((bank) => (filterSet.has(bank) ? filterSet.delete(bank) : filterSet.add(bank)));
      },
    ],
  ];
  for (const [label, apply] of actions) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "button is-small select-control-button";
    btn.textContent = label;
    btn.addEventListener("click", () => {
      apply(getFilterSet(), getPresent());
      onChange();
    });
    container.appendChild(btn);
  }
}

function filterByName(rows, needle) {
  if (!needle) return rows;
  return rows.filter((r) => (r.name || "").toLowerCase().includes(needle));
}

function bankCell(isProgram, bank, number, bankType) {
  const td = document.createElement("td");
  td.textContent = formatBankNumber({ isProgram, bank, number }, bankType);
  return td;
}

function refCell(text, unavailable) {
  const td = document.createElement("td");
  td.textContent = text;
  if (unavailable) {
    td.className = "col-refs-unavailable";
    td.title =
      "Combi usage is only confirmed correct for 8 individually-verified banks so far (INT-A..D, " +
      "USER-A/D/F/AA) -- other banks would risk a wrong count due to the Combi-internal bank " +
      "numbering not matching this bank's index everywhere. See docs/content/format/index.md's Combi Timbre " +
      "references section.";
  }
  return td;
}

// A row's own "more actions" menu -- a small "⋯" button in its own trailing
// column (always the table's LAST column, per direct request -- keeps it in
// a fixed spot regardless of how wide the Name column's own content runs),
// opening showRowContextMenu() with the given items. Shared by Programs'
// and Combis' own row-building (`pane-program-editor.js`/`pane-combi-
// editor.js` both call this the same way with their own action list --
// today just "Reset entry…" each, but this is the one place a THIRD table
// or a second action gets added, not a second hand-rolled copy of the
// button/column). `items` is `showRowContextMenu()`'s own `[{label,
// onSelect}]` shape, passed straight through.
function menuCell(items) {
  const td = document.createElement("td");
  td.className = "row-menu-cell";
  const btn = document.createElement("button");
  btn.type = "button";
  btn.className = "button is-small row-menu-button";
  btn.title = "More actions";
  btn.textContent = "⋯";  // horizontal ellipsis, same affordance the Duplicates panel already uses
  btn.addEventListener("click", (ev) => {
    ev.stopPropagation();  // don't also trigger the row's own click (expand/collapse)
    showRowContextMenu(ev, items);
  });
  td.appendChild(btn);
  return td;
}

// The shared Programs/Combis/Duplicates tab coordinator -- owns the ONE
// filter input and bank-filter/select-control button rows all three
// categories show/hide into (typing in the filter keeps filtering whichever
// tab is currently showing, since it's the same live input regardless of
// tab), which of the three is currently visible, and load()/
// onDatasetChanged() orchestration across all three (a write in any one of
// them -- e.g. resolving a duplicate Program, which can also repoint a Combi
// Timbre reference -- can affect the other two, so a full reload always
// refreshes all three rather than trying to track which ones actually
// changed). Delegates the actual rendering to createProgramsPanel()/
// createDuplicatesPanel() (pane-program-editor.js) and createCombisPanel()
// (pane-combi-editor.js) -- this function owns none of their state, only
// which DOM container each renders into and when to call it.
//
// Split out of what was then library.js (2026-08-14) -- this coordinator
// isn't Program- or Combi-specific, so it doesn't belong owned by either's
// file; see this file's own top-of-file comment.
function createLibraryPanels(
  root,
  {
    log,
    showToast,
    getDatasetId,
    getProgramBankType,
    onDropProgram,
    onSwapProgram,
    onMoveProgram,
    onJumpToInstrument,
    onJumpToSetlist,
    onRefreshOppositeLibrary,
    onSetlistRefsRepointed,
  }
) {
  root.innerHTML = `
    <input class="filter-input library-filter input is-small" type="text" placeholder="Filter / search..." />
    <div class="select-control-area">
      <div class="select-control-row" data-select-control="programs"></div>
      <div class="select-control-row" data-select-control="combis" hidden></div>
    </div>
    <div class="bank-filter-area">
      <div class="bank-filter-row" data-bank-filter="programs"></div>
      <div class="bank-filter-row" data-bank-filter="combis" hidden></div>
    </div>
    <div class="library-body">
      <div class="lib-panel" data-panel="programs">
        <div class="lib-panel-table" data-panel-table="programs"></div>
      </div>
      <div class="lib-panel" data-panel="combis" hidden>
        <div class="lib-panel-table" data-panel-table="combis"></div>
      </div>
      <div class="lib-panel" data-panel="duplicates" hidden></div>
    </div>
  `;

  const filterInput = root.querySelector(".library-filter");
  // The outer per-category divs -- used only for show/hide (showPanel()).
  const panels = {
    programs: root.querySelector('[data-panel="programs"]'),
    combis: root.querySelector('[data-panel="combis"]'),
    duplicates: root.querySelector('[data-panel="duplicates"]'),
  };
  // Where each table actually gets (re)built -- separate from `panels` above
  // so rebuilding a table on every render/filter keystroke doesn't also
  // wipe out that category's bank-filter buttons.
  const panelTables = {
    programs: root.querySelector('[data-panel-table="programs"]'),
    combis: root.querySelector('[data-panel-table="combis"]'),
  };
  // Bank-filter buttons live outside .library-body now (a sibling above it,
  // not inside the scrolling area) -- per explicit request, they were
  // scrolling out of view along with the table. showPanel() below toggles
  // which one (if either -- Duplicates has no bank filter at all) is shown,
  // mirroring `panels`' own show/hide.
  const bankFilterRows = {
    programs: root.querySelector('[data-bank-filter="programs"]'),
    combis: root.querySelector('[data-bank-filter="combis"]'),
  };
  // Select-none/all/invert row, one per category, sitting between the
  // Filter input and the bank-filter buttons -- same show/hide-by-
  // `currentTab` treatment as bankFilterRows (Duplicates has neither).
  const selectControlRows = {
    programs: root.querySelector('[data-select-control="programs"]'),
    combis: root.querySelector('[data-select-control="combis"]'),
  };

  let currentTab = "programs";
  const getFilterText = () => filterInput.value;

  const programsPanel = createProgramsPanel(
    { panelTable: panelTables.programs, bankFilterRow: bankFilterRows.programs, selectControlRow: selectControlRows.programs },
    { getDatasetId, getFilterText, getProgramBankType, onDropProgram, onSwapProgram, onMoveProgram, onJumpToSetlist, onJumpToInstrument, log, showToast }
  );

  const duplicatesPanel = createDuplicatesPanel(
    { panel: panels.duplicates },
    {
      getDatasetId,
      getFilterText,
      onJumpToInstrument,
      log,
      onRefreshOppositeLibrary,
      onNeedsFullReload: () => load(),
      onSetlistRefsRepointed,
    }
  );

  const combisPanel = createCombisPanel(
    { panelTable: panelTables.combis, bankFilterRow: bankFilterRows.combis, selectControlRow: selectControlRows.combis },
    {
      getDatasetId,
      getFilterText,
      getProgramBankType,
      findProgram: programsPanel.findProgram,
      onJumpToSetlist,
      onJumpToInstrument,
      log,
      onRefreshOppositeLibrary,
      onNeedsFullReload: () => load(),
      onSetlistRefsRepointed,
    }
  );

  function renderCurrentTab() {
    if (currentTab === "programs") programsPanel.render();
    else if (currentTab === "combis") combisPanel.render();
    else duplicatesPanel.render();
  }

  // Called by the shell when its own "Programs"/"Combis"/"Duplicates"
  // category button is clicked -- name is "programs"|"combis"|"duplicates".
  function showPanel(name) {
    currentTab = name;
    Object.entries(panels).forEach(([panelName, el]) => {
      el.hidden = panelName !== currentTab;
    });
    // Duplicates has no bank-filter row (or select-control row) at all --
    // both stay hidden there.
    Object.entries(bankFilterRows).forEach(([rowName, el]) => {
      el.hidden = rowName !== currentTab;
    });
    Object.entries(selectControlRows).forEach(([rowName, el]) => {
      el.hidden = rowName !== currentTab;
    });
    renderCurrentTab();
  }

  filterInput.addEventListener("input", () => renderCurrentTab());

  // `resetFilters` distinguishes two different reasons to call this:
  // - true (onDatasetChanged() below): a genuinely NEW dataset is showing --
  //   delegates to each sub-panel's own onDatasetChanged(), which resets
  //   that category's filter selections (the previous ones belong to a
  //   different file's banks and mean nothing here).
  // - false (the default -- every other caller, e.g. onDropProgram's
  //   cross-pane refresh in app.js, resolving a duplicate, or a Combi
  //   rearrange): the SAME dataset just changed underneath this same view --
  //   delegates to each sub-panel's own refresh(), which re-fetches but
  //   keeps whatever the user had filtered to.
  async function load({ resetFilters = false } = {}) {
    if (resetFilters) {
      await programsPanel.onDatasetChanged();
      await combisPanel.onDatasetChanged();
      await duplicatesPanel.onDatasetChanged();
    } else {
      await programsPanel.refresh();
      await combisPanel.refresh();
      await duplicatesPanel.refresh();
    }
    renderCurrentTab();
  }

  // Called by the shell (pane.js's createPane()) whenever its shared
  // dataset-select changes -- either a fresh selection, or the dataset this
  // pane was showing having been closed elsewhere (getDatasetId() will
  // already reflect that by the time this is called). Always a genuinely
  // new dataset -- see load()'s own doc comment for why this is the one
  // caller that resets filters. Returns load()'s own promise -- CORRECTED
  // 2026-08-15: this used to fire load() without awaiting or returning it,
  // so `await libraryPanels.onDatasetChanged()` (createPane()'s
  // loadDataset()/resetToEmpty()) resolved immediately rather than once
  // Programs/Combis/Duplicates had actually finished fetching. Harmless
  // as long as nothing after that await depended on the fetch being done
  // (the UI just caught up a moment later once load() itself finished and
  // called renderCurrentTab()) -- until updateCategoryTabAvailability()
  // (same session) became the first caller that actually needs the counts
  // to be current the instant it runs, and read stale (usually empty)
  // data as a result, reported directly: a real file with a genuine
  // Combi showed the Combis tab disabled.
  function onDatasetChanged() {
    return load({ resetFilters: true });
  }

  // Called by the shell after it's already switched to this Program's/
  // Combi's category (via showPanel()) -- delegates to that category's own
  // jumpToEntry(), which expands that exact entry's usage/Timbre row and
  // scrolls it into view, same as clicking the row directly. Clears any
  // active text filter first (shared across tabs, see this function's own
  // top-of-file comment) so it can't hide the entry being jumped to.
  function jumpToEntry(isProgram, bank, number) {
    filterInput.value = "";
    if (isProgram) programsPanel.jumpToEntry(bank, number);
    else combisPanel.jumpToEntry(bank, number);
  }

  // `refresh` is just `load` under a name that makes sense to an outside
  // caller -- exposed so app.js's onDropProgram can re-fetch this pane's
  // Programs table after a copy lands in it, without resetting bank
  // filters/expanded state any harder than onDatasetChanged already does.
  // `getCounts` exposed so createPane()'s own updateCategoryTabAvailability()
  // can disable the Programs/Combis/Duplicates tabs for a dataset with none
  // of a given kind at all.
  return {
    onDatasetChanged,
    showPanel,
    jumpToEntry,
    refresh: load,
    getCounts: () => ({
      programs: programsPanel.getProgramCount(),
      combis: combisPanel.getCombiCount(),
      duplicates: duplicatesPanel.getGroupCount(),
    }),
  };
}

function createPane(paneId, root, { onDropEntry, onDropProgram, onSwapProgram, onMoveProgram, onCopySetlist, getOpposite, log, showToast }) {
  root.innerHTML = `
    <div class="pane-header">
      <div class="pane-header-row dataset-select-row">
        <div class="select is-small is-fullwidth dataset-select-wrap">
          <select class="dataset-select"></select>
        </div>
        <button class="button is-small unload-dataset-button" type="button" disabled title="Free this dataset from memory -- warns first if it has unsaved changes">Unload</button>
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
  const unloadDatasetButton = root.querySelector(".unload-dataset-button");
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

  // Disables (greys out, blocks clicks on) any of the Setlist/Programs/
  // Combis/Duplicates tabs whose category has zero rows for the CURRENT
  // dataset -- checked after every dataset selection change (loadDataset()/
  // resetToEmpty() below), per direct request. Internals is deliberately
  // exempt (always relevant, even to show "0 of everything") and always
  // wins the fallback below, since it's the one category guaranteed
  // available even for a completely empty/no dataset. A dataset with
  // genuinely zero Set Lists is a real, confirmed case now (a sound-bank-
  // only PCG -- see STATE.md entry 38's SDB1-is-optional fix), and zero
  // Duplicate groups is the ORDINARY case for most real files, not an edge
  // case -- Programs/Combis being empty is rarer but not impossible (e.g. a
  // backup that only included Set Lists).
  function updateCategoryTabAvailability() {
    const counts =
      currentDatasetId == null
        ? { setlist: 0, programs: 0, combis: 0, duplicates: 0 }
        : { setlist: setlistPanel.getSetlistCount(), ...libraryPanels.getCounts() };
    categoryTabs.forEach((t) => {
      const category = t.dataset.category;
      if (category === "internals") return;
      t.classList.toggle("is-tab-disabled", counts[category] === 0);
    });
    // If the currently active tab just became disabled (its category lost
    // its last row, or this is a brand new/empty dataset), switch to the
    // first tab that's actually available -- Internals if nothing else is.
    const activeTab = [...categoryTabs].find((t) => t.dataset.category === currentCategory);
    if (activeTab && activeTab.classList.contains("is-tab-disabled")) {
      const firstAvailable = [...categoryTabs].find((t) => !t.classList.contains("is-tab-disabled"));
      switchCategory(firstAvailable ? firstAvailable.dataset.category : "internals");
    }
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
    tab.addEventListener("click", () => {
      if (tab.classList.contains("is-tab-disabled")) return;
      switchCategory(tab.dataset.category);
    });
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
  // own call sites in pane-program-editor.js/pane-combi-editor.js/this file)
  // AND the destination it jumped
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

  // Shift+click on any jump button sets `toOpposite: true` (each of the 5
  // call sites across pane-setlist-editor.js/pane-combi-editor.js/
  // pane-program-editor.js passes `ev.shiftKey` straight through) -- rather
  // than jumping within THIS pane, it makes the OPPOSITE pane show the
  // SAME dataset (switching it first via loadDataset() if it's currently
  // showing something else, or nothing at all) and jumps there instead,
  // leaving THIS pane exactly where it was. Recurses into the opposite
  // pane's OWN jumpToInstrument()/jumpToSetlistEntry() (exposed on its
  // returned interface below) WITHOUT `toOpposite` set, so that pane's own
  // switchCategory()/applyNavEntry()/nav-history all run normally against
  // itself -- this pane's own jump functions never reach into another
  // pane's internals directly, only through its public interface, same
  // discipline refreshOppositeLibrary()/refreshSetlistEverywhere() already
  // use elsewhere in this file.
  //
  // Shift+Cmd+click (`keepDataset`, from `ev.metaKey` at each call site,
  // only meaningful together with `toOpposite`) is a second, deliberately
  // different gesture (2026-08-14): jump to the SAME bank/number coordinate
  // in the opposite pane WITHOUT switching its dataset first, even if it's
  // showing something completely different. Real use case: a donated/
  // foreign PCG's Combi whose Timbres only reference default/GM-ish
  // Programs (so there's nothing distinctive to match by content) -- with
  // your own reference dataset already open in the opposite pane, this
  // peeks at whatever your own unit already has at that exact same
  // coordinate, which a dataset-switching jump can't do (it would replace
  // that reference dataset with the foreign one before jumping).
  async function jumpToOppositePane(to, from, keepDataset) {
    const opposite = getOpposite();
    if (!opposite) return;
    if (keepDataset) {
      if (opposite.getCurrentDatasetId() == null) {
        showToast("Opposite pane has no dataset open -- nothing to jump to there.");
        return;
      }
    } else if (opposite.getCurrentDatasetId() !== currentDatasetId) {
      const displayName = knownDatasets.find((d) => d.datasetId === currentDatasetId);
      await opposite.loadDataset(currentDatasetId, displayName ? displayName.displayName : "");
    }
    if (to.kind === "instrument") {
      opposite.jumpToInstrument({ isProgram: to.isProgram, bank: to.bank, number: to.number, from });
    } else {
      opposite.jumpToSetlistEntry({ setlistIndex: to.setlistIndex, songIndex: to.songIndex, from });
    }
  }

  // Called when a Setlist row's Bank button, OR a Combi Timbre row's own
  // bank-jump button (pane-combi-editor.js's buildTimbreRow(), only shown for a
  // confirmed Timbre bank code -- see formatTimbreRef()), is clicked --
  // switches this pane to its Programs/Combis category and expands+scrolls
  // to that exact entry, instead of just showing a bank/number label. This
  // SAME pane, never the opposite one, UNLESS `toOpposite` is set (see
  // jumpToOppositePane() above) -- `jumpToInstrument` is a closure over
  // this one createPane() call's own `switchCategory`/`libraryPanels`, and
  // is handed to both createSetlistPanel() and createLibraryPanels() as
  // their own `onJumpToInstrument` prop. `from` is the location the click
  // itself happened at (each call site knows its own containing row -- e.g.
  // the Setlist song, or the Combi a Timbre reference lives on -- see their
  // own call sites), recorded into nav history so Back returns to that
  // EXACT row rather than just its category.
  function jumpToInstrument({ isProgram, bank, number, from, toOpposite, keepOppositeDataset }) {
    const to = { kind: "instrument", isProgram, bank, number };
    if (toOpposite) {
      jumpToOppositePane(to, from, keepOppositeDataset);
      return;
    }
    applyNavEntry(to);
    pushNavHistory(from, to);
  }

  // The reverse direction: a Program's usage-row Set List reference
  // (pane-program-editor.js's buildUsageRow(), only shown once the row's "Set List
  // usage" list is expanded) jumping to its Setlist entry -- switches this
  // pane to its Setlist category and hands off to setlistPanel's own
  // jumpToEntry(), same "always this SAME pane, unless toOpposite" guarantee
  // as jumpToInstrument() above. Same `from`-recording as jumpToInstrument().
  function jumpToSetlistEntry({ setlistIndex, songIndex, from, toOpposite, keepOppositeDataset }) {
    const to = { kind: "setlist", setlistIndex, songIndex };
    if (toOpposite) {
      jumpToOppositePane(to, from, keepOppositeDataset);
      return;
    }
    applyNavEntry(to);
    pushNavHistory(from, to);
  }

  // Refreshes the opposite pane's Library tables (Programs/Combis/
  // Duplicates) if it happens to be showing the SAME dataset -- a write
  // made through THIS pane's own bridge call (e.g. resolving a duplicate)
  // only refreshes ITS OWN library view via load(); the opposite pane's
  // separate in-memory programs/combis/duplicateGroups state has no other
  // way to learn the underlying file just changed. Same "which pane(s)
  // need refreshing" idea as app.js's onDropProgram(), just reached via
  // getOpposite() (already available here for "copy Set List to opposite")
  // instead of iterating every pane from the shell.
  async function refreshOppositeLibrary(datasetId) {
    const opposite = getOpposite();
    if (opposite && opposite.getCurrentDatasetId() === datasetId) await opposite.refreshLibrary();
  }

  // Refreshes the Setlist view in THIS pane AND the opposite pane (if it's
  // showing the same dataset) -- unlike a Program copy (never repoints an
  // existing Set List reference, since its target is always an empty slot,
  // see onDropProgram in app.js), a Combi swap/move-within-bank/move-to-
  // bank CAN repoint real Set List references. Without this, the Setlist
  // tab's own cached entries (bank/number/instrumentName per slot) go stale
  // exactly the way library.js's old Programs/Combis/Duplicates state did
  // before refreshOppositeLibrary() above was added (reported and fixed the
  // same way, entry 33 in STATE.md) -- just for the Setlist panel this
  // time, and in BOTH panes, not just the opposite one: THIS pane's own
  // Setlist tab is exactly as stale as the opposite pane's after a write
  // made from THIS pane's own Combis tab. `setlistPanel` is referenced here
  // before its own declaration below purely textually -- by the time this
  // function is actually CALLED (async, after a drag-and-drop), `setlistPanel`
  // is long since assigned; only synchronous access before assignment would
  // be a problem.
  async function refreshSetlistEverywhere(datasetId) {
    await setlistPanel.refreshEntries();
    const opposite = getOpposite();
    if (opposite && opposite.getCurrentDatasetId() === datasetId) await opposite.refreshEntries();
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
    showToast,
    getDatasetId: getCurrentDatasetId,
    getProgramBankType,
    onDropProgram,
    onSwapProgram,
    onMoveProgram,
    onJumpToInstrument: jumpToInstrument,
    onJumpToSetlist: jumpToSetlistEntry,
    onRefreshOppositeLibrary: refreshOppositeLibrary,
    onSetlistRefsRepointed: refreshSetlistEverywhere,
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
    unloadDatasetButton.disabled = false;
    resetNavHistory();
    await refreshProgramBankTypes();
    await setlistPanel.onDatasetChanged(displayName);
    await libraryPanels.onDatasetChanged();
    await internalsPanel.onDatasetChanged();
    updateCategoryTabAvailability();
  }

  // Back to the "nothing selected" state -- used both for the dataset-select's
  // own placeholder option and when the dataset this pane was showing gets
  // closed from elsewhere (another pane).
  async function resetToEmpty() {
    currentDatasetId = null;
    saveFileButton.disabled = true;
    unloadDatasetButton.disabled = true;
    resetNavHistory();
    await refreshProgramBankTypes();
    await setlistPanel.onDatasetChanged();
    await libraryPanels.onDatasetChanged();
    await internalsPanel.onDatasetChanged();
    updateCategoryTabAvailability();
  }

  // "Unload" (pane header, beside the dataset selector) -- frees this
  // dataset from memory entirely (EditorBridge::closeDataset(), a GLOBAL
  // free, not just "deselect it from this pane" -- the same dataset could
  // be showing in the opposite pane too). Warns first via a plain native
  // confirm() if the dataset has unsaved changes -- "dirty" here means a
  // real raw-byte write (see PcgFile::isDirty()'s own doc comment), not the
  // in-memory-only legacy copyEntry() operation, which can't be lost this
  // way since it was never save-durable in the first place. Reads the dirty flag via window.isDatasetDirty() -- a direct
  // point query straight from PcgFile itself (see its own doc comment in
  // EditorBridge.h), NOT `knownDatasets` (the cache populated by the last
  // onDatasetsChanged() broadcast): reported directly, 2026-08-15, that a
  // Combi swap/move/copy (and every other write across the app) only
  // refreshes its OWN pane's view, never the global refreshDatasets()
  // broadcast, so a list-based cache goes stale the instant any edit
  // happens anywhere. The dirty flag lives on the raw data itself and is
  // always current the instant it's asked for -- asking directly avoids
  // relying on any cache staying fresh at all.
  // `closeDataset()` itself doesn't know or care which pane(s) are showing
  // this dataset -- the refreshDatasets() call below (after the close)
  // re-broadcasts the now-shorter open list, and every pane's own
  // onDatasetsChanged() listener (this one and the opposite one) already
  // resets itself if its currentDatasetId just disappeared.
  unloadDatasetButton.addEventListener("click", async () => {
    if (currentDatasetId == null) return;
    const dirtyCheck = await window.isDatasetDirty(currentDatasetId);
    if (dirtyCheck.ok && dirtyCheck.dirty) {
      const displayName = knownDatasets.find((d) => d.datasetId === currentDatasetId);
      // window.showConfirmDialog() (confirm-dialog.js), NOT window.confirm() --
      // see that file's own doc comment: WKWebView silently drops native JS
      // confirm() dialogs under CHOC's WebView here, so the warning never
      // appeared at all and Unload looked like it did nothing.
      const confirmed = await window.showConfirmDialog(
        `"${displayName ? displayName.displayName : "This dataset"}" has unsaved changes. Unload it anyway?`,
        { confirmLabel: "Unload", isDanger: true }
      );
      if (!confirmed) return;
    }
    await window.closeDataset(currentDatasetId);
    await refreshDatasets();
  });

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
    // Exposed so the OPPOSITE pane's own jumpToOppositePane() (a shift+click
    // on any jump button) can make THIS pane jump to an instrument/Setlist
    // entry from the outside, through the exact same public entry points a
    // same-pane click already uses -- never reaches into this pane's
    // switchCategory()/applyNavEntry()/nav-history directly.
    jumpToInstrument,
    jumpToSetlistEntry,
  };
}
