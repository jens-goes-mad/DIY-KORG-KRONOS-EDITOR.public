// The Programs and Duplicates categories' own renderers -- both operate
// purely on Program bytes/references (Duplicates never touches a Combi
// directly, even though resolving a duplicate can repoint a Combi Timbre
// reference as a side effect -- see PcgFile::resolveDuplicates()'s own doc
// comment), so both live here together rather than splitting Duplicates into
// its own file. pane.js's createLibraryPanels() (the shared Programs/Combis/
// Duplicates tab coordinator -- filter input, load()/onDatasetChanged()
// orchestration, tab switching) instantiates createProgramsPanel()/
// createDuplicatesPanel() below and wires them together with pane-combi-
// editor.js's createCombisPanel(), the same way createPane() composes
// createSetlistPanel() (pane-setlist-editor.js) and createLibraryPanels()
// (pane.js) themselves. Split out of what was then library.js (2026-08-14),
// continuing the same file-per-editor direction pane-setlist-editor.js
// started -- pane-combi-editor.js (renamed from library.js the same day,
// once nothing Program-related was left in it) completes the split.

// Programs table: filter/search + per-bank filter buttons (elements.
// bankFilterRow/selectControlRow -- the shared coordinator in pane.js owns
// their surrounding DOM/show-hide, this just renders INTO them), one row per
// Program with an expandable Set List/Combi usage row, and Program-to-
// Program drag-and-drop (copy raw bytes into another slot, same dataset or a
// different pane's -- see onDropProgram in app.js and PcgFile::
// copyProgramFrom()'s own doc comment for the validation guards).
//
// `elements` are DOM containers the coordinator already created and
// positions/shows-hides; this function only ever renders INTO them, never
// creates or toggles their visibility itself -- mirrors how
// createSetlistPanel() is handed its own `container` to fill.
// `callbacks.getFilterText()` reads the ONE filter input shared across
// Programs/Combis/Duplicates (typing there keeps filtering whichever tab is
// currently showing) -- owned by the coordinator, not this panel, so it's a
// getter rather than this panel's own state.
function createProgramsPanel(
  { panelTable, bankFilterRow, selectControlRow },
  { getDatasetId, getFilterText, getProgramBankType, onDropProgram, onJumpToSetlist, onJumpToInstrument, log }
) {
  // Set during a Programs row's own dragstart, cleared on dragend -- lets a
  // row being dragged OVER (not just dropped on) show immediate reject
  // feedback if the two banks' engine types don't match, same technique as
  // pane-setlist-editor.js's draggedFromDatasetId (HTML5 DataTransfer
  // payloads aren't readable during dragover, so a plain shared variable
  // stands in). The actual copy still goes through EditorBridge::
  // copyProgram()'s own validation regardless -- this is only a hover hint.
  let draggedProgram = null;

  let programs = [];
  let expandedProgramKey = null;  // `${bank}-${number}` of the one expanded usage row, if any
  // Bank-filter state -- `present` is which bank indices actually have
  // entries in the current dataset (recomputed on every fetch), `filter` is
  // which of those are currently "pressed" (shown), independently user-
  // toggleable. Only reset to match `present` (show everything) on a
  // genuinely new dataset (onDatasetChanged()) -- a same-dataset refresh
  // (refresh()) keeps whatever the user had filtered to instead.
  let programPresentBanks = new Set();
  let programBankFilter = new Set();

  function refreshBankButtons() {
    renderBankFilterRow(bankFilterRow, PROGRAM_BANK_NAMES, programPresentBanks, programBankFilter, () => render(), getProgramBankType);
  }

  // Wired up once (not re-created per fetch/render) -- see
  // createSelectControlRow()'s own comment for why getters instead of
  // captured Set values.
  createSelectControlRow(selectControlRow, {
    getPresent: () => programPresentBanks,
    getFilterSet: () => programBankFilter,
    onChange: () => {
      refreshBankButtons();
      render();
    },
  });

  // Each Set List/Combi usage entry below renders as its own bank-jump-style
  // button (reusing .bank-jump-button's look, same as the Combi Timbre bank
  // reference button -- see pane-combi-editor.js's buildTimbreRow()) rather than plain
  // text: a Program can be referenced from many places, so unlike the
  // single-target Combi-Timbre-to-Program jump, this is one jump target per
  // list item. Set List jumps go through onJumpToSetlist (switches this pane
  // to its Setlist category, selects that Set List, opens+scrolls to that
  // slot); Combi jumps reuse the existing onJumpToInstrument (isProgram: false).
  function buildUsageRow(program) {
    const tr = document.createElement("tr");
    tr.className = "editor-row";  // reuses the shared expand-row look from pane.js/style.css
    const td = document.createElement("td");
    td.colSpan = 5;  // Bank, Name, Type, #STL, #CMB -- a real <table> again

    const box = document.createElement("div");
    box.textContent = "Loading usage...";
    td.appendChild(box);
    tr.appendChild(td);

    (async () => {
      const usage = await window.getProgramUsage(getDatasetId(), program.bank, program.number);
      box.innerHTML = "";
      if (!usage.ok) {
        box.textContent = `Error: ${usage.error}`;
        return;
      }

      const heading = document.createElement("div");
      heading.className = "usage-heading";
      heading.textContent = `Set List usage (${usage.setlistUsages.length}):`;
      box.appendChild(heading);

      if (usage.setlistUsages.length === 0) {
        const none = document.createElement("div");
        none.className = "usage-empty";
        none.textContent = "No Set List slot directly references this Program.";
        box.appendChild(none);
      } else {
        const list = document.createElement("ul");
        list.className = "usage-list";
        for (const u of usage.setlistUsages) {
          const li = document.createElement("li");
          const btn = document.createElement("button");
          btn.type = "button";
          btn.className = "button is-small bank-jump-button usage-jump-button";
          btn.textContent = `${u.setlistName} -- slot ${kronosNumber(u.songIndex)}`;
          btn.title =
            "Show this in the Setlist view (Shift+click: show in the opposite pane instead, switching its " +
            "dataset to match this one; Shift+Cmd+click: same, but keep whatever dataset the opposite pane " +
            "already has open)";
          btn.addEventListener("click", (ev) => {
            ev.stopPropagation();  // don't also toggle this usage row closed
            onJumpToSetlist({
              setlistIndex: u.setlistIndex,
              songIndex: u.songIndex,
              from: { kind: "instrument", isProgram: true, bank: program.bank, number: program.number },
              toOpposite: ev.shiftKey,
              keepOppositeDataset: ev.metaKey,
            });
          });
          li.appendChild(btn);
          list.appendChild(li);
        }
        box.appendChild(list);
      }

      if (!usage.combiUsagesAvailable) {
        const note = document.createElement("div");
        note.className = "usage-note";
        note.textContent =
          "Combi usage: not available for this bank yet -- only confirmed for 8 individually-verified " +
          "banks so far (INT-A..D, USER-A/D/F/AA). See docs/content/format/index.md's Combi Timbre references section.";
        box.appendChild(note);
      } else {
        const combiHeading = document.createElement("div");
        combiHeading.className = "usage-heading";
        combiHeading.textContent = `Combi usage (${usage.combiUsages.length}):`;
        box.appendChild(combiHeading);

        if (usage.combiUsages.length === 0) {
          const none = document.createElement("div");
          none.className = "usage-empty";
          none.textContent = "No Combi's Timbres reference this Program.";
          box.appendChild(none);
        } else {
          const list = document.createElement("ul");
          list.className = "usage-list";
          for (const c of usage.combiUsages) {
            const li = document.createElement("li");
            const btn = document.createElement("button");
            btn.type = "button";
            btn.className = "button is-small bank-jump-button usage-jump-button";
            btn.textContent = `${formatBankNumber({ isProgram: false, bank: c.bank, number: c.number })} "${c.name || "(empty)"}"`;
            btn.title =
              "Show this Combi in this pane's Combis view (Shift+click: show in the opposite pane instead, " +
              "switching its dataset to match this one; Shift+Cmd+click: same, but keep whatever dataset the " +
              "opposite pane already has open)";
            if (!c.active) {
              btn.textContent += " (via an Off Timbre only)";
              btn.classList.add("timbre-inactive-ref");
            }
            btn.addEventListener("click", (ev) => {
              ev.stopPropagation();  // don't also toggle this usage row closed
              onJumpToInstrument({
                isProgram: false,
                bank: c.bank,
                number: c.number,
                from: { kind: "instrument", isProgram: true, bank: program.bank, number: program.number },
                toOpposite: ev.shiftKey,
                keepOppositeDataset: ev.metaKey,
              });
            });
            li.appendChild(btn);
            list.appendChild(li);
          }
          box.appendChild(list);
        }
      }
    })();

    return tr;
  }

  function render() {
    const needle = getFilterText().trim().toLowerCase();
    const rows = filterByName(programs, needle).filter((p) => programBankFilter.has(p.bank));

    panelTable.innerHTML = "";
    const table = document.createElement("table");
    table.className = "table is-fullwidth is-hoverable is-narrow programs-table";
    table.innerHTML =
      colgroupHtml([2.6, null, 1.3, 1.3, 1.3]) +
      "<thead><tr><th>Bank</th><th>Name</th><th " +
      "title=\"HD-1 or EXi -- not yet cross-checked against a real backup, see docs/external/README.md\">Type</th>" +
      "<th title=\"Set List references\">#STL</th>" +
      "<th title=\"Combi references\">#CMB</th></tr></thead><tbody></tbody>";
    const tbody = table.querySelector("tbody");

    for (const p of rows) {
      const tr = document.createElement("tr");
      const nameTd = document.createElement("td");
      nameTd.textContent = p.name || "(empty)";
      const typeTd = document.createElement("td");
      typeTd.textContent = p.bankType || "";
      tr.append(
        bankCell(true, p.bank, p.number),
        nameTd,
        typeTd,
        refCell(String(p.setlistReferenceCount), false),
        p.combiReferenceCountAvailable
          ? refCell(String(p.combiReferenceCount), false)
          : refCell("n/a", true)
      );

      const key = `${p.bank}-${p.number}`;
      tr.dataset.entryKey = key;  // lets jumpToEntry() find this exact row after a re-render
      // Bulma's own `tr.is-selected` highlight, not a hand-rolled class.
      if (key === expandedProgramKey) tr.classList.add("is-selected");
      tr.addEventListener("click", () => {
        expandedProgramKey = expandedProgramKey === key ? null : key;
        render();
      });

      // Drag this Program onto another Program row (same pane or a
      // different pane's dataset) to copy its raw bytes into that slot --
      // see onDropProgram in app.js. Copy, not move: dragstart's payload
      // never mutates the source, only drop() (on the TARGET row) calls
      // into the bridge.
      tr.draggable = true;
      tr.addEventListener("dragstart", (ev) => {
        draggedProgram = { datasetId: getDatasetId(), bank: p.bank, number: p.number, bankType: p.bankType };
        ev.dataTransfer.setData("application/json", JSON.stringify(draggedProgram));
        ev.dataTransfer.effectAllowed = "copy";
      });
      tr.addEventListener("dragend", () => {
        draggedProgram = null;
      });
      tr.addEventListener("dragover", (ev) => {
        // Reject up front (no preventDefault -- the browser's own "not
        // allowed" cursor takes over, no `drop` fires here at all) if the
        // dragged Program's engine type doesn't match this row's own bank.
        // EditorBridge::copyProgram() enforces this regardless; this is
        // just immediate hover feedback instead of only after the drop.
        if (draggedProgram != null && draggedProgram.bankType !== p.bankType) {
          tr.classList.remove("drop-target");
          return;
        }
        ev.preventDefault();
        ev.dataTransfer.dropEffect = "copy";
        tr.classList.add("drop-target");
      });
      tr.addEventListener("dragleave", () => tr.classList.remove("drop-target"));
      tr.addEventListener("drop", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        tr.classList.remove("drop-target");
        const raw = ev.dataTransfer.getData("application/json");
        if (!raw) return;
        onDropProgram(JSON.parse(raw), { datasetId: getDatasetId(), bank: p.bank, number: p.number });
      });

      tbody.appendChild(tr);

      if (key === expandedProgramKey) tbody.appendChild(buildUsageRow(p));
    }

    panelTable.appendChild(table);
  }

  async function fetchPrograms() {
    const datasetId = getDatasetId();
    programs = datasetId == null ? [] : await window.listPrograms(datasetId);
    if (datasetId != null) log(`[Library:Programs] Loaded dataset ${datasetId}: ${programs.length} Programs.`);
    programPresentBanks = new Set(programs.map((p) => p.bank));
  }

  // Called by the coordinator (pane.js's createLibraryPanels()) whenever the
  // shared dataset selection changes to a genuinely NEW dataset -- previous
  // filter selections belong to a different file's banks and mean nothing
  // here, so reset to "show everything."
  async function onDatasetChanged() {
    expandedProgramKey = null;
    await fetchPrograms();
    programBankFilter = new Set(programPresentBanks);
    refreshBankButtons();
    render();
  }

  // Called by the coordinator for every OTHER reload (e.g. after a Program
  // copy lands, or a duplicate gets resolved) -- the SAME dataset just
  // changed underneath this view, so re-fetch but keep whatever the user had
  // filtered to. Only drops a filter entry for a bank that no longer has any
  // rows at all, so a stale entry doesn't just silently do nothing forever.
  async function refresh() {
    await fetchPrograms();
    programBankFilter = new Set([...programBankFilter].filter((b) => programPresentBanks.has(b)));
    refreshBankButtons();
    render();
  }

  // Called by the coordinator (via its own jumpToEntry()) after already
  // switching to the "programs" tab -- expands this exact entry's usage row
  // and scrolls it into view, same as clicking the row directly. Makes sure
  // the target bank's filter button is "pressed" first, so it can't hide the
  // entry being jumped to.
  function jumpToEntry(bank, number) {
    const key = `${bank}-${number}`;
    expandedProgramKey = key;
    programBankFilter.add(bank);
    refreshBankButtons();
    render();
    const row = panelTable.querySelector(`[data-entry-key="${key}"]`);
    if (row) scrollRowBelowHeader(row);
  }

  // Combis' formatTimbreRef() (pane-combi-editor.js) needs read access to a Program's
  // name for a confirmed Timbre bank reference -- this is that one read
  // path, handed to createCombisPanel() as a callback rather than Combis
  // reaching into this closure's `programs` array directly.
  function findProgram(bank, number) {
    return programs.find((p) => p.bank === bank && p.number === number);
  }

  return { onDatasetChanged, refresh, render, jumpToEntry, findProgram };
}

