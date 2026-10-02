// Headless checks for cross-dataset-results.js (the shared Cross Dataset result
// store). Both files are plain scripts (no modules, like every frontend/*.js
// file), so this runs with the store concatenated in front of it, in any JS
// engine -- from frontend/:
//   cat cross-dataset-results.js cross-dataset-results.test.js | node
//   (no node here? JavaScriptCore works too:)
//   cat cross-dataset-results.js cross-dataset-results.test.js > /tmp/t.js && osascript -l JavaScript /tmp/t.js
// The row fixtures are the real bridge shape (EditorBridge::
// findProgramDifferencesAcrossDatasets(): kind + both sides' bank/number, -1
// for "no slot on this side").

var window = typeof window !== "undefined" ? window : globalThis;

(async function () {
  let failed = 0;
  let passed = 0;
  function check(label, ok, extra) {
    if (ok) passed++;
    else {
      failed++;
      console.log(`FAIL: ${label}${extra !== undefined ? " -- " + JSON.stringify(extra) : ""}`);
    }
  }
  const sorted = (set) => [...set].sort().join(",");
  const itemsOf = (datasetId) => {
    const groups = getCrossDatasetFilterCategories(datasetId, "programs");
    if (!groups) return null;
    const byKey = {};
    for (const g of groups) for (const it of g.items) byKey[it.key] = sorted(it.slots);
    return byKey;
  };

  const MODE_NAMES = ["duplicates", "differences", "comparePrograms", "compareCombis"];
  let notifications = 0;
  onCrossDatasetResultsChanged(() => notifications++);

  const diff = (kind, a, b) => ({
    kind,
    aBank: a ? a[0] : -1, aNumber: a ? a[1] : -1, aName: "",
    bBank: b ? b[0] : -1, bNumber: b ? b[1] : -1, bName: "", bankType: 0,
  });
  const rows = [
    diff("onlyInA", [1, 91]), diff("onlyInA", [4, 0]),
    diff("onlyInB", null, [6, 0]),
    diff("renamed", [2, 50], [0, 50]),
    diff("modifiedTwin", [0, 1], [13, 1]),
    diff("moved", [0, 0], [13, 0]), diff("moved", [0, 2], [13, 2]),
  ];
  // The same comparison run the other way round (B picked as A): sides swapped.
  const swap = (r) => ({
    ...r,
    kind: r.kind === "onlyInA" ? "onlyInB" : r.kind === "onlyInB" ? "onlyInA" : r.kind,
    aBank: r.bBank, aNumber: r.bNumber, bBank: r.aBank, bNumber: r.aNumber,
  });

  // One Find's results (every mode present -- the sidebar always stores all four);
  // `modes` overrides some, the rest are empty.
  const empty = () => ({ duplicates: { groups: [] }, differences: { rows: [] }, comparePrograms: { programs: [] }, compareCombis: { combis: [] } });
  const store = (pair, modes = {}) => setCrossDatasetResults(pair, { ...empty(), ...modes });
  const P12 = { idA: 1, idB: 2, editCountA: 5, editCountB: 7 };

  // --- empty store -----------------------------------------------------------
  check("empty store: no categories", getCrossDatasetFilterCategories(1, "programs") === null);
  check("empty store: no result, no pair", getCrossDatasetResult("differences") === null && getCrossDatasetPair() === null);

  // --- per-dataset categories (Differences) ------------------------------------
  store(P12, { differences: { rows } });
  check("set notifies once", notifications === 1, notifications);
  check("sidebar view keeps A/B + payload", (() => {
    const r = getCrossDatasetResult("differences");
    return r && r.idA === 1 && r.idB === 2 && r.rows === rows;
  })());
  check("pair getter", JSON.stringify(getCrossDatasetPair()) === '{"idA":1,"idB":2}');
  const one = itemsOf(1);
  const two = itemsOf(2);
  check("dataset A: Only in this file = onlyInA, own slots", one["differences:onlyHere"] === "1-91,4-0", one);
  check("dataset A: renamed/modified/moved use the A side", one["differences:renamed"] === "2-50" &&
    one["differences:modifiedTwin"] === "0-1" && one["differences:moved"] === "0-0,0-2", one);
  check("dataset B: Only in this file = onlyInB, own slots", two["differences:onlyHere"] === "6-0", two);
  check("dataset B: renamed/modified/moved use the B side", two["differences:renamed"] === "0-50" &&
    two["differences:modifiedTwin"] === "13-1" && two["differences:moved"] === "13-0,13-2", two);
  check("a dataset outside the pair gets nothing", getCrossDatasetFilterCategories(3, "programs") === null);
  check("after a Find, every mode is listed -- empty categories too", (() => {
    store(P12);
    const progs = getCrossDatasetFilterCategories(1, "programs");
    const combis = getCrossDatasetFilterCategories(1, "combis");
    return progs.length === 3 && combis.length === 1 &&
      [...progs, ...combis].every((g) => g.items.every((it) => it.slots.size === 0));
  })());

  // --- A/B order doesn't matter to a pane -------------------------------------
  store({ idA: 2, idB: 1, editCountA: 7, editCountB: 5 }, { differences: { rows: rows.map(swap) } });
  check("swapped pair: dataset 1 sees the same filter data", JSON.stringify(itemsOf(1)) === JSON.stringify(one), itemsOf(1));
  check("swapped pair: dataset 2 sees the same filter data", JSON.stringify(itemsOf(2)) === JSON.stringify(two), itemsOf(2));

  // --- Duplicates / Compare PROG / Compare COMBI -----------------------------
  {
    const member = (datasetId, bank, number) => ({ datasetId, bank, number, name: "", filename: "", bankType: 0 });
    const combi = (bank, number, nameA, nameB, changeCount) =>
      ({ bank, number, nameA, nameB, changes: Array.from({ length: changeCount }, (_, i) => "c" + i) });
    store(P12, {
      duplicates: {
        groups: [
          { members: [member(1, 0, 0), member(2, 13, 0)] },
          { members: [member(1, 0, 2), member(1, 3, 3), member(2, 13, 2)] },  // two copies in file 1
        ],
      },
      differences: { rows },
      comparePrograms: { programs: [{ bank: 3, number: 5, nameA: "", nameB: "", bankType: 0 }] },
      compareCombis: {
        combis: [
          combi(7, 1, "Separate Ways", "Separate Ways", 2),
          combi(7, 100, "Tainted Love", "Your Song", 5),   // different song
          combi(7, 101, "Tainted Love", "Your Song 2", 3), // renamed but only 3 changes: still the same song
          combi(2, 3, "Init Combi", "INIT COMBI", 1),       // template slot on both sides: left out
        ],
      },
    });

    const progs = getCrossDatasetFilterCategories(1, "programs");
    check("Programs: modes in the sidebar's order, one-category modes ungrouped",
      progs.map((g) => (g.group || "-") + ":" + g.items.map((it) => it.key).join("+")).join(" ") ===
        "-:duplicates Differences:differences:onlyHere+differences:renamed+differences:modifiedTwin+differences:moved -:comparePrograms",
      progs.map((g) => [g.group, g.items.map((it) => it.key)]));
    check("Duplicates: only this file's own copies (all of them)", itemsOf(1).duplicates === "0-0,0-2,3-3", itemsOf(1));
    check("Duplicates: the other file sees its own", itemsOf(2).duplicates === "13-0,13-2", itemsOf(2));
    check("Compare PROG: the same slot for both files", itemsOf(1).comparePrograms === "3-5" && itemsOf(2).comparePrograms === "3-5");

    const combisOf = (id) => {
      const byKey = {};
      for (const g of getCrossDatasetFilterCategories(id, "combis")) for (const it of g.items) byKey[it.key] = sorted(it.slots);
      return byKey;
    };
    check("Compare COMBI: grouped, three categories",
      getCrossDatasetFilterCategories(1, "combis").map((g) => g.group + ":" + g.items.map((it) => it.label).join("+")).join() ===
        "Compare COMBI:All diverged+Edited+Different songs");
    check("Compare COMBI: All diverged leaves out Init-Combi-on-both-sides slots",
      combisOf(1)["compareCombis:diverged"] === "7-1,7-100,7-101", combisOf(1));
    check("Compare COMBI: Different songs = different name AND 4+ changes",
      combisOf(2)["compareCombis:differentSong"] === "7-100", combisOf(2));
    check("Compare COMBI: Edited = the rest (a rename with only 3 changes is still Edited)",
      combisOf(1)["compareCombis:edited"] === "7-1,7-101", combisOf(1));
    check("Compare COMBI: Edited + Different songs = All diverged, no overlap", (() => {
      const items = getCrossDatasetFilterCategories(1, "combis")[0].items;
      const [all, edited, song] = items.map((it) => it.slots);
      return [...edited].every((s) => !song.has(s)) && edited.size + song.size === all.size;
    })());
    check("Compare COMBI gives the Programs list nothing", !progs.some((g) => g.group === "Compare COMBI"));
    check("Program modes give the Combis list nothing", getCrossDatasetFilterCategories(1, "combis").length === 1);
    check("shared rules: isInitCombiRow / isDifferentSong",
      isInitCombiRow({ nameA: "Init Combi", nameB: "INIT COMBI" }) && !isInitCombiRow({ nameA: "Init Combi", nameB: "Song" }) &&
      isDifferentSong({ nameA: "A", nameB: "B", changes: [1, 2, 3, 4] }) && !isDifferentSong({ nameA: "A", nameB: "A", changes: [1, 2, 3, 4, 5] }));
  }

  // --- one Find replaces everything; clearing ----------------------------------
  store({ idA: 1, idB: 3, editCountA: 6, editCountB: 1 }, { differences: { rows } });
  check("a new Find replaces the old pair completely",
    getCrossDatasetFilterCategories(2, "programs") === null && getCrossDatasetFilterCategories(3, "programs") !== null &&
    getCrossDatasetResult("duplicates").groups.length === 0);
  clearCrossDatasetResults();
  check("clear drops every mode", MODE_NAMES.every((m) => getCrossDatasetResult(m) === null) && getCrossDatasetPair() === null);
  const before = notifications;
  clearCrossDatasetResults();
  check("clearing an empty store doesn't notify", notifications === before);

  // --- revalidation ----------------------------------------------------------
  const P13 = { idA: 1, idB: 3, editCountA: 6, editCountB: 1 };
  const list = (countA, countB) => [
    { datasetId: 1, editCount: countA },
    { datasetId: 3, editCount: countB },
  ];
  store(P13, { differences: { rows } });
  await revalidateCrossDatasetResults(list(6, 1));
  check("revalidate, nothing edited: kept", !!getCrossDatasetResult("differences"));
  await revalidateCrossDatasetResults(list(6, 2));
  check("revalidate, B edited: every mode dropped",
    MODE_NAMES.every((m) => getCrossDatasetResult(m) === null) && getCrossDatasetFilterCategories(1, "programs") === null);
  store(P13);
  await revalidateCrossDatasetResults([{ datasetId: 1, editCount: 6 }]);
  check("revalidate, B closed: dropped", !getCrossDatasetResult("differences"));
  store(P13);
  window.listDatasets = async () => list(9, 1);
  await revalidateCrossDatasetResults();
  check("revalidate without a list fetches one (A edited: dropped)", !getCrossDatasetResult("differences"));
  const n = notifications;
  await revalidateCrossDatasetResults();
  check("revalidate on an empty store: no notification, no fetch needed", notifications === n);

  console.log(`${passed} passed, ${failed} failed`);
  if (failed > 0) throw new Error(`${failed} check(s) failed`);
})().catch((err) => {
  console.log(String(err && err.stack ? err.stack : err));
  throw err;
});
