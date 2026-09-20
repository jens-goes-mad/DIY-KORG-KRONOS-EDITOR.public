// Cross-dataset tools (STATE.md entries 89 + 91) -- GLOBAL (not per-pane)
// tools that compare Programs/Combis across currently open datasets,
// complementing the existing PER-DATASET Duplicates tab
// (pane-program-editor.js's createDuplicatesPanel(), EditorBridge::
// findDuplicatePrograms()), which only ever looks inside one file. Reached
// via a topbar icon next to the pane-visibility [left|both|right] buttons
// (index.html).
//
// One sidebar (createSidebarPanel(), sidebar-panel.js), a mode toggle at
// the top choosing between two independent tools, each with its own
// "filters"/"results" pair of views -- a second sidebar shell per tool
// would just be more machinery for no benefit, and the two tools already
// share the open-dataset list + bank-union plumbing (refreshOpenDatasets()).
//
// - **"Find duplicates"** (entry 89, original feature): pick N datasets +
//   a bank filter, find every byte-exact duplicate GROUP across them
//   (findDuplicateProgramsAcrossDatasets()) -- "same content, different
//   location", Programs only.
// - **"Compare two files"** (entry 91, 2026-09-19 per direct request):
//   pick exactly 2 datasets (A/B) + a bank filter, find every (bank,
//   number) slot BOTH files actually have a Program OR Combi in whose
//   content DIFFERS between them (findDivergentProgramsAcrossDatasets()/
//   findDivergentCombisAcrossDatasets()) -- "same slot, different
//   content" -- the inverse question. Two snapshots of what's meant to be
//   the same rig, showing what's drifted apart. Setlist comparison
//   deliberately NOT included yet (per direct decision) -- Setlist slots
//   have no existing per-slot content hash the way Programs/Combis do;
//   building one is its own follow-up once confirmed against real bytes,
//   not guessed at here.
//
// Caching (both tools): the last search's result stays displayed across
// sidebar close/reopen (so repeat open/close is instant, no bridge round-
// trip) as long as (a) the exact selection hasn't changed and (b) none of
// the datasets involved have gone dirty since, checked via listDatasets()'s
// own `dirty` flag -- a coarse, ANY-write-invalidates signal (entry 89's
// own "dropped on ANY write... coarse, not a new per-dataset version
// counter" decision), not a byte-level "did the search actually change"
// check. Clicking Find/Compare always recomputes and overwrites the cache
// regardless.
//
// Both tools are read-only: no resolve/write action from this view at all.
// For "Compare two files" specifically, clicking a divergence row jumps
// BOTH panes at once -- dataset A's slot into the LEFT pane, dataset B's
// into the RIGHT -- rather than the single-target click/shift+click
// "Find duplicates" uses, since the whole point of this tool is a live
// side-by-side look at what changed, and this app already has two panes
// built for exactly that.
//
// Wrapped in an IIFE, same reason every other app-level sidebar file is
// (STATE.md entry 60) -- classic <script> tags on one page share ONE global
// lexical scope for let/const.
(function () {

const sidebar = window.createSidebarPanel(document.getElementById("crossDatasetDuplicatesPanelRoot"), { edge: "right" });

let toolMode = "duplicates";  // "duplicates" | "compare"

let knownDatasets = [];              // last listDatasets() result: [{datasetId, displayName, setlistCount, dirty}]

// --- "Find duplicates" state -------------------------------------------
let selectedDatasetIds = new Set();  // persists across sidebar re-opens, filtered down whenever a dataset closes
let presentBanksUnion = new Set();   // union of every OPEN dataset's own present Program banks -- see refreshOpenDatasets()
let selectedBanks = new Set();
let duplicatesViewMode = "filters";  // "filters" | "results"
let isSearching = false;
let resultGroups = [];
// { datasetIds:[sorted], bankFilter:[sorted], groups, dirtyByDataset:Map<datasetId,bool> } or null
let cache = null;

// --- "Compare two files" state ------------------------------------------
let compareDatasetIdA = null;
let compareDatasetIdB = null;
let compareBankFilter = new Set();   // Programs-only filter; reuses presentBanksUnion for its button list
let compareViewMode = "filters";     // "filters" | "results"
let compareIsSearching = false;
let compareProgramDivergences = [];
let compareCombiDivergences = [];
// { idA, idB, bankFilter:[sorted], programs, combis, dirtyA, dirtyB } or null
let compareCache = null;
// Which Combi divergence rows are expanded to show their own per-change
// resolve rows (STATE.md entry 94) -- keyed "bank-number", same convention
// as the Duplicates panel's own expandedDuplicateKeys. Program rows are
// never expandable (no `changes` to show -- see PcgFile::ProgramDivergence's
// own doc comment), so this Set only ever holds Combi keys.
let expandedCombiKeys = new Set();

function combiDivergenceKey(bank, number) {
  return `${bank}-${number}`;
}

function sortedIds(set) {
  return [...set].sort((a, b) => a - b);
}

function arraysEqual(a, b) {
  return a.length === b.length && a.every((v, i) => v === b[i]);
}

// `displayName` is a full path everywhere else in this app (datasets.js's
// own comment) -- this file's own compare-results column headers want a
// short label, same reasoning EditorBridge's basenameOf() gives the
// duplicate-finder's own `filename` field.
function basenameOfPath(path) {
  const idx = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
  return idx === -1 ? path : path.slice(idx + 1);
}

// A cached "Find duplicates" result stays usable only for the EXACT
// selection it was computed for, and only while none of ITS OWN datasets'
// dirty flags have flipped since (see this file's own top comment).
// `freshDatasets` is a just-fetched listDatasets() result.
function cacheIsUsable(freshDatasets) {
  if (!cache) return false;
  if (!arraysEqual(cache.datasetIds, sortedIds(selectedDatasetIds))) return false;
  if (!arraysEqual(cache.bankFilter, sortedIds(selectedBanks))) return false;
  for (const id of cache.datasetIds) {
    const fresh = freshDatasets.find((d) => d.datasetId === id);
    if (!fresh) return false; // that dataset closed since the cache was built
    if (fresh.dirty !== cache.dirtyByDataset.get(id)) return false; // touched since
  }
  return true;
}

// Same idea as cacheIsUsable() above, for "Compare two files".
function compareCacheIsUsable(freshDatasets) {
  if (!compareCache) return false;
  if (compareCache.idA !== compareDatasetIdA || compareCache.idB !== compareDatasetIdB) return false;
  if (!arraysEqual(compareCache.bankFilter, sortedIds(compareBankFilter))) return false;
  const freshA = freshDatasets.find((d) => d.datasetId === compareCache.idA);
  const freshB = freshDatasets.find((d) => d.datasetId === compareCache.idB);
  if (!freshA || !freshB) return false; // one of the two closed since
  if (freshA.dirty !== compareCache.dirtyA || freshB.dirty !== compareCache.dirtyB) return false;
  return true;
}

async function refreshOpenDatasets() {
  knownDatasets = await window.listDatasets();
  const openIds = new Set(knownDatasets.map((d) => d.datasetId));

  // "Find duplicates": keep the user's own checkbox choices across sidebar
  // re-opens; default to "everything open" the first time, or once every
  // previous choice has closed out from under it.
  selectedDatasetIds = new Set([...selectedDatasetIds].filter((id) => openIds.has(id)));
  if (selectedDatasetIds.size === 0) selectedDatasetIds = new Set(openIds);

  // The bank filter list is the UNION of every OPEN dataset's own present
  // Program banks, deliberately NOT restricted to just the checked ones
  // (2026-09-11, per direct decision) -- so toggling a dataset checkbox
  // never reshuffles which bank buttons are even enabled. Shared as-is by
  // "Compare two files" below -- both tools want the same union.
  presentBanksUnion = new Set();
  await Promise.all(
    knownDatasets.map(async (d) => {
      const entries = await window.getProgramBankTypes(d.datasetId);
      for (const e of entries) presentBanksUnion.add(e.bank);
    })
  );
  selectedBanks = new Set([...selectedBanks].filter((b) => presentBanksUnion.has(b)));
  if (selectedBanks.size === 0) selectedBanks = new Set(presentBanksUnion);

  if (cacheIsUsable(knownDatasets)) {
    resultGroups = cache.groups;
    duplicatesViewMode = "results";
  } else {
    cache = null;
    duplicatesViewMode = "filters";
  }

  // "Compare two files": keep prior picks if both are still open and
  // distinct; otherwise default to the first two distinct open datasets.
  // A is re-defaulted AVOIDING WHATEVER B ALREADY HOLDS, and vice versa --
  // picking A's fallback first without checking against B's surviving
  // value (a real bug caught while verifying this in isolation) could
  // silently re-collide the two onto the same dataset, e.g. A closes while
  // B=7 survives, and knownDatasets[0] also happens to be 7.
  if (compareDatasetIdA != null && !openIds.has(compareDatasetIdA)) compareDatasetIdA = null;
  if (compareDatasetIdB != null && !openIds.has(compareDatasetIdB)) compareDatasetIdB = null;
  if (compareDatasetIdA != null && compareDatasetIdA === compareDatasetIdB) compareDatasetIdB = null;
  if (compareDatasetIdA == null) {
    const candidate = knownDatasets.find((d) => d.datasetId !== compareDatasetIdB);
    compareDatasetIdA = candidate ? candidate.datasetId : null;
  }
  if (compareDatasetIdB == null) {
    const candidate = knownDatasets.find((d) => d.datasetId !== compareDatasetIdA);
    compareDatasetIdB = candidate ? candidate.datasetId : null;
  }

  compareBankFilter = new Set([...compareBankFilter].filter((b) => presentBanksUnion.has(b)));
  if (compareBankFilter.size === 0) compareBankFilter = new Set(presentBanksUnion);

  if (compareCacheIsUsable(knownDatasets)) {
    compareProgramDivergences = compareCache.programs;
    compareCombiDivergences = compareCache.combis;
    compareViewMode = "results";
  } else {
    compareCache = null;
    compareViewMode = "filters";
  }

  sidebar.update();
}

async function runFind() {
  const datasetIds = sortedIds(selectedDatasetIds);
  const bankFilter = sortedIds(selectedBanks);
  isSearching = true;
  sidebar.update();

  const groups = await window.findDuplicateProgramsAcrossDatasets(datasetIds, bankFilter);

  isSearching = false;
  resultGroups = groups;
  cache = {
    datasetIds,
    bankFilter,
    groups,
    dirtyByDataset: new Map(knownDatasets.filter((d) => datasetIds.includes(d.datasetId)).map((d) => [d.datasetId, d.dirty])),
  };
  duplicatesViewMode = "results";
  sidebar.update();
}

async function runCompare() {
  if (compareDatasetIdA == null || compareDatasetIdB == null || compareDatasetIdA === compareDatasetIdB) return;
  const idA = compareDatasetIdA;
  const idB = compareDatasetIdB;
  const bankFilter = sortedIds(compareBankFilter);
  compareIsSearching = true;
  sidebar.update();

  const [programs, combis] = await Promise.all([
    window.findDivergentProgramsAcrossDatasets(idA, idB, bankFilter),
    window.findDivergentCombisAcrossDatasets(idA, idB),
  ]);

  compareIsSearching = false;
  compareProgramDivergences = programs;
  compareCombiDivergences = combis;
  const freshA = knownDatasets.find((d) => d.datasetId === idA);
  const freshB = knownDatasets.find((d) => d.datasetId === idB);
  compareCache = {
    idA, idB, bankFilter, programs, combis,
    dirtyA: freshA ? freshA.dirty : false,
    dirtyB: freshB ? freshB.dirty : false,
  };
  compareViewMode = "results";
  sidebar.update();
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
  await runCompare();
}

function buildFiltersView(bodyEl) {
  const datasetsHeading = document.createElement("h3");
  datasetsHeading.className = "sidebar-section-heading";
  datasetsHeading.textContent = "Datasets to search";
  bodyEl.appendChild(datasetsHeading);

  if (knownDatasets.length === 0) {
    const empty = document.createElement("div");
    empty.className = "usage-empty";
    empty.textContent = "No datasets are open.";
    bodyEl.appendChild(empty);
  } else {
    for (const d of knownDatasets) {
      const label = document.createElement("label");
      label.className = "cross-dataset-dup-checkbox-row";
      const checkbox = document.createElement("input");
      checkbox.type = "checkbox";
      checkbox.checked = selectedDatasetIds.has(d.datasetId);
      checkbox.addEventListener("change", () => {
        if (checkbox.checked) selectedDatasetIds.add(d.datasetId);
        else selectedDatasetIds.delete(d.datasetId);
        sidebar.update();
      });
      const span = document.createElement("span");
      span.textContent = `#${d.datasetId} — ${d.displayName}`;
      label.append(checkbox, span);
      bodyEl.appendChild(label);
    }
  }

  const banksHeading = document.createElement("h3");
  banksHeading.className = "sidebar-section-heading";
  banksHeading.textContent = "Banks to search";
  bodyEl.appendChild(banksHeading);
  const bankFilterRow = document.createElement("div");
  bankFilterRow.className = "bank-filter-row";
  // No getBankType() here (unlike pane.js's own Programs bank-filter row) --
  // a bank's engine type is a per-FILE fact (ProgramBankType), and this row
  // spans several files at once, potentially with different answers for the
  // same bank index; showing one file's answer next to a checkbox that
  // isn't scoped to that file would be misleading rather than helpful.
  renderBankFilterRow(bankFilterRow, PROGRAM_BANK_NAMES, presentBanksUnion, selectedBanks, () => {}, null);
  bodyEl.appendChild(bankFilterRow);

  const findBtn = document.createElement("button");
  findBtn.type = "button";
  findBtn.className = "button is-small accent-button";
  findBtn.textContent = isSearching ? "Searching..." : "Find";
  const notEnoughDatasets = selectedDatasetIds.size < 2;
  findBtn.disabled = isSearching || notEnoughDatasets;
  findBtn.title = notEnoughDatasets
    ? "Select at least 2 datasets -- a match confined to one dataset is already covered by that dataset's own Duplicates tab."
    : "";
  findBtn.addEventListener("click", runFind);
  bodyEl.appendChild(findBtn);
}

function buildResultsView(bodyEl) {
  const heading = document.createElement("h3");
  heading.className = "sidebar-section-heading";
  heading.textContent = `${resultGroups.length} duplicate group(s) across ${cache.datasetIds.length} dataset(s)`;
  bodyEl.appendChild(heading);

  const backBtn = document.createElement("button");
  backBtn.type = "button";
  backBtn.className = "button is-small";
  backBtn.textContent = "← New search";
  backBtn.addEventListener("click", () => {
    duplicatesViewMode = "filters";
    sidebar.update();
  });
  bodyEl.appendChild(backBtn);

  if (resultGroups.length === 0) {
    const empty = document.createElement("div");
    empty.className = "usage-empty";
    empty.textContent = "No cross-file duplicate Programs found for this selection.";
    bodyEl.appendChild(empty);
    return;
  }

  const hint = document.createElement("div");
  hint.className = "usage-note";
  hint.textContent = "Click a row to jump to it in the right pane; Shift+click for the left pane.";
  bodyEl.appendChild(hint);

  const table = document.createElement("table");
  // is-hoverable (2026-09-20, entry 96 fix) -- Bulma's own shared row-hover
  // treatment (a subtle dark overlay), matching the Setlist/Programs/Combis
  // tables exactly, instead of this table's own former hand-rolled orange
  // hover -- see style.css's own comment on .cross-dataset-dup-row.is-open
  // for why that orange hover was ALSO hiding an opened row's own orange
  // title in practice.
  table.className = "table is-fullwidth is-hoverable is-narrow cross-dataset-dup-table";
  table.innerHTML = "<thead><tr><th>Name</th><th>Datasource</th><th>ID</th></tr></thead>";
  const tbody = document.createElement("tbody");

  for (const group of resultGroups) {
    const groupHeaderRow = document.createElement("tr");
    groupHeaderRow.className = "cross-dataset-dup-group-header";
    const groupTd = document.createElement("td");
    groupTd.colSpan = 3;
    groupTd.textContent = `${group.members.length} copies`;
    groupHeaderRow.appendChild(groupTd);
    tbody.appendChild(groupHeaderRow);

    for (const member of group.members) {
      const tr = document.createElement("tr");
      tr.className = "cross-dataset-dup-row";
      tr.tabIndex = 0;

      const nameTd = document.createElement("td");
      nameTd.textContent = member.name;
      const dsTd = document.createElement("td");
      dsTd.textContent = member.filename;
      const idTd = document.createElement("td");
      idTd.textContent = formatBankNumber({ isProgram: true, bank: member.bank, number: member.number }, member.bankType);
      tr.append(nameTd, dsTd, idTd);

      tr.addEventListener("click", (evt) => jumpToMember(member, evt.shiftKey));
      tr.addEventListener("keydown", (evt) => {
        if (evt.key === "Enter" || evt.key === " ") {
          evt.preventDefault();
          jumpToMember(member, evt.shiftKey);
        }
      });
      tbody.appendChild(tr);
    }
  }
  table.appendChild(tbody);
  bodyEl.appendChild(table);
}

function buildCompareFiltersView(bodyEl) {
  const heading = document.createElement("h3");
  heading.className = "sidebar-section-heading";
  heading.textContent = "Datasets to compare";
  bodyEl.appendChild(heading);

  if (knownDatasets.length < 2) {
    const empty = document.createElement("div");
    empty.className = "usage-empty";
    empty.textContent = "Open at least 2 datasets to compare.";
    bodyEl.appendChild(empty);
    return;
  }

  function buildDatasetSelect(labelText, currentId, onChange) {
    const label = document.createElement("label");
    label.textContent = labelText;
    const wrap = document.createElement("div");
    wrap.className = "select is-small is-fullwidth";
    const select = document.createElement("select");
    for (const d of knownDatasets) {
      const opt = document.createElement("option");
      opt.value = String(d.datasetId);
      opt.textContent = `#${d.datasetId} — ${d.displayName}`;
      opt.selected = d.datasetId === currentId;
      select.appendChild(opt);
    }
    select.addEventListener("change", () => {
      onChange(Number(select.value));
      sidebar.update();
    });
    wrap.appendChild(select);
    bodyEl.append(label, wrap);
  }

  buildDatasetSelect("Dataset A (opens in the left pane)", compareDatasetIdA, (v) => { compareDatasetIdA = v; });
  buildDatasetSelect("Dataset B (opens in the right pane)", compareDatasetIdB, (v) => { compareDatasetIdB = v; });

  const banksHeading = document.createElement("h3");
  banksHeading.className = "sidebar-section-heading";
  banksHeading.textContent = "Banks to compare (Programs only -- Combis have no bank filter)";
  bodyEl.appendChild(banksHeading);
  const bankFilterRow = document.createElement("div");
  bankFilterRow.className = "bank-filter-row";
  renderBankFilterRow(bankFilterRow, PROGRAM_BANK_NAMES, presentBanksUnion, compareBankFilter, () => {}, null);
  bodyEl.appendChild(bankFilterRow);

  const compareBtn = document.createElement("button");
  compareBtn.type = "button";
  compareBtn.className = "button is-small accent-button";
  compareBtn.textContent = compareIsSearching ? "Comparing..." : "Compare";
  const sameDataset = compareDatasetIdA != null && compareDatasetIdA === compareDatasetIdB;
  compareBtn.disabled = compareIsSearching || compareDatasetIdA == null || compareDatasetIdB == null || sameDataset;
  compareBtn.title = sameDataset ? "Pick two different datasets -- a dataset never diverges from itself." : "";
  compareBtn.addEventListener("click", runCompare);
  bodyEl.appendChild(compareBtn);
}

// One expanded Combi divergence's own per-change resolve rows (STATE.md
// entry 94, direct request) -- one row per `changes` entry, each with a
// "←"/"→" button that copies that SPECIFIC change's raw bytes one way or
// the other. `nameAHeader`/`nameBHeader` (already the short basenames
// buildCompareResultsView() computed) go into the button tooltips so
// "which file am I about to overwrite" reads as a real filename, not just
// "A"/"B" -- those letters mean this table's own left/right column, not
// either Norton pane, which is worth a real name to avoid confusing the two.
function buildCombiChangeRows(tbody, d, nameAHeader, nameBHeader) {
  for (const description of d.changes) {
    const tr = document.createElement("tr");
    tr.className = "cross-dataset-dup-change-row";

    // colspan over the ID + NameA columns (per direct request) -- the
    // description text is this row's whole point and gets the room; only
    // the actions cell (NameB's own column) stays separate.
    const descTd = document.createElement("td");
    descTd.colSpan = 2;
    descTd.textContent = description;
    tr.appendChild(descTd);

    const actionsTd = document.createElement("td");
    actionsTd.className = "cross-dataset-dup-change-actions";
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
    actionsTd.append(leftBtn, rightBtn);
    tr.appendChild(actionsTd);

    tbody.appendChild(tr);
  }
}

// Shared table renderer for both the Programs and Combis divergence
// sections below -- `isProgram` picks whether formatBankNumber() gets a
// bankType suffix at all (Combis have none), and which click interaction a
// row gets:
//   - Program rows have no `changes` to show (Programs have no confirmed
//     internal field layout in this repo yet -- see PcgFile::
//     ProgramDivergence's own doc comment) -- UNCHANGED from before this
//     entry: a single click jumps both panes (jumpToDivergence()).
//   - Combi rows (STATE.md entry 94, direct request): a single click
//     toggles an inline expand showing one resolve row per `changes`
//     entry (buildCombiChangeRows() above), same "click a row to expand
//     it inline" interaction/coloring this app's Setlist row editors
//     already use (accordion-* CSS classes, style.css); a DOUBLE click
//     jumps both panes instead (the single-click behavior every row used
//     to have). The two coexist via a short delay on the single click,
//     cancelled if a dblclick follows -- the standard way to let both
//     gestures share one element without the browser's own
//     click-click-then-dblclick sequence firing the single-click action
//     twice first.
function buildDivergenceTable(bodyEl, { heading, rows, isProgram, nameAHeader, nameBHeader, emptyMessage }) {
  const h = document.createElement("h3");
  h.className = "sidebar-section-heading";
  h.textContent = heading;
  bodyEl.appendChild(h);

  if (rows.length === 0) {
    const empty = document.createElement("div");
    empty.className = "usage-empty";
    empty.textContent = emptyMessage;
    bodyEl.appendChild(empty);
    return;
  }

  const table = document.createElement("table");
  // is-hoverable (2026-09-20, entry 96 fix) -- Bulma's own shared row-hover
  // treatment (a subtle dark overlay), matching the Setlist/Programs/Combis
  // tables exactly, instead of this table's own former hand-rolled orange
  // hover -- see style.css's own comment on .cross-dataset-dup-row.is-open
  // for why that orange hover was ALSO hiding an opened row's own orange
  // title in practice.
  table.className = "table is-fullwidth is-hoverable is-narrow cross-dataset-dup-table";
  table.innerHTML = `<thead><tr><th>ID</th><th>${nameAHeader}</th><th>${nameBHeader}</th></tr></thead>`;
  const tbody = document.createElement("tbody");
  for (const d of rows) {
    const tr = document.createElement("tr");
    tr.className = "cross-dataset-dup-row";
    tr.tabIndex = 0;

    const key = combiDivergenceKey(d.bank, d.number);
    const isExpanded = !isProgram && expandedCombiKeys.has(key);
    if (isExpanded) tr.classList.add("is-open");

    const idTd = document.createElement("td");
    // Small expand/collapse hint, Combi rows only -- same chevron
    // convention (▸/▾) this app's Setlist accordion sections already use.
    const idLabel = formatBankNumber({ isProgram, bank: d.bank, number: d.number }, isProgram ? d.bankType : null);
    idTd.textContent = isProgram ? idLabel : `${isExpanded ? "▾" : "▸"} ${idLabel}`;
    const aTd = document.createElement("td");
    aTd.textContent = d.nameA;
    const bTd = document.createElement("td");
    bTd.textContent = d.nameB;
    tr.append(idTd, aTd, bTd);

    if (isProgram) {
      // Unchanged from before entry 94 -- nothing to expand for a Program
      // row, so a single click still jumps both panes directly.
      tr.addEventListener("click", () => jumpToDivergence(true, d.bank, d.number));
      tr.addEventListener("keydown", (evt) => {
        if (evt.key === "Enter" || evt.key === " ") {
          evt.preventDefault();
          jumpToDivergence(true, d.bank, d.number);
        }
      });
    } else {
      let pendingExpandTimer = null;
      tr.addEventListener("click", () => {
        if (pendingExpandTimer) return;  // a dblclick may still follow -- don't double-fire
        pendingExpandTimer = setTimeout(() => {
          pendingExpandTimer = null;
          if (expandedCombiKeys.has(key)) expandedCombiKeys.delete(key);
          else expandedCombiKeys.add(key);
          sidebar.update();
        }, 220);
      });
      tr.addEventListener("dblclick", () => {
        if (pendingExpandTimer) {
          clearTimeout(pendingExpandTimer);
          pendingExpandTimer = null;
        }
        jumpToDivergence(false, d.bank, d.number);
      });
      tr.addEventListener("keydown", (evt) => {
        if (evt.key === "Enter" || evt.key === " ") {
          evt.preventDefault();
          if (expandedCombiKeys.has(key)) expandedCombiKeys.delete(key);
          else expandedCombiKeys.add(key);
          sidebar.update();
        }
      });
    }
    tbody.appendChild(tr);

    if (isExpanded && d.changes && d.changes.length > 0) {
      buildCombiChangeRows(tbody, d, nameAHeader, nameBHeader);
    }
  }
  table.appendChild(tbody);
  bodyEl.appendChild(table);
}

function buildCompareResultsView(bodyEl) {
  const knownA = knownDatasets.find((d) => d.datasetId === compareCache.idA);
  const knownB = knownDatasets.find((d) => d.datasetId === compareCache.idB);
  const nameA = knownA ? basenameOfPath(knownA.displayName) : `#${compareCache.idA}`;
  const nameB = knownB ? basenameOfPath(knownB.displayName) : `#${compareCache.idB}`;

  const summary = document.createElement("h3");
  summary.className = "sidebar-section-heading";
  summary.textContent =
    `${compareProgramDivergences.length} Program + ${compareCombiDivergences.length} Combi divergence(s)`;
  bodyEl.appendChild(summary);

  const backBtn = document.createElement("button");
  backBtn.type = "button";
  backBtn.className = "button is-small";
  backBtn.textContent = "← New comparison";
  backBtn.addEventListener("click", () => {
    compareViewMode = "filters";
    sidebar.update();
  });
  bodyEl.appendChild(backBtn);

  const hint = document.createElement("div");
  hint.className = "usage-note";
  hint.textContent =
    "Programs: click a row to open A in the left pane and B in the right pane, both jumped to that slot. " +
    "Combis: click a row to see exactly what changed and resolve it with ←/→; double-click to jump both panes instead.";
  bodyEl.appendChild(hint);

  buildDivergenceTable(bodyEl, {
    heading: "Programs",
    rows: compareProgramDivergences,
    isProgram: true,
    nameAHeader: nameA,
    nameBHeader: nameB,
    emptyMessage: "No Program divergences found.",
  });
  buildDivergenceTable(bodyEl, {
    heading: "Combis",
    rows: compareCombiDivergences,
    isProgram: false,
    nameAHeader: nameA,
    nameBHeader: nameB,
    emptyMessage: "No Combi divergences found.",
  });
}

function buildModeToggle(bodyEl) {
  const wrap = document.createElement("div");
  wrap.className = "buttons has-addons mb-3 cross-dataset-mode-toggle";
  for (const [key, label] of [
    ["duplicates", "Find duplicates"],
    ["compare", "Compare two files"],
  ]) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "button is-small" + (toolMode === key ? " is-link" : "");
    btn.textContent = label;
    btn.addEventListener("click", () => {
      if (toolMode === key) return;
      toolMode = key;
      sidebar.update({ title: key === "duplicates" ? "Find duplicates across files" : "Compare two files" });
    });
    wrap.appendChild(btn);
  }
  bodyEl.appendChild(wrap);
}

function buildBody(bodyEl) {
  buildModeToggle(bodyEl);
  if (toolMode === "duplicates") {
    if (duplicatesViewMode === "results" && cache) buildResultsView(bodyEl);
    else buildFiltersView(bodyEl);
  } else {
    if (compareViewMode === "results" && compareCache) buildCompareResultsView(bodyEl);
    else buildCompareFiltersView(bodyEl);
  }
}

window.toggleCrossDatasetDuplicatesPanel = () => {
  if (sidebar.isOpen()) {
    sidebar.close();
    return;
  }
  sidebar.open({ title: "Find duplicates across files", build: buildBody });
  refreshOpenDatasets();
};

})();
