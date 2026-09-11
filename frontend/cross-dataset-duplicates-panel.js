// Cross-dataset Program duplicate finder (STATE.md entry 89, 2026-09-11) --
// a GLOBAL (not per-pane) tool that finds byte-exact duplicate Programs
// across ALL currently open datasets at once, complementing the existing
// PER-DATASET Duplicates tab (pane-program-editor.js's
// createDuplicatesPanel(), EditorBridge::findDuplicatePrograms()), which
// only ever looks inside one file. Reached via a topbar icon next to the
// pane-visibility [left|both|right] buttons (index.html).
//
// One sidebar (createSidebarPanel(), sidebar-panel.js), two views rather
// than a separate results overlay on top of it -- STATE.md entry 89's own
// "in-window overlay, not a second native window" decision is already
// satisfied by the sidebar's own slide-in shell; a SECOND overlay mechanism
// stacked on top of that would just be more machinery for no real benefit.
// "filters" view: dataset checkboxes + bank filter + Find. "results" view:
// the grouped duplicate table; "New search" goes back to "filters".
//
// Caching: the last search's result stays displayed across sidebar close/
// reopen (so repeat open/close is instant, no bridge round-trip) as long as
// (a) the dataset/bank selection hasn't changed and (b) none of ITS OWN
// datasets have gone dirty since, checked via listDatasets()'s own `dirty`
// flag -- a coarse, ANY-write-invalidates signal (STATE.md entry 89's own
// "dropped on ANY write... coarse, not a new per-dataset version counter"
// decision), not a byte-level "did the search actually change" check.
// Clicking "Find" always recomputes and overwrites the cache regardless.
//
// Scope, per STATE.md entry 89: Programs only (Combi cross-dataset
// duplicates explicitly deferred), no resolve/write action from this view
// at all -- read-only jump-to-pane is the whole feature.
//
// Wrapped in an IIFE, same reason every other app-level sidebar file is
// (STATE.md entry 60) -- classic <script> tags on one page share ONE global
// lexical scope for let/const.
(function () {

const sidebar = window.createSidebarPanel(document.getElementById("crossDatasetDuplicatesPanelRoot"), { edge: "right" });

let knownDatasets = [];              // last listDatasets() result: [{datasetId, displayName, setlistCount, dirty}]
let selectedDatasetIds = new Set();  // persists across sidebar re-opens, filtered down whenever a dataset closes
let presentBanksUnion = new Set();   // union of every OPEN dataset's own present Program banks -- see refreshOpenDatasets()
let selectedBanks = new Set();
let mode = "filters";                // "filters" | "results"
let isSearching = false;
let resultGroups = [];
// { datasetIds:[sorted], bankFilter:[sorted], groups, dirtyByDataset:Map<datasetId,bool> } or null
let cache = null;

function sortedIds(set) {
  return [...set].sort((a, b) => a - b);
}

function arraysEqual(a, b) {
  return a.length === b.length && a.every((v, i) => v === b[i]);
}

// A cached result stays usable only for the EXACT selection it was computed
// for, and only while none of ITS OWN datasets' dirty flags have flipped
// since (see this file's own top comment). `freshDatasets` is a just-
// fetched listDatasets() result.
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

async function refreshOpenDatasets() {
  knownDatasets = await window.listDatasets();
  const openIds = new Set(knownDatasets.map((d) => d.datasetId));

  // Keep the user's own checkbox choices across sidebar re-opens; default to
  // "everything open" the first time, or once every previous choice has
  // closed out from under it.
  selectedDatasetIds = new Set([...selectedDatasetIds].filter((id) => openIds.has(id)));
  if (selectedDatasetIds.size === 0) selectedDatasetIds = new Set(openIds);

  // The bank filter list is the UNION of every OPEN dataset's own present
  // Program banks, deliberately NOT restricted to just the checked ones
  // (2026-09-11, per direct decision) -- so toggling a dataset checkbox
  // never reshuffles which bank buttons are even enabled.
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
    mode = "results";
  } else {
    cache = null;
    mode = "filters";
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
  mode = "results";
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

// Click -> right pane, Shift+click -> left pane (per direct request),
// mirroring every other cross-reference jump button in this app. Reuses
// each pane's own loadDataset()/jumpToInstrument() -- no new navigation
// primitive needed.
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
    mode = "filters";
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
  table.className = "table is-fullwidth is-narrow cross-dataset-dup-table";
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

function buildBody(bodyEl) {
  if (mode === "results" && cache) buildResultsView(bodyEl);
  else buildFiltersView(bodyEl);
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
