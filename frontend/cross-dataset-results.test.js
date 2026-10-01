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

  // --- empty store -----------------------------------------------------------
  check("empty store: no categories", getCrossDatasetFilterCategories(1, "programs") === null);
  check("empty store: no result", getCrossDatasetResult("differences") === null);

  // --- per-dataset categories ------------------------------------------------
  setCrossDatasetResult("differences", { idA: 1, idB: 2, editCountA: 5, editCountB: 7 }, { rows });
  check("set notifies", notifications === 1, notifications);
  check("sidebar view keeps A/B + payload", (() => {
    const r = getCrossDatasetResult("differences");
    return r && r.idA === 1 && r.idB === 2 && r.rows === rows;
  })());
  const one = itemsOf(1);
  const two = itemsOf(2);
  check("dataset A: Only in this file = onlyInA, own slots", one["differences:onlyHere"] === "1-91,4-0", one);
  check("dataset A: renamed/modified/moved use the A side", one["differences:renamed"] === "2-50" &&
    one["differences:modifiedTwin"] === "0-1" && one["differences:moved"] === "0-0,0-2", one);
  check("dataset B: Only in this file = onlyInB, own slots", two["differences:onlyHere"] === "6-0", two);
  check("dataset B: renamed/modified/moved use the B side", two["differences:renamed"] === "0-50" &&
    two["differences:modifiedTwin"] === "13-1" && two["differences:moved"] === "13-0,13-2", two);
  check("a dataset outside the pair gets nothing", getCrossDatasetFilterCategories(3, "programs") === null);
  check("Differences gives the Combis list nothing (Programs only)",
    JSON.stringify(getCrossDatasetFilterCategories(1, "combis")) === "[]");
  check("every category is listed, even an empty one", (() => {
    setCrossDatasetResult("differences", { idA: 1, idB: 2, editCountA: 5, editCountB: 7 }, { rows: [] });
    const items = getCrossDatasetFilterCategories(1, "programs")[0].items;
    return items.length === 4 && items.every((it) => it.slots.size === 0);
  })());
  setCrossDatasetResult("differences", { idA: 1, idB: 2, editCountA: 5, editCountB: 7 }, { rows });

  // --- A/B order doesn't matter to a pane -------------------------------------
  setCrossDatasetResult("differences", { idA: 2, idB: 1, editCountA: 7, editCountB: 5 }, { rows: rows.map(swap) });
  check("swapped pair: dataset 1 sees the same filter data", JSON.stringify(itemsOf(1)) === JSON.stringify(one), itemsOf(1));
  check("swapped pair: dataset 2 sees the same filter data", JSON.stringify(itemsOf(2)) === JSON.stringify(two), itemsOf(2));
  setCrossDatasetResult("differences", { idA: 1, idB: 2, editCountA: 5, editCountB: 7 }, { rows });

  // --- Duplicates / Compare PROG / Compare COMBI -----------------------------
  {
    const pair = { idA: 1, idB: 2, editCountA: 5, editCountB: 7 };
    const member = (datasetId, bank, number) => ({ datasetId, bank, number, name: "", filename: "", bankType: 0 });
    setCrossDatasetResult("duplicates", pair, {
      groups: [
        { members: [member(1, 0, 0), member(2, 13, 0)] },
        { members: [member(1, 0, 2), member(1, 3, 3), member(2, 13, 2)] },  // two copies in file 1
      ],
    });
    setCrossDatasetResult("comparePrograms", pair, { programs: [{ bank: 3, number: 5, nameA: "", nameB: "", bankType: 0 }] });
    const combi = (bank, number, nameA, nameB, changeCount) =>
      ({ bank, number, nameA, nameB, changes: Array.from({ length: changeCount }, (_, i) => "c" + i) });
    setCrossDatasetResult("compareCombis", pair, {
      combis: [
        combi(7, 1, "Separate Ways", "Separate Ways", 2),
        combi(7, 100, "Tainted Love", "Your Song", 5),   // different song
        combi(7, 101, "Tainted Love", "Your Song 2", 3), // renamed but only 3 changes: still the same song
        combi(2, 3, "Init Combi", "INIT COMBI", 1),       // template slot on both sides: left out
      ],
    });

    const one = getCrossDatasetFilterCategories(1, "programs");
    check("Programs: modes in the sidebar's order, one-category modes ungrouped",
      one.map((g) => (g.group || "-") + ":" + g.items.map((it) => it.key).join("+")).join(" ") ===
        "-:duplicates Differences:differences:onlyHere+differences:renamed+differences:modifiedTwin+differences:moved -:comparePrograms",
      one.map((g) => [g.group, g.items.map((it) => it.key)]));
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
    check("Compare COMBI gives the Programs list nothing", !one.some((g) => g.group === "Compare COMBI"));
    check("shared rules: isInitCombiRow / isDifferentSong",
      isInitCombiRow({ nameA: "Init Combi", nameB: "INIT COMBI" }) && !isInitCombiRow({ nameA: "Init Combi", nameB: "Song" }) &&
      isDifferentSong({ nameA: "A", nameB: "B", changes: [1, 2, 3, 4] }) && !isDifferentSong({ nameA: "A", nameB: "A", changes: [1, 2, 3, 4, 5] }));
    clearCrossDatasetResults("duplicates");
    clearCrossDatasetResults("comparePrograms");
    clearCrossDatasetResults("compareCombis");
  }

  // --- one pair at a time ----------------------------------------------------
  setCrossDatasetResult("comparePrograms", { idA: 1, idB: 2, editCountA: 5, editCountB: 7 }, { programs: [] });
  check("same pair + same edit counts: other modes are kept", !!getCrossDatasetResult("differences"));
  setCrossDatasetResult("comparePrograms", { idA: 1, idB: 2, editCountA: 6, editCountB: 7 }, { programs: [] });
  check("same pair, edited since: other modes are dropped",
    !getCrossDatasetResult("differences") && !!getCrossDatasetResult("comparePrograms"));
  setCrossDatasetResult("differences", { idA: 1, idB: 3, editCountA: 6, editCountB: 1 }, { rows });
  check("another pair replaces everything",
    !getCrossDatasetResult("comparePrograms") && getCrossDatasetFilterCategories(2, "programs") === null &&
    getCrossDatasetFilterCategories(3, "programs") !== null);

  // --- clearing --------------------------------------------------------------
  setCrossDatasetResult("comparePrograms", { idA: 1, idB: 3, editCountA: 6, editCountB: 1 }, { programs: [] });
  clearCrossDatasetResults("comparePrograms");
  check("clear(mode) drops only that mode", !getCrossDatasetResult("comparePrograms") && !!getCrossDatasetResult("differences"));
  const before = notifications;
  clearCrossDatasetResults("comparePrograms");
  check("clearing something already gone doesn't notify", notifications === before);

  // --- revalidation ----------------------------------------------------------
  const list = (countA, countB) => [
    { datasetId: 1, editCount: countA },
    { datasetId: 3, editCount: countB },
  ];
  await revalidateCrossDatasetResults(list(6, 1));
  check("revalidate, nothing edited: kept", !!getCrossDatasetResult("differences"));
  await revalidateCrossDatasetResults(list(6, 2));
  check("revalidate, B edited: dropped", !getCrossDatasetResult("differences") && getCrossDatasetFilterCategories(1, "programs") === null);
  setCrossDatasetResult("differences", { idA: 1, idB: 3, editCountA: 6, editCountB: 1 }, { rows });
  await revalidateCrossDatasetResults([{ datasetId: 1, editCount: 6 }]);
  check("revalidate, B closed: dropped", !getCrossDatasetResult("differences"));
  setCrossDatasetResult("differences", { idA: 1, idB: 3, editCountA: 6, editCountB: 1 }, { rows });
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
