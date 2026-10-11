// Headless, node-runnable checks for setlist-print.js's pure functions --
// the marker parsing, the print model and the row packing. The in-browser
// part (measuring real row heights) is covered by setlist-print.test.html.
// Run directly: `node setlist-print.test.js` (requires ../package.json's
// `"type": "module"` to load as ESM).
//
// Exits non-zero on any failed assertion, for CI/ctest-style usage.

import { SET_BREAK_NAME, gigMarkerName, parseGigDate, isSetBreakName } from "./setlist-markers.js";
import { buildPrintModel, packRows, buildPrintDocument } from "./setlist-print.js";
import { NAME_MAX_LENGTH } from "./setlist-editor-name.js";
import { EMERGENCY_EXIT_SETLIST, demoGigArrangement } from "./setlist-print-fixture.js";

let failures = 0;

function check(label, actual, expected) {
  const pass = JSON.stringify(actual) === JSON.stringify(expected);
  if (!pass) {
    failures++;
    console.error(`FAIL -- ${label} (got ${JSON.stringify(actual)}, expected ${JSON.stringify(expected)})`);
  } else {
    console.log(`PASS -- ${label}`);
  }
}

// Same rule as pane.js's looksLikeEmptySetlistName(), which the app passes in.
const isEmpty = (name) => !name || /init setlist/i.test(name);

// Markers
check("GIG marker name as written by the row menu", gigMarkerName("2025-10-28"), "---- GIG 2025-10-28 ----");
check("GIG marker round-trips to its date", parseGigDate(gigMarkerName("2025-10-28")), "2025-10-28");
check("the GIG marker exactly fills the slot name field", gigMarkerName("2025-10-28").length, NAME_MAX_LENGTH);
check("the SET BREAK marker fits the slot name field", SET_BREAK_NAME.length <= NAME_MAX_LENGTH, true);
check("SET BREAK marker as written by the row menu is recognized", isSetBreakName(SET_BREAK_NAME), true);
check("the markers don't look like empty slots (drag-and-drop won't overwrite them)", [isEmpty(SET_BREAK_NAME), isEmpty(gigMarkerName("2025-10-28"))], [false, false]);
check("GIG date from a Set List name", parseGigDate("GIG 2025-10-28 Wiener Hof"), "2025-10-28");
check("no GIG date in an ordinary name", parseGigDate("Gigantic"), null);
check("SET BREAK marker recognized", isSetBreakName("---- SET BREAK ----"), true);
check("SET BREAK without dashes recognized", isSetBreakName("set break"), true);
check("a song mentioning a break is not a marker", isSetBreakName("Break It Up"), false);

// Model, real setlist as stored: no markers, so one set, no date
const plain = buildPrintModel(EMERGENCY_EXIT_SETLIST, { isEmpty });
check("all 67 used real slots become song rows", plain.rows.length, 67);
check("without markers everything is set 1", [...new Set(plain.rows.map((r) => r.set))], [1]);
check("without a GIG marker there's no date", plain.gigDate, null);
check("first row keeps its real slot number and name", [plain.rows[0].slot, plain.rows[0].name], [0, "Rolling in the Deep"]);
check("real slot gaps (60..63 unused) keep real slot numbers", plain.rows.find((r) => r.name === "M.Miller 80ies MEDLEY").slot, 64);

// Model, demo gig arrangement with markers
const gig = buildPrintModel(demoGigArrangement(EMERGENCY_EXIT_SETLIST), { isEmpty });
check("GIG marker slot sets the date", gig.gigDate, "2025-10-28");
check("GIG marker slot is not printed as a row", gig.rows.some((r) => /GIG/.test(r.name || "")), false);
check("67 songs + 2 set breaks", [gig.rows.filter((r) => r.type === "song").length, gig.rows.filter((r) => r.type === "break").length], [67, 2]);
check("songs after the first break are set 2", gig.rows.find((r) => r.type === "song" && r.set === 2).name, EMERGENCY_EXIT_SETLIST.slots[24].name);
check("a break row carries the set it ends", gig.rows.filter((r) => r.type === "break").map((r) => r.set), [1, 2]);
check(
  "blank and '- Init Setlist -' slots are skipped",
  buildPrintModel({ name: "x", slots: [{ index: 0, name: "" }, { index: 1, name: "- Init Setlist -" }, { index: 2, name: "Song", comment: "c" }] }, { isEmpty }).rows.length,
  1
);

// Packing
check("rows fill a page until the next one doesn't fit", packRows([40, 40, 40, 40], 100), [[0, 1], [2, 3]]);
check("a row exactly filling the rest still fits", packRows([60, 40, 10], 100), [[0, 1], [2]]);
check("an oversized row gets its own page, never dropped", packRows([30, 250, 30], 100), [[0], [1], [2]]);
check("no rows, no pages", packRows([], 100), []);
check("a forced break ends the page even with room left", packRows([10, 10, 10, 10], 100, [false, true]), [[0, 1], [2, 3]]);
check("a forced break on the last row adds no empty page", packRows([10, 10], 100, [false, true]), [[0, 1]]);

// Document
const html = buildPrintDocument({ title: "a </script><b>", gigDate: null, rows: [{ type: "song", slot: 0, name: "</script>", comment: "<!--", set: 1 }] });
check("model JSON can't close its <script> early", html.split("</script>").length - 1, 2);
check("title text is escaped", html.includes("<title>a &lt;/script&gt;&lt;b&gt;</title>"), true);
check("packRows is embedded for the in-browser layout", html.includes("function packRows("), true);

if (failures > 0) {
  console.error(`\n${failures} check(s) FAILED`);
  process.exit(1);
} else {
  console.log("\nAll checks passed");
}
