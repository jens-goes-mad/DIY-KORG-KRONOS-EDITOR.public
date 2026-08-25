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
  { getDatasetId, getFilterText, getProgramBankType, onDropProgram, onSwapProgram, onJumpToSetlist, onJumpToInstrument, log, showToast }
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

  // Experimental (see STATE.md's multi-window entry) -- opens a separate
  // native window (main.cpp's createEditorWindow(), triggered via
  // window.openSgx2EditorWindow()) for exactly this Program's SGX-2 data.
  // `label` (no bankType passed to formatBankNumber -- see its own doc
  // comment for the ", (EXi)" suffix that would otherwise duplicate what
  // the Type button itself already says) becomes the window's title,
  // between "Editor" and "(experimental)", so several open SGX-2 windows
  // stay distinguishable from each other. The bridge itself (main.cpp) is
  // what actually prevents two windows opening for the SAME Program --
  // this just passes the (datasetId, bank, number) key through; if a
  // window for it is already open, the bridge brings that one to front
  // instead of creating a duplicate. window.openSgx2EditorWindow only
  // exists at all when the private companion submodule (private/diy-korg-
  // kronos-editor) was actually compiled in -- main.cpp binds it inside an
  // `#ifdef EDITOR_HAS_SGX2_MODULE` guard, itself set only when that
  // submodule's own CMakeLists.txt was found (see the root CMakeLists.txt
  // and STATE.md's repo-split entry). Every public build (no access to the
  // private repo) never binds it, so it's simply undefined here rather
  // than present-but-broken -- checked directly, per direct request, so a
  // click shows a clear toast instead of throwing or silently doing
  // nothing.
  async function openSgx2Editor(p) {
    if (typeof window.openSgx2EditorWindow !== "function") {
      showToast("SGX-2 editor: feature not available in this build.", { isError: true });
      return;
    }
    const label = formatBankNumber({ isProgram: true, bank: p.bank, number: p.number });
    const result = await window.openSgx2EditorWindow(getDatasetId(), p.bank, p.number, label);
    if (result && result.ok === false) showToast(result.error, { isError: true });
  }

  // A Program row's right-click "local menu" -- currently just "Reset
  // entry". Chosen over a per-bank-header button (2026-08-20 discussion):
  // the bank filter buttons already make it unclear which banks are even
  // showing, and a bank-level action would only make that worse, whereas a
  // per-row menu sits on the exact slot it affects regardless of filter
  // state. Plain DOM, not lit-html (nothing else in this file uses it) --
  // one Bulma dropdown-content positioned at the click point, mounted on
  // document.body (outlives this row's own re-renders) and torn down on the
  // next click/Escape/scroll, same dismiss model a native context menu has.
  let rowMenuEl = null;
  function closeRowMenu() {
    if (!rowMenuEl) return;
    rowMenuEl.remove();
    rowMenuEl = null;
    document.removeEventListener("click", closeRowMenu);
    document.removeEventListener("keydown", onRowMenuKeydown);
    document.removeEventListener("scroll", closeRowMenu, true);
  }
  function onRowMenuKeydown(ev) {
    if (ev.key === "Escape") closeRowMenu();
  }
  function openRowMenu(ev, p) {
    ev.preventDefault();
    closeRowMenu();
    rowMenuEl = document.createElement("div");
    rowMenuEl.className = "dropdown-menu program-row-menu";
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
    rowMenuEl.style.display = "block";
    rowMenuEl.style.position = "fixed";
    rowMenuEl.style.left = `${ev.clientX}px`;
    rowMenuEl.style.top = `${ev.clientY}px`;
    rowMenuEl.style.zIndex = "1000";
    const content = document.createElement("div");
    content.className = "dropdown-content";
    const resetItem = document.createElement("a");
    resetItem.className = "dropdown-item";
    resetItem.href = "#";
    resetItem.textContent = "Reset entry…";
    resetItem.addEventListener("click", (clickEv) => {
      clickEv.preventDefault();
      closeRowMenu();
      resetEntry(p);
    });
    content.appendChild(resetItem);
    rowMenuEl.appendChild(content);
    document.body.appendChild(rowMenuEl);
    // Deferred so this SAME contextmenu event's own bubble-up doesn't
    // immediately trigger the outside-click listener it just registered.
    setTimeout(() => {
      document.addEventListener("click", closeRowMenu);
      document.addEventListener("keydown", onRowMenuKeydown);
      document.addEventListener("scroll", closeRowMenu, true);
    }, 0);
  }

  // Writes this slot's bank-matching Init Program template over it (see
  // EditorBridge::resetProgram()'s own doc comment) -- unlike Duplicates'
  // "resolve" action, nothing else in the file is repointed: anything
  // already referencing this slot keeps pointing at it, now showing the
  // reset content. Confirmed first (isDanger: true) since it can't be
  // undone -- no undo/rollback exists anywhere else in this app either.
  async function resetEntry(p) {
    const label = formatBankNumber({ isProgram: true, bank: p.bank, number: p.number });
    const confirmed = await window.showConfirmDialog(
      `Reset ${label} ("${p.name || "(empty)"}") to its bank's factory Init Program? This can't be undone.`,
      { confirmLabel: "Reset", isDanger: true }
    );
    if (!confirmed) return;
    const result = await window.resetProgram(getDatasetId(), p.bank, p.number);
    if (!result.ok) {
      showToast(`Reset failed: ${result.error}`, { isError: true });
      return;
    }
    log(`[Library:Programs] Reset ${label} to its bank's Init Program.`);
    await refresh();
  }

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
      // Bank narrowed 30% (2.6 -> 1.82) and Type widened 50% (1.3 -> 1.95),
      // per direct request 2026-08-16 -- Type now sometimes holds a button
      // (the SGX-2 open-editor case below), which needs more room than the
      // bank column, whose "I-A 042"-style labels never do.
      colgroupHtml([1.82, null, 1.95, 1.3, 1.3]) +
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
      // Exi = 1 (kronos::ProgramBankType::Exi) -- exiAlgorithmType is only
      // meaningful for an EXi-bank Program (see ProgramInfo::
      // exiAlgorithmType's doc comment); an HD-1 row just shows "HD-1".
      // SGX-2 = 8 (EXI_ALGORITHM_NAMES' own index, pane.js) is a special
      // case, per direct request 2026-08-16: rather than plain text, the
      // Type cell becomes a real button opening the (experimental) SGX-2
      // editor window for THIS exact Program -- the only engine with a
      // real (if placeholder) window to open at all so far.
      const isSgx2 = p.bankType === 1 && p.exiAlgorithmType === 8;
      if (isSgx2) {
        const typeBtn = document.createElement("button");
        typeBtn.type = "button";
        typeBtn.className = "button is-small is-link is-light sgx2-open-button";
        typeBtn.textContent = `${programBankTypeName(p.bankType)} (${exiEngineName(p.exiAlgorithmType)})`;
        typeBtn.title = "Open the SGX-2 editor for this Program (experimental)";
        // Row click (below) toggles the Set List/Combi usage expansion --
        // this button does something else entirely, so it must not also
        // trigger that.
        typeBtn.addEventListener("click", (ev) => {
          ev.stopPropagation();
          openSgx2Editor(p);
        });
        typeTd.appendChild(typeBtn);
      } else {
        typeTd.textContent =
          p.bankType === 1 && p.exiAlgorithmType != null
            ? `${programBankTypeName(p.bankType)} (${exiEngineName(p.exiAlgorithmType)})`
            : p.bankType != null
              ? programBankTypeName(p.bankType)
              : "";
      }
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
      tr.addEventListener("contextmenu", (ev) => openRowMenu(ev, p));

      // Drag this Program onto another Program row (same pane or a
      // different pane's dataset) to COPY its raw bytes into that slot --
      // see onDropProgram in app.js. Hold Shift while dropping to SWAP the
      // two Programs' content instead (same dataset only) -- see
      // onSwapProgram in app.js for why this exists (copyProgram()'s own
      // DuplicateExists guard makes a plain copy meaningless between two
      // slots that are both genuinely empty "Init Program" -- every one is
      // byte-identical to every other one, so it always trips that guard
      // even though nothing's actually wrong; a swap sidesteps it
      // entirely). `effectAllowed = "copyMove"` (not just "copy") is what
      // lets dragover below actually switch the cursor to a "move" hint
      // when Shift is held -- effectAllowed set at dragstart caps which
      // dropEffect values the browser will honor later. dragstart's
      // payload never mutates the source either way -- only drop() (on the
      // TARGET row) calls into the bridge.
      tr.draggable = true;
      tr.addEventListener("dragstart", (ev) => {
        draggedProgram = { datasetId: getDatasetId(), bank: p.bank, number: p.number, bankType: p.bankType };
        ev.dataTransfer.setData("application/json", JSON.stringify(draggedProgram));
        ev.dataTransfer.effectAllowed = "copyMove";
      });
      tr.addEventListener("dragend", () => {
        draggedProgram = null;
      });
      tr.addEventListener("dragover", (ev) => {
        // Reject up front (no preventDefault -- the browser's own "not
        // allowed" cursor takes over, no `drop` fires here at all) if the
        // dragged Program's engine type doesn't match this row's own bank.
        // EditorBridge::copyProgram()/swapProgram() enforce this
        // regardless; this is just immediate hover feedback instead of
        // only after the drop.
        if (draggedProgram != null && draggedProgram.bankType !== p.bankType) {
          tr.classList.remove("drop-target");
          return;
        }
        ev.preventDefault();
        ev.dataTransfer.dropEffect = ev.shiftKey ? "move" : "copy";
        tr.classList.add("drop-target");
      });
      tr.addEventListener("dragleave", () => tr.classList.remove("drop-target"));
      tr.addEventListener("drop", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        tr.classList.remove("drop-target");
        const raw = ev.dataTransfer.getData("application/json");
        if (!raw) return;
        const target = { datasetId: getDatasetId(), bank: p.bank, number: p.number };
        if (ev.shiftKey) onSwapProgram(JSON.parse(raw), target);
        else onDropProgram(JSON.parse(raw), target);
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

  // getProgramCount exposed so pane.js's own updateCategoryTabAvailability()
  // can disable the Programs tab for a dataset with none at all.
  return { onDatasetChanged, refresh, render, jumpToEntry, findProgram, getProgramCount: () => programs.length };
}