// Duplicates table: read-only, no bank filter of its own (unlike Programs/
// Combis) -- every byte-exact duplicate Program group in the current
// dataset, one row per group, click to expand and see (and resolve) its
// copies. Scoped to a single selected dataset -- no cross-dataset dedup
// here, that's a real future idea, not this pass.
function createDuplicatesPanel(
  { panel },
  { getDatasetId, getFilterText, log, onRefreshOppositeLibrary, onNeedsFullReload, onSetlistRefsRepointed }
) {
  let duplicateGroups = [];
  // Joined `${bank}-${number}` lists identifying which duplicate groups are
  // expanded -- a Set, several can be open at once, same multi-open model as
  // pane-combi-editor.js's expandedCombiKeys (the Internals pane's expandedTopics, a
  // Setlist row's own accordion sections, follow the same idea).
  const expandedDuplicateKeys = new Set();

  // One duplicate group's expanded detail -- every copy in the group as its
  // own button (same `.bank-filter-row`/`.bank-filter-button` look
  // Programs/Combis' bank filters and the Internals pane's bank grid already
  // use, see internals.js's buildBankButtonGrid()), captioned the same "Bank
  // Number (Engine)" way `formatBankNumber()` already produces elsewhere.
  // Every button here represents a real, present copy (unlike Internals'
  // present-vs-missing buttons), so none are disabled -- all are equally
  // valid candidates for "which copy to keep," hence no color styling either.
  function buildDuplicateGroupRow(group) {
    const tr = document.createElement("tr");
    tr.className = "editor-row";
    const td = document.createElement("td");
    td.colSpan = 2;

    const wrap = document.createElement("div");
    wrap.className = "duplicate-copies";
    const row = document.createElement("div");
    row.className = "bank-filter-row";
    for (const p of group) {
      const label = formatBankNumber({ isProgram: true, bank: p.bank, number: p.number }, p.bankType);
      // Combi usage is only confirmed correct for 8 individually-verified
      // banks (INT-A..D, USER-A/D/F/AA -- see refCell's own comment in
      // pane.js) -- "n/a" here matches the old table's fallback rather than
      // showing a potentially-wrong count.
      const combiText = p.combiUsageCountAvailable ? `#${p.combiUsageCount}` : "n/a";
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "button is-small bank-filter-button";
      // Visible on the button itself, not just a hover-only title -- a
      // native app's tooltips are far less discoverable than a browser's.
      btn.textContent = `${label} (Combi ${combiText} / Set List #${p.setlistUsageCount})`;
      btn.title = `Keep ${label} as this group's only copy -- clears every OTHER duplicate to its bank's Init Program, and repoints their Combi/Set List references here.`;
      // Applies immediately, no confirm step -- matches every other write in
      // this app (drag-and-drop, Program copy, A-Z sort), per explicit
      // request. See PcgFile::resolveDuplicates()'s own doc comment for the
      // full behavior; combiRefsSkipped only shows up in the toast when
      // non-zero, since it's 0 for the vast majority of real cases now that
      // all 20 Program banks have a confirmed Combi Timbre code.
      btn.addEventListener("click", async () => {
        const result = await window.resolveDuplicateProgram(getDatasetId(), p.bank, p.number);
        if (!result.ok) {
          showToast(result.error);
          return;
        }
        showToast(
          `Kept ${label} -- cleared ${result.clearedPrograms} duplicate(s), ` +
            `repointed ${result.setlistRefsRepointed} Set List slot(s), ${result.combiRefsRepointed} Combi Timbre(s)` +
            (result.combiRefsSkipped ? `, skipped ${result.combiRefsSkipped} Combi Timbre(s) (unconfirmed bank)` : "")
        );
        // A resolved duplicate changes Programs (this group shrinks/
        // disappears) AND can repoint Combi Timbre/Set List references --
        // every one of the coordinator's three tabs needs to re-fetch, not
        // just this one, hence the coordinator's own reload rather than a
        // local one.
        await onNeedsFullReload();
        // The opposite pane's own Library tables are a SEPARATE copy of this
        // same data (each pane's createLibraryPanels() loads and caches
        // independently) -- if it's showing this same dataset, it has no
        // other way to learn the file just changed underneath it.
        await onRefreshOppositeLibrary(getDatasetId());
        // Repointed Set List slots need the Setlist tab's own cached
        // entries refreshed too, in both panes -- same staleness class as
        // pane-combi-editor.js's Combi rearrange gestures, see
        // refreshSetlistEverywhere()'s own doc comment in pane.js.
        if (result.setlistRefsRepointed > 0) await onSetlistRefsRepointed(getDatasetId());
      });
      row.appendChild(btn);
    }
    wrap.appendChild(row);
    td.appendChild(wrap);
    tr.appendChild(td);
    return tr;
  }

  function render() {
    const needle = getFilterText().trim().toLowerCase();
    panel.innerHTML = "";

    const visibleGroups = duplicateGroups.filter((group) => {
      const groupName = group[0].name || "(empty)";
      return !needle || groupName.toLowerCase().includes(needle);
    });

    if (visibleGroups.length === 0) {
      const empty = document.createElement("div");
      empty.className = "usage-empty";
      empty.textContent = "No byte-exact duplicate Programs found.";
      panel.appendChild(empty);
      return;
    }

    // Same Entry-row + `.editor-row` expand/collapse shape as the Programs/
    // Combis usage rows (buildUsageRow()/pane-combi-editor.js's buildTimbreRow()) --
    // one row per duplicate group, click to reveal its copies.
    const table = document.createElement("table");
    table.className = "table is-fullwidth is-hoverable is-narrow";
    table.innerHTML = colgroupHtml([8, 4]) + "<thead><tr><th>Name</th><th>Copies</th></tr></thead><tbody></tbody>";
    const tbody = table.querySelector("tbody");

    for (const group of visibleGroups) {
      const groupName = group[0].name || "(empty)";
      const key = group.map((p) => `${p.bank}-${p.number}`).join(",");
      const isOpen = expandedDuplicateKeys.has(key);

      const tr = document.createElement("tr");
      const nameTd = document.createElement("td");
      nameTd.textContent = groupName;
      const countTd = document.createElement("td");
      countTd.textContent = `${group.length} identical copies`;
      tr.append(nameTd, countTd);

      tr.dataset.entryKey = key;
      if (isOpen) tr.classList.add("is-selected");
      tr.addEventListener("click", () => {
        if (expandedDuplicateKeys.has(key)) expandedDuplicateKeys.delete(key);
        else expandedDuplicateKeys.add(key);
        render();
      });
      tbody.appendChild(tr);

      if (isOpen) tbody.appendChild(buildDuplicateGroupRow(group));
    }

    panel.appendChild(table);
  }

  async function fetchDuplicates() {
    const datasetId = getDatasetId();
    duplicateGroups = datasetId == null ? [] : await window.findDuplicatePrograms(datasetId);
    if (datasetId != null) log(`[Library:Duplicates] Loaded dataset ${datasetId}: ${duplicateGroups.length} duplicate groups.`);
  }

  async function onDatasetChanged() {
    expandedDuplicateKeys.clear();
    await fetchDuplicates();
    render();
  }

  async function refresh() {
    await fetchDuplicates();
    render();
  }

  return { onDatasetChanged, refresh, render };
}
