// The last Cross Dataset analysis result, shared between the sidebar that
// runs it (cross-dataset-duplicates-panel.js) and the panes that filter by it
// (pane.js's "Cross Dataset" dropdown next to the name filter) -- see
// STATE.md's "Cross Dataset results as a filter" RFC. Plain script, loaded
// right after datasets.js and shaped like it: module state + getters + a
// change broadcast.
//
// ONE pair of datasets at a time (project owner's decision, 2026-10-01), and
// ONE Find produces every mode's result at once (2026-10-02): Duplicates,
// Differences, Compare PROG and Compare COMBI are always stored -- and
// replaced or dropped -- together, so there is never a half-searched state.
// Holding several pairs at once is possible later (key by pair,
// keep several entries); it would add a "compared with which file" choice to
// the pane dropdown, since one dataset could then belong to several pairs.
//
// Staleness: each result remembers both datasets' `editCount` (PcgFile::
// editCount(), via listDatasets()) from when the search ran. Any write into
// either file bumps that, so revalidateCrossDatasetResults() -- called by the
// sidebar on every datasets.js broadcast and by each pane's library load(),
// which every write path already goes through -- drops a result as soon as
// the slots it lists may no longer be true.
//
// The sidebar reads results in its own A/B orientation (getCrossDatasetResult());
// a pane reads them per dataset id (getCrossDatasetFilterCategories()), so
// which file was A and which was B never matters to a pane.

let crossDatasetResult = null;  // null, or { idA, idB, editCountA, editCountB, modes: { [mode]: payload } }
let crossDatasetCategoriesCache = new Map();  // `${datasetId}:${kind}` -> categories (see getCrossDatasetFilterCategories())
const crossDatasetResultListeners = [];

function notifyCrossDatasetResultsChanged() {
  crossDatasetCategoriesCache = new Map();
  for (const listener of crossDatasetResultListeners) listener();
}

// Registers `listener()`, called whenever a result is stored or dropped.
function onCrossDatasetResultsChanged(listener) {
  crossDatasetResultListeners.push(listener);
}

// Compare COMBI row rules, shared by the sidebar's table and the panes' Combi
// categories. A row is { bank, number, nameA, nameB, changes: [description] }.
//
// An untouched "Init Combi" template slot on BOTH sides: nothing to compare --
// hidden by default in the sidebar, always left out of the pane categories.
function isInitCombiRow(d) {
  return /init combi/i.test(d.nameA) && /init combi/i.test(d.nameB);
}

// A Combi with a different NAME and more than 3 changes is a different song,
// not an edited one -- listing every differing block is just noise.
const DIFFERENT_SONG_MIN_CHANGES = 4;

function isDifferentSong(d) {
  return d.nameA !== d.nameB && d.changes.length >= DIFFERENT_SONG_MIN_CHANGES;
}

// One Program/Combi slot as a Set key -- the same `${bank}-${number}` shape
// the pane tables already use for their row keys.
function crossDatasetSlotKey(bank, number) {
  return `${bank}-${number}`;
}

// `mode`'s payload plus the pair it describes ({ idA, idB, ...payload }), or null.
function getCrossDatasetResult(mode) {
  const r = crossDatasetResult;
  if (!r || !r.modes[mode]) return null;
  return { idA: r.idA, idB: r.idB, ...r.modes[mode] };
}

// The pair the stored result describes ({ idA, idB }), or null.
function getCrossDatasetPair() {
  const r = crossDatasetResult;
  return r ? { idA: r.idA, idB: r.idB } : null;
}

// Stores one Find's results, replacing whatever was there. `pair` = { idA, idB,
// editCountA, editCountB } as of the search; `modes` = { duplicates: {groups},
// differences: {rows}, comparePrograms: {programs}, compareCombis: {combis} }.
function setCrossDatasetResults(pair, modes) {
  crossDatasetResult = { ...pair, modes };
  notifyCrossDatasetResultsChanged();
}

// Drops everything (another pair picked, a file closed or edited, a failed Find).
function clearCrossDatasetResults() {
  if (!crossDatasetResult) return;
  crossDatasetResult = null;
  notifyCrossDatasetResultsChanged();
}

