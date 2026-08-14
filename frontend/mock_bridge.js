// Fallback bridge for plain-browser mode (open index.html directly, no
// native app build). Fabricates fake data so the "Open..." button, the Set
// List picker, filter/search, and drag-and-drop swap/copy can all be
// exercised with real devtools -- but it CANNOT open real files: a plain
// browser page has no filesystem access at all (that's a web-platform
// restriction, not a Choc one -- Choc's native bridge is what gives the
// real app actual access to disk), so openFileDialog() below fakes a
// picker via window.prompt() instead of a real one.
//
// When running inside the native app, choc's addInitScript() injects the
// real window.openFileDialog/listDatasets/listSetlists/getEntries/
// copyEntry/reorderSongEntry *before* this script runs, so this file becomes
// a no-op there.
(function () {
  if (window.openFileDialog) return;

  console.warn(
    "[mock_bridge] No native bridge detected -- running in plain-browser mode with fabricated " +
      "Set List data. Dropped files are NOT actually parsed (browsers can't read arbitrary binary " +
      "formats meaningfully here anyway); see README.md."
  );

  const mockSongsByList = [
    ["Rolling in the Deep", "Sex on Fire", "Keep the Faith", "Separate Ways", "Call Me", "Weak"],
    ["Burning down the House", "Aint no Love in the City", "Smooth", "Fragile", "Gravity"],
    ["Marillion", "Marillion", "Marillion"],
    ["Pink Floyd"],
  ];

  // Real Comment text the project owner confirmed renders "nearly identical"
  // to an actual Kronos (pane.js's Font-size-driven wrap preview, see
  // STATE.md) -- planted on Setlist 000 / Song 000 below so the preview can
  // be exercised in mock/browser mode too, without the real native app.
  // Exact whitespace/line breaks preserved as given, not reformatted.
  const MOCK_WRAP_TEST_COMMENT =
    "Intro (1): [e-#f-h]  oben bolero h, #c, e  rechts Tonleiter\n" +
    "                 single note: a__   Ende: d__\n" +
    "Str (2): D   A/#c   bm7   bm7 | G   D/#f   em7\n" +
    "             #fm   G   D   A\n" +
    "Ref (3): D   A/#c   em7 | em7   G   A |:2\n" +
    "              D   A  em\n" +
    "Str (2): wie Str1 | 1/2 Str1 | A D A\n" +
    "Bridge (4): C+c, G+h, #A, F+a | C G dm A\n" +
    "8 * Ref (bm / D im Wechsel)";

  const datasets = {};  // datasetId -> { displayName, setlists: [{index, name}], songs: {setlistIndex: [{index, label}]}, programs, combis }
  let nextDatasetId = 1;

  const ok = (extra) => Promise.resolve(Object.assign({ ok: true }, extra));
  const fail = (error) => Promise.resolve({ ok: false, error });

  // Mirrors PcgFile.cpp's looksLikeEmptyProgramName() -- a real untouched
  // Program slot is named Korg's own factory "Init Program"/"Init EXi
  // Program", not a blank string (docs/content/format/index.md §5.5). This
  // mock's own fake datasets (makeFakePrograms() etc.) only ever use blank
  // names for "free", so this mostly just keeps the fake in sync with the
  // real backend's rule rather than fixing an actual observable mock bug.
  function looksLikeEmptyProgramName(name) {
    if (!name) return true;
    const lower = name.toLowerCase();
    return lower === "init exi program" || lower.includes("init program");
  }

  function makeFakeSong(k, label) {
    return {
      index: k,
      label,
      paramsFound: label !== "",
      isProgram: k % 4 !== 0,
      bank: 0,
      number: k,
      color: (k % 16) + 1,  // cycle through all 16 real colors (pane.js's SETLIST_COLOR_NAMES/_HEX) for visual testing
      holdTime: 5,
      volume: 127,
      fontSize: "S",  // baseline/default, matches SlotParams' own default (PcgFile.h)
      comment: "",
      instrumentName: "",
    };
  }

  // Same encoding setlist-comment.js's encodeSetlistComment() uses (bits
  // 6-7 of byte+12, bit 4 of byte+17) -- duplicated by hand here rather
  // than importing that module, matching this file's existing pattern for
  // Color (below) and Volume: a plain script, not built to load ES modules.
  const FONT_SIZE_VALUE = { S: 0, XS: 1, M: 2, L: 3, XL: 4 };
  const FONT_SIZE_BY_VALUE = ["S", "XS", "M", "L", "XL"];

  // Mock mode has no real SBK1 bytes to hand back -- getSongRecordBytes()/
  // putSongRecordBytes() below need SOME 542-byte buffer that the real
  // frontend/components/kronos codecs can decode/encode against, so this
  // synthesizes one from a fake entry's own fields, close enough to the
  // real byte layout (docs/content/format/index.md §4.3, src/kronos/PcgFile.cpp's kSbk*
  // constants) for Color (byte+12 bits2-5)/Volume (byte+16)/Font size
  // (byte+12 bits6-7 + byte+17 bit4)/Comment (byte+18..) to round-trip
  // correctly. Transpose/isProgram's other bits aren't reconstructed (mock
  // entries don't track them), so they'll always decode as their zero/
  // default value here -- fine, no mock UI reads them.
  // Mirrors makeFakeSlotBytes() below, but for the separate SDB1 name
  // record (PcgFile::nameRecordBytes()'s real 28-byte shape: a 4-byte
  // marker + 24-byte ASCII name) -- mock entries only have one `label`
  // field standing in for both a real slot's name AND its params, so this
  // just projects that same label into the name-record shape.
  const NAME_RECORD_SIZE = 28;
  function makeFakeNameBytes(entry) {
    const bytes = new Uint8Array(NAME_RECORD_SIZE);
    const label = entry.label || "";
    for (let i = 0; i < label.length && i < 24; i++) {
      bytes[4 + i] = label.charCodeAt(i) & 0xff;
    }
    return bytes;
  }

  const SBK_RECORD_SIZE = 542;
  function makeFakeSlotBytes(entry) {
    const bytes = new Uint8Array(SBK_RECORD_SIZE);
    const colorField = ((Math.max(1, Math.min(16, entry.color)) - 1) << 2) & 0x3c;
    const fontValue = FONT_SIZE_VALUE[entry.fontSize] ?? FONT_SIZE_VALUE.S;
    const fontLowBits = (fontValue & 2 ? 0x80 : 0) | (fontValue & 1 ? 0x40 : 0);
    bytes[12] = colorField | fontLowBits | (entry.isProgram ? 0x01 : 0x00);
    bytes[13] = entry.bank & 0x1f;
    bytes[14] = entry.number & 0xff;
    bytes[15] = (entry.holdTime + 1) & 0xff;
    bytes[16] = Math.max(0, Math.min(127, entry.volume));
    bytes[17] = fontValue & 4 ? 0x10 : 0x00;
    const comment = entry.comment || "";
    for (let i = 0; i < comment.length && 18 + i < SBK_RECORD_SIZE - 1; i++) {
      bytes[18 + i] = comment.charCodeAt(i) & 0xff;
    }
    return bytes;
  }

  // Mock-only Programs -- deliberately includes a repeated name ("Init
  // Program") on two different banks, standing in for a real byte-exact
  // duplicate (the real bridge hashes full record content; this fake data
  // has no such content to hash, so duplicates here are grouped by name
  // instead, purely for exercising the Duplicates tab's UI).
  function makeFakePrograms() {
    const names = ["Berlin Grand SW2 U.C.", "Rain Again", "Init Program", "Init Program", "Subdivisions"];
    const programs = [];
    for (let bank = 0; bank < 2; bank++) {
      names.forEach((name, number) =>
        // bank 0/1 are both within the confirmed INT-A..D range (see
        // kronos::isConfirmedTimbreProgramBank()), so mock mode marks
        // Combi refs available too, mirroring the real bridge. bankType
        // here is purely cosmetic (alternating HD-1/EXi by bank) -- mock
        // mode has no real bytes to classify, see PcgFile.h's
        // ProgramBankType doc comment for how the real bridge does this.
        programs.push({
          bank,
          number,
          name,
          bankType: bank === 0 ? "HD-1" : "EXi",
          setlistReferenceCount: 0,
          combiReferenceCountAvailable: true,
          combiReferenceCount: 0,
        })
      );
      // One genuinely empty slot per bank (number 5) -- gives copyProgram()
      // below a real target to succeed into, not just reject; every other
      // slot above already has a name, so a same-type drop onto one of
      // those correctly exercises the "target slot occupied" rejection
      // instead.
      programs.push({
        bank,
        number: names.length,
        name: "",
        bankType: bank === 0 ? "HD-1" : "EXi",
        setlistReferenceCount: 0,
        combiReferenceCountAvailable: true,
        combiReferenceCount: 0,
      });
    }
    return programs;
  }

  // Mock-only Timbre references -- three "active" slots followed by 13
  // defaults, standing in for the real bridge's per-Combi Timbre array
  // (see docs/content/format/index.md's "Combi Timbre references" section).
  // bankName is "" for all of these on purpose, matching the real bridge's
  // contract exactly (kronos::timbreBankName()'s own doc comment in
  // PcgFile.cpp, EditorBridge.cpp's combiToValue()): rawBankCode 0/1/20 all
  // have a confirmed PBK1 index, so the real backend leaves bankName blank
  // and expects the frontend to derive the name from
  // PROGRAM_BANK_NAMES[programBank] itself (pane-combi-editor.js's formatTimbreRef())
  // -- hardcoding "INT-B"/"USER-D" here would just be a second copy of that
  // same fact, exactly the kind of drift-prone duplication removed
  // elsewhere (2026-08-11).
  function makeFakeTimbres() {
    const timbres = [
      // number:0/rawBankCode:1 deliberately matches makeFakePrograms()'s own
      // bank1/number0 ("Berlin Grand SW2 U.C.") -- exercises the new name/
      // engine-type lookup in mock mode too, not just the real bridge.
      { number: 0, rawBankCode: 1, bankName: "", status: "Internal", isDefault: false },
      { number: 15, rawBankCode: 20, bankName: "", status: "Internal", isDefault: false },
      // A real reference that's currently switched off -- exercises the
      // "referenced but inactive" display case in mock mode too.
      { number: 90, rawBankCode: 0, bankName: "", status: "Off", isDefault: false },
      // GM (raw code 6, confirmed 2026-08-12) -- permanently indexless, so
      // bankName IS populated here (unlike the entries above) -- exercises
      // the "confirmed by name, no jump-to-Program button, no Program
      // name" display case in mock mode too.
      { number: 91, rawBankCode: 6, bankName: "GM", status: "Internal", isDefault: false },
    ];
    for (let i = timbres.length; i < 16; i++) {
      timbres.push({ number: 0, rawBankCode: 0, bankName: "", status: "Off", isDefault: true });
    }
    return timbres;
  }

  function makeFakeCombis() {
    const names = ["K-Lab: Katja's House", "Stradivarius Goes POP", "Rolling in the Deep"];
    const combis = [];
    for (let bank = 0; bank < 2; bank++) {
      names.forEach((name, number) => {
        // "Rolling in the Deep" gets a fabricated usage so the Set List
        // "badges" column has something real to render in mock mode too.
        const setlistUsages =
          name === "Rolling in the Deep"
            ? [{ setlistIndex: 1, setlistName: "Mock List 1", songIndex: 0 }]
            : [];
        combis.push({
          bank,
          number,
          name,
          setlistReferenceCount: setlistUsages.length,
          setlistUsages,
          timbres: makeFakeTimbres(),
        });
      });
      // One genuinely blank "Init Combi" per bank -- gives moveCombiToBank()
      // below a real same-bank filler to vacate a slot into, mirroring
      // makeFakePrograms()'s own "one empty slot per bank" pattern. Real
      // Init Combi records are abundant in real files (see STATE.md); mock
      // mode only needs one per bank to exercise the happy path.
      combis.push({
        bank,
        number: names.length,
        name: "Init Combi",
        setlistReferenceCount: 0,
        setlistUsages: [],
        timbres: makeFakeTimbres(),
      });
    }
    return combis;
  }

  function makeFakeFile(fileName) {
    const names = ["Mock Set List", ...mockSongsByList.map((_, i) => `Mock List ${i + 1}`)];
    const setlists = names.map((name, index) => ({ index, name: `${name} (${fileName})` }));
    const songs = {};
    setlists.forEach((s, i) => {
      const titles = mockSongsByList[i - 1] || [];
      songs[s.index] = Array.from({ length: 16 }, (_, k) => makeFakeSong(k, titles[k] || ""));
    });

    // Setlist 000's songs are otherwise all blank (index 0 has no entry in
    // mockSongsByList, deliberately -- see the `titles[i - 1]` offset
    // above), so Song 000 needs a real label of its own here to actually
    // have paramsFound=true and be clickable at all.
    if (songs[0] && songs[0][0]) {
      songs[0][0] = makeFakeSong(0, "Wrap Test");
      songs[0][0].comment = MOCK_WRAP_TEST_COMMENT;
    }

    return { displayName: fileName, setlists, songs, programs: makeFakePrograms(), combis: makeFakeCombis() };
  }

  // A plain browser tab can't show a real native file picker (or read an
  // arbitrary local path at all) -- window.prompt() stands in for it here
  // just so the "Open..." button has SOMETHING to do in mock mode.
  window.openFileDialog = () => {
    const displayName = window.prompt("Mock mode has no real file picker -- type a fake file name:", "mock-file.pcg");
    if (!displayName) return Promise.resolve({ ok: true, cancelled: true });
    // Mirrors the real bridge's dedup (EditorBridge::openFileAtPath): typing
    // the same fake name twice reuses the existing mock dataset instead of
    // creating a duplicate, so mock mode exercises the same "already open"
    // behavior the native dialog does.
    const existingId = Object.keys(datasets).find((id) => datasets[id].displayName === displayName);
    if (existingId) {
      const d = datasets[existingId];
      return ok({ datasetId: Number(existingId), displayName: d.displayName, setlistCount: d.setlists.length, alreadyOpen: true });
    }
    const datasetId = nextDatasetId++;
    datasets[datasetId] = makeFakeFile(displayName);
    return ok({ datasetId, displayName: datasets[datasetId].displayName, setlistCount: datasets[datasetId].setlists.length });
  };

  // Mirrors openFileDialog() above -- mock mode can't write a real file
  // either, so window.prompt() stands in for the native Save dialog just so
  // the "Save As..." button has something to do.
  window.saveFileDialog = (datasetId) => {
    const dataset = datasets[datasetId];
    if (!dataset) return fail(`Dataset ${datasetId} has no file loaded`);
    const path = window.prompt("Mock mode can't write a real file -- type a fake save path:", dataset.displayName);
    if (!path) return Promise.resolve({ ok: true, cancelled: true });
    return ok({ path });
  };

  window.listDatasets = () =>
    Promise.resolve(
      Object.entries(datasets).map(([datasetId, d]) => ({
        datasetId: Number(datasetId),
        displayName: d.displayName,
        setlistCount: d.setlists.length,
      }))
    );

  window.closeDataset = (datasetId) => {
    delete datasets[datasetId];
    return ok();
  };

  window.listSetlists = (datasetId) => Promise.resolve(datasets[datasetId] ? datasets[datasetId].setlists : []);

  window.getEntries = (datasetId, setlistIndex) => {
    const dataset = datasets[datasetId];
    return Promise.resolve(dataset && dataset.songs[setlistIndex] ? dataset.songs[setlistIndex] : []);
  };

  // Mirrors PcgFile::reorderSong()'s semantics: `.index` is a slot's
  // POSITION in the Set List (always 0..N-1 by array order for mock data),
  // not content tied to a particular song -- moving an entry means
  // splicing its content to a new position and letting every slot between
  // the old and new position shift by one, exactly what Array.splice()
  // does here. Backs the Setlist drag-and-drop "insert before/after"
  // gesture (pane.js's dropZoneForEvent()/app.js's onDropEntry()).
  window.reorderSongEntry = (datasetId, setlistIndex, fromIndex, toIndex) => {
    const list = datasets[datasetId] && datasets[datasetId].songs[setlistIndex];
    if (!list) return fail(`Dataset ${datasetId} has no such Set List loaded`);
    const fromIdx = list.findIndex((e) => e.index === fromIndex);
    const toIdx = list.findIndex((e) => e.index === toIndex);
    if (fromIdx < 0 || toIdx < 0) return fail("Entry index out of range");
    if (fromIdx === toIdx) return ok();
    const [moved] = list.splice(fromIdx, 1);
    list.splice(toIdx, 0, moved);
    list.forEach((e, i) => { e.index = i; });
    return ok();
  };

  window.copyEntry = (srcDatasetId, srcSetlistIndex, srcIndex, dstDatasetId, dstSetlistIndex, dstIndex) => {
    const srcList = datasets[srcDatasetId] && datasets[srcDatasetId].songs[srcSetlistIndex];
    const dstList = datasets[dstDatasetId] && datasets[dstDatasetId].songs[dstSetlistIndex];
    if (!srcList || !dstList) return fail("Source or destination Set List not loaded");
    const srcIdx = srcList.findIndex((e) => e.index === srcIndex);
    const dstIdx = dstList.findIndex((e) => e.index === dstIndex);
    if (srcIdx < 0 || dstIdx < 0) return fail("Entry index out of range");
    const dstOriginalIndex = dstList[dstIdx].index;
    dstList[dstIdx] = Object.assign({}, srcList[srcIdx], { index: dstOriginalIndex });
    return ok();
  };

  // "Copy all to opposite" (pane.js's setlist-info row) -- same dataset
  // only (the button itself is only enabled when both panes already show
  // it, see updateCopyButtonState()), overwrites every slot of
  // dstSetlistIndex with srcSetlistIndex's content, keeping each dst slot's
  // own `.index` (position) -- mirrors PcgFile::copySetlist() only moving
  // content, never the destination Set List's own slot count/positions.
  window.copySetlistEntries = (datasetId, srcSetlistIndex, dstSetlistIndex) => {
    const srcList = datasets[datasetId] && datasets[datasetId].songs[srcSetlistIndex];
    const dstList = datasets[datasetId] && datasets[datasetId].songs[dstSetlistIndex];
    if (!srcList || !dstList) return fail(`Dataset ${datasetId} has no such Set List loaded`);
    if (srcSetlistIndex === dstSetlistIndex) return ok();
    for (let i = 0; i < dstList.length; i++) {
      if (srcList[i]) dstList[i] = Object.assign({}, srcList[i], { index: dstList[i].index });
    }
    return ok();
  };

  // A-Z/Z-A (pane.js's setlistRow sort buttons) -- a REAL, immediate
  // reorder now, not a display-only convenience (see PcgFile::
  // sortSetlist()'s own doc comment for why). Mirrors its empty-slots-
  // trail-last convention and reindexes `.index` 0..N-1 by new position,
  // same pattern as reorderSongEntry()'s mock above.
  window.sortSetlistEntries = (datasetId, setlistIndex, ascending) => {
    const list = datasets[datasetId] && datasets[datasetId].songs[setlistIndex];
    if (!list) return fail(`Dataset ${datasetId} has no such Set List loaded`);
    list.sort((a, b) => {
      if (!a.label !== !b.label) return a.label ? -1 : 1;
      if (!a.label) return 0;
      return ascending ? a.label.localeCompare(b.label) : b.label.localeCompare(a.label);
    });
    list.forEach((e, i) => { e.index = i; });
    return ok();
  };

  window.setComment = (datasetId, setlistIndex, songIndex, newComment) => {
    const list = datasets[datasetId] && datasets[datasetId].songs[setlistIndex];
    if (!list) return fail(`Dataset ${datasetId} has no such Set List loaded`);
    const entry = list.find((e) => e.index === songIndex);
    if (!entry) return fail("Entry index out of range");
    entry.comment = newComment;
    return ok();
  };

  // The Setlist Color/Volume/Comment row editors (frontend/pane.js) read/
  // write through these two instead of setComment() above -- see
  // makeFakeSlotBytes()'s own comment for how a mock 542-byte record is
  // synthesized.
  window.getSongRecordBytes = (datasetId, setlistIndex, songIndex) => {
    const list = datasets[datasetId] && datasets[datasetId].songs[setlistIndex];
    if (!list) return fail(`Dataset ${datasetId} has no file loaded`);
    const entry = list.find((e) => e.index === songIndex);
    if (!entry) return fail("No SBK1 record for that Set List slot");
    return ok({ bytes: Array.from(makeFakeSlotBytes(entry)) });
  };

  window.putSongRecordBytes = (datasetId, setlistIndex, songIndex, bytes) => {
    const list = datasets[datasetId] && datasets[datasetId].songs[setlistIndex];
    if (!list) return fail(`Dataset ${datasetId} has no file loaded`);
    const entry = list.find((e) => e.index === songIndex);
    if (!entry || !Array.isArray(bytes) || bytes.length !== SBK_RECORD_SIZE) {
      return fail("Couldn't write that Set List slot's record (wrong size, or index out of range)");
    }
    // Re-derive the mock entry's own fields from the written bytes, same
    // discipline as the real PcgFile::putSongRecordBytes() -- a cached
    // field must never go stale after a direct "raw bytes" write, even in
    // mock mode.
    entry.color = ((bytes[12] & 0x3c) >> 2) + 1;
    entry.volume = bytes[16];
    const fontValue = (bytes[17] & 0x10 ? 4 : 0) | (bytes[12] & 0x80 ? 2 : 0) | (bytes[12] & 0x40 ? 1 : 0);
    entry.fontSize = FONT_SIZE_BY_VALUE[fontValue] || "S";
    let end = 18;
    while (end < bytes.length && bytes[end] !== 0) end++;
    entry.comment = bytes.slice(18, end).map((b) => String.fromCharCode(b)).join("");
    return ok();
  };

  // The Setlist drag-and-drop "copy over" gesture (app.js's onDropEntry())
  // reads/writes a slot's name through these two, separately from its
  // params above -- see makeFakeNameBytes()'s own comment for why mock
  // mode needs a second synthesized buffer rather than reusing
  // makeFakeSlotBytes().
  window.getNameRecordBytes = (datasetId, setlistIndex, songIndex) => {
    const list = datasets[datasetId] && datasets[datasetId].songs[setlistIndex];
    if (!list) return fail(`Dataset ${datasetId} has no file loaded`);
    const entry = list.find((e) => e.index === songIndex);
    if (!entry) return fail("No SDB1 name record for that Set List slot");
    return ok({ bytes: Array.from(makeFakeNameBytes(entry)) });
  };

  window.putNameRecordBytes = (datasetId, setlistIndex, songIndex, bytes) => {
    const list = datasets[datasetId] && datasets[datasetId].songs[setlistIndex];
    if (!list) return fail(`Dataset ${datasetId} has no file loaded`);
    const entry = list.find((e) => e.index === songIndex);
    if (!entry || !Array.isArray(bytes) || bytes.length !== NAME_RECORD_SIZE) {
      return fail("Couldn't write that Set List slot's name record (wrong size, or index out of range)");
    }
    let end = 4;
    while (end < bytes.length && bytes[end] !== 0) end++;
    entry.label = bytes.slice(4, end).map((b) => String.fromCharCode(b)).join("");
    return ok();
  };

  window.listPrograms = (datasetId) => Promise.resolve(datasets[datasetId] ? datasets[datasetId].programs : []);

  window.listCombis = (datasetId) => Promise.resolve(datasets[datasetId] ? datasets[datasetId].combis : []);

  // Mirrors makeFakePrograms()'s own bank 0 = HD-1 / bank 1 = EXi convention,
  // independent of which programs actually exist in a bank -- same as the
  // real bridge's getProgramBankTypes(), which is a per-bank listing, not
  // derived from any specific Program row.
  window.getProgramBankTypes = (datasetId) =>
    Promise.resolve(datasets[datasetId] ? [{ bank: 0, bankType: "HD-1" }, { bank: 1, bankType: "EXi" }] : []);

  // Mirrors makeFakePrograms()/makeFakeCombis()' own record counts (6 per
  // Program bank -- 5 named + 1 empty filler slot; 3 per Combi bank, no
  // filler) -- for exercising internals.js in mock mode. Only 2 of the 20
  // expected Program banks and 2 of 14 expected Combi banks "exist" here,
  // deliberately, so the pane's own "N of 20/14 found" shortfall messaging
  // has something real to show in mock mode too, not just when a real
  // backup is actually missing banks. Same reasoning for topLevelChunks --
  // a plausible but incomplete subset (no DKT1/WSQ1/GLB1/DPI1), consistent
  // with what this project has never actually parsed for any real file.
  window.getDatasetInternals = (datasetId) => {
    if (!datasets[datasetId]) return fail(`Dataset ${datasetId} has no file loaded`);
    return ok({
      topLevelChunks: ["DIV1", "SLS1", "PRG1", "CMB1"],
      programBanks: [
        { index: 0, bankType: "HD-1", numRecords: 6, bytesPerRecord: 4960 },
        // Real data (two independent backup files, checked 2026-08-13):
        // EXi banks are ALSO 4960 bytes, not the 3706 this mock used to
        // show -- see PcgFile.h's programRecordBytes()/copyProgramFrom()
        // doc comments and STATE.md entry 31 for the correction.
        { index: 1, bankType: "EXi", numRecords: 6, bytesPerRecord: 4960 },
      ],
      combiBanks: [
        { index: 0, numRecords: 3, bytesPerRecord: 4048 },
        { index: 1, numRecords: 3, bytesPerRecord: 4048 },
      ],
    });
  };

  window.getProgramUsage = (datasetId, bank, number) => {
    const dataset = datasets[datasetId];
    if (!dataset) return fail(`Dataset ${datasetId} has no file loaded`);

    const setlistUsages = [];
    for (const setlist of dataset.setlists) {
      const list = dataset.songs[setlist.index] || [];
      for (const song of list) {
        if (song.isProgram && song.bank === bank && song.number === number) {
          setlistUsages.push({ setlistIndex: setlist.index, setlistName: setlist.name, songIndex: song.index });
        }
      }
    }
    const combiUsagesAvailable = bank >= 0 && bank <= 3;
    const combiUsages = combiUsagesAvailable
      ? dataset.combis
          .filter((c) => c.timbres.some((t) => !t.isDefault && t.rawBankCode === bank && t.number === number))
          .map((c) => ({
            bank: c.bank,
            number: c.number,
            name: c.name,
            active: c.timbres.some(
              (t) => !t.isDefault && t.rawBankCode === bank && t.number === number && t.status !== "Off"
            ),
          }))
      : [];
    return ok({ setlistUsages, combiUsagesAvailable, combiUsages });
  };

  // Mirrors EditorBridge::copyProgram()'s three rejection guards (see
  // PcgFile::copyProgramFrom()'s doc comment for the real thing) using
  // `name` as mock mode's stand-in for real byte content, same convention
  // findDuplicatePrograms() above already uses -- mock data has no actual
  // bytes to hash.
  window.copyProgram = (srcDatasetId, srcBank, srcNumber, dstDatasetId, dstBank, dstNumber) => {
    const srcDataset = datasets[srcDatasetId];
    const dstDataset = datasets[dstDatasetId];
    if (!srcDataset) return fail("Source dataset is no longer open.");
    if (!dstDataset) return fail("Destination dataset is no longer open.");

    const srcProgram = srcDataset.programs.find((p) => p.bank === srcBank && p.number === srcNumber);
    if (!srcProgram) return fail("Can't copy: source or destination bank/number is out of range.");

    const dstBankType = dstBank === 0 ? "HD-1" : "EXi";  // same convention as getProgramBankTypes() above
    if (srcProgram.bankType !== dstBankType) {
      return fail(
        "Can't copy: source and destination banks are different engine types (HD-1/EXi) -- " +
          "a Program can only be loaded into a bank of the matching type."
      );
    }

    const existingAtTarget = dstDataset.programs.find((p) => p.bank === dstBank && p.number === dstNumber);
    if (existingAtTarget && !looksLikeEmptyProgramName(existingAtTarget.name)) {
      return fail("Can't copy: the destination slot already holds a different Program.");
    }

    const duplicate = dstDataset.programs.find(
      (p) => p.name && p.name === srcProgram.name && !(srcDatasetId === dstDatasetId && p.bank === srcBank && p.number === srcNumber)
    );
    if (duplicate) {
      return fail("Can't copy: a byte-identical Program already exists in the destination dataset.");
    }

    if (existingAtTarget) {
      existingAtTarget.name = srcProgram.name;
      existingAtTarget.bankType = dstBankType;
    } else {
      dstDataset.programs.push({
        bank: dstBank,
        number: dstNumber,
        name: srcProgram.name,
        bankType: dstBankType,
        setlistReferenceCount: 0,
        combiReferenceCountAvailable: true,
        combiReferenceCount: 0,
      });
    }
    return ok();
  };

  // Mirrors PcgFile::resolveDuplicates() -- see its own doc comment in
  // PcgFile.h for the real behavior this reproduces in mock terms. Same
  // mock simplifications as elsewhere in this file: `name` stands in for
  // real byte-identical content (findDuplicatePrograms() below/
  // copyProgram() above), and a Combi Timbre's rawBankCode is treated as
  // directly equal to a Program's own `bank` (getProgramUsage()'s
  // combiUsages computation above already makes this same simplification --
  // mock mode has no real kConfirmedTimbreBanks translation table to
  // replicate, so combiRefsSkipped is always 0 here).
  window.resolveDuplicateProgram = (datasetId, bank, number) => {
    const dataset = datasets[datasetId];
    if (!dataset) return fail(`Dataset ${datasetId} has no file loaded`);

    const keep = dataset.programs.find((p) => p.bank === bank && p.number === number);
    if (!keep) return fail("No such Program to keep");

    const duplicates = dataset.programs.filter(
      (p) => p.name && p.name === keep.name && !(p.bank === bank && p.number === number)
    );

    let clearedPrograms = 0;
    let setlistRefsRepointed = 0;
    let combiRefsRepointed = 0;

    for (const dup of duplicates) {
      dup.name = dup.bankType === "HD-1" ? "Init Program" : "Init EXi Program";
      clearedPrograms++;

      for (const setlistIndex of Object.keys(dataset.songs)) {
        for (const song of dataset.songs[setlistIndex]) {
          if (!song.isProgram || song.bank !== dup.bank || song.number !== dup.number) continue;
          song.bank = bank;
          song.number = number;
          setlistRefsRepointed++;
        }
      }

      for (const combi of dataset.combis) {
        for (const t of combi.timbres) {
          if (t.isDefault || t.rawBankCode !== dup.bank || t.number !== dup.number) continue;
          t.rawBankCode = bank;
          t.number = number;
          combiRefsRepointed++;
        }
      }
    }

    return ok({ clearedPrograms, setlistRefsRepointed, combiRefsRepointed, combiRefsSkipped: 0 });
  };

  // Mirrors PcgFile::swapCombis() -- see its own doc comment in PcgFile.h.
  // Single pass over dataset.songs checking both original positions at
  // once (not two sequential repoints), same reasoning as the real
  // backend: a second pass searching for "whoever now references B" would
  // re-catch what the first pass just wrote.
  window.swapCombis = (datasetId, bankA, numberA, bankB, numberB) => {
    const dataset = datasets[datasetId];
    if (!dataset) return fail(`Dataset ${datasetId} has no file loaded`);

    const a = dataset.combis.find((c) => c.bank === bankA && c.number === numberA);
    if (!a) return fail("No such Combi at the first position");
    const b = dataset.combis.find((c) => c.bank === bankB && c.number === numberB);
    if (!b) return fail("No such Combi at the second position");
    if (bankA === bankB && numberA === numberB) return ok({ setlistRefsRepointed: 0 });

    const aContent = { name: a.name, setlistReferenceCount: a.setlistReferenceCount, setlistUsages: a.setlistUsages, timbres: a.timbres };
    const bContent = { name: b.name, setlistReferenceCount: b.setlistReferenceCount, setlistUsages: b.setlistUsages, timbres: b.timbres };
    Object.assign(a, bContent);
    Object.assign(b, aContent);

    let setlistRefsRepointed = 0;
    for (const setlistIndex of Object.keys(dataset.songs)) {
      for (const song of dataset.songs[setlistIndex]) {
        if (song.isProgram) continue;
        if (song.bank === bankA && song.number === numberA) {
          song.bank = bankB;
          song.number = numberB;
          setlistRefsRepointed++;
        } else if (song.bank === bankB && song.number === numberB) {
          song.bank = bankA;
          song.number = numberA;
          setlistRefsRepointed++;
        }
      }
    }
    return ok({ setlistRefsRepointed });
  };

  // Mirrors PcgFile::moveCombiWithinBank() -- see its own doc comment.
  // Mock simplification: shifts only the OTHER Combis that already exist
  // in mock data within the [fromNumber..toNumber] range, rather than a
  // full 128-slot shift -- mock fixtures are sparse (a handful of named
  // Combis per bank, see makeFakeCombis()), so there's nothing real to
  // shift in any gap between them anyway.
  window.moveCombiWithinBank = (datasetId, bank, fromNumber, toNumber) => {
    const dataset = datasets[datasetId];
    if (!dataset) return fail(`Dataset ${datasetId} has no file loaded`);
    if (fromNumber === toNumber) return ok({ setlistRefsRepointed: 0 });

    const moving = dataset.combis.find((c) => c.bank === bank && c.number === fromNumber);
    if (!moving) return fail("No such Combi to move");

    const inRange = dataset.combis.filter((c) => {
      if (c.bank !== bank || c.number === fromNumber) return false;
      return toNumber < fromNumber
        ? c.number >= toNumber && c.number < fromNumber
        : c.number > fromNumber && c.number <= toNumber;
    });

    let setlistRefsRepointed = 0;
    const repoint = (fromNum, toNum) => {
      for (const setlistIndex of Object.keys(dataset.songs)) {
        for (const song of dataset.songs[setlistIndex]) {
          if (song.isProgram || song.bank !== bank || song.number !== fromNum) continue;
          song.number = toNum;
          setlistRefsRepointed++;
        }
      }
    };

    for (const c of inRange) {
      const oldNumber = c.number;
      c.number = toNumber < fromNumber ? oldNumber + 1 : oldNumber - 1;
      repoint(oldNumber, c.number);
    }
    moving.number = toNumber;
    repoint(fromNumber, toNumber);

    return ok({ setlistRefsRepointed });
  };

  // Mirrors PcgFile::moveCombiToBank() -- see its own doc comment,
  // including why the vacated source is filled from a same-bank "Init
  // Combi" (makeFakeCombis() adds one per bank) rather than a shipped
  // template.
  window.moveCombiToBank = (datasetId, srcBank, srcNumber, dstBank, dstNumber) => {
    const dataset = datasets[datasetId];
    if (!dataset) return fail(`Dataset ${datasetId} has no file loaded`);
    if (srcBank === dstBank) return fail("Use moveCombiWithinBank() for a same-bank move");

    const src = dataset.combis.find((c) => c.bank === srcBank && c.number === srcNumber);
    if (!src) return fail("No such source Combi");
    const dst = dataset.combis.find((c) => c.bank === dstBank && c.number === dstNumber);
    if (!dst) return fail("No such destination Combi");

    const dstReferenced = Object.keys(dataset.songs).some((setlistIndex) =>
      dataset.songs[setlistIndex].some((song) => !song.isProgram && song.bank === dstBank && song.number === dstNumber)
    );
    if (dstReferenced) {
      return fail("Can't overwrite -- the destination Combi is still referenced by at least one Set List slot");
    }

    const filler = dataset.combis.find((c) => c.bank === srcBank && c.number !== srcNumber && c.name === "Init Combi");
    if (!filler) return fail('Can\'t vacate -- no other "Init Combi" slot exists in this bank to fill it with');

    Object.assign(dst, {
      name: src.name,
      setlistReferenceCount: src.setlistReferenceCount,
      setlistUsages: src.setlistUsages,
      timbres: src.timbres,
    });
    Object.assign(src, {
      name: "- Init Combi -",
      setlistReferenceCount: filler.setlistReferenceCount,
      setlistUsages: filler.setlistUsages,
      timbres: filler.timbres,
    });

    let setlistRefsRepointed = 0;
    for (const setlistIndex of Object.keys(dataset.songs)) {
      for (const song of dataset.songs[setlistIndex]) {
        if (song.isProgram || song.bank !== srcBank || song.number !== srcNumber) continue;
        song.bank = dstBank;
        song.number = dstNumber;
        setlistRefsRepointed++;
      }
    }

    return ok({ setlistRefsRepointed });
  };

  // Mirrors PcgFile::copyCombi() -- see its own doc comment. Source is left
  // completely untouched; the destination must look empty (case-insensitive
  // "init combi" substring, same check as the real backend's
  // looksLikeEmptyCombiName()) or this refuses.
  window.copyCombi = (datasetId, srcBank, srcNumber, dstBank, dstNumber) => {
    const dataset = datasets[datasetId];
    if (!dataset) return fail(`Dataset ${datasetId} has no file loaded`);
    if (srcBank === dstBank && srcNumber === dstNumber) return fail("Source and destination are the same slot");

    const src = dataset.combis.find((c) => c.bank === srcBank && c.number === srcNumber);
    if (!src) return fail("No such source Combi");
    const dst = dataset.combis.find((c) => c.bank === dstBank && c.number === dstNumber);
    if (!dst) return fail("No such destination Combi");

    if (!(dst.name || "").toLowerCase().includes("init combi")) {
      return fail('Can\'t copy here -- the destination isn\'t an empty ("Init Combi") slot. Drop directly onto a real Combi to swap instead.');
    }

    const dstReferenced = Object.keys(dataset.songs).some((setlistIndex) =>
      dataset.songs[setlistIndex].some((song) => !song.isProgram && song.bank === dstBank && song.number === dstNumber)
    );
    if (dstReferenced) {
      return fail("Can't copy here -- the destination Combi is still referenced by at least one Set List slot");
    }

    Object.assign(dst, {
      name: src.name,
      setlistReferenceCount: src.setlistReferenceCount,
      setlistUsages: src.setlistUsages,
      timbres: src.timbres,
    });

    return ok({ setlistRefsRepointed: 0 });
  };

  // Minimal mock-only mirror of PcgFile.cpp's kConfirmedTimbreBanks -- only
  // the raw codes makeFakeTimbres() actually uses (0/1/20/6), not the full
  // 20-bank table (no mock scenario needs the rest, same "flag the real
  // scope, don't over-build" reasoning as every other simplification in this
  // file). Returns null for a code with no matching bank in mock's own tiny
  // 2-bank `programs` array (20/6 included -- correctly "not resolvable
  // here", same as the real backend would report for a genuinely absent
  // bank, not a guess).
  function mockProgramBankForTimbreCode(rawBankCode) {
    if (rawBankCode === 0) return 0;
    if (rawBankCode === 1) return 1;
    return null;
  }

  // Mirrors PcgFile::analyzeCombiCrossDatasetCopy() -- see its own doc
  // comment. Uses NAME equality as the mock stand-in for the real backend's
  // contentHash comparison (mock Programs have no real bytes to hash), same
  // convention window.findDuplicatePrograms() below already uses for
  // same-file dedup.
  //
  // Known mock-testing limitation, not silently glossed over: every mock
  // dataset is built by the same makeFakePrograms()/makeFakeCombis()
  // generator, so any two mock datasets' Programs are always name-identical
  // -- a cross-dataset copy between two freshly-opened mock files will
  // always resolve every dependency as "found" and never exercise the
  // sliding panel's bank-picker path. That path is covered by
  // tests/pcg_file_test.cpp's real two-file fixture and a real-file smoke
  // probe instead, not by mock/manual testing.
  window.analyzeCombiCrossDatasetCopy = (srcDatasetId, srcBank, srcNumber, dstDatasetId, dstBank, dstNumber) => {
    const srcDataset = datasets[srcDatasetId];
    const dstDataset = datasets[dstDatasetId];
    if (!srcDataset) return fail(`Dataset ${srcDatasetId} has no file loaded`);
    if (!dstDataset) return fail(`Dataset ${dstDatasetId} has no file loaded`);

    const dstCombi = dstDataset.combis.find((c) => c.bank === dstBank && c.number === dstNumber);
    if (!dstCombi) return fail("No such destination Combi");
    if (!(dstCombi.name || "").toLowerCase().includes("init combi")) {
      return fail('Can\'t copy here -- the destination isn\'t an empty ("Init Combi") slot. Drop directly onto a real Combi to swap instead.');
    }
    const dstReferenced = Object.keys(dstDataset.songs).some((setlistIndex) =>
      dstDataset.songs[setlistIndex].some((song) => !song.isProgram && song.bank === dstBank && song.number === dstNumber)
    );
    if (dstReferenced) {
      return fail("Can't copy here -- the destination Combi is still referenced by at least one Set List slot");
    }

    const srcCombi = srcDataset.combis.find((c) => c.bank === srcBank && c.number === srcNumber);
    if (!srcCombi) return fail("No such source Combi");

    const dependencies = [];
    const seenUnresolved = [];
    srcCombi.timbres.forEach((t, i) => {
      if (t.isDefault) return;
      const programBank = mockProgramBankForTimbreCode(t.rawBankCode);
      if (programBank == null) return;
      const srcProgram = srcDataset.programs.find((p) => p.bank === programBank && p.number === t.number);
      if (!srcProgram || !srcProgram.name) return;

      const match = dstDataset.programs.find((p) => p.name && p.name === srcProgram.name);
      const dep = {
        timbreIndex: i,
        srcBank: srcProgram.bank,
        srcNumber: srcProgram.number,
        name: srcProgram.name,
        bankType: srcProgram.bankType,
        found: !!match,
        foundBank: match ? match.bank : 0,
        foundNumber: match ? match.number : 0,
      };
      dependencies.push(dep);
      if (!dep.found && !seenUnresolved.some((u) => u.srcBank === dep.srcBank && u.srcNumber === dep.srcNumber)) {
        seenUnresolved.push({ srcBank: dep.srcBank, srcNumber: dep.srcNumber, name: dep.name, bankType: dep.bankType });
      }
    });

    const dstBanks = [...new Set(dstDataset.programs.map((p) => p.bank))];
    const unresolved = seenUnresolved.map((u) => {
      const candidateBanks = dstBanks
        .filter((bank) => dstDataset.programs.find((p) => p.bank === bank)?.bankType === u.bankType)
        .filter((bank) => dstDataset.programs.some((p) => p.bank === bank && looksLikeEmptyProgramName(p.name)))
        .sort((a, b) => a - b);
      return Object.assign({ candidateBanks }, u);
    });

    return ok({ dependencies, unresolved });
  };

  // Mirrors PcgFile::applyCombiCrossDatasetCopy() -- see its own doc
  // comment. Re-resolves fresh (name-equality, same mock stand-in as
  // analyzeCombiCrossDatasetCopy() above) rather than trusting a prior
  // analyze() call, same "don't trust stale analysis" discipline as the
  // real backend.
  window.applyCombiCrossDatasetCopy = (srcDatasetId, srcBank, srcNumber, dstDatasetId, dstBank, dstNumber, placements) => {
    const srcDataset = datasets[srcDatasetId];
    const dstDataset = datasets[dstDatasetId];
    if (!srcDataset) return fail(`Dataset ${srcDatasetId} has no file loaded`);
    if (!dstDataset) return fail(`Dataset ${dstDatasetId} has no file loaded`);

    const dstCombi = dstDataset.combis.find((c) => c.bank === dstBank && c.number === dstNumber);
    if (!dstCombi) return fail("No such destination Combi");
    if (!(dstCombi.name || "").toLowerCase().includes("init combi")) {
      return fail('Can\'t copy here -- the destination isn\'t an empty ("Init Combi") slot. Drop directly onto a real Combi to swap instead.');
    }
    const dstReferenced = Object.keys(dstDataset.songs).some((setlistIndex) =>
      dstDataset.songs[setlistIndex].some((song) => !song.isProgram && song.bank === dstBank && song.number === dstNumber)
    );
    if (dstReferenced) {
      return fail("Can't copy here -- the destination Combi is still referenced by at least one Set List slot");
    }

    const srcCombi = srcDataset.combis.find((c) => c.bank === srcBank && c.number === srcNumber);
    if (!srcCombi) return fail("No such source Combi");

    const resolvedTimbres = [];  // {timbreIndex, dstBank, dstNumber}
    const resolvedPrograms = [];  // {srcBank, srcNumber, dstBank, dstNumber, alreadyPresent}
    for (let i = 0; i < srcCombi.timbres.length; i++) {
      const t = srcCombi.timbres[i];
      if (t.isDefault) continue;
      const programBank = mockProgramBankForTimbreCode(t.rawBankCode);
      if (programBank == null) continue;
      const srcProgram = srcDataset.programs.find((p) => p.bank === programBank && p.number === t.number);
      if (!srcProgram || !srcProgram.name) continue;

      const already = resolvedPrograms.find((r) => r.srcBank === srcProgram.bank && r.srcNumber === srcProgram.number);
      if (already) {
        resolvedTimbres.push({ timbreIndex: i, dstBank: already.dstBank, dstNumber: already.dstNumber });
        continue;
      }

      const match = dstDataset.programs.find((p) => p.name && p.name === srcProgram.name);
      if (match) {
        resolvedPrograms.push({ srcBank: srcProgram.bank, srcNumber: srcProgram.number, dstBank: match.bank, dstNumber: match.number, alreadyPresent: true });
        resolvedTimbres.push({ timbreIndex: i, dstBank: match.bank, dstNumber: match.number });
        continue;
      }

      const placement = (placements || []).find((pl) => pl.srcBank === srcProgram.bank && pl.srcNumber === srcProgram.number);
      if (!placement) return fail(`No destination chosen for "${srcProgram.name}" (${srcProgram.bank}/${srcProgram.number})`);
      // Mirrors PcgFile.cpp's own dstNumber >= 0 branch: an exact slot the
      // user picked (a per-bank dropdown of that bank's own empty Program
      // names) is re-validated fresh here rather than trusted, same as the
      // real backend -- falls back to "first free slot in the bank" when
      // omitted (dstNumber == -1, the ProgramPlacement default).
      let free;
      if (placement.dstNumber != null && placement.dstNumber >= 0) {
        const exact = dstDataset.programs.find((p) => p.bank === placement.dstBank && p.number === placement.dstNumber);
        if (!exact || !looksLikeEmptyProgramName(exact.name)) {
          return fail(`The chosen destination slot for "${srcProgram.name}" is no longer free -- pick another.`);
        }
        free = exact;
      } else {
        free = dstDataset.programs
          .filter((p) => p.bank === placement.dstBank && looksLikeEmptyProgramName(p.name))
          .sort((a, b) => a.number - b.number)[0];
        if (!free) return fail(`No free slot left in the chosen bank for "${srcProgram.name}"`);
      }

      resolvedPrograms.push({ srcBank: srcProgram.bank, srcNumber: srcProgram.number, dstBank: free.bank, dstNumber: free.number, alreadyPresent: false });
      resolvedTimbres.push({ timbreIndex: i, dstBank: free.bank, dstNumber: free.number });
    }

    for (const rp of resolvedPrograms) {
      if (rp.alreadyPresent) continue;
      const slot = dstDataset.programs.find((p) => p.bank === rp.dstBank && p.number === rp.dstNumber);
      const srcProgram = srcDataset.programs.find((p) => p.bank === rp.srcBank && p.number === rp.srcNumber);
      Object.assign(slot, {
        name: srcProgram.name,
        bankType: srcProgram.bankType,
        setlistReferenceCount: 0,
        combiReferenceCountAvailable: true,
        combiReferenceCount: 0,
      });
    }

    const newTimbres = srcCombi.timbres.map((t, i) => {
      const resolved = resolvedTimbres.find((r) => r.timbreIndex === i);
      if (!resolved) return t;
      // Mock has no rawBankCode<->bank translation to invert (see
      // mockProgramBankForTimbreCode()'s own comment on why it's partial) --
      // number is enough for mock's own find-by-(bank,number) lookups
      // elsewhere to keep working; rawBankCode is left as-is, matching
      // whichever bank this Timbre already pointed at.
      return Object.assign({}, t, { number: resolved.dstNumber });
    });
    Object.assign(dstCombi, {
      name: srcCombi.name,
      setlistReferenceCount: 0,
      setlistUsages: [],
      timbres: newTimbres,
    });

    return ok({ setlistRefsRepointed: 0 });
  };

  window.findDuplicatePrograms = (datasetId) => {
    const dataset = datasets[datasetId];
    if (!dataset) return Promise.resolve([]);

    const byName = {};
    for (const p of dataset.programs) {
      if (!p.name) continue;  // empty slots (makeFakePrograms()'s copyProgram() target) aren't "duplicates" of each other
      (byName[p.name] = byName[p.name] || []).push(p);
    }
    const groups = Object.values(byName)
      .filter((g) => g.length >= 2)
      .map((g) => g.map((p) => Object.assign({ setlistUsageCount: 0, combiUsageCountAvailable: true, combiUsageCount: 0 }, p)));
    return Promise.resolve(groups);
  };
})();
