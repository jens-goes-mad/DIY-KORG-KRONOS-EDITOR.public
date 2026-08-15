// The Combis category's own renderer -- pane.js's createLibraryPanels() (the
// shared Programs/Combis/Duplicates tab coordinator) instantiates
// createCombisPanel() below the same way it instantiates
// createProgramsPanel()/createDuplicatesPanel() (pane-program-editor.js).
// Combis rows are draggable (swap / move within a bank / move to a
// different bank -- see buildCombisRow's own comment, STATE.md's Combi
// rearrangement entry for the full design and why a Combi's own vacated
// slot is always safe to fill: unlike Programs, a Combi is only ever
// referenced by Set List slots, never by anything else).
//
// This file used to be library.js, the original everything-in-one-file
// Programs/Combis/Duplicates renderer -- Programs/Duplicates moved out to
// pane-program-editor.js and the shared tab-switching coordinator moved to
// pane.js on 2026-08-14 (neither is Combi-specific), leaving this file with
// nothing but Combis code; renamed the same day once nothing else was left
// to split out, completing the same file-per-editor direction pane-setlist-
// editor.js/pane-program-editor.js already established.

// Set during a Combi row's own dragstart, cleared on dragend -- module-level
// (not inside createCombisPanel() below) because createCombisPanel() is
// called once PER PANE, so a per-pane `let` here would only ever be visible
// to dragover handlers in the SAME pane the drag started in: a drag from one
// pane onto a row in the OTHER pane (both showing the same dataset -- cross-
// DATASET dragging is a separate, deliberate restriction, checked via
// datasetId below, not this) would see this as still `null` and never light
// up `.drop-target` at all. Same fix pane-setlist-editor.js's own
// draggedFromDatasetId already uses for Setlist rows -- see buildCombisRow's
// own comment for the three gestures this backs (swap / move within bank /
// move to a different bank).
let draggedCombi = null;

// Mirrors kronos::TimbreStatus's Off value (PcgFile.h: `enum class
// TimbreStatus { Off, Internal, External, Ex2, Unknown }`, Off = 0) -- the
// bridge sends this as a raw int now (EditorBridge stopped formatting it to
// a string 2026-08-15, see STATE.md), and "Off" is the only status this
// file ever actually branches on (Internal/External/Ex2 are never shown as
// text anywhere in this app today), so a single named constant is enough --
// no need for a full name-lookup table here the way FontSize/ProgramBankType
// (pane-setlist-editor.js/pane.js) need one for real display strings.
const TIMBRE_STATUS_OFF = 0;

