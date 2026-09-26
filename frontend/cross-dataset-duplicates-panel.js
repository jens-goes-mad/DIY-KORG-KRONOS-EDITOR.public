// Cross Dataset analysis (STATE.md entries 89, 91, 99) -- GLOBAL (not per-pane)
// tools that compare Programs/Combis across two open datasets, complementing
// the per-dataset Duplicates tab (pane-program-editor.js's
// createDuplicatesPanel()), which only ever looks inside one file. Reached via
// a topbar icon next to the pane-visibility [left|both|right] buttons.
//
// One sidebar, ONE screen (revised 2026-09-26, per direct request): a mode
// toggle, two dataset dropdowns (A and B), a Find button, and the result
// rendered inline BELOW them -- no separate "results" view or "new search"
// button, no bank filter. Find is disabled until two DIFFERENT datasets are
// picked.
//
// - **Find duplicates**: Programs with the same content in A and in B, at any
//   slot (findDuplicateProgramsAcrossDatasets(), matched on a location-
//   independent content hash -- see ProgramDecoder.h's
//   hashProgramRecordForComparison()).
// - **Find differences** (Programs only): the content-keyed set difference --
//   only in A, only in B, renamed, modified twins, moved
//   (findProgramDifferencesAcrossDatasets()). Rows jump each pane to its OWN
//   slot.
// - **Compare PROG**: position-matched Program divergence -- same (bank,
//   number) slot in both files, different content (entry 91).
// - **Compare COMBI**: the same slot-by-slot comparison for Combis, with a
//   readable per-row summary (Volume, Timbres, IFX, MFX, TFX, EQ, Mixer), the
//   click-to-expand detail and click-to-resolve breakdown (entries 92-95). A
//   Combi whose NAME differs and has more than 3 changes is shown as
//   "Different song" instead of listing every difference.
//
// The dropdowns follow dataset open/close live: this file subscribes to
// datasets.js's onDatasetsChanged() broadcast (fed by every refreshDatasets(),
// i.e. any pane opening/closing a file), so they never go stale while the
// sidebar is open, and the last result is dropped as soon as either picked
// dataset changes (closed, or its `dirty` flag flipped).
//
// Read-only except the Combi resolve buttons in "Compare two files". Wrapped
// in an IIFE, same reason every other app-level sidebar file is (STATE.md
// entry 60).
(function () {

const sidebar = window.createSidebarPanel(document.getElementById("crossDatasetDuplicatesPanelRoot"), { edge: "right" });

const TITLE = "Cross Dataset analysis";

let toolMode = "duplicates";  // "duplicates" | "differences" | "comparePrograms" | "compareCombis"

let knownDatasets = [];       // last datasets.js broadcast: [{datasetId, displayName, setlistCount, dirty}]
let compareDatasetIdA = null; // A/B are shared by every mode (names kept from when only "Compare" had them)
let compareDatasetIdB = null;
let isBusy = false;

// Per mode: null, or { idA, idB, dirtyA, dirtyB, ...payload }. Dropped when the
// picked datasets change or get touched (see datasetsChanged()).
const results = { duplicates: null, differences: null, comparePrograms: null, compareCombis: null };
// Which Combi divergence rows are expanded to show their own per-change resolve
// rows (STATE.md entry 94) -- keyed "bank-number".
let expandedCombiKeys = new Set();
// Compare COMBI hides slots that are just "Init Combi" on BOTH sides (untouched
// template slots -- noise when comparing two backups) unless this is turned off.
let hideInitCombis = true;

function isInitCombiName(name) {
  return /init combi/i.test(name);
}

// An untouched template slot on both sides: nothing to compare.
function isInitCombiRow(d) {
  return isInitCombiName(d.nameA) && isInitCombiName(d.nameB);
}

// A Combi with a different NAME and more than 3 changes is a different song,
// not an edited one -- listing every differing block is just noise.
const DIFFERENT_SONG_MIN_CHANGES = 4;

function isDifferentSong(d) {
  return d.nameA !== d.nameB && d.changes.length >= DIFFERENT_SONG_MIN_CHANGES;
}

// What the Combi table's "Changes" column shows: just how many things differ,
// or "Different song" (see isDifferentSong()). The per-change breakdown is one
// click away (the expanded row).
function changesLabel(d) {
  return isDifferentSong(d) ? "Different song" : String(d.changes.length);
}

// --- Tabulator tables ------------------------------------------------------
// Every table in this sidebar is a Tabulator (vendor/TABULATOR_VERSION.txt): click a
// column header to sort, type in the filter box under it to filter -- UI only, over
// the rows already in the page (nothing is stored elsewhere). Tabulator is used ONLY
// here; the Setlist/Programs/Combis pane tables (drag-and-drop, accordion editors)
// stay hand-written.
//
// This panel redraws its whole body on many events (sidebar.update()), so table
// instances are destroyed before each redraw (destroyTables()) and each table's sort
// + filter state is kept per key and restored on the next build -- a heading
// collapse or a Combi resolve never loses what was typed.
const tableInstances = [];
const tableStates = new Map();  // key -> { sorters: [{column, dir}], filters: [{field, value}] }

function destroyTables() {
  for (const t of tableInstances) {
    try { t.destroy(); } catch (e) { /* already gone */ }
  }
  tableInstances.length = 0;
}

// Forgets the saved sort/filter of one mode's tables (called when that mode's rows are replaced).
const TABLE_KEY_PREFIX = { duplicates: "dup:", differences: "diff:", comparePrograms: "prog:", compareCombis: "combi:" };
function forgetTableStates(mode) {
  for (const k of [...tableStates.keys()]) if (k.startsWith(TABLE_KEY_PREFIX[mode])) tableStates.delete(k);
}

// `hostEl` must already be in the document (Tabulator measures it). The table fills
// all the height the result pane has left (`height: "100%"` of the absolutely-sized
// host, see newTableHost()), so ITS ROWS are the scroll area -- the header (titles,
// sort arrows, filter boxes) stays put -- and Tabulator's virtual rendering keeps
// thousands of rows cheap.
function createTable(hostEl, key, { columns, data, ...options }) {
  const saved = tableStates.get(key) || {};
  const table = new Tabulator(hostEl, {
    data,
    columns,
    layout: "fitColumns",
    height: "100%",
    placeholder: "No matching rows",
    columnDefaults: { headerFilter: "input", headerFilterPlaceholder: "filter", tooltip: true, resizable: false },
    initialSort: saved.sorters,
    initialHeaderFilter: saved.filters,
    ...options,
  });
  // Only start remembering once the table is built, so the build's own initial
  // (empty) events cannot overwrite the state being restored.
  table.on("tableBuilt", () => {
    table.on("dataSorted", (sorters) => {
      const st = tableStates.get(key) || {};
      st.sorters = sorters.map((x) => ({ column: x.field, dir: x.dir }));
      tableStates.set(key, st);
    });
    table.on("dataFiltered", (filters) => {
      const st = tableStates.get(key) || {};
      st.filters = filters.filter((f) => typeof f.field === "string").map((f) => ({ field: f.field, value: f.value }));
      tableStates.set(key, st);
    });
  });
  tableInstances.push(table);
  return table;
}

// Rows are focusable and Enter/Space runs `activate(event)` (the keyboard twin of a
// click). The listener is bound once per row element; `activate` is looked up each time
// because the row formatter runs again on every re-render.
function decorateRow(el, activate) {
  el.tabIndex = 0;
  el._crossDatasetActivate = activate;
  if (el._crossDatasetKeyBound) return;
  el._crossDatasetKeyBound = true;
  el.addEventListener("keydown", (evt) => {
    if (evt.target !== el) return;  // typing in a filter box / pressing a button must not activate the row
    if (evt.key === "Enter" || evt.key === " ") {
      evt.preventDefault();
      el._crossDatasetActivate(evt);
    }
  });
}

// Sorts an ID column by (bank, number) instead of by its text.
const sortById = (a, b, aRow, bRow) => aRow.getData().sortId - bRow.getData().sortId;

// The result's title line, e.g. "1201 Duplicate Group(s)". Plain text -- the result is one
// table that fills the pane, so there is nothing to fold.
function addHeading(bodyEl, text) {
  const h = document.createElement("h3");
  h.className = "sidebar-section-heading";
  h.textContent = text;
  bodyEl.appendChild(h);
}

function combiDivergenceKey(bank, number) {
  return `${bank}-${number}`;
}

// `displayName` is a full path everywhere else in this app -- result headers
// want a short label.
function basenameOfPath(path) {
  const idx = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
  return idx === -1 ? path : path.slice(idx + 1);
}

function isDirty(datasets, id) {
  const d = datasets.find((x) => x.datasetId === id);
  return d ? d.dirty : null;
}

// Called for every datasets.js broadcast (a dataset opened, closed or re-listed)
// and once at startup. Keeps the dropdown picks valid, drops results that no
// longer describe the picked datasets, and redraws if the sidebar is open.
function datasetsChanged(datasets) {
  knownDatasets = datasets;
  const openIds = new Set(datasets.map((d) => d.datasetId));
  if (compareDatasetIdA != null && !openIds.has(compareDatasetIdA)) compareDatasetIdA = null;
  if (compareDatasetIdB != null && !openIds.has(compareDatasetIdB)) compareDatasetIdB = null;
  if (compareDatasetIdA != null && compareDatasetIdA === compareDatasetIdB) compareDatasetIdB = null;
  // Fill an empty pick with a sensible default (first open dataset that the
  // other dropdown isn't already using), so two open files work out of the box.
  if (compareDatasetIdA == null) {
    const c = datasets.find((d) => d.datasetId !== compareDatasetIdB);
    compareDatasetIdA = c ? c.datasetId : null;
  }
  if (compareDatasetIdB == null) {
    const c = datasets.find((d) => d.datasetId !== compareDatasetIdA);
    compareDatasetIdB = c ? c.datasetId : null;
  }
  for (const mode of Object.keys(results)) {
    const r = results[mode];
    if (!r) continue;
    if (r.idA !== compareDatasetIdA || r.idB !== compareDatasetIdB ||
        isDirty(datasets, r.idA) !== r.dirtyA || isDirty(datasets, r.idB) !== r.dirtyB) {
      results[mode] = null;
    }
  }
  sidebar.update();
}

function canFind() {
  return !isBusy && compareDatasetIdA != null && compareDatasetIdB != null && compareDatasetIdA !== compareDatasetIdB;
}

// Runs the current mode's search for the picked A/B. Always clears the busy flag,
// even if the bridge throws, so the sidebar can never stay stuck on "Finding...".
async function run(mode = toolMode, { keepTableState = false } = {}) {
  if (!canFind()) return;
  const idA = compareDatasetIdA;
  const idB = compareDatasetIdB;
  isBusy = true;
  sidebar.update();
  try {
    let payload;
    if (mode === "duplicates") {
      payload = { groups: await window.findDuplicateProgramsAcrossDatasets([idA, idB], []) };
    } else if (mode === "differences") {
      payload = { rows: await window.findProgramDifferencesAcrossDatasets(idA, idB, []) };
    } else if (mode === "comparePrograms") {
      payload = { programs: await window.findDivergentProgramsAcrossDatasets(idA, idB, []) };
    } else {
      payload = { combis: await window.findDivergentCombisAcrossDatasets(idA, idB) };
    }
    const fresh = await window.listDatasets();  // current dirty flags, without re-broadcasting
    results[mode] = { idA, idB, dirtyA: isDirty(fresh, idA), dirtyB: isDirty(fresh, idB), ...payload };
    if (!keepTableState) forgetTableStates(mode);  // new rows: old sort/filter no longer describe them
  } catch (err) {
    results[mode] = null;
    showToast(`Search failed: ${err && err.message ? err.message : err}`, { isError: true });
  } finally {
    isBusy = false;
    sidebar.update();
  }
}

// Resolves which pane currently sits on the given screen side, by DOM
// position -- NOT by paneId "A"/"B" -- same convention app.js's own
// setPaneVisibility()/swapPanes() already rely on (paneId is a stable
// label, not a screen position; swapPanes() only ever reorders elements).
function paneAtSide(rightSide) {
  const els = document.querySelectorAll(".panes .pane");
  if (els.length === 0) return null;
  const el = rightSide ? els[els.length - 1] : els[0];
  return el ? panes[el.dataset.pane] : null;
}

// "Find duplicates": click -> right pane, Shift+click -> left pane (per
// direct request), mirroring every other cross-reference jump button in
// this app. Reuses each pane's own loadDataset()/jumpToInstrument() -- no
// new navigation primitive needed.
async function jumpToMember(member, toLeftPane) {
  const pane = paneAtSide(!toLeftPane);
  if (!pane) return;
  if (pane.getCurrentDatasetId() !== member.datasetId) {
    const known = knownDatasets.find((d) => d.datasetId === member.datasetId);
    await pane.loadDataset(member.datasetId, known ? known.displayName : "");
  }
  pane.jumpToInstrument({ isProgram: true, bank: member.bank, number: member.number });
  sidebar.close();
}

// "Compare two files": a single click jumps BOTH panes at once -- dataset
// A's slot into the LEFT pane, dataset B's into the RIGHT -- see this
// file's own top comment for why that's a deliberately different gesture
// from "Find duplicates"'s click/shift+click.
async function jumpToDivergence(isProgram, bank, number) {
  const leftPane = paneAtSide(false);
  const rightPane = paneAtSide(true);
  const knownA = knownDatasets.find((d) => d.datasetId === compareDatasetIdA);
  const knownB = knownDatasets.find((d) => d.datasetId === compareDatasetIdB);

  if (leftPane) {
    if (leftPane.getCurrentDatasetId() !== compareDatasetIdA) {
      await leftPane.loadDataset(compareDatasetIdA, knownA ? knownA.displayName : "");
    }
    leftPane.jumpToInstrument({ isProgram, bank, number });
  }
  if (rightPane) {
    if (rightPane.getCurrentDatasetId() !== compareDatasetIdB) {
      await rightPane.loadDataset(compareDatasetIdB, knownB ? knownB.displayName : "");
    }
    rightPane.jumpToInstrument({ isProgram, bank, number });
  }
  sidebar.close();
}

// Resolves ONE specific Combi divergence change (STATE.md entry 94, direct
// request) by copying its exact raw bytes from one dataset's record into
// the other's. `direction` is "a-to-b" (the "→" button: A's value
// overwrites B) or "b-to-a" (the "←" button: B's value overwrites A) --
// "A"/"B" here are ALWAYS this sidebar's own compareDatasetIdA/B, i.e. the
// LEFT/RIGHT columns of the comparison table -- NOT whichever Norton pane
// currently shows which side (those can be independently swapped, see
// app.js's swapPanes()). `description` must be the EXACT string the
// resolve button's own row is currently showing -- the bridge re-derives
// the byte ranges fresh from that text (see EditorBridge::
// resolveCombiDivergenceChange()'s own doc comment for why a fresh
// recompute, not a cached range, is used).
async function resolveCombiChange(bank, number, description, direction) {
  const result = await window.resolveCombiDivergenceChange(compareDatasetIdA, compareDatasetIdB, bank, number, description, direction);
  if (!result.ok) {
    showToast(`Resolve failed: ${result.error}`, { isError: true });
    return;
  }

  // If either Norton pane currently shows the dataset that was actually
  // written to, refresh its library view -- the same "did a write land
  // somewhere already visible" check every other cross-pane write in this
  // app already does (e.g. app.js's onDropProgram).
  for (const pane of Object.values(panes)) {
    if (pane.getCurrentDatasetId() === result.resolvedDatasetId) await pane.refreshLibrary();
  }

  // Re-run the SAME comparison rather than hand-editing the cached result --
  // this one resolved change (and the whole Combi row, once nothing about
  // it diverges any more) disappears naturally from the fresh list, no
  // manual array surgery needed, and can never drift out of sync with what
  // the datasets actually contain now.
  await run("compareCombis", { keepTableState: true });  // a resolve must not drop what was typed/sorted
}

// "Find differences": unlike jumpToDivergence() the two sides usually sit at
// DIFFERENT slots, so each pane jumps to its own coordinates -- and a side
// with no partner ("only in" rows) is simply not touched.
async function jumpToDifference(row) {
  const leftPane = paneAtSide(false);
  const rightPane = paneAtSide(true);
  const knownA = knownDatasets.find((d) => d.datasetId === compareDatasetIdA);
  const knownB = knownDatasets.find((d) => d.datasetId === compareDatasetIdB);

  if (leftPane && row.aBank >= 0) {
    if (leftPane.getCurrentDatasetId() !== compareDatasetIdA) {
      await leftPane.loadDataset(compareDatasetIdA, knownA ? knownA.displayName : "");
    }
    leftPane.jumpToInstrument({ isProgram: true, bank: row.aBank, number: row.aNumber });
  }
  if (rightPane && row.bBank >= 0) {
    if (rightPane.getCurrentDatasetId() !== compareDatasetIdB) {
      await rightPane.loadDataset(compareDatasetIdB, knownB ? knownB.displayName : "");
    }
    rightPane.jumpToInstrument({ isProgram: true, bank: row.bBank, number: row.bNumber });
  }
  sidebar.close();
}

// One expanded Combi divergence's detail (STATE.md entry 94): one line per
// `changes` entry, each with a "←"/"→" button that copies that SPECIFIC change's raw
// bytes one way or the other -- or, for a "Different song", a single note. The
// file names go into the button tooltips so "which file am I about to overwrite"
// reads as a real filename; "A"/"B" are this sidebar's dropdowns, not either
// Norton pane. Lives inside its table row (appended by the row formatter).
function buildCombiDetail(d, nameAHeader, nameBHeader) {
  const wrap = document.createElement("div");
  wrap.className = "cross-dataset-detail";
  // Clicks in here must not toggle / jump the row it belongs to.
  wrap.addEventListener("click", (evt) => evt.stopPropagation());
  wrap.addEventListener("dblclick", (evt) => evt.stopPropagation());

  if (isDifferentSong(d)) {
    const note = document.createElement("div");
    note.className = "cross-dataset-detail-line";
    note.textContent =
      `Different song: "${d.nameA}" vs "${d.nameB}" -- ${d.changes.length} changes, not listed. ` +
      "Double-click the row to open both.";
    wrap.appendChild(note);
    return wrap;
  }

  for (const description of d.changes) {
    const line = document.createElement("div");
    line.className = "cross-dataset-detail-line";
    const text = document.createElement("span");
    text.className = "cross-dataset-detail-text";
    text.textContent = description;

    const leftBtn = document.createElement("button");
    leftBtn.type = "button";
    leftBtn.className = "button is-small cross-dataset-dup-resolve-button";
    leftBtn.textContent = "←";
    leftBtn.title = `Copy "${nameBHeader}"'s value into "${nameAHeader}"`;
    leftBtn.addEventListener("click", (evt) => {
      evt.stopPropagation();
      resolveCombiChange(d.bank, d.number, description, "b-to-a");
    });
    const rightBtn = document.createElement("button");
    rightBtn.type = "button";
    rightBtn.className = "button is-small cross-dataset-dup-resolve-button";
    rightBtn.textContent = "→";
    rightBtn.title = `Copy "${nameAHeader}"'s value into "${nameBHeader}"`;
    rightBtn.addEventListener("click", (evt) => {
      evt.stopPropagation();
      resolveCombiChange(d.bank, d.number, description, "a-to-b");
    });
    const actions = document.createElement("div");
    actions.className = "cross-dataset-detail-actions";
    actions.append(leftBtn, rightBtn);

    line.append(text, actions);
    wrap.appendChild(line);
  }
  return wrap;
}

// The explanation under a result heading, folded away by default so the table gets the room.
function addHint(bodyEl, text) {
  const details = document.createElement("details");
  details.className = "cross-dataset-hint";
  const summary = document.createElement("summary");
  summary.textContent = "How to read this";
  const note = document.createElement("div");
  note.className = "usage-note";
  note.textContent = text;
  details.append(summary, note);
  bodyEl.appendChild(details);
}

function addEmptyMessage(bodyEl, message) {
  const empty = document.createElement("div");
  empty.className = "usage-empty";
  empty.textContent = message;
  bodyEl.appendChild(empty);
}

// Tabulator turns its host element INTO the `.tabulator` table, so the styling hook
// (`.cross-dataset-tabulator`) is a wrapper around it. The wrapper takes the rest of
// the result pane's height (`.cross-dataset-table-fill`, style.css) and the host is
// absolutely positioned inside it, so `height: "100%"` resolves.
function newTableHost(bodyEl) {
  const wrap = document.createElement("div");
  wrap.className = "cross-dataset-tabulator cross-dataset-table-fill";
  const host = document.createElement("div");
  wrap.appendChild(host);
  bodyEl.appendChild(wrap);  // in the document BEFORE new Tabulator() -- it measures its host
  return host;
}

// Compare PROG / Compare COMBI: one row per diverged slot.
//   - Program rows: a click jumps both panes (jumpToDivergence()).
//   - Combi rows (entry 94): a click expands/collapses the change list inside the row;
//     a DOUBLE click jumps both panes instead. The two share one row via a short
//     delay on the single click, cancelled if a dblclick follows.
function buildDivergenceTable(bodyEl, { rows, isProgram, nameAHeader, nameBHeader, emptyMessage }) {
  if (rows.length === 0) {
    addEmptyMessage(bodyEl, emptyMessage);
    return;
  }

  const chevron = (open) => (open ? "▾ " : "▸ ");
  const data = rows.map((d) => {
    const key = combiDivergenceKey(d.bank, d.number);
    const label = formatBankNumber({ isProgram, bank: d.bank, number: d.number }, isProgram ? d.bankType : null);
    const open = !isProgram && expandedCombiKeys.has(key);
    return {
      key,
      label,
      idCell: isProgram ? label : chevron(open) + label,
      sortId: d.bank * 1000 + d.number,
      nameA: d.nameA,
      nameB: d.nameB,
      changes: isProgram ? "" : changesLabel(d),
      // "Different song" sorts after every plain count.
      sortChanges: isProgram ? 0 : isDifferentSong(d) ? 1e6 : d.changes.length,
      _open: open,
      d,
    };
  });

  const columns = [
    { title: "ID", field: "idCell", width: isProgram ? 96 : 104, sorter: sortById },
    { title: nameAHeader, field: "nameA", widthGrow: 1 },
    { title: nameBHeader, field: "nameB", widthGrow: 1 },
  ];
  if (!isProgram) {
    columns.push({
      title: "Changes",
      field: "changes",
      width: 116,
      sorter: (a, b, aRow, bRow) => aRow.getData().sortChanges - bRow.getData().sortChanges,
    });
  }

  const jumpBoth = (row) => jumpToDivergence(isProgram, row.getData().d.bank, row.getData().d.number);
  const toggleOpen = (row) => {
    const rowData = row.getData();
    const open = !rowData._open;
    if (open) expandedCombiKeys.add(rowData.key);
    else expandedCombiKeys.delete(rowData.key);
    row.update({ _open: open, idCell: chevron(open) + rowData.label });
    row.reformat();  // re-run the row formatter: show / hide the change list
    // Tabulator pins its body height at build time; without a redraw an expanded/
    // collapsed row leaves stale blank space (or clips the list).
    table.redraw();
  };
  const activate = isProgram ? jumpBoth : toggleOpen;

  const table = createTable(newTableHost(bodyEl), isProgram ? "prog:table" : "combi:table", {
    columns,
    data,
    index: "key",
    rowFormatter: (row) => {
      const el = row.getElement();
      const rowData = row.getData();
      el.classList.toggle("is-open", !!rowData._open);
      decorateRow(el, () => activate(row));
      const old = el.querySelector(":scope > .cross-dataset-detail");
      if (old) old.remove();
      if (!isProgram && rowData._open) el.appendChild(buildCombiDetail(rowData.d, nameAHeader, nameBHeader));
    },
  });

  if (isProgram) {
    table.on("rowClick", (evt, row) => jumpBoth(row));
  } else {
    const timers = new Map();
    table.on("rowClick", (evt, row) => {
      const key = row.getData().key;
      if (timers.has(key)) return;  // a dblclick may still follow -- don't double-fire
      timers.set(key, setTimeout(() => {
        timers.delete(key);
        toggleOpen(row);
      }, 220));
    });
    table.on("rowDblClick", (evt, row) => {
      const key = row.getData().key;
      if (timers.has(key)) {
        clearTimeout(timers.get(key));
        timers.delete(key);
      }
      jumpBoth(row);
    });
  }
}

// The Differences result is ONE table; a dropdown picks which category it shows.
// Rows show each side's own slot + name; a click opens both panes, each at its own
// slot. `side`: "a" / "b" = only that file's column (the "only in ..." categories);
// omitted = both columns.
function buildDifferenceTable(bodyEl, { key, rows, nameAHeader, nameBHeader, side }) {
  const label = (bank, number, name, bankType) =>
    bank < 0 ? "" : `${formatBankNumber({ isProgram: true, bank, number }, bankType)}  ${name}`;
  const data = rows.map((r, i) => ({
    key: String(i),
    a: label(r.aBank, r.aNumber, r.aName, r.bankType),
    b: label(r.bBank, r.bNumber, r.bName, r.bankType),
    sortA: r.aBank * 1000 + r.aNumber,
    sortB: r.bBank * 1000 + r.bNumber,
    row: r,
  }));
  const colA = { title: nameAHeader, field: "a", widthGrow: 1, sorter: (x, y, xr, yr) => xr.getData().sortA - yr.getData().sortA };
  const colB = { title: nameBHeader, field: "b", widthGrow: 1, sorter: (x, y, xr, yr) => xr.getData().sortB - yr.getData().sortB };
  const columns = side === "a" ? [colA] : side === "b" ? [colB] : [colA, colB];

  const table = createTable(newTableHost(bodyEl), key, {
    columns,
    data,
    index: "key",
    placeholder: "None.",
    rowFormatter: (row) => decorateRow(row.getElement(), () => jumpToDifference(row.getData().row)),
  });
  table.on("rowClick", (evt, row) => jumpToDifference(row.getData().row));
}

// --- Inline result renderers --------------------------------------------

function buildDuplicatesResults(bodyEl, r) {
  addHeading(bodyEl, `${r.groups.length} Duplicate Group(s)`);

  if (r.groups.length === 0) {
    const empty = document.createElement("div");
    empty.className = "usage-empty";
    empty.textContent = "No Program with the same content exists in both files.";
    bodyEl.appendChild(empty);
    return;
  }

  addHint(bodyEl, "Same content, any slot (the slot-dependent header bytes and Drum Track reference are ignored). " +
    "Click a row to jump to it in the right pane; Shift+click for the left pane.");

  const data = [];
  r.groups.forEach((group, gi) => {
    group.members.forEach((member, mi) => {
      data.push({
        key: `${gi}-${mi}`,
        group: gi,
        name: member.name,
        filename: member.filename,
        idCell: formatBankNumber({ isProgram: true, bank: member.bank, number: member.number }, member.bankType),
        sortId: member.bank * 1000 + member.number,
        member,
      });
    });
  });
  const jump = (row, evt) => jumpToMember(row.getData().member, !!(evt && evt.shiftKey));
  const table = createTable(newTableHost(bodyEl), "dup:table", {
    data,
    index: "key",
    columns: [
      { title: "Name", field: "name", widthGrow: 2 },
      { title: "Datasource", field: "filename", widthGrow: 2 },
      { title: "ID", field: "idCell", width: 96, sorter: sortById },
    ],
    // One header per duplicate group ("N copies"), like before; not collapsible.
    groupBy: "group",
    groupToggleElement: false,
    groupHeader: (value, count) => `${count} copies`,
    rowFormatter: (row) => decorateRow(row.getElement(), (evt) => jump(row, evt)),
  });
  table.on("rowClick", (evt, row) => jump(row, evt));
}

// Which Differences category the dropdown shows; survives redraws. Unset (or set to
// something that no longer exists) = the first category that has rows.
let diffCategory = null;

function buildDifferenceResults(bodyEl, r) {
  const nameA = basenameOfPath((knownDatasets.find((d) => d.datasetId === r.idA) || {}).displayName || `#${r.idA}`);
  const nameB = basenameOfPath((knownDatasets.find((d) => d.datasetId === r.idB) || {}).displayName || `#${r.idB}`);
  const of = (kind) => r.rows.filter((row) => row.kind === kind);

  addHeading(bodyEl, `${r.rows.length - of("moved").length} Difference(s) + ${of("moved").length} Moved`);

  addHint(
    bodyEl,
    "Matched by content, not slot. Pick a category, then click a row to open A in the left pane and B in the right pane, each at its own slot. " +
      "Renamed = identical except the name. Modified twins = same name, different content. Moved = identical content in a different slot."
  );

  const categories = [
    { kind: "onlyInA", title: `Only in ${nameA}`, side: "a" },
    { kind: "onlyInB", title: `Only in ${nameB}`, side: "b" },
    { kind: "renamed", title: "Renamed (same sound, different name)" },
    { kind: "modifiedTwin", title: "Modified twins (same name, different content)" },
    { kind: "moved", title: "Moved (identical, different slot)" },
  ];
  if (!categories.some((c) => c.kind === diffCategory)) {
    const first = categories.find((c) => of(c.kind).length > 0);
    diffCategory = (first || categories[0]).kind;
  }

  const wrap = document.createElement("div");
  wrap.className = "select is-small is-fullwidth mb-2";
  const select = document.createElement("select");
  for (const c of categories) {
    const opt = document.createElement("option");
    opt.value = c.kind;
    opt.textContent = `${c.title} (${of(c.kind).length})`;
    select.appendChild(opt);
  }
  select.value = diffCategory;
  select.addEventListener("change", () => {
    diffCategory = select.value;
    sidebar.update();
  });
  wrap.appendChild(select);
  bodyEl.appendChild(wrap);

  const current = categories.find((c) => c.kind === diffCategory);
  buildDifferenceTable(bodyEl, {
    key: `diff:${current.kind}`,
    rows: of(current.kind),
    nameAHeader: nameA,
    nameBHeader: nameB,
    side: current.side,
  });
}

function nameOfDataset(id) {
  const d = knownDatasets.find((x) => x.datasetId === id);
  return basenameOfPath(d ? d.displayName : `#${id}`);
}

function buildCompareProgramResults(bodyEl, r) {
  addHeading(bodyEl, `${r.programs.length} Program Divergence(s)`);

  addHint(bodyEl, "Same slot in both files, different content. Click a row to open A in the left pane and B in the right pane, both jumped to that slot.");

  buildDivergenceTable(bodyEl, {
    rows: r.programs, isProgram: true,
    nameAHeader: nameOfDataset(r.idA), nameBHeader: nameOfDataset(r.idB),
    emptyMessage: "No Program divergences found.",
  });
}

function buildCompareCombiResults(bodyEl, r) {
  const hiddenInit = hideInitCombis ? r.combis.filter(isInitCombiRow) : [];
  const combis = hideInitCombis ? r.combis.filter((d) => !isInitCombiRow(d)) : r.combis;
  const differentSongs = combis.filter(isDifferentSong).length;
  const title =
    `${combis.length} Combi Divergence(s)` +
    (differentSongs ? ` (${differentSongs} Different Song${differentSongs > 1 ? "s" : ""})` : "");
  addHeading(bodyEl, title);

  // Untouched "Init Combi" slots are hidden by default; this brings them back.
  const initLabel = document.createElement("label");
  initLabel.className = "cross-dataset-dup-checkbox-row";
  const initBox = document.createElement("input");
  initBox.type = "checkbox";
  initBox.checked = hideInitCombis;
  initBox.addEventListener("change", () => {
    hideInitCombis = initBox.checked;
    sidebar.update();
  });
  const initText = document.createElement("span");
  const initCount = r.combis.filter(isInitCombiRow).length;
  initText.textContent = `Hide "Init Combi" slots (${initCount})`;
  initLabel.append(initBox, initText);
  bodyEl.appendChild(initLabel);

  addHint(bodyEl, "Same slot in both files, different content. Click a row to see exactly what changed and resolve it with ←/→; " +
    "double-click to open both panes at that slot. A different name plus more than 3 changes is shown as a different song.");

  buildDivergenceTable(bodyEl, {
    rows: combis, isProgram: false,
    nameAHeader: nameOfDataset(r.idA), nameBHeader: nameOfDataset(r.idB),
    emptyMessage: hiddenInit.length > 0 ? "Only \"Init Combi\" slots differ (hidden)." : "No Combi divergences found.",
  });
}

// --- Shared controls ------------------------------------------------------

const MODES = [
  ["duplicates", "Duplicates"],
  ["differences", "Differences"],
  ["comparePrograms", "Compare PROG"],
  ["compareCombis", "Compare COMBI"],
];

function buildModeToggle(bodyEl) {
  const wrap = document.createElement("div");
  wrap.className = "buttons has-addons mb-3 cross-dataset-mode-toggle";
  for (const [key, label] of MODES) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "button is-small" + (toolMode === key ? " is-link" : "");
    btn.textContent = label;
    btn.addEventListener("click", () => {
      if (toolMode === key) return;
      toolMode = key;
      sidebar.update();
    });
    wrap.appendChild(btn);
  }
  bodyEl.appendChild(wrap);
}

function buildDatasetSelect(bodyEl, labelText, currentId, onChange) {
  const label = document.createElement("label");
  label.textContent = labelText;
  const wrap = document.createElement("div");
  wrap.className = "select is-small is-fullwidth";
  const select = document.createElement("select");
  const placeholder = document.createElement("option");
  placeholder.value = "";
  placeholder.textContent = knownDatasets.length === 0 ? "No files open" : "(select a dataset)";
  select.appendChild(placeholder);
  for (const d of knownDatasets) {
    const opt = document.createElement("option");
    opt.value = String(d.datasetId);
    opt.textContent = `#${d.datasetId} — ${d.displayName}`;
    select.appendChild(opt);
  }
  select.value = currentId != null && knownDatasets.some((d) => d.datasetId === currentId) ? String(currentId) : "";
  select.disabled = isBusy;
  select.addEventListener("change", () => {
    onChange(select.value === "" ? null : Number(select.value));
    for (const mode of Object.keys(results)) results[mode] = null;  // a result describes ONE pair
    sidebar.update();
  });
  wrap.appendChild(select);
  bodyEl.append(label, wrap);
}

function buildBody(bodyEl) {
  destroyTables();  // the body is about to be rebuilt from scratch
  buildModeToggle(bodyEl);
  buildDatasetSelect(bodyEl, "Dataset A (opens in the left pane)", compareDatasetIdA, (v) => { compareDatasetIdA = v; });
  buildDatasetSelect(bodyEl, "Dataset B (opens in the right pane)", compareDatasetIdB, (v) => { compareDatasetIdB = v; });

  const findBtn = document.createElement("button");
  findBtn.type = "button";
  findBtn.className = "button is-small accent-button mt-2 mb-3";
  findBtn.textContent = isBusy ? "Searching..." : "Find";
  findBtn.disabled = !canFind();
  if (!canFind() && !isBusy) {
    findBtn.title = "Pick two different datasets.";
  }
  findBtn.addEventListener("click", () => run());
  bodyEl.appendChild(findBtn);

  const r = results[toolMode];
  if (!r) return;
  // Only this area scrolls; the mode toggle, dropdowns and Find button above stay put.
  const resultsEl = document.createElement("div");
  resultsEl.className = "cross-dataset-results";
  bodyEl.appendChild(resultsEl);
  if (toolMode === "duplicates") buildDuplicatesResults(resultsEl, r);
  else if (toolMode === "differences") buildDifferenceResults(resultsEl, r);
  else if (toolMode === "comparePrograms") buildCompareProgramResults(resultsEl, r);
  else buildCompareCombiResults(resultsEl, r);
}

window.toggleCrossDatasetDuplicatesPanel = () => {
  if (sidebar.isOpen()) {
    sidebar.close();
    return;
  }
  sidebar.open({ title: TITLE, build: buildBody });
  refreshDatasets();  // datasets.js -- re-broadcasts the current list, incl. fresh dirty flags
};

onDatasetsChanged(datasetsChanged);

})();