// Drops the result if either dataset was closed or edited since the search.
// `datasets` is a listDatasets() array; omitted, a fresh one is fetched.
async function revalidateCrossDatasetResults(datasets) {
  if (!crossDatasetResult) return;
  const list = datasets || (await window.listDatasets());
  const r = crossDatasetResult;
  if (!r) return;  // cleared while the list was being fetched
  const editCountOf = (id) => {
    const d = list.find((x) => x.datasetId === id);
    return d ? d.editCount : undefined;
  };
  if (editCountOf(r.idA) !== r.editCountA || editCountOf(r.idB) !== r.editCountB) clearCrossDatasetResults();
}

// What a pane showing `datasetId` can filter its `kind` ("programs" | "combis")
// list by: null if the stored result doesn't involve this dataset, otherwise
// [{ group, items: [{ key, label, slots: Set<slotKey> }] }] -- every category
// this file has from its OWN point of view (its own slots, "this file" rather
// than A/B), in the sidebar's mode order. `group` is the sidebar mode's name
// for a mode with several categories (an <optgroup>), null for a mode that is
// one category by itself (a plain option named after the mode). Only modes
// in the stored result are listed (after a Find, all of them); a category with
// no slots is still listed (the dropdown shows it disabled).
function getCrossDatasetFilterCategories(datasetId, kind) {
  const r = crossDatasetResult;
  if (!r || (datasetId !== r.idA && datasetId !== r.idB)) return null;
  const cacheKey = `${datasetId}:${kind}`;
  if (!crossDatasetCategoriesCache.has(cacheKey)) {
    crossDatasetCategoriesCache.set(cacheKey, buildCrossDatasetCategories(r, datasetId === r.idA, kind));
  }
  return crossDatasetCategoriesCache.get(cacheKey);
}

function slotSet(rows, toSlot) {
  const slots = new Set();
  for (const row of rows) {
    const slot = toSlot(row);
    if (slot) slots.add(slot);
  }
  return slots;
}

function buildCrossDatasetCategories(r, isA, kind) {
  const datasetId = isA ? r.idA : r.idB;
  const { duplicates, differences, comparePrograms, compareCombis } = r.modes;
  const groups = [];
  // Compare PROG / COMBI rows are the SAME slot in both files.
  const sameSlot = (d) => crossDatasetSlotKey(d.bank, d.number);

  if (kind === "programs" && duplicates) {
    // Every copy that lives in THIS file -- the group's other members are the other file's.
    const members = duplicates.groups.flatMap((g) => g.members).filter((m) => m.datasetId === datasetId);
    groups.push({
      group: null,
      items: [{ key: "duplicates", label: "Duplicates", slots: slotSet(members, (m) => crossDatasetSlotKey(m.bank, m.number)) }],
    });
  }
  if (kind === "programs" && differences) {
    // This file's own side of a Differences row; -1 = no slot here.
    const ownSlots = (rowKind) => slotSet(differences.rows.filter((row) => row.kind === rowKind), (row) => {
      const bank = isA ? row.aBank : row.bBank;
      return bank >= 0 ? crossDatasetSlotKey(bank, isA ? row.aNumber : row.bNumber) : null;
    });
    // "Only in the other file" is deliberately missing -- those slots don't exist here.
    groups.push({
      group: "Differences",
      items: [
        { key: "differences:onlyHere", label: "Only in this file", slots: ownSlots(isA ? "onlyInA" : "onlyInB") },
        { key: "differences:renamed", label: "Renamed", slots: ownSlots("renamed") },
        { key: "differences:modifiedTwin", label: "Modified twins", slots: ownSlots("modifiedTwin") },
        { key: "differences:moved", label: "Moved", slots: ownSlots("moved") },
      ],
    });
  }
  if (kind === "programs" && comparePrograms) {
    groups.push({
      group: null,
      items: [{ key: "comparePrograms", label: "Compare PROG", slots: slotSet(comparePrograms.programs, sameSlot) }],
    });
  }
  if (kind === "combis" && compareCombis) {
    const rows = compareCombis.combis.filter((d) => !isInitCombiRow(d));
    groups.push({
      group: "Compare COMBI",
      items: [
        // "All diverged" = Edited + Different songs (the sidebar's own count); the other two never overlap.
        { key: "compareCombis:diverged", label: "All diverged", slots: slotSet(rows, sameSlot) },
        { key: "compareCombis:edited", label: "Edited", slots: slotSet(rows.filter((d) => !isDifferentSong(d)), sameSlot) },
        { key: "compareCombis:differentSong", label: "Different songs", slots: slotSet(rows.filter(isDifferentSong), sameSlot) },
      ],
    });
  }
  return groups;
}