// Combis table: filter/search + per-bank filter buttons + a Combis-only Set
// List filter dropdown (elements.bankFilterRow/selectControlRow -- the
// shared coordinator in pane.js owns their surrounding DOM/show-hide, this
// just renders INTO them), one row per Combi with an expandable Timbre-
// references row, and the three Combi rearrange drag gestures.
//
// `callbacks.findProgram(bank, number)` is pane-program-editor.js's
// createProgramsPanel().findProgram -- formatTimbreRef() below needs read
// access to a Program's name for a confirmed Timbre bank reference, handed
// down as a callback rather than reaching into that other file's closure
// directly.
function createCombisPanel(
  { panelTable, bankFilterRow, selectControlRow },
  {
    getDatasetId,
    getFilterText,
    getProgramBankType,
    findProgram,
    onJumpToSetlist,
    onJumpToInstrument,
    log,
    onRefreshOppositeLibrary,
    onNeedsFullReload,
    onSetlistRefsRepointed,
  }
) {
  let combis = [];
  // `${bank}-${number}` keys of every expanded Timbre row -- several can be
  // open at once. Safe to allow here in a way Setlist rows can't: every
  // Combi row is read-only, so there's no in-flight-edit data-corruption
  // risk multi-open needs to guard against (see pane-setlist-editor.js's
  // openPanels/openSlotEditors for that guard, which only matters once a
  // row can actually be written to).
  const expandedCombiKeys = new Set();
  // Bank-filter state -- `present` is which bank indices actually have
  // entries in the current dataset (recomputed on every fetch), `filter` is
  // which of those are currently "pressed" (shown), independently user-
  // toggleable. Only reset to match `present` (show everything) on a
  // genuinely new dataset (onDatasetChanged()) -- a same-dataset refresh
  // (refresh()) keeps whatever the user had filtered to instead.
  let combiPresentBanks = new Set();
  let combiBankFilter = new Set();

  // Set List filter for this panel only -- which Set List (if any) a shown
  // Combi must be referenced by, via the same `c.setlistUsages` array
  // already loaded for the "Set Lists"/"#STL" columns (EditorBridge::
  // listCombis() computes this once per dataset load from
  // PcgFile::setlistUsageCounts(), see its own doc comment in PcgFile.h --
  // no new backend work needed, this filter just re-slices data already in
  // memory). -1 means "no filter, show every Combi" (all bank-filtered rows
  // still apply). Reset to -1 only on a genuinely new dataset, same as the
  // bank filter above.
  let allSetlists = [];
  let selectedSetlistIndex = -1;

  function refreshBankButtons() {
    renderBankFilterRow(bankFilterRow, COMBI_BANK_NAMES, combiPresentBanks, combiBankFilter, () => render());
  }

  // Wired up once (not re-created per fetch/render) -- see
  // createSelectControlRow()'s own comment for why getters instead of
  // captured Set values.
  createSelectControlRow(selectControlRow, {
    getPresent: () => combiPresentBanks,
    getFilterSet: () => combiBankFilter,
    onChange: () => {
      refreshBankButtons();
      render();
    },
  });

  // This panel's own Set List filter dropdown, sitting right beside the
  // None/All/Invert buttons in the same row (appended into the same
  // container, not a separate one -- per explicit request). Options
  // repopulated on every fetch (populateSetlistFilterSelect() below) since
  // the Set List list is per-dataset, same lifecycle as
  // combiPresentBanks/combiBankFilter above.
  const setlistFilterSelect = document.createElement("select");
  setlistFilterSelect.className = "setlist-filter-select";
  {
    const wrap = document.createElement("div");
    wrap.className = "select is-small setlist-filter-select-wrap";
    wrap.appendChild(setlistFilterSelect);
    selectControlRow.appendChild(wrap);
    setlistFilterSelect.addEventListener("change", () => {
      selectedSetlistIndex = Number(setlistFilterSelect.value);
      render();
    });
  }

  // Rebuilds the dropdown's <option>s from `allSetlists` (refreshed on every
  // fetch) -- a fixed "All Set Lists" option (value -1, the default/no-
  // filter state) followed by one option per real Set List, same
  // `kronosNumber(index) + name` label pane-setlist-editor.js's own Set List
  // selector uses (populateSetlistSelect()) for a consistent look across
  // panes. Re-applies `selectedSetlistIndex` afterwards so a jumpToEntry()
  // or in-place refresh() doesn't silently reset an already-chosen filter.
  function populateSetlistFilterSelect() {
    setlistFilterSelect.innerHTML = "";
    const allOpt = document.createElement("option");
    allOpt.value = "-1";
    allOpt.textContent = "All Set Lists";
    setlistFilterSelect.appendChild(allOpt);
    for (const s of allSetlists) {
      const opt = document.createElement("option");
      opt.value = String(s.index);
      opt.textContent = `${kronosNumber(s.index)}  ${s.name}`;
      setlistFilterSelect.appendChild(opt);
    }
    setlistFilterSelect.value = String(selectedSetlistIndex);
  }

  // Small pill per Set List reference (name + slot number) -- only shown
  // when there are few enough (<=10) to stay readable; above that, the
  // "#STL" count column still shows the total, just without the
  // per-reference breakdown. Bulma's own `.tags`/`.tag` (a real pill/chip
  // component), not a hand-rolled one -- each pill is a `<button class=
  // "tag">` (Bulma styles `.tag` the same regardless of element) so it can
  // jump to that Set List slot, same as the Program usage row's own Set
  // List references (pane-program-editor.js's buildUsageRow()).
  function badgesCell(setlistUsages, combi) {
    const td = document.createElement("td");
    if (setlistUsages && setlistUsages.length > 0 && setlistUsages.length <= 10) {
      const wrap = document.createElement("div");
      wrap.className = "tags";
      for (const u of setlistUsages) {
        const badge = document.createElement("button");
        badge.type = "button";
        badge.className = "tag setlist-usage-badge";
        badge.textContent = `${u.setlistName} (${kronosNumber(u.songIndex)})`;
        badge.title =
          "Show this in the Setlist view (Shift+click: show in the opposite pane instead, switching its " +
          "dataset to match this one; Shift+Cmd+click: same, but keep whatever dataset the opposite pane " +
          "already has open)";
        badge.addEventListener("click", (ev) => {
          ev.stopPropagation();  // don't also toggle this Combi row's Timbre list open/closed
          onJumpToSetlist({
            setlistIndex: u.setlistIndex,
            songIndex: u.songIndex,
            from: { kind: "instrument", isProgram: false, bank: combi.bank, number: combi.number },
            toOpposite: ev.shiftKey,
            keepOppositeDataset: ev.metaKey,
          });
        });
        wrap.appendChild(badge);
      }
      td.appendChild(wrap);
    }
    return td;
  }

  // Mirrors PcgFile.cpp's kConfirmedTimbreBanks -- all 20 Program bank
  // indices now have a confirmed raw Combi Timbre code (2026-08-14,
  // USER-FF was the last gap), one small table here too so this mirror
  // can't drift out of sync with the backend's own list (docs/content/format/index.md
  // §6.2).
  // (No `name` field, unlike the old version of this table -- see
  // formatTimbreRef() below for why.)
  // INT-A..D coincide (both number spaces use 0..3); USER-A/D/F/AA use a
  // *different* number in each space (e.g. USER-D is Program bank index 9
  // but Timbre code 20) -- a Timbre's rawBankCode must be translated to a
  // Program bank index before it can be compared against ProgramInfo.bank
  // (getProgramBankType()'s map, the `programs` array's own .bank field)
  // at all; outside this table, rawBankCode isn't known to mean the same
  // thing as a Program's own .bank field, so looking up its type/name
  // would be a guess, not a lookup -- exactly what this project doesn't do.
  //
  // CORRECTED 2026-08-10: USER-A/D/F/AA's indices were 8/11/13/14, an
  // extrapolation later contradicted by real hardware -- see
  // kConfirmedTimbreBanks's own doc comment in PcgFile.cpp for the full
  // derivation (GM/g(d) aren't stored PBK1/MBK1 chunks, USER-A..G is 7
  // banks not 6, so everything from USER-A onward sits 2 indices earlier).
  // USER-G/USER-GG added the same day once independently confirmed by name
  // against real hardware too.
  //
  // PROMOTED 2026-08-11: INT-F/USER-B/C/CC/DD used to be name-only
  // confirmed (a raw code checked against real hardware, but no matching
  // index) -- meaning `programBankForConfirmedTimbreCode()` returned null
  // for them, so formatTimbreRef() below never looked up their actual
  // Program name (reported bug: Combi U-A 002 "Sex on Fire" Timbre 2,
  // raw bank 5/INT-F, showed the bank label but no Program name at all).
  // Once §5.2's full 20-bank order got confirmed against real hardware,
  // every one of them turned out to already have a confirmed index too --
  // promoted here to match PcgFile.cpp.
  //
  // USER-BB/EE added 2026-08-11, checked directly against real Combis
  // (setlist_test_2.PCG: Combi I-A 000 "K-Lab: Katja's House" Timbre 9,
  // Combi U-A 014 "KARMA Org 1'2'3  Piano 4" Timbre 7).
  //
  // CORRECTED 2026-08-14, retracting a 2026-08-11 misreading: this array
  // briefly had `{programBankIndex: 10, rawBankCode: 4}` (INT-E's own
  // index paired with the wrong code) -- the project owner re-checked the
  // same real Combi and confirmed real hardware actually shows `INT-E`
  // for that reference, not `USER-E`. Fixed: `INT-E` is raw code 4
  // (`{4, 4}`, the "obvious" extrapolation was right all along -- no
  // anomaly), `USER-E` is raw code 21 (`{10, 21}`, confirmed separately
  // via Combi I-A 001 "Stradivarius Goes POP" Timbre 7) -- also exactly
  // the "obvious" gap in `USER-A..G`'s 17-23 block. See
  // kConfirmedTimbreBanks' own doc comment in PcgFile.cpp for the full
  // story -- kept as a methodology note, not scrubbed from history.
  const CONFIRMED_TIMBRE_BANKS = [
    { programBankIndex: 0, rawBankCode: 0 },
    { programBankIndex: 1, rawBankCode: 1 },
    { programBankIndex: 2, rawBankCode: 2 },
    { programBankIndex: 3, rawBankCode: 3 },
    { programBankIndex: 4, rawBankCode: 4 },
    { programBankIndex: 5, rawBankCode: 5 },
    { programBankIndex: 6, rawBankCode: 17 },
    { programBankIndex: 7, rawBankCode: 18 },
    { programBankIndex: 8, rawBankCode: 19 },
    { programBankIndex: 9, rawBankCode: 20 },
    { programBankIndex: 10, rawBankCode: 21 },
    { programBankIndex: 11, rawBankCode: 22 },
    { programBankIndex: 12, rawBankCode: 23 },
    { programBankIndex: 13, rawBankCode: 24 },
    { programBankIndex: 14, rawBankCode: 25 },
    { programBankIndex: 15, rawBankCode: 26 },
    { programBankIndex: 16, rawBankCode: 27 },
    { programBankIndex: 17, rawBankCode: 28 },
    { programBankIndex: 18, rawBankCode: 29 },
    { programBankIndex: 19, rawBankCode: 30 },
  ];

  // The confirmed Program bank index for a Timbre's raw bank code, or null
  // if that code isn't independently confirmed yet.
  function programBankForConfirmedTimbreCode(rawBankCode) {
    const entry = CONFIRMED_TIMBRE_BANKS.find((b) => b.rawBankCode === rawBankCode);
    return entry ? entry.programBankIndex : null;
  }

  // Formats one Timbre's Program reference for display: the confirmed bank
  // name when known, otherwise the raw numeric code so it's still honest
  // about what was found (see docs/content/format/index.md's "Combi Timbre references"
  // section -- only some bank codes have been identified so far). For a
  // confirmed bank, also shows the engine type (getProgramBankType(), same
  // per-bank map the Programs panel's own bank-filter buttons use) and the
  // actual Program name via findProgram() (pane-program-editor.js's
  // createProgramsPanel().findProgram, handed down as a callback). A Timbre
  // can hold a real reference while switched off (status != Off is NOT the
  // same thing as isDefault -- see TimbreRef's doc comment in PcgFile.h),
  // so that's called out explicitly rather than hidden -- it still counts
  // as "this Combi references that Program." Only ever called for a
  // non-default Timbre -- buildTimbreRow() below skips isDefault ones
  // entirely before calling this, so there's no "--" case to handle here.
  // Returns `{ref, name}` rather than one combined string -- buildTimbreRow()
  // renders them as two separately-aligned columns, so the Program name
  // isn't a plain divider/quote suffix hanging off a variable-length ref.
  //
  // Bank label source, deliberately not duplicated (2026-08-11): when
  // `programBank` is confirmed, the name comes from PROGRAM_BANK_NAMES
  // (pane.js's single source of truth for Program bank display names) --
  // NOT from `t.bankName`, which the backend sends as "" for exactly that
  // case (kronos::timbreBankName(), see its own doc comment in PcgFile.cpp).
  // `t.bankName` IS populated for a raw code confirmed by name but with no
  // PBK1 index at all -- GM (raw code 6, confirmed 2026-08-12) is the real
  // case this was built for: permanently indexless (not one of the 20
  // stored Program banks), so `programBank` stays null and this ref falls
  // through to the `t.bankName` branch below, showing "GM" with no Program
  // name (there's nothing to look one up from without a confirmed bank).
  function formatTimbreRef(t) {
    const programBank = programBankForConfirmedTimbreCode(t.rawBankCode);
    const bank =
      programBank !== null
        ? PROGRAM_BANK_NAMES[programBank]
        : t.bankName
          ? abbreviateBankName(t.bankName)
          : `code ${t.rawBankCode}`;
    let ref = `${bank} ${kronosNumber(t.number)}`;
    let name = "";
    if (programBank !== null) {
      const bankType = getProgramBankType(programBank);
      if (bankType != null) ref += ` (${programBankTypeName(bankType)})`;
      const program = findProgram(programBank, t.number);
      if (program && program.name) name = program.name;
    }
    if (t.status === TIMBRE_STATUS_OFF) ref += " (off)";
    // `programBank` (not just `ref`) is returned too -- buildTimbreRow()
    // below needs it to know both WHETHER a jump target exists (only for a
    // confirmed bank -- an unidentified "code N" reference has no real
    // Program bank/number to jump to) and, if so, which bank to jump to
    // (this is a Timbre's own rawBankCode-derived index, not the same
    // number PROGRAM_BANK_NAMES[programBank] is a label FOR -- see
    // programBankForConfirmedTimbreCode()'s own doc comment).
    return { ref, name, programBank };
  }

  function buildTimbreRow(combi) {
    const tr = document.createElement("tr");
    tr.className = "editor-row";  // reuses the shared expand-row look from pane.js/style.css
    const td = document.createElement("td");
    td.colSpan = 4;  // Bank, Name, Set Lists, #STL -- a real <table> again

    const heading = document.createElement("div");
    heading.className = "usage-heading";
    heading.textContent = "Timbre Program references:";
    td.appendChild(heading);

    // Default (unassigned, "--") Timbres are omitted entirely rather than
    // listed dimmed -- a 16-Timbre Combi that only really uses 2 or 3
    // doesn't need 13 "--" lines to scroll past. One Timbre per line (no
    // multi-column packing -- see .timbre-list in style.css) so a long
    // Program reference has the row's full width to wrap into instead of
    // wrapping awkwardly inside a half-width column. `i` stays the
    // Timbre's own ORIGINAL 1-16 slot number even with entries skipped --
    // this must read as "which of the Combi's 16 physical Timbre slots",
    // not a renumbered count of however many are shown.
    const activeTimbres = combi.timbres.filter((t) => !t.isDefault);
    if (activeTimbres.length === 0) {
      const none = document.createElement("div");
      none.className = "usage-empty";
      none.textContent = "No Timbres assigned -- all 16 are unused defaults.";
      td.appendChild(none);
    } else {
      const list = document.createElement("ul");
      list.className = "usage-list timbre-list";
      combi.timbres.forEach((t, i) => {
        if (t.isDefault) return;
        const li = document.createElement("li");
        li.className = t.status === TIMBRE_STATUS_OFF ? "timbre-inactive-ref" : "";
        const label = document.createElement("span");
        label.className = "timbre-label";
        label.textContent = `Timbre ${i + 1}:`;
        const { ref, name, programBank } = formatTimbreRef(t);
        // A button, same look/behavior as the Setlist table's own Bank
        // button (pane-setlist-editor.js) -- only when `programBank` is
        // confirmed, since an unidentified "code N" reference has no real
        // Program bank/number to jump to. Reuses `onJumpToInstrument`, the
        // exact same per-pane closure the Setlist table's Bank button
        // already calls (pane.js's jumpToInstrument()) -- always jumps
        // within THIS pane, never the opposite one, by construction (see
        // its own doc comment in pane.js).
        let refSpan;
        if (programBank !== null) {
          refSpan = document.createElement("button");
          refSpan.type = "button";
          refSpan.className = "button is-small bank-jump-button timbre-ref";
          refSpan.title =
            `Show Program ${ref} in this pane's Programs view (Shift+click: show in the opposite pane ` +
            "instead, switching its dataset to match this one; Shift+Cmd+click: same, but keep whatever " +
            "dataset the opposite pane already has open -- e.g. jump to this same bank/number in your own " +
            "reference dataset, useful when this Timbre's Program is unremarkable/default and there's " +
            "nothing distinctive to match by content)";
          refSpan.addEventListener("click", (ev) => {
            ev.stopPropagation();
            onJumpToInstrument({
              isProgram: true,
              bank: programBank,
              number: t.number,
              from: { kind: "instrument", isProgram: false, bank: combi.bank, number: combi.number },
              toOpposite: ev.shiftKey,
              keepOppositeDataset: ev.metaKey,
            });
          });
        } else {
          refSpan = document.createElement("span");
          refSpan.className = "timbre-ref";
        }
        refSpan.textContent = ref;
        const nameSpan = document.createElement("span");
        nameSpan.className = "timbre-name";
        nameSpan.textContent = name;
        li.append(label, refSpan, nameSpan);
        list.appendChild(li);
      });
      td.appendChild(list);
    }

    tr.appendChild(td);
    return tr;
  }

  function render() {
    const needle = getFilterText().trim().toLowerCase();
    const rows = filterByName(combis, needle)
      .filter((c) => combiBankFilter.has(c.bank))
      .filter(
        (c) => selectedSetlistIndex < 0 || (c.setlistUsages || []).some((u) => u.setlistIndex === selectedSetlistIndex)
      );

    panelTable.innerHTML = "";
    const table = document.createElement("table");
    table.className = "table is-fullwidth is-hoverable is-narrow combis-table";
    table.innerHTML =
      colgroupHtml([1.6, 3, null, 0.9]) +
      "<thead><tr><th>Bank</th><th>Name</th><th>Set Lists</th>" +
      "<th title=\"Set List references\">#STL</th></tr></thead><tbody></tbody>";
    const tbody = table.querySelector("tbody");

    for (const c of rows) {
      const tr = document.createElement("tr");
      const nameTd = document.createElement("td");
      nameTd.textContent = c.name || "(empty)";
      tr.append(
        bankCell(false, c.bank, c.number),
        nameTd,
        badgesCell(c.setlistUsages, c),
        refCell(String(c.setlistReferenceCount), false)
      );

      const key = `${c.bank}-${c.number}`;
      tr.dataset.entryKey = key;  // lets jumpToEntry() find this exact row after a re-render
      if (expandedCombiKeys.has(key)) tr.classList.add("is-selected");
      tr.addEventListener("click", () => {
        if (expandedCombiKeys.has(key)) expandedCombiKeys.delete(key);
        else expandedCombiKeys.add(key);
        render();
      });

      // Four gestures, still the same 3-zone drop pane-setlist-editor.js's
      // Setlist table already uses (dropZoneForEvent(), a shared top-level
      // function in pane.js) -- "onto" just branches in two depending on
      // what's already at the target:
      //  - Drop ONTO an EMPTY Combi row (its name case-insensitively
      //    contains "init combi" -- Korg's own literal "Init Combi", or
      //    this app's own vacated-slot rename from moveCombiToBank(),
      //    "- Init Combi -") -> copy (PcgFile::copyCombi()). The source is
      //    left completely untouched -- this is how to duplicate a Combi
      //    (e.g. keeping a shared patch that needs a few Timbre levels
      //    adjusted differently per physical setup) without editing the
      //    original.
      //  - Drop ONTO any other (occupied) Combi row -> swap (same or
      //    different bank -- always safe, nothing is destroyed, so no bank
      //    restriction).
      //  - Drop BEFORE/AFTER a row in the SAME bank -> move within bank
      //    (shift the intervening range, PcgFile::moveCombiWithinBank()).
      //  - Drop BEFORE/AFTER a row in a DIFFERENT bank -> move to that
      //    bank, overwriting the target (PcgFile::moveCombiToBank()) --
      //    there's no "shift" concept spanning two independent banks'
      //    128-slot arrays, so before/after collapses to the same thing as
      //    onto once a bank boundary is crossed (including the empty-slot
      //    copy case above -- before/after never copies, only onto does).
      // Swap/move-within-bank/move-to-bank stay same-dataset-only (a bank/
      // number reference isn't portable across two different files' bank
      // layouts, same reasoning as Setlist slots) -- a drag originating from
      // a different dataset isn't even shown as a valid target during
      // dragover for those. The empty-slot COPY case above is the one
      // exception (2026-08-14): it's the only gesture where the destination
      // dataset's own Set Lists can never end up with a dangling reference
      // (the source is never touched), so it's allowed cross-dataset -- via
      // startCombiCrossDatasetCopy() (frontend/combi-cross-dataset-panel.js)
      // instead of calling window.copyCombi() directly, since a Combi
      // dragged into a DIFFERENT dataset can reference Programs that don't
      // exist there yet and need resolving first.
      tr.draggable = true;
      tr.addEventListener("dragstart", (ev) => {
        draggedCombi = { datasetId: getDatasetId(), bank: c.bank, number: c.number };
        ev.dataTransfer.setData("application/json", JSON.stringify(draggedCombi));
        ev.dataTransfer.effectAllowed = "move";
      });
      tr.addEventListener("dragend", () => {
        draggedCombi = null;
      });
      tr.addEventListener("dragover", (ev) => {
        // Cross-dataset only shows as a valid target for the empty-slot
        // COPY gesture -- see this row's own dragstart/drop comment above
        // for why that's the one exception to "same dataset only".
        const sameDataset = draggedCombi != null && draggedCombi.datasetId === getDatasetId();
        const crossDatasetCopyTarget = draggedCombi != null && !sameDataset && looksLikeEmptyCombiName(c.name);
        if (!sameDataset && !crossDatasetCopyTarget) {
          tr.classList.remove("drop-target");
          return;
        }
        ev.preventDefault();
        ev.dataTransfer.dropEffect = "move";
        tr.classList.add("drop-target");
      });
      tr.addEventListener("dragleave", () => tr.classList.remove("drop-target"));
      tr.addEventListener("drop", async (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        tr.classList.remove("drop-target");
        const raw = ev.dataTransfer.getData("application/json");
        if (!raw) return;
        const source = JSON.parse(raw);
        if (source.datasetId === getDatasetId() && source.bank === c.bank && source.number === c.number) return;  // dropped on itself

        const zone = dropZoneForEvent(tr, ev);
        const sourceLabel = formatBankNumber({ isProgram: false, bank: source.bank, number: source.number });
        const targetLabel = formatBankNumber({ isProgram: false, bank: c.bank, number: c.number });
        const targetLooksEmpty = looksLikeEmptyCombiName(c.name);

        if (source.datasetId !== getDatasetId()) {
          // Only the empty-slot COPY gesture goes cross-dataset -- see this
          // row's own dragstart/drop comment above.
          if (zone !== "on" || !targetLooksEmpty) return;
          await startCombiCrossDatasetCopy({
            srcDatasetId: source.datasetId,
            srcBank: source.bank,
            srcNumber: source.number,
            dstDatasetId: getDatasetId(),
            dstBank: c.bank,
            dstNumber: c.number,
            dstPaneEl: panelTable.closest(".pane"),
            sourceLabel,
            targetLabel,
            onApplied: async () => {
              await onNeedsFullReload();
              await onRefreshOppositeLibrary(getDatasetId());
            },
          });
          return;
        }

        let result, description;
        if (zone === "on" && targetLooksEmpty) {
          result = await window.copyCombi(getDatasetId(), source.bank, source.number, c.bank, c.number);
          description = `Copied ${sourceLabel} -> ${targetLabel}`;
        } else if (zone === "on") {
          result = await window.swapCombis(getDatasetId(), source.bank, source.number, c.bank, c.number);
          description = `Swapped ${sourceLabel} <-> ${targetLabel}`;
        } else if (source.bank === c.bank) {
          const toNumber = zone === "before" ? c.number : c.number + 1;
          result = await window.moveCombiWithinBank(getDatasetId(), c.bank, source.number, toNumber);
          description = `Moved ${sourceLabel} to position ${kronosNumber(toNumber)}`;
        } else {
          result = await window.moveCombiToBank(getDatasetId(), source.bank, source.number, c.bank, c.number);
          description = `Moved ${sourceLabel} -> ${targetLabel}, overwriting it`;
        }

        if (!result.ok) {
          showToast(result.error, { isError: true });
          return;
        }
        showToast(`${description} -- repointed ${result.setlistRefsRepointed} Set List slot(s).`);
        await onNeedsFullReload();
        await onRefreshOppositeLibrary(getDatasetId());
        // A swap/move-within-bank/move-to-bank can repoint real Set List
        // references (a copy never does -- setlistRefsRepointed is always 0
        // for it, see copyCombi()'s own doc comment) -- the Setlist tab's
        // own cached entries need refreshing too, in both this pane and the
        // opposite one, or they'd keep showing the pre-repoint bank/number.
        if (result.setlistRefsRepointed > 0) await onSetlistRefsRepointed(getDatasetId());
      });

      tbody.appendChild(tr);

      if (expandedCombiKeys.has(key) && c.timbres) tbody.appendChild(buildTimbreRow(c));
    }

    panelTable.appendChild(table);
  }

  async function fetchCombis() {
    const datasetId = getDatasetId();
    if (datasetId == null) {
      combis = [];
      allSetlists = [];
    } else {
      combis = await window.listCombis(datasetId);
      allSetlists = await window.listSetlists(datasetId);
      log(`[Library:Combis] Loaded dataset ${datasetId}: ${combis.length} Combis.`);
    }
    combiPresentBanks = new Set(combis.map((c) => c.bank));
  }

  // Called by the coordinator (pane.js's createLibraryPanels()) whenever the
  // shared dataset selection changes to a genuinely NEW dataset -- previous
  // filter selections belong to a different file's banks and mean nothing
  // here, so reset to "show everything."
  async function onDatasetChanged() {
    expandedCombiKeys.clear();
    await fetchCombis();
    combiBankFilter = new Set(combiPresentBanks);
    selectedSetlistIndex = -1;
    refreshBankButtons();
    populateSetlistFilterSelect();
    render();
  }

  // Called by the coordinator for every OTHER reload (e.g. after a Combi
  // rearrange, or a duplicate Program gets resolved -- which can repoint a
  // Combi Timbre reference) -- the SAME dataset just changed underneath this
  // view, so re-fetch but keep whatever the user had filtered to.
  async function refresh() {
    await fetchCombis();
    combiBankFilter = new Set([...combiBankFilter].filter((b) => combiPresentBanks.has(b)));
    refreshBankButtons();
    populateSetlistFilterSelect();
    render();
  }

  // Called by the coordinator (via its own jumpToEntry()) after already
  // switching to the "combis" tab -- expands this exact entry's Timbre row
  // and scrolls it into view, same as clicking the row directly. Makes sure
  // the target bank's filter button is "pressed" first, and resets the Set
  // List filter to "All Set Lists" so a filter chosen for browsing doesn't
  // silently hide the exact entry someone just clicked through to from a
  // Setlist row or a Combi Timbre reference.
  function jumpToEntry(bank, number) {
    const key = `${bank}-${number}`;
    expandedCombiKeys.add(key);
    combiBankFilter.add(bank);
    refreshBankButtons();
    selectedSetlistIndex = -1;
    populateSetlistFilterSelect();
    render();
    const row = panelTable.querySelector(`[data-entry-key="${key}"]`);
    if (row) scrollRowBelowHeader(row);
  }

  // getCombiCount exposed so pane.js's own updateCategoryTabAvailability()
  // can disable the Combis tab for a dataset with none at all.
  return { onDatasetChanged, refresh, render, jumpToEntry, getCombiCount: () => combis.length };
}