// Duplicates: read-only, no bank filter of its own (unlike Programs/
// Combis). Two vertical sub-tabs (Programs / Combi, style.css's
// .duplicates-subtabs -- rotated 90 degrees, per direct request, since the
// pane is already tight on horizontal width and a normal tab row would
// compete with the table for it). Each sub-tab covers two DIFFERENT
// questions about the same underlying idea, "library hygiene":
// - "Same content, different position" (Programs only -- the original
//   Duplicates feature) -- byte-exact copies, safely auto-resolvable
//   (resolveDuplicateProgram() below).
// - "Same name, different content" (Programs AND Combi, added later per
//   direct request) -- entries that coincidentally share a name but are
//   NOT the same thing, e.g. two Programs both called "Bass 1" that
//   diverged over time. Read-only: unlike a byte-exact duplicate, there's
//   no safe automatic resolution here (the content really does differ,
//   only a person can decide what that means), so this is a discovery
//   tool, not a write action -- see PcgFile::NameCollisionGroup's own doc
//   comment (PcgFile.h) for the exact grouping rule.
// Scoped to a single selected dataset -- no cross-dataset comparison here,
// that's a real future idea, not this pass.
function createDuplicatesPanel(
  { panel },
  {
    getDatasetId,
    getFilterText,
    onJumpToInstrument,
    log,
    onRefreshOppositeLibrary,
    onNeedsFullReload,
    onSetlistRefsRepointed,
  }
) {
  let duplicateGroups = [];
  let combiDuplicateGroups = [];
  let programNameCollisions = [];
  let combiNameCollisions = [];
  let activeSubTab = "programs";  // "programs" | "combi"
  // Which of the two views (per sub-tab) is showing right now -- a dropdown
  // (render() below) replaces what used to be two always-stacked sections,
  // per explicit request once BOTH sub-tabs had two real views to switch
  // between (Combi's own byte-exact duplicate detection is new, see
  // PcgFile::findDuplicateCombis()).
  let activeView = { programs: "content", combi: "content" };  // "content" | "name"
  // Joined `${bank}-${number}` lists identifying which duplicate groups are
  // expanded -- a Set, several can be open at once, same multi-open model as
  // pane-combi-editor.js's expandedCombiKeys (the Internals pane's expandedTopics, a
  // Setlist row's own accordion sections, follow the same idea).
  const expandedDuplicateKeys = new Set();
  // Same idea, keyed `${p or c}:${name}` (name collisions are grouped by
  // name, not by a set of bank/number members) -- one shared Set for both
  // sub-tabs since the prefix already keeps Program/Combi keys from
  // colliding with each other.
  const expandedCollisionKeys = new Set();

  // The resolve-picker sidebar's own state -- null when closed. `group` is
  // a flat array of full Program/Combi info entries (a live reference into
  // duplicateGroups/combiDuplicateGroups OR the flattened variants of a
  // programNameCollisions/combiNameCollisions entry -- see
  // requireByteExactMatch below); `src` is the chosen "keep" entry's
  // `${bank}-${number}` key (or null, nothing chosen yet); `dupl` is the
  // Set of `${bank}-${number}` keys checked to fold INTO src. Single shared
  // instance (not one per sub-tab) -- only one group's picker is ever open
  // at a time, per explicit request ("sidebar stays open" refers to
  // surviving a resolve, not to multiple pickers coexisting).
  //
  // `requireByteExactMatch` picks which of the Duplicates panel's two
  // checks this picker instance is servicing, same meaning as the
  // PcgFile-level parameter of the same name (2026-08-25, per direct
  // request/decision -- "Same name, different content" entries can be
  // consolidated too, same picker UI, but MUST NEVER be required to be
  // byte-identical, and MUST NEVER have their own bytes cleared, since
  // they're expected to genuinely differ -- destroying that real,
  // non-recoverable content wasn't acceptable): true opens from the "Same
  // content, different location" table (byte-exact groups, clears folded-in
  // Program bytes to Init Program); false opens from the "Same name,
  // different content" table (name-collision groups, flattened across ALL
  // their variants -- never clears anything, only repoints references).
  // `nameGroupKey` (only set when requireByteExactMatch is false) is
  // `{name, bankType}`, used to re-find this same NameCollisionGroup after
  // a resolve -- unlike a byte-exact group, a name-collision group's OWN
  // members never disappear from it just because their references moved
  // (nothing about their content -- or hash -- changed), so re-syncing by
  // name/bankType is what applyResolvePicker() below needs instead of the
  // byte-exact path's own bank/number lookup.
  let resolvePicker = null;
  // Lazily created on first open, appended to document.body once (not into
  // `panel` itself -- position:fixed doesn't need to live inside the
  // scrolling pane content, and this way it survives render()'s own
  // `panel.innerHTML = ""` unaffected). Reuses the cross-dataset Combi copy
  // panel's own CSS shell (style.css's `.cross-dataset-panel*` -- backdrop,
  // slide-in, header/body/footer) rather than inventing a second one, but
  // built with plain DOM here (not lit-html, unlike that file's own PILOT)
  // to stay consistent with the rest of this already-plain-DOM panel.
  let resolvePanelEls = null;

  function ensureResolvePanelEls() {
    if (resolvePanelEls) return resolvePanelEls;
    const backdrop = document.createElement("div");
    backdrop.className = "cross-dataset-panel-backdrop";
    backdrop.hidden = true;
    backdrop.addEventListener("click", closeResolvePicker);

    const panelEl = document.createElement("div");
    panelEl.className = "cross-dataset-panel duplicate-resolve-panel";
    panelEl.hidden = true;

    document.body.append(backdrop, panelEl);
    resolvePanelEls = { backdrop, panelEl };
    return resolvePanelEls;
  }

  function openResolvePicker(group, isProgram, requireByteExactMatch, nameGroupKey) {
    resolvePicker = { isProgram, group, requireByteExactMatch, nameGroupKey: nameGroupKey || null, src: null, dupl: new Set() };
    renderResolvePicker();
  }

  function closeResolvePicker() {
    resolvePicker = null;
    renderResolvePicker();
  }

  // Slides in from whichever screen edge is nearest THIS pane -- same idea
  // combi-cross-dataset-panel.js's own slideDirectionFor() uses for its
  // destination pane, just inlined here (this panel only ever cares about
  // its OWN pane, never a second one).
  function resolvePanelSlideDirection() {
    const paneEl = panel.closest(".pane");
    return paneEl && paneEl.matches(".pane:last-of-type") ? "right" : "left";
  }

  function renderResolvePicker() {
    const { backdrop, panelEl } = ensureResolvePanelEls();

    if (!resolvePicker) {
      backdrop.classList.remove("is-visible");
      panelEl.classList.remove("is-open");
      backdrop.hidden = true;
      panelEl.hidden = true;
      return;
    }

    backdrop.hidden = false;
    panelEl.hidden = false;
    panelEl.classList.remove("slide-from-left", "slide-from-right");
    panelEl.classList.add(`slide-from-${resolvePanelSlideDirection()}`);
    // Two-step reveal (unhide this frame, add the transition classes next
    // frame) -- same trick combi-cross-dataset-panel.js's own renderPanel()
    // uses, so the slide-in transition actually animates instead of
    // snapping straight to its open position.
    requestAnimationFrame(() => {
      backdrop.classList.add("is-visible");
      panelEl.classList.add("is-open");
    });

    const { group, isProgram, requireByteExactMatch } = resolvePicker;

    panelEl.innerHTML = "";
    const header = document.createElement("div");
    header.className = "cross-dataset-panel-header";
    const title = document.createElement("h2");
    title.className = "cross-dataset-panel-title";
    // "Resolve" (byte-exact -- these copies really are identical) vs
    // "Consolidate" (name-collision -- these entries genuinely differ, the
    // user is choosing to treat them as close enough on purpose) -- two
    // different words for two different levels of consequence, per this
    // picker's own requireByteExactMatch doc comment above.
    title.textContent = requireByteExactMatch
      ? `Resolve duplicates -- ${group[0] ? group[0].name || "(empty)" : ""}`
      : `Consolidate variants -- ${nameCollisionGroupLabel(resolvePicker.nameGroupKey, isProgram)}`;
    const closeBtn = document.createElement("button");
    closeBtn.type = "button";
    closeBtn.className = "cross-dataset-panel-close";
    closeBtn.title = "Close";
    closeBtn.textContent = "✕";
    closeBtn.addEventListener("click", closeResolvePicker);
    header.append(title, closeBtn);

    const body = document.createElement("div");
    body.className = "cross-dataset-panel-body";

    if (group.length < 2) {
      const done = document.createElement("div");
      done.className = "usage-empty";
      done.textContent = requireByteExactMatch
        ? "Every duplicate in this group has already been resolved."
        : "Nothing left to consolidate in this group.";
      body.appendChild(done);
    } else {
      const table = document.createElement("table");
      table.className = "table is-fullwidth is-narrow duplicate-resolve-table";
      table.innerHTML = "<thead><tr><th>Src</th><th>Dupl</th><th>Slot</th></tr></thead><tbody></tbody>";
      const tbody = table.querySelector("tbody");

      for (const entry of group) {
        const entryKey = `${entry.bank}-${entry.number}`;
        const isSrc = resolvePicker.src === entryKey;
        const label = isProgram
          ? formatBankNumber({ isProgram: true, bank: entry.bank, number: entry.number }, entry.bankType)
          : formatBankNumber({ isProgram: false, bank: entry.bank, number: entry.number });

        const tr = document.createElement("tr");

        const srcTd = document.createElement("td");
        const srcRadio = document.createElement("input");
        srcRadio.type = "radio";
        srcRadio.name = "duplicate-resolve-src";
        srcRadio.checked = isSrc;
        srcRadio.title = `Keep ${label} -- the copy every checked "Dupl" below gets folded into.`;
        srcRadio.addEventListener("change", () => {
          resolvePicker.src = entryKey;
          resolvePicker.dupl.delete(entryKey);  // a copy can't be its own duplicate
          renderResolvePicker();
        });
        srcTd.appendChild(srcRadio);

        const duplTd = document.createElement("td");
        const duplCheckbox = document.createElement("input");
        duplCheckbox.type = "checkbox";
        duplCheckbox.checked = resolvePicker.dupl.has(entryKey);
        duplCheckbox.disabled = isSrc;
        duplCheckbox.title = !requireByteExactMatch
          ? `Fold ${label} into Src -- repoints its ${isProgram ? "Combi/Set List" : "Set List"} references to Src. ` +
            "Its own content is left exactly as-is -- it genuinely differs from Src, so nothing is cleared or overwritten."
          : isProgram
            ? `Fold ${label} into Src -- clears its own bytes to its bank's Init Program, repoints its Combi/Set List references to Src.`
            : `Fold ${label} into Src -- repoints its Set List references to Src (its own content is left untouched -- no Init Combi template exists yet).`;
        duplCheckbox.addEventListener("change", () => {
          if (duplCheckbox.checked) resolvePicker.dupl.add(entryKey);
          else resolvePicker.dupl.delete(entryKey);
          renderResolvePicker();
        });
        duplTd.appendChild(duplCheckbox);

        const slotTd = document.createElement("td");
        slotTd.textContent = label;

        tr.append(srcTd, duplTd, slotTd);
        tbody.appendChild(tr);
      }
      body.appendChild(table);
    }

    const footer = document.createElement("div");
    footer.className = "cross-dataset-panel-footer";
    // Only appears once BOTH a src and at least one dupl are chosen, per
    // explicit request -- there's nothing coherent to resolve otherwise.
    if (resolvePicker.src != null && resolvePicker.dupl.size > 0) {
      const resolveBtn = document.createElement("button");
      resolveBtn.type = "button";
      resolveBtn.className = "button is-small is-link";
      resolveBtn.textContent = requireByteExactMatch
        ? `Resolve ${resolvePicker.dupl.size} into Src`
        : `Consolidate ${resolvePicker.dupl.size} into Src`;
      resolveBtn.addEventListener("click", () => applyResolvePicker());
      footer.appendChild(resolveBtn);
    }

    panelEl.append(header, body, footer);
  }

  async function applyResolvePicker() {
    if (!resolvePicker || resolvePicker.src == null || resolvePicker.dupl.size === 0) return;
    const { isProgram, requireByteExactMatch } = resolvePicker;
    const srcParts = resolvePicker.src.split("-").map(Number);
    const [srcBank, srcNumber] = srcParts;
    const targets = [...resolvePicker.dupl].map((key) => {
      const [bank, number] = key.split("-").map(Number);
      return { bank, number };
    });

    const result = isProgram
      ? await window.resolveDuplicateProgram(getDatasetId(), srcBank, srcNumber, targets, requireByteExactMatch)
      : await window.resolveDuplicateCombis(getDatasetId(), srcBank, srcNumber, targets, requireByteExactMatch);
    if (!result.ok) {
      showToast(result.error, { isError: true });
      return;
    }
    // requireByteExactMatch=false never clears anything (clearedPrograms is
    // always 0 there -- see PcgFile::resolveDuplicates()'s own doc
    // comment), so that toast line is worded around "consolidated"/
    // repointing only, never "cleared".
    showToast(
      requireByteExactMatch
        ? isProgram
          ? `Resolved ${targets.length} duplicate(s) -- cleared ${result.clearedPrograms}, repointed ` +
              `${result.setlistRefsRepointed} Set List slot(s), ${result.combiRefsRepointed} Combi Timbre(s)` +
              (result.combiRefsSkipped ? `, skipped ${result.combiRefsSkipped} Combi Timbre(s) (unconfirmed bank)` : "")
          : `Resolved ${targets.length} duplicate(s) -- repointed ${result.setlistRefsRepointed} Set List slot(s).`
        : isProgram
          ? `Consolidated ${targets.length} variant(s) -- repointed ${result.setlistRefsRepointed} Set List slot(s), ` +
              `${result.combiRefsRepointed} Combi Timbre(s)` +
              (result.combiRefsSkipped ? `, skipped ${result.combiRefsSkipped} Combi Timbre(s) (unconfirmed bank)` : "")
          : `Consolidated ${targets.length} variant(s) -- repointed ${result.setlistRefsRepointed} Set List slot(s).`
    );

    // Same reload chain the old per-copy "Keep only this" button used --
    // see its own comments (now removed) for why each step is needed.
    await onNeedsFullReload();
    await onRefreshOppositeLibrary(getDatasetId());
    if (result.setlistRefsRepointed > 0) await onSetlistRefsRepointed(getDatasetId());

    // "Sidebar stays open" (explicit request) -- re-sync it to whatever the
    // FRESH data says post-reload (onNeedsFullReload() re-fetches
    // everything from scratch, see fetchDuplicates() below), not just a
    // locally-filtered guess, in case something else changed the group
    // shape concurrently.
    if (requireByteExactMatch) {
      // Byte-exact groups genuinely shrink as members get cleared -- find
      // the fresh group still containing src, or close the picker if src no
      // longer shows up as a duplicate of anything at all (every remaining
      // member just got folded in).
      const freshGroups = isProgram ? duplicateGroups : combiDuplicateGroups;
      const freshGroup = freshGroups.find((g) => g.some((e) => `${e.bank}-${e.number}` === resolvePicker.src));
      if (freshGroup) {
        resolvePicker.group = freshGroup;
        resolvePicker.dupl.clear();
      } else {
        resolvePicker = null;
      }
    } else {
      // Name-collision groups never shrink from a consolidate -- resolving
      // only repoints references, it never changes any entry's own content
      // (or contentHash), so the SAME variants/members are still there
      // afterward. Re-find by name/bankType (nameGroupKey, set when this
      // picker was opened) rather than by src's own bank/number, since
      // that's the actual identity of a name-collision group.
      const freshNameGroups = isProgram ? programNameCollisions : combiNameCollisions;
      const freshNameGroup = freshNameGroups.find(
        (g) => g.name === resolvePicker.nameGroupKey.name && g.bankType === resolvePicker.nameGroupKey.bankType
      );
      if (freshNameGroup) {
        resolvePicker.group = freshNameGroup.variants.flatMap((v) => v.members);
        resolvePicker.dupl.clear();
      } else {
        resolvePicker = null;
      }
    }
    renderResolvePicker();
  }

  // One duplicate group's expanded detail -- every copy in the group gets a
  // plain navigation button (jumps there, same click/Shift-click/Shift+Cmd-
  // click convention as buildUsageRow()'s Combi jump button below -- per
  // explicit request that Duplicates navigate "as usual"). No write action
  // lives here at all (2026-08-25, replaces an earlier "click a copy to
  // resolve the WHOLE group" design, and the "Keep only this"-per-copy
  // button that briefly replaced it) -- resolving now happens exclusively
  // through the group title row's own "..." menu button (renderExact-
  // DuplicatesTable() below), which opens a picker sidebar letting the user
  // choose SPECIFIC duplicates to fold in rather than an all-or-nothing
  // whole group. `isProgram` only affects the informational text shown:
  // Programs show a Combi-usage count (n/a for unconfirmed banks -- see
  // refCell's own comment in pane.js), Combis don't (a Combi isn't
  // referenced BY other Combis).
  function buildDuplicateGroupRow(group, isProgram) {
    const tr = document.createElement("tr");
    tr.className = "editor-row";
    const td = document.createElement("td");
    td.colSpan = 2;

    const wrap = document.createElement("div");
    wrap.className = "duplicate-copies";
    const row = document.createElement("div");
    row.className = "bank-filter-row";
    for (const entry of group) {
      const label = isProgram
        ? formatBankNumber({ isProgram: true, bank: entry.bank, number: entry.number }, entry.bankType)
        : formatBankNumber({ isProgram: false, bank: entry.bank, number: entry.number });

      const navBtn = document.createElement("button");
      navBtn.type = "button";
      navBtn.className = "button is-small bank-filter-button";
      navBtn.textContent = isProgram
        ? `${label} (Combi ${entry.combiUsageCountAvailable ? `#${entry.combiUsageCount}` : "n/a"} / Set List #${entry.setlistUsageCount})`
        : `${label} (Set List #${entry.setlistReferenceCount})`;
      navBtn.title =
        `Jump to ${label}. Shift+click: opposite pane. Shift+Cmd+click: opposite pane, ` +
        "same coordinate, keep its own dataset.";
      navBtn.addEventListener("click", (ev) => {
        ev.stopPropagation();
        onJumpToInstrument({
          isProgram,
          bank: entry.bank,
          number: entry.number,
          from: null,
          toOpposite: ev.shiftKey,
          keepOppositeDataset: ev.metaKey,
        });
      });
      row.appendChild(navBtn);
    }
    wrap.appendChild(row);
    td.appendChild(wrap);
    tr.appendChild(td);
    return tr;
  }

  // "Same content, different position" -- one table shape shared by both
  // sub-tabs now that Combi duplicate detection is real too (2026-08-25;
  // `groups`/`isProgram`/`emptyMessage` let the same function serve either).
  // Returns a DOM node (a table, or an empty-state message) rather than
  // appending itself, matching renderNameCollisionTable() below so render()
  // can place either kind of section the same way.
  function renderExactDuplicatesTable(groups, isProgram, emptyMessage) {
    const needle = getFilterText().trim().toLowerCase();
    const visibleGroups = groups.filter((group) => {
      const groupName = group[0].name || "(empty)";
      return !needle || groupName.toLowerCase().includes(needle);
    });

    if (visibleGroups.length === 0) {
      const empty = document.createElement("div");
      empty.className = "usage-empty";
      empty.textContent = emptyMessage;
      return empty;
    }

    // Same Entry-row + `.editor-row` expand/collapse shape as the Programs/
    // Combis usage rows (buildUsageRow()/pane-combi-editor.js's buildTimbreRow()) --
    // one row per duplicate group, click to reveal its copies.
    const table = document.createElement("table");
    table.className = "table is-fullwidth is-hoverable is-narrow";
    table.innerHTML = colgroupHtml([8, 4]) + "<thead><tr><th>Name</th><th>Copies</th></tr></thead><tbody></tbody>";
    const tbody = table.querySelector("tbody");

    // Prefixed by kind so a Program group and a Combi group that happen to
    // cover the exact same bank/number set (unlikely, but not impossible)
    // don't collide in the shared expandedDuplicateKeys Set -- same idea
    // expandedCollisionKeys below already uses for name-collision groups.
    const keyPrefix = isProgram ? "p:" : "c:";
    for (const group of visibleGroups) {
      const groupName = group[0].name || "(empty)";
      const key = keyPrefix + group.map((e) => `${e.bank}-${e.number}`).join(",");
      const isOpen = expandedDuplicateKeys.has(key);

      const tr = document.createElement("tr");
      const nameTd = document.createElement("td");
      nameTd.textContent = groupName;
      const countTd = document.createElement("td");
      countTd.className = "duplicate-group-count-cell";
      const countLabel = document.createElement("span");
      countLabel.textContent = `${group.length} identical copies`;
      // "..." opens the resolve picker (renderResolvePicker() below) for
      // exactly this group -- visible whether the row is expanded or not,
      // per explicit request, so opening the picker doesn't need expanding
      // first. Own click handler, not the row's -- stopPropagation() so it
      // doesn't ALSO toggle expand/collapse.
      const menuBtn = document.createElement("button");
      menuBtn.type = "button";
      menuBtn.className = "button is-small duplicate-resolve-menu-button";
      menuBtn.title = "Resolve duplicates in this group";
      menuBtn.textContent = "⋯";  // horizontal ellipsis -- a compact "more actions" affordance
      menuBtn.addEventListener("click", (ev) => {
        ev.stopPropagation();
        openResolvePicker(group, isProgram, /*requireByteExactMatch=*/true);
      });
      countTd.append(countLabel, menuBtn);
      tr.append(nameTd, countTd);

      tr.dataset.entryKey = key;
      if (isOpen) tr.classList.add("is-selected");
      tr.addEventListener("click", () => {
        if (expandedDuplicateKeys.has(key)) expandedDuplicateKeys.delete(key);
        else expandedDuplicateKeys.add(key);
        render();
      });
      tbody.appendChild(tr);

      if (isOpen) tbody.appendChild(buildDuplicateGroupRow(group, isProgram));
    }

    return table;
  }

  // Programs now get a bank-type suffix ("Bass 1 (HD-1)") -- an HD-1 and an
  // EXi Program sharing a name are two INDEPENDENT groups now
  // (findProgramNameCollisions()'s own doc comment in PcgFile.h), so two
  // groups can legitimately show the exact same bare name; the suffix is
  // what tells them apart at a glance. Combis have no such distinction
  // (group.bankType is always -1 there), so this is a no-op for them.
  function nameCollisionGroupLabel(group, isProgram) {
    const name = group.name || "(empty)";
    if (!isProgram) return name;
    const bankTypeName = PROGRAM_BANK_TYPE_NAMES[group.bankType];
    return bankTypeName ? `${name} (${bankTypeName})` : name;
  }

  // One "Same name, different content" group's expanded detail -- one
  // visually separated cluster of buttons per variant (style.css's
  // .name-collision-variant + a rule between them), so entries that ARE
  // byte-identical to each other read as one cluster and entries that
  // genuinely differ read as separate ones -- the whole point of this
  // check. The group's own title row (renderNameCollisionTable() below) has
  // its own "..." resolve-picker menu now (2026-08-25 -- consolidating
  // variants across this whole group, not just within one), but each
  // individual badge here stays a plain navigation button, same click/
  // Shift-click/Shift+Cmd-click convention as everywhere else -- there's
  // still no per-badge write action, only the group-level picker.
  function buildNameCollisionGroupRow(group, isProgram) {
    const tr = document.createElement("tr");
    tr.className = "editor-row";
    const td = document.createElement("td");
    td.colSpan = 2;

    const wrap = document.createElement("div");
    wrap.className = "duplicate-copies";
    group.variants.forEach((variant, i) => {
      const cluster = document.createElement("div");
      cluster.className = "bank-filter-row name-collision-variant";
      for (const m of variant.members) {
        const label = isProgram
          ? formatBankNumber({ isProgram: true, bank: m.bank, number: m.number }, m.bankType)
          : formatBankNumber({ isProgram: false, bank: m.bank, number: m.number });
        const badge = document.createElement("button");
        badge.type = "button";
        badge.className = "button is-small bank-filter-button";
        badge.textContent = label;
        badge.title =
          `${label} -- variant ${i + 1} of ${group.variants.length} sharing the name "${group.name}". ` +
          (variant.members.length > 1
            ? `Byte-identical to the ${variant.members.length - 1} other entr${variant.members.length > 2 ? "ies" : "y"} in this same cluster. `
            : "Its content differs from every other entry sharing this name. ") +
          "Click to jump there. Shift+click: opposite pane. Shift+Cmd+click: opposite pane, same coordinate, keep its own dataset.";
        badge.addEventListener("click", (ev) => {
          ev.stopPropagation();
          onJumpToInstrument({
            isProgram,
            bank: m.bank,
            number: m.number,
            from: null,
            toOpposite: ev.shiftKey,
            keepOppositeDataset: ev.metaKey,
          });
        });
        cluster.appendChild(badge);
      }
      wrap.appendChild(cluster);
    });
    td.appendChild(wrap);
    tr.appendChild(td);
    return tr;
  }

  function renderNameCollisionTable(groups, isProgram, emptyMessage) {
    const needle = getFilterText().trim().toLowerCase();
    const visibleGroups = groups.filter((g) => !needle || g.name.toLowerCase().includes(needle));

    if (visibleGroups.length === 0) {
      const empty = document.createElement("div");
      empty.className = "usage-empty";
      empty.textContent = emptyMessage;
      return empty;
    }

    const table = document.createElement("table");
    table.className = "table is-fullwidth is-hoverable is-narrow";
    table.innerHTML = colgroupHtml([8, 4]) + "<thead><tr><th>Name</th><th>Variants</th></tr></thead><tbody></tbody>";
    const tbody = table.querySelector("tbody");

    for (const group of visibleGroups) {
      const totalMembers = group.variants.reduce((sum, v) => sum + v.members.length, 0);
      // Includes bankType now (2026-08-25) -- an HD-1 "Bass 1" and an EXi
      // "Bass 1" are two INDEPENDENT groups now that grouping itself splits
      // on bank type (findProgramNameCollisions()'s own doc comment in
      // PcgFile.h), so the plain name alone is no longer a unique key here.
      // Always -1 for Combi groups, so this is a no-op there.
      const key = `${isProgram ? "p" : "c"}:${group.bankType}:${group.name}`;
      const isOpen = expandedCollisionKeys.has(key);

      const tr = document.createElement("tr");
      const nameTd = document.createElement("td");
      nameTd.textContent = nameCollisionGroupLabel(group, isProgram);
      const countTd = document.createElement("td");
      countTd.className = "duplicate-group-count-cell";
      const countLabel = document.createElement("span");
      countLabel.textContent = `${group.variants.length} variants, ${totalMembers} entries`;
      // "..." opens the SAME resolve-picker sidebar the byte-exact table's
      // own trigger uses (2026-08-25, per direct request: "Same name,
      // different content" entries often really are minor modifications of
      // the same sound, worth being able to consolidate the same way) --
      // flattened across ALL this group's variants (not just one), since
      // consolidating across variants is the whole point here.
      // requireByteExactMatch=false: the picker must never require these
      // entries to be byte-identical (they're expected to differ) and must
      // never clear a folded-in entry's own bytes (it's genuinely
      // different, non-recoverable content -- see resolvePicker's own doc
      // comment above for the full reasoning).
      const menuBtn = document.createElement("button");
      menuBtn.type = "button";
      menuBtn.className = "button is-small duplicate-resolve-menu-button";
      menuBtn.title = "Consolidate variants in this group";
      menuBtn.textContent = "⋯";
      menuBtn.addEventListener("click", (ev) => {
        ev.stopPropagation();
        openResolvePicker(
          group.variants.flatMap((v) => v.members),
          isProgram,
          /*requireByteExactMatch=*/false,
          { name: group.name, bankType: group.bankType }
        );
      });
      countTd.append(countLabel, menuBtn);
      tr.append(nameTd, countTd);

      if (isOpen) tr.classList.add("is-selected");
      tr.addEventListener("click", () => {
        if (expandedCollisionKeys.has(key)) expandedCollisionKeys.delete(key);
        else expandedCollisionKeys.add(key);
        render();
      });
      tbody.appendChild(tr);

      if (isOpen) tbody.appendChild(buildNameCollisionGroupRow(group, isProgram));
    }

    return table;
  }

  // The two views available on EITHER sub-tab, chosen via the dropdown
  // render() builds below -- kept as one lookup table rather than inlined
  // per-branch logic so both sub-tabs (Programs/Combi) share exactly the
  // same option list/order.
  const DUPLICATE_VIEWS = [
    ["content", "Same content, different location"],
    ["name", "Same name, different content"],
  ];

  function render() {
    panel.innerHTML = "";

    const body = document.createElement("div");
    body.className = "duplicates-body";

    // Vertical (rotated 90 degrees, see style.css) sub-tab strip -- Bulma's
    // own .is-link "pressed" look, remapped to this app's own --editor-
    // accent (darkorange) rather than Bulma's default blue, per explicit
    // request -- see style.css's shared .is-link override comment (also
    // used by bank-filter/pane-visibility buttons).
    const subtabs = document.createElement("div");
    subtabs.className = "duplicates-subtabs";
    for (const [key, label] of [
      ["programs", "Programs"],
      ["combi", "Combi"],
    ]) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "button is-small duplicates-subtab-button" + (activeSubTab === key ? " is-link" : "");
      btn.textContent = label;
      btn.addEventListener("click", () => {
        if (activeSubTab === key) return;
        activeSubTab = key;
        render();
      });
      subtabs.appendChild(btn);
    }

    const content = document.createElement("div");
    content.className = "duplicates-content";

    // Replaces what used to be two always-stacked sections (2026-08-25, per
    // explicit request) -- a dropdown per sub-tab picks which one view
    // shows, the same "Same content, different location" / "Same name,
    // different content" wording the old section headings used, now as
    // <option> labels doing double duty as the section's own heading.
    const viewSelect = document.createElement("select");
    viewSelect.className = "duplicates-view-select";
    for (const [value, label] of DUPLICATE_VIEWS) {
      const opt = document.createElement("option");
      opt.value = value;
      opt.textContent = label;
      viewSelect.appendChild(opt);
    }
    viewSelect.value = activeView[activeSubTab];
    viewSelect.addEventListener("change", () => {
      activeView[activeSubTab] = viewSelect.value;
      render();
    });
    const viewSelectWrap = document.createElement("div");
    viewSelectWrap.className = "select is-small is-fullwidth duplicates-view-select-wrap";
    viewSelectWrap.appendChild(viewSelect);
    content.appendChild(viewSelectWrap);

    const isProgram = activeSubTab === "programs";
    const table =
      activeView[activeSubTab] === "content"
        ? renderExactDuplicatesTable(
            isProgram ? duplicateGroups : combiDuplicateGroups,
            isProgram,
            isProgram ? "No byte-exact duplicate Programs found." : "No byte-exact duplicate Combis found."
          )
        : renderNameCollisionTable(
            isProgram ? programNameCollisions : combiNameCollisions,
            isProgram,
            isProgram
              ? "No Programs share a name with different content."
              : "No Combis share a name with different content."
          );
    content.appendChild(table);

    body.append(subtabs, content);
    panel.appendChild(body);
  }

  // Calls window[fnName](...args), never throwing/rejecting -- treats a
  // missing binding (an older native build without it yet) or a genuine
  // failure the same way, as "nothing found." Reported directly: pane.js's
  // loadDataset() awaits setlistPanel.onDatasetChanged() then
  // libraryPanels.onDatasetChanged() (which reaches fetchDuplicates()
  // below) BEFORE its own updateCategoryTabAvailability() call -- the one
  // thing that actually enables ANY category tab. An uncaught rejection
  // here used to abort that whole chain, leaving Setlist/Programs/Combis
  // ALL stuck disabled, not just Duplicates -- confirmed by tracing
  // loadDataset()'s own sequential awaits, not guessed. One still-
  // experimental fetch failing must never be able to take down totally
  // unrelated tabs.
  async function safeFetch(fnName, ...args) {
    try {
      const fn = window[fnName];
      if (typeof fn !== "function") {
        log(`[Library:Duplicates] window.${fnName} isn't available (older build?) -- treating as empty.`);
        return [];
      }
      return await fn(...args);
    } catch (err) {
      log(`[Library:Duplicates] ${fnName} failed: ${err}`);
      return [];
    }
  }

  async function fetchDuplicates() {
    const datasetId = getDatasetId();
    if (datasetId == null) {
      duplicateGroups = [];
      combiDuplicateGroups = [];
      programNameCollisions = [];
      combiNameCollisions = [];
      return;
    }
    [duplicateGroups, combiDuplicateGroups, programNameCollisions, combiNameCollisions] = await Promise.all([
      safeFetch("findDuplicatePrograms", datasetId),
      safeFetch("findDuplicateCombis", datasetId),
      safeFetch("findProgramNameCollisions", datasetId),
      safeFetch("findCombiNameCollisions", datasetId),
    ]);
    log(
      `[Library:Duplicates] Loaded dataset ${datasetId}: ${duplicateGroups.length} Program duplicate group(s), ` +
        `${combiDuplicateGroups.length} Combi duplicate group(s), ${programNameCollisions.length} Program name ` +
        `clash(es), ${combiNameCollisions.length} Combi name clash(es).`
    );
  }

  // Wraps a synchronous DOM-building call the same way safeFetch() above
  // wraps a bridge call -- closes a real gap that reproduced the exact
  // incident safeFetch() was originally built for (2026-08-25, reported
  // directly: "setlist is again deactivated" after this session's Duplicates
  // rework): safeFetch() only ever protected the FETCH, never the render()
  // that immediately follows using that same still-new, still-evolving
  // data. A synchronous throw inside render() (or closeResolvePicker(),
  // which itself renders) propagates out of onDatasetChanged()/refresh()
  // below exactly the way a rejected fetch used to -- straight back into
  // pane.js's loadDataset() chain, BEFORE its own
  // updateCategoryTabAvailability() call, which is what actually disables
  // EVERY category tab (Setlist included), not just this one. Logs the
  // real error (not just "something failed") so a future occurrence is
  // actually diagnosable from the app's own log panel instead of only
  // showing up as "Setlist won't enable" with no further clue.
  function safeRender(fn, label) {
    try {
      fn();
    } catch (err) {
      log(`[Library:Duplicates] ${label} failed: ${err && err.stack ? err.stack : err}`);
    }
  }

  async function onDatasetChanged() {
    expandedDuplicateKeys.clear();
    expandedCollisionKeys.clear();
    // A genuinely NEW dataset (unlike refresh() below, called after e.g. a
    // resolve on the SAME dataset -- see pane.js's load()'s own doc
    // comment) -- any open resolve picker's `group` is a stale reference
    // into the OLD dataset's now-irrelevant data, so close it rather than
    // leave it showing meaningless entries.
    safeRender(closeResolvePicker, "closeResolvePicker()");
    await fetchDuplicates();
    safeRender(render, "render()");
  }

  async function refresh() {
    await fetchDuplicates();
    safeRender(render, "render()");
  }

  // getGroupCount exposed so pane.js's own updateCategoryTabAvailability()
  // can disable the Duplicates tab for a dataset with nothing to show on
  // EITHER sub-tab at all -- the common case for most real files, unlike
  // Setlist/Programs/Combis actually being genuinely empty.
  return {
    onDatasetChanged,
    refresh,
    render,
    getGroupCount: () =>
      duplicateGroups.length + combiDuplicateGroups.length + programNameCollisions.length + combiNameCollisions.length,
  };
}
