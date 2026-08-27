=== STATE BLOCK — GOALS, ACHIEVEMENTS, BLIND SPOTS ===
Date: 2026-08-08
Status: Working prototype, git repo (github.com/jens-goes-mad/
        DIY-KORG-KRONOS-EDITOR, `main` branch) with a public Hugo/GitHub
        Pages docs site (jens-goes-mad.github.io/DIY-KORG-KRONOS-EDITOR)
        and CI building all 4 platform targets on every relevant push.
        Core file format (Set List names + params + instrument-name
        cross-reference, now including Font size/Transpose/Combi Timbre
        references) is reverse-engineered and wired into a real CHOC app
        with drag-and-drop open and two independent panes, each with its
        own dataset selector and a Setlist/Programs/Combis/Duplicates/
        Internals category navbar (Internals: read-only bank/chunk
        diagnostics -- which Program/Combi banks and top-level chunks a
        loaded dataset actually has, since a real backup can apparently be
        saved with only a subset of data included; surfaced a significant
        pre-existing gap while being built -- see Format Blind Spot #9)
        -- browse/filter Set Lists with drag-and-drop copy-over/insert-with-
        shift reordering (real byte-level writes to both a slot's SBK1
        params and its separate SDB1 name record, same-Set-List only for
        now; ctrl/cmd+click marks rows for a future bulk action, no action
        wired up yet -- see "Setlist reorder/copy-over + multi-select
        groundwork" under "ARCHITECTURE" below), editable
        Comment/Color/Volume fields (all three writing straight into a
        loaded file's own raw bytes, independently open per row, safely
        blocked from being opened on the same slot in both panes at once
        with a toast popup explaining why -- see "Setlist Color/Volume/
        Comment row editors" and "Setlist editor UI polish + cross-pane
        edit lock + toasts" under "ARCHITECTURE" below), and a read-only
        Program/Combi library with byte-exact duplicate detection -- see
        "PROGRAM/COMBI LIBRARY EDITOR" below. A save-to-disk building block
        now exists too (`PcgFile::save()`/`saveFileAs()`, no UI yet), and
        real-hardware validation of it is actively underway -- see "First
        real save-to-disk piece, plus real-hardware validation tooling"
        under "ARCHITECTURE" below, and App/UI Blind Spot #9. The Comment editor's Font
        size now drives a live wrap-preview (real per-size character-width ratios,
        confirmed 2026-08-07 to render "nearly identical" to a real Kronos at a
        ~600px textarea width) -- see "Comment editor: live Font-size wrap preview"
        under "ARCHITECTURE" below. A
        componentized frontend pattern (small, standalone, byte-level-
        tested UI pieces) and a matching backend decoder/encoder
        architecture are now the deliberate direction -- see "ARCHITECTURE:
        DECODER/ENCODER REFACTOR" below, currently the
        active thread of work.

--- GOAL ---

A cross-platform, cross-architecture editor for Korg Kronos `.PCG`/`.SNG`
backup files, built on CHOC (HTML/JS/CSS UI + native C++ bridge) -- same
stack choice as the sibling DIY-MIDI-METRONOME/EDITOR project, reused
rather than reinvented (same CMakeLists.txt structure, vendored
third_party/choc, fetchResource/bind wiring in main.cpp). Preferred
implementation language: C++.

First iteration scope, as originally given:
  1. Open a file, extract its Set List(s) to memory.
  2. Show all entries of the selected Set List; filter/search, copy/swap
     entries via drag and drop.
  3. A Norton-Commander-style dual pane to move/copy entries between
     Set Lists.

A sibling reference project (a running CHOC-based app + CI pipeline) was
mentioned early on as something the project owner would link in later for
conventions; not linked yet -- see Blind Spots.

--- THE FILE FORMAT (reverse-engineered; full byte-level detail in README.md) ---

The `.PCG`/`.SNG` container is chunked (RIFF/IFF-like, but big-endian, and
every chunk header is preceded by one still-unexplained 4-byte field).
`PCG1` (the whole file) has these top-level children:

  DIV1  -- small fixed table, not decoded
  SLS1  -- Set Lists (see below)
  PRG1  -- Programs: 20 sub-banks (MBK1/PBK1 tags, interleaved)
  CMB1  -- Combis: 14 sub-banks (CBK1 tags)
  DKT1  -- Drum Kits -- NOT explored
  WSQ1  -- Wave Sequences -- NOT explored
  GLB1  -- Global settings -- NOT explored
  DPI1  -- unidentified -- NOT explored

Set List names (`SLS1 > SLD1 > SDB1`): one `SDB1` chunk holds all 128 of
the unit's Set Lists. Header (count/numSetlists=128/bytesPerSetlist=3612)
followed by 128 blocks of 129 28-byte records each (1 name + 128 songs).
Record = `[4-byte marker][24-byte ASCII name]`. A marker value of
`28 0f 01 00` on the record right after a name flags "this is where the
128 songs start" -- names and markers otherwise look identical, so this is
the only way to find a Set List's boundary. Verified end to end: all 128
Set Lists extract correctly, including 5 real user-named ones with real
song titles.

Per-slot parameters (`SLS1 > STL1 > SBK1`, a sibling of SLD1 found only by
a *generic* chunk-tag scan, not a targeted SDB1-only search): same header
shape, 128 Set List blocks of a 40-byte header + 128 song records on a
542-byte stride. Confirmed record layout (all field offsets relative to
record start):

  +12  `4*(color-1) + type` (bits0-5 only!) -- type bit0 (1=Program, 0=Combi),
       color 1-based; bits6-7 of this SAME byte are Font size's low 2 bits
  +13  bank index (bits0-4 only!); bits5-7 of this SAME byte are
       Transpose's high 3 bits
  +14  program/combi number within that bank
  +15  Hold Time + 1
  +16  Volume, 0-127, no transform
  +17  Font size's high bit (bit4) + Transpose's low 3 bits (bits5-7);
       bit3 and bits0-2 still unexplained
  +18  Comment (free ASCII text, can contain literal \r\n, NUL-terminated)

  CONFIRMED (2026-08-01, via a properly isolated test file, test_1.PCG):
  Font size = 3 bits (0=S the true baseline, 1=XS, 2=M, 3=L, 4=XL) and
  Transpose = a 6-bit two's-complement value (-32..+31), each packed a
  few bits at a time across the two bytes noted above -- full derivation
  in docs/README.md §4.4. This was a real, actionable bug fix: Bank and
  Color were being read as the FULL byte (unmasked) until this was found,
  silently corrupted on any real slot that also had a non-default Font
  size/Transpose set -- fixed in PcgFile.cpp, masks now applied to all
  four fields.

Instrument-name cross-reference (`CMB1 > CBK1` for Combi, `PRG1 > MBK1`/
`PBK1` for Program): both use one shared record shape -- the same 12-byte
header again, then fixed-size records each starting with a 24-byte name
field 4 bytes in (space/NUL-padded, NOT NUL-terminated -- a full 24-char
name has no terminator at all). A slot's bank/number index directly into
`[bank][number]` of whichever list matches its type. Verified against
three independent ground-truth anchors the project owner gave directly
(not guessed): "Rolling in the Deep" (Combi, bank 7 = USER-A / record 9),
"Berlin Grand SW2 U.C." and "Rain Again" (Program, bank 0, records 0 and
127) -- all three matched exactly. Bank values outside the stored range
(>=20 for Program, >=14 for Combi) are real gaps, not bugs -- near-certainly
GM/GM2 references (fixed MIDI-spec content, not stored per-file); a
one-off bank-231/192 reference each looks like genuine data corruption in
that one slot rather than a parsing issue. Across a full real-ish test
file, 143/152 assigned slots resolved to a name; all 9 misses were
out-of-range, zero were in-range failures.

Deliberately NOT solved (see Blind Spots): the `used`/count header
field's meaning, SBK1 +17's bit3/bits0-2, the 4-byte chunk-header prefix
field, and exactly which of the 20 PRG1 banks maps to which *display
label* (the name lookup mechanism itself is confirmed; the specific
letter shown per bank index is a positional assumption modeled on the
project owner's given naming order, not independently verified the way
Combi's order was).

--- WHAT'S BUILT AND WORKING ---

  - `src/kronos/PcgFile.{h,cpp}`: parses SDB1 (names), SBK1 (Program/Combi/
    bank/number/Color/Hold Time/Volume/Comment per song), and CBK1/MBK1/
    PBK1 (real instrument names, cross-referenced into `Song::instrumentName`)
    per the layout above. `load(path)` and `loadFromMemory(bytes)` share
    one parsing path. Every optional chunk degrades gracefully (a file
    missing/mismatching SBK1 or the instrument banks still gets Set List
    names fine, just with emptier fields) rather than failing the whole load.
  - `src/bridge/EditorBridge.{h,cpp}`: holds loaded files keyed by a "pane"
    id ("A"/"B"). Exposes `openFile`(path, unused by the UI but kept for
    CLI/debug)/`openFileBytes`(base64, what the UI actually uses)/
    `listSetlists`/`getEntries`/`moveEntry`(swap within a Set List)/
    `copyEntry`(across panes/Set Lists, whole Song incl. all params)/
    `setComment`. Nothing is ever written back to disk -- everything is
    in-memory rearrangement only.
  - `frontend/`: two side-by-side panes (`pane.js`, one instance per side).
    Each pane is a drag-and-drop target (drop a `.PCG` file -> read via the
    browser's File API -> base64 -> `openFileBytes`; confirmed working by
    the project owner in the real app) with a Set List picker, filter/
    search, and a table of that Set List's 128 song slots showing Kronos's
    own 000-127 numbering, Type/Bank/Vol/Hold/Color columns, and the real
    instrument name as a subtitle under the slot's own label (shown
    always, even when identical to the label -- confirms the lookup found
    something). Dragging a row within a pane swaps two songs (all fields);
    dragging to a different pane/Set List copies the whole song across.
    Clicking a row's Song/Type cell expands an inline multiline Comment
    editor (monospace, grows to fit content past 10 rows); clicking # opens
    a 16-button Color editor (real Kronos colors); clicking Vol opens a
    0-127 slider. All three route through the SAME shared raw-bytes cache
    per row and can be open together on one row at once (see "Setlist
    Color/Volume/Comment row editors" under ARCHITECTURE below for why that
    matters). Color/Volume commit immediately (color on click, volume on
    slider release); Comment keeps its Apply button. If two panes point at
    the same dataset AND the same Set List, opening an editor on a slot
    already open in the other pane is blocked outright (a toast explains
    why) rather than risking one pane's write silently overwriting the
    other's -- see "Setlist editor UI polish + cross-pane edit lock +
    toasts" under ARCHITECTURE below. No typed-path/Open-button UI (removed
    per explicit request -- drag-and-drop is the only way in, though the
    bridge's path-based `openFile` still exists underneath).
  - `frontend/mock_bridge.js`: fake in-memory backend (mirrors the real
    API) so the UI can be exercised in a plain browser with no native
    build -- cannot exercise real file parsing (browsers have no
    filesystem access at all, which is the whole reason Choc's native
    bridge exists).
  - `frontend/components/kronos/setlist-comment.{js,css,test.html}` and its
    sibling `setlist-slot-params.{js,test.html}` (Color/Volume): the
    componentized codec pieces (see "ARCHITECTURE" below) -- built,
    self-tested standalone, AND wired into `pane.js` via a dynamic
    `import()` (see "Setlist Color/Volume/Comment row editors" below).
  - `docs/`: a public Hugo/GitHub Pages site
    (jens-goes-mad.github.io/DIY-KORG-KRONOS-EDITOR) alongside the format
    reference doc (`docs/README.md`, mirrored -- keep in sync by hand --
    into `docs/content/format/`). Also has an Overview page, a Building-
    the-app page, and an App Architecture & Components page.
  - Why drag-and-drop is the *only* open mechanism: a plain
    `<input type="file">` does trigger a real native NSOpenPanel inside the
    Choc-wrapped WKWebView, but the resulting sheet renders behind the app
    window instead of in front (a choc/WKWebView z-order quirk, root cause
    not isolated). Drag-and-drop sidesteps it entirely and is arguably the
    better design regardless -- it works identically on every platform,
    whereas recovering an absolute path from a WebView's file picker is
    inconsistent-to-impossible across WebKit/GTK-WebKit/WebView2. Tradeoff:
    base64-encoding a 50-70MB file in JS and shipping it through the
    bridge as one JSON string isn't fast -- fine for occasional loads, not
    a "reload on every drop" workflow.
  - Build: CMake, C++17, Debug (frontend/ read live off disk) vs Release
    (embedded via tools/embed_resources.py) split, mirroring
    DIY-MIDI-METRONOME/EDITOR's setup exactly. No MIDI/audio deps (no
    rtmidi) -- this tool only reads local files, no device I/O.

--- PROGRAM/COMBI LIBRARY EDITOR (planned, Phase 1 built) ---

Goal beyond just Set Lists: browse every Program/Combi directly, see where
each Program is actually used, find byte-exact duplicate Programs, and
eventually delete the unused ones and repoint Combis at a single kept
copy. That last part means writing to a real Kronos backup for the first
time ever in this project, based on a part of the format that isn't
reverse-engineered at all yet -- so the work was explicitly split into
three phases, only the first of which is built:

  - **Phase 1 (DONE)**: read-only. `PcgFile` gained `ProgramInfo`/
    `CombiInfo` (flat `[[{bank, number, name}]]` lists, populated from the
    same PRG1/CMB1 chunks the instrument-name cross-reference already
    walked) plus `programSetlistUsages(bank, number)` (every Program-type
    Set List slot referencing it) and `findDuplicatePrograms()` (groups of
    2+ Programs whose full ~4960-byte record hashes to the same FNV-1a
    value -- computed during parsing since the raw file bytes aren't kept
    around afterward). `EditorBridge` exposes `listPrograms`/`listCombis`/
    `getProgramUsage`/`findDuplicatePrograms`. `frontend/library.js` is a
    new top-level "Library" tab (alongside "Set Lists") with Programs/
    Combis/Duplicates sub-tabs, a Pane A/B source selector, filter/search,
    and click-to-expand usage info per Program -- read-only, no drag-and-
    drop, no delete/consolidate buttons (nothing to wire them to yet).
    Verified against the real 47.9MB sample: 2560 Programs / 1792 Combis
    (matches 20x128 / 14x128 exactly), the known "Berlin Grand SW2 U.C."
    anchor round-trips correctly, and 62 duplicate groups covering 500 of
    2560 Programs were found -- including real repeated content like
    "Snappy Clav" and "Kompton Clav" turning up twice each in different
    banks, a good sanity check.
  - Caveat found during Phase 1: **bank 0 / number 0 is also the all-zero
    byte value**, so `programSetlistUsages(0, 0)` massively over-counts --
    16,000+ "usages" on the real sample, because a Set List slot that was
    never actually assigned a Program still reads as bank 0/number 0.
    Every other bank/number spot-checked (e.g. bank 2/number 8, bank
    19/number 0) returns a small, correct-looking count. There's no known
    flag distinguishing "really assigned to 0/0" from "never touched" --
    documented in `PcgFile.h`, not fixed (no known fix without a new
    reverse-engineering lead, same as the other format blind spots below).
  - **Phase 2 (structure CONFIRMED, not wired into usage-counting yet)**:
    each Combi's 16 Timbres sit at a fixed 188-byte stride starting 4806
    bytes into the record; the first 3 bytes are Program number, a raw
    bank code, and a status byte (Off/Internal/External/Ex2 -- top 3
    bits). Confirmed via real Combi samples the project owner provided,
    cross-checked against an independent external reference
    (`DaBlick/PCG-Tools`, see `docs/references/`) -- see `docs/README.md`
    §6 for the full derivation, including a Combi that initially looked
    like a model gap but turned out to be the project owner's
    recollection of that Combi not matching what was actually saved.
    `PcgFile`'s `TimbreRef`/`CombiInfo::timbres` and `timbreBankName()`
    are built and smoke-tested; still TODO: wire this into real
    Combi-usage counting (today's `getProgramUsage` still explicitly
    flags `combiUsagesAvailable: false` rather than silently implying
    zero) -- deliberately not done yet since only 8 of the ~34 possible
    bank codes are confirmed, and a real "Combi refs" count would need
    every bank a user's file actually uses to resolve correctly.
  - **Phase 3 (not started, depends on Phase 2)**: actual deletion of
    unused duplicate Programs and repointing Combis at a kept copy -- the
    first real write-back to a `.PCG` file this project would ever do.
    Needs a dry-run/preview step and a strong recommendation to keep an
    untouched backup, since there's no way to run Korg's own file
    validator from here to confirm nothing broke.

--- ARCHITECTURE: DECODER/ENCODER REFACTOR (decided 2026-08-01, in progress) ---

A deliberate architectural direction, agreed with the project owner, for both the
frontend and backend, growing out of building `frontend/components/kronos/
setlist-comment.js` (Comment + Font size, see docs/content/components/index.md for the
full rationale):

  - **Frontend**: small, standalone UI pieces under `frontend/components/{kronos,
    generic}/`, each split into a pure codec (`decode(bytes) -> state` / `encode(bytes,
    state) -> newBytes`, no DOM), a component (owns the actual UI, operates only on
    `state`), and a standalone `.test.html` harness with real committed self-checks --
    no CHOC, no native build, just a static file server. `setlist-comment.js` is the
    first and so far only one built this way; a generic reusable envelope/ADSR editor
    (shared across every Kronos synth engine's envelope curves) was discussed as a strong
    future candidate given how much of the Kronos reuses the same ADSR-shaped UI.
  - **Backend (Program decoder BUILT 2026-08-01, verified zero-regression against the
    real 47.9MB file)**: `PcgFile` no longer discards the raw file bytes after parsing
    (`data_`, retained). `src/kronos/ProgramDecoder.{h,cpp}` is the first small,
    per-record decoder (mirrors the frontend pattern): `decodeProgramFields()` (raw
    Kronos fields) and `hashProgramRecord()` (our own derived bookkeeping) as separate
    functions, plus `PcgFile::decodeProgram(bank, number)` proving a record can be
    re-decoded on demand from the retained bytes, not just once at load. This also
    happens to be the cleanest fix for a staleness class of bug the project owner
    flagged before it was ever written: if a component/decoder holds a byte snapshot
    captured once and something else changes the underlying record in the meantime, a
    later write silently reverts that other change. With raw bytes as the *one* retained
    copy (not a byte snapshot plus a separate structured shadow copy), every decode
    always reads the current state -- there's nothing to go stale.
  - **Backend (Combi decoder BUILT 2026-08-01, same day, covered by
    `pcg_file_test`)**: `src/kronos/CombiDecoder.{h,cpp}` is the second per-record
    decoder, same shape as ProgramDecoder -- `decodeCombiFields()` returns raw Kronos
    fields (name) plus each Combi's 16 Timbre-to-Program references (`TimbreRef`, moved
    out of `PcgFile.cpp`'s old inline `collectCombiRecords()`/`readCombiTimbres()`/
    `decodeTimbreStatus()`, now dead code and removed). No `hash()` -- byte-exact
    duplicate detection was only ever requested for Programs. `PcgFile::decodeCombi(bank,
    number)` mirrors `decodeProgram()`, backed by a new `combiBankLocations_` (mirrors
    `programBankLocations_`). `tests/pcg_file_test.cpp`'s synthetic fixture grew a CBK1
    bank to cover this: a direct `decodeCombiFields()` unit test (name-padding trim,
    truncated-record degrade), plus end-to-end `combis()`/`decodeCombi()` assertions
    through `PcgFile::loadFromMemory()`. `timbreBankName()`/`isConfirmedTimbreProgramBank()`
    stayed in `PcgFile.cpp` rather than moving, since `PcgFile.cpp` itself calls them (in
    `combiUsagesForProgram()`/`combiUsageCounts()`), not just the decoder.
  - **Sequencing (explicit, small-iterations-first)**: Program decoder done, test
    infrastructure landed (Blind Spot #14), Combi decoder done, Set List slot raw-byte
    read/write done (2026-08-05, see "Setlist Color/Volume/Comment row editors" below) --
    unlike Program/Combi, this didn't need a new standalone `SetlistDecoder.{h,cpp}`:
    every SBK1 song record's byte offset is one deterministic formula off a single
    retained anchor (`sbkSongsStart_`), not a per-bank location table, so
    `PcgFile::songRecordBytes()`/`putSongRecordBytes()` just reuse the *existing*
    `readSlotParams()`/`readComment()` functions already living in `PcgFile.cpp`.
  - **Datasets: decoupling "loaded file" from "pane" (BUILT 2026-08-01, human-verified
    in the real app -- "works like a charm")**: the two-pane UI used to conflate two
    different things a user wants to do -- (1) rearrange entries between Set Lists
    *within the same backup* to build a new gig Set List, which needs both panes
    looking at the *same* loaded file so an edit in one is visible in the other, vs.
    (2) compare/merge two *different* backups side by side, which needs two genuinely
    independent files. The old model (`EditorBridge`'s `m_panes`, keyed by the
    frontend's own `paneId` string, one `PcgFile` per pane, 1:1) did neither correctly:
    dropping the same file onto both panes silently forked it into two unrelated
    in-memory copies. We agreed the fix: promote **dataset** (one loaded file) to a
    first-class concept, identified by an id `EditorBridge` mints itself on open
    (`m_datasets`, keyed by `int datasetId`, never a caller-supplied string), fully
    decoupled from which pane displays it.
    - `openFile`/`openFileBytes` no longer take a paneId -- they mint a new dataset
      every call and return `{datasetId, displayName, setlistCount}`. New
      `listDatasets()` (every open dataset, for any selector to populate itself) and
      `closeDataset(datasetId)` (frees one, a harmless no-op if already gone). Every
      other method (`getEntries`, `moveEntry`, `copyEntry`, `setComment`,
      `listPrograms`, `listCombis`, `getProgramUsage`, `findDuplicatePrograms`) renamed
      its paneId arg(s) to datasetId -- `copyEntry` needed **no logic change at all**,
      since it already had no same-id special case; pointing both panes at one dataset
      and dragging between them "just works" purely from this rename.
    - `datasetId` is a plain `int` (not a string) specifically to avoid a real footgun:
      `std::map<std::string, ...>` sorts "10" before "2" lexicographically, which would
      silently scramble selector option order past the ninth open dataset. Matches how
      `setlistIndex`/`songIndex` were already round-tripped as numbers through
      `<select>` values elsewhere in this codebase.
    - New `frontend/datasets.js` (no build step, plain script like every other
      `frontend/*.js` file): `refreshDatasets()`/`onDatasetsChanged(listener)` (a small
      pub/sub -- a listener fires immediately with whatever's cached, and again on
      every future refresh from *any* pane or Library, so a file dropped anywhere shows
      up as a selectable option everywhere) plus a shared `populateDatasetSelect()` DOM
      helper used identically by `pane.js` and `library.js` so neither duplicates it.
    - Both Set List panes and the Library view each got their own dataset-select
      dropdown (Library's replaced its old hardcoded "Pane A"/"Pane B" `<option>`s --
      the closest existing analog, but never actually generated from a registry).
      Dropping a file always creates a *new* dataset (never silently overwrites); a
      pane's own selector lets it switch to *any* already-open dataset, including one
      another pane opened. `app.js`'s `onDropEntry` now compares dataset identity (not
      pane identity) to decide reorder-vs-copy, and refreshes every pane whose
      `getCurrentDatasetId()` matches either side of the move/copy -- this is the
      concrete mechanism that makes "both panes on the same dataset" behave like one
      shared document.
    - `frontend/mock_bridge.js` mirrors the new shape (`datasets = {}` keyed by a local
      counter) so the no-native-build frontend dev path stays usable.
    - Verified: full app + `pcg_file_test` build clean, `ctest` passes (this refactor
      doesn't touch `PcgFile`/decoders at all), all JS syntax-checked, `datasets.js`'s
      actual pub/sub + selector-population logic passed a headless Node smoke test, and
      a live click-through in the real app confirmed multi-dataset open, switching, and
      shared-dataset drag/drop end to end -- "works like a charm."
    - Two issues surfaced during that click-through, deliberately left unfixed for now
      (see Blind Spots #17/#18) -- neither blocks using the feature.
  - **Per-pane category navbar (BUILT 2026-08-03)**: the top-level "Set Lists" vs
    "Library" split is gone -- each pane is now a shell with its own single dataset
    selector plus a category navbar (Setlist / Programs / Combis / Duplicates; Global
    later, once `GLB1` is ever parsed -- not added as a placeholder tab, since there's
    nothing behind it yet). Motivation: the old split meant you couldn't put a Setlist
    view and a Programs view side by side, or compare two datasets' Program banks
    directly -- exactly the kind of comparison the `explore/sqlite-patch-datastore`
    branch's physical-bank-placement work would want. Built by splitting both existing
    factories into peer content renderers plus a thin shell:
    - `frontend/pane.js`'s `createSetlistPanel()` -- the old `createPane()` body
      (table/filter/comment-editor/drag-drop), now reading which dataset to show via a
      `getDatasetId()` accessor instead of owning a dataset-select itself.
    - `frontend/library.js`'s `createLibraryPanels()` (renamed from `createLibrary()`)
      -- same idea: dropped its own dataset-select *and* its own internal
      Programs/Combis/Duplicates tab bar (that nav is now the shell's, so all four
      categories are true peers) -- exposes `showPanel(name)` for the shell to call
      instead of handling its own tab clicks.
    - `createPane()` (same entry point `app.js` already called) is now the shell:
      owns the one dataset-select + category nav, notifies both renderers via
      `onDatasetChanged()` on any dataset change (regardless of which category is
      currently visible, so switching back to a hidden one later still shows fresh
      data), and still returns `{ refreshEntries, getCurrentDatasetId }` unchanged --
      `app.js`'s `onDropEntry` needed zero changes.
    - **Deliberately deferred, per explicit agreement**: no drag-and-drop for
      Programs/Combis rows (the hard physical-bank-placement problem from the
      exploration branch -- Setlist row drag/copy is unaffected); Duplicates stays
      scoped to the pane's single selected dataset, no cross-dataset dedup yet.
    - No backend/bridge changes at all -- purely a frontend reorganization reusing
      already-tested render logic; `pcg_file_test`/`ctest` untouched and still passing.
  - **Chunk-based data flow for components (designed 2026-08-01, first real feature BUILT
    2026-08-05 for Setlist slots -- see below)**: two deliberately different tiers, not
    one architecture for everything --
    - *Bulk/list views* (Programs table, dedup, etc.) stay served by native decoders
      walking the whole retained buffer -- real efficiency win here (e.g.
      `findDuplicatePrograms()` hashes ~12.7MB across ~2560 records; genuinely faster
      native than doing the same in a WebView's JS engine, and avoids repeatedly
      shipping large data across the JS/native bridge for something already sitting in
      native memory).
    - *Detail/edit views* (Comment+Font-size today, more later) request the *specific
      raw byte chunk* they need (one record's bytes) via the bridge, and do their own
      decode/encode entirely in JS -- exactly `setlist-comment.js`'s existing pattern,
      generalized. Preserves the "test without building the native app" property
      specifically where it matters most: interactive UI a human iterates on.
    - **Why not a deferred edit overlay**: originally considered keeping the retained
      buffer strictly immutable and layering pending edits on top as a `{position ->
      new bytes}` overlay (for undo/redo, and to avoid touching canonical state).
      Rejected: overlay keys are position-based (bank/number), and any *reorder*
      operation (moving/swapping Programs, Combis, Set List entries -- already a core
      feature) would leave a stale overlay entry silently applying to whatever record
      now occupies that position instead of the one it was meant for. **Decided
      instead**: `encode()` writes back *immediately* into `data_` via a new bridge
      method, `putRecordBytes()` -- baking the edit directly into the bytes that any
      later reorder would move, rather than tracking it separately. Safe specifically
      because this app is single-threaded JS with exactly one user editing at a time --
      no concurrent-writer conflicts to resolve. Undo/redo stays achievable this way too
      (each `putRecordBytes()` call is a discrete, reversible operation -- keep old
      bytes alongside new in a history stack), just not built yet.
    - **`putRecordBytes()` must keep the structured cache in sync**: `Song.comment`/
      `Song.params.fontSize` etc. are cached fields, parsed once out of `data_` into
      `setlists_` at load time. A raw-byte-only write would leave them stale (Set List
      table keeps showing the old comment). Required behavior once built: (1) overwrite
      bytes in `data_`, (2) re-run the *existing* SBK1 decode on just the newly-written
      bytes, (3) update the corresponding `Song` in `setlists_` from that fresh decode --
      so the structured cache is always derived from canonical bytes right after a
      write, never hand-maintained separately. **Realized 2026-08-05** as
      `PcgFile::putSongRecordBytes()` -- see below, same three-step discipline, just for
      Setlist song records instead of Programs.
    - **Cross-pane refresh gap -- RESOLVED (2026-08-01), superseded by the Datasets
      refactor below**: the plan on this line used to be a narrow fix (a
      `getCurrentSetlistIndex()` pane accessor, `app.js` checking "is the other pane on
      the same Set List"). What actually got built is more general and solves the
      problem at its root instead: "loaded file" became its own first-class concept
      (a *dataset*) decoupled from "which pane shows it" -- see the Datasets subsection
      below. `onDropEntry` now refreshes every pane whose `getCurrentDatasetId()` matches
      either side of a move/copy, which handles "both panes on the same Set List" as one
      case of the more general "both panes on the same dataset."
  - **Streaming/mmap for raw bytes -- considered, not applicable to the current data
    path**: retaining the whole file in `data_` isn't a preference for "native heap over
    streaming" -- it's a consequence of how bytes actually arrive. The app's only wired
    file-opening mechanism (drag-and-drop) means the whole file is already fully
    materialized in memory multiple times (browser `File.arrayBuffer()`, a base64
    string shipped across the bridge, then decoded back to bytes) *before*
    `PcgFile::loadFromMemory` ever sees it -- there's no live socket/handle left to
    stream from by that point. `PcgFile::load(path)` (a plain `ifstream` read, not wired
    to any UI control -- dropped in favor of drag-and-drop specifically because of the
    NSOpenPanel-behind-the-window bug, see Blind Spots) is the one place a real
    seek/mmap-based reader would genuinely help. Worth revisiting *if* path-based
    opening ever becomes primary again (e.g. if that native-dialog bug gets fixed).
  - **No encoder yet beyond `setlist-comment.js`, deliberately**: every current
    Program/Combi use case (table population, dedup) is read-only. An encoder gets
    built once there's an actual write feature driving its real shape, same "don't
    build for hypothetical future needs" principle already applied elsewhere in this
    project. **Renaming Programs/Combis/Set Lists** was explicitly named as a likely
    upcoming feature that would need one -- not started.
  - **Open/Save dialog**: real write-back (`putRecordBytes()`, and eventually saving to
    disk) will need either a working native Save dialog (the NSOpenPanel-behind-the-
    window bug, unresolved, see Blind Spots) or some other path-recovery mechanism --
    drag-and-drop is input-only, it can't hand back a path to save *to*. Explicitly
    deferred: this project is still in read-only territory (Phase 1/2 of the Library
    Editor, no Program/Combi/Set-List encoder exists yet either), so fixing this now
    would be solving a problem too early.
  - **Setlist Color/Volume/Comment row editors (BUILT 2026-08-05)**: per-column click
    routing on a Set List row (# -> Color, Vol -> Volume, Song/Type -> Comment, same
    fallback as before), all three independently toggleable so several can be open on
    the SAME row at once (explicit request) -- this is what turned "Comment still writes
    via the old struct-only path" from a style inconsistency into a real data-loss bug:
    with Color/Volume writing raw bytes and Comment only mutating `Setlist::songs[i]
    .comment` in memory, whichever committed second would silently discard the other
    (a raw-byte write re-derives every decoded field, including comment, from the bytes
    it just wrote). Fix: Comment was retrofitted onto the same raw-byte path, so all
    three editors on a row share one raw-bytes cache and one write path.
    - Backend: `PcgFile` retains one anchor per Set List (`sbkSongsStart_`, set during
      the existing SBK1 parse loop, no new scan) plus `songRecordBytes(setlistIndex,
      songIndex)` / `putSongRecordBytes(...)` -- the latter writes into `data_`, then
      re-derives `Song::params`/`comment` via the *existing* `readSlotParams()`/
      `readComment()`, same "cached field must never go stale after a direct write"
      discipline as `copyProgramFrom()`. Does NOT re-resolve `instrumentName` -- none of
      these three editors touch bank/number, so it stays valid; would need revisiting if
      a future editor ever did.
    - Bridge: `getSongRecordBytes`/`putSongRecordBytes` (array-of-numbers over the
      bridge, same `choc::value::createArray`/`addArrayElement` pattern every other
      array-returning method already uses -- 542 bytes needs no base64). `setComment` is
      unused by the new UI now but left in place in case anything else still calls it.
    - Frontend: new sibling codec `frontend/components/kronos/setlist-slot-params.js`
      (`decodeSlotColor`/`encodeSlotColor`/`decodeSlotVolume`/`encodeSlotVolume`) next to
      `setlist-comment.js`, same masked-read-modify-write discipline (Color shares byte
      +12 with isProgram/Font size, masked to bits2-5 only; Volume is a clean unpacked
      byte at +16, no masking needed) -- own `.test.html`/`.test.js`, reusing
      `test-fixtures.js`'s real "Rolling in the Deep" record (byte+12=`0xc0` confirms
      Color=1/Standard, byte+16=`0x7f` confirms Volume=127 on that real record).
      `pane.js` loads both codecs via a cached dynamic `import()` expression -- works
      from inside a plain (non-`type="module"`) script without converting `index.html`'s
      scripts to ES modules.
    - `pane.js`'s expand-state grew from `Set<songIndex>` to `Map<songIndex,
      Set<"comment"|"color"|"volume">>`, plus a `Map<songIndex, Uint8Array>` raw-bytes
      cache shared by every editor open on that row (`getSlotBytes()` fetches once,
      `commitSlotBytes()` is the one write path all three editors call through). Color
      commits on click, Volume commits on the slider's `change` event specifically (not
      `input`) so it writes once on release/blur, not once per pixel of drag.
    - `frontend/mock_bridge.js` gained fake `getSongRecordBytes`/`putSongRecordBytes`,
      synthesizing a 542-byte buffer from a mock entry's own fields (`makeFakeSlotBytes()`)
      so mock/no-native-build mode can exercise the whole feature.
    - Verified: `pcg_file_test`/`ctest` extended (round-trip, byte-boundary/neighbor-
      record isolation, wrong-size/out-of-range rejection), deliberately broken then
      restored per this project's standard practice; full app + test target both build
      clean; every touched JS file `node --check`ed; the new codec's headless
      `.test.js` passes (22 checks) against the real fixture bytes. Initially NOT
      visually click-tested in a real browser (no browser-automation tool available in
      the environment this was built in -- a static-file-server mock-mode smoke test,
      asset/import-path resolution only, stood in instead) -- since then click-tested
      live by the project owner in the real app across several follow-up rounds (see
      below), with real bugs found and fixed that the static smoke test couldn't have
      caught.
  - **Setlist editor UI polish + cross-pane edit lock + toasts (BUILT 2026-08-05, same
    day, driven by live click-through feedback)**: several rounds of real-app testing
    against the row editors above surfaced concrete issues -- fixed in order:
    - **Row order**: editor rows now append in a fixed Color/Comment/Volume order,
      matching the columns' own left-to-right order (#, Song/Type, Vol) instead of the
      original Comment/Color/Volume order.
    - **Volume editor**: gained a "Volume" text label in front of the slider.
    - **Editor-row visual treatment**: a generic `.editor-row` class (added alongside
      each row's specific `comment-editor-row`/`color-editor-row`/`volume-editor-row`
      class, kept as unused-for-now per-type hooks) carries a shared 10px left padding
      and a `darkorange` 1px left border, so an open editor reads as a distinct
      "you're now editing" section instead of just another table row. Took two passes
      to land correctly: the left padding was initially silently overridden by Bulma's
      own `.table.is-narrow td{padding:.25em .5em}` rule, which -- having two classes
      plus an element in its selector -- out-specifies a plain `.editor-row td` (one
      class plus an element) regardless of stylesheet order; fixed by matching Bulma's
      own `.table.is-narrow` prefix in the override selector.
    - **`tr.is-selected` row highlight** (marks "this row has an editor open") recolored
      from Bulma's default green/teal to the same `darkorange`. Same specificity lesson
      hit twice: Bulma sets the `--bulma-table-row-active-background-color` custom
      property it reads on `.table` itself, not `:root` -- a `:root`-level override
      loses because a rule that directly matches an element always wins over an
      inherited value, regardless of source order or specificity. Landed on a simpler
      fix that sidesteps the whole custom-property indirection: a plain
      `background`/`color` declaration on `.table tr.is-selected` (two classes + a
      type selector) beats Bulma's own `.is-selected` (one class) outright.
    - **`.instrument-name` contrast**: the Combi/Program name shown under a Setlist
      row's label sets its own dim-gray `color` directly, which -- being a directly-set
      property -- doesn't inherit the selected row's white text regardless of the row's
      own color; needed its own `.table tr.is-selected .instrument-name{color:#000}`
      override to stay readable against the orange highlight.
    - **Slot color palette brightened** (`SETLIST_COLOR_HEX`, `pane.js`) -- the initial
      pass read as too dark/muted; all 16 values bumped ~28% brighter (Black/White
      handled separately so they stay sane), keeping the demo palette in
      `setlist-slot-params.test.html` in sync.
    - **Cross-pane edit-lock (the one genuinely severe bug this round)**: two panes can
      legitimately point at the SAME dataset AND the same Set List at once (a real,
      already-supported setup -- see the Datasets subsection above) -- if both opened an
      editor on the SAME slot, each pane's raw-bytes cache is its own independent copy,
      so whichever pane committed second would silently overwrite whatever the other
      had just written, unnoticed. Fixed with a module-level `openSlotEditors` map
      (`pane.js`, shared across both pane instances, keyed `datasetId:setlistIndex:
      songIndex` -> owning paneId): `toggleEditor()` now refuses to open a slot another
      pane already holds, and a `releaseAllSlotLocks()` helper drops every lock a pane
      owns whenever its `expandedTypes` gets cleared wholesale (Set List switch, dataset
      switch/close) so a lock never outlives the editor state it was protecting.
    - **Toast notifications** (`app.js`'s new `showToast()`): the cross-pane block above
      needed to actually be noticed -- the persistent bottom status bar is easy to miss
      or get overwritten before being read. A small (~15-line) hand-rolled transient
      popup, deliberately not a pulled-in library (see its own doc comment -- worth
      revisiting with something like Notyf/Toastify if toast usage ever grows beyond
      simple single-line messages), styled with Bulma's own semantic `--bulma-warning`/
      `-invert` color pair (the same background/text combination `.notification.
      is-warning` uses internally) rather than a one-off hand-picked color. One shared
      `#toastContainer` (`index.html`, fixed to the viewport) so toasts from either pane
      stack in the same place.
    - Verified: every touched JS file `node --check`ed, CSS brace-balance checked,
      backend build/`ctest` re-confirmed green (none of this round touches the backend).
      UI changes confirmed against real click-through by the project owner (this is what
      surfaced the padding-specificity and cross-pane-race issues above in the first
      place) rather than only the earlier static smoke test.
  - **Setlist editor panel: real accordion + Close button (BUILT 2026-08-06)**: the
    "several editor rows stacked under one slot" shape from the round above still read
    as three unrelated table rows (no title/chevron affordance) with no single way to
    close all of them -- re-clicking the same far-off column to collapse a section was
    exactly the same "counterintuitive" complaint the previous round was meant to fix,
    just moved one level down. Redesigned into a real accordion: ONE `<tr class=
    "editor-row">` per slot (`pane.js`'s `buildEditorRow()`), containing all three
    Color/Comment/Volume sections as collapsible items (chevron + title, title suffixed
    with a live value summary when collapsed -- "Color — Blue", "Volume — 100") plus one
    dedicated Close button (`closePanel()`) that's the only way to fully dismiss the
    panel. Column clicks and clicking a section's own header now both go through the
    same `openSection(entry, type)` -- ensure the panel is open (fetching bytes/codecs
    and the cross-pane lock on first open only), then toggle that one section, never
    closing the whole panel.
    - State split in two: `openPanels` (which slots show a panel at all) vs.
      `expandedSections` (which of an open panel's three items are individually
      expanded) -- collapsing every section no longer auto-closes the panel; only the
      Close button does, matching what was actually being asked for.
    - The Font size button bar (XS/S/M/L/XL) -- present in the original standalone
      `setlist-comment.js` component but never carried over when Comment editing moved
      into this accordion section -- was brought back inside the Comment section,
      above the textarea. Deliberately NOT immediate-apply like Color: Font size and
      Comment text are encoded together by the same `encodeSetlistComment()` call, and
      an immediate commit would trigger `commitSlotBytes()`'s `renderRows()`, which
      rebuilds the section from the just-written bytes -- silently discarding an
      unapplied Comment draft. Font size clicks instead update local pending state
      (exactly like the textarea's own draft) until Apply commits both together.
      `entry.fontSize` (already returned by the bridge, just never round-tripped
      through an edit before) is now kept in sync by `commitSlotBytes()`, and
      `mock_bridge.js`'s synthetic SBK1 bytes gained the matching bit-packing (byte+12
      bits6-7 / byte+17 bit4) so Font size survives a close/reopen in mock mode too.
    - Color's swatch grid also needed a fix here: `auto-fill`/`minmax(3.5em, ...)` was
      sized for the old full-width panel, and came up short once Color's content sat
      indented under an accordion header -- switched to a fixed 8-column grid (2 clean
      rows of 8, buttons just shrink to fit) instead of trying to re-tune the minmax.
    - Verified: every touched JS file `node --check`ed, CSS brace-balance checked,
      backend build/`ctest` re-confirmed green (frontend-only round).
  - **"Select: None/All/Invert" bank-filter control (BUILT 2026-08-06)**: a small
    reusable component, `library.js`'s `createSelectControlRow()`, sitting between the
    Filter input and the bank-filter buttons on both Programs and Combis (explicitly
    requested to work on both, not just one) -- bulk-mutates that category's bank-filter
    `Set` instead of requiring one click per bank. Takes getter functions
    (`getPresent`/`getFilterSet`), not the `Set`s directly: `load()` reassigns
    `programBankFilter`/`combiBankFilter` to a brand-new `Set` on every dataset load
    rather than mutating in place, so a plain captured reference taken once at setup
    time would silently go stale after the very first load. Wired up once per category
    (unlike the bank buttons themselves, these three have no per-bank state of their
    own to redraw). Invert only touches *present* banks -- absent ones have no button
    and nothing to toggle.
  - **First real save-to-disk piece, plus real-hardware validation tooling (BUILT
    2026-08-06)**: the "no save-back-to-file at all" blind spot (below) starts getting
    closed, driven by a concrete, immediate need -- checking every Setlist edit this
    project has built (Comment/Color/Volume/Font size) against an actual Kronos, not
    just this app's own reader. Deliberately minimal, matching this project's "build
    for a concrete need, not speculatively" rule:
    - `PcgFile::save(path, error)` -- writes the retained `data_` buffer straight to
      `path`, verbatim, no serialization step of its own. This is trivial specifically
      *because* every edit already lands directly in `data_` the moment it happens
      (`copyProgramFrom()`, `putSongRecordBytes()`) -- there's no separate in-memory
      model that would need re-flattening into file bytes first.
    - `EditorBridge::saveFileAs(datasetId, path)` -- the bridge counterpart, bound in
      `main.cpp`. No Save dialog UI yet (no native Save panel, no dirty-tracking, no
      keyboard shortcut) -- explicitly deferred until the write path is actually proven
      against real hardware; building a polished Save button before knowing whether the
      bytes it produces are even acceptable to a real Kronos would be solving the wrong
      problem first.
    - `tests/pcg_file_test.cpp` gained `testSaveRoundTrip()` -- save() then load() the
      result, confirm Set List count/name/Comment and Program count all match the
      pre-save state. Verified via this project's usual deliberate-break-then-restore
      (temporarily require one extra Set List post-round-trip, confirmed the check
      actually fails, restored).
    - **Two ways to drive a batch of test edits, both explicitly built to exercise the
      REAL production write path, not a parallel reimplementation** -- the whole point
      of this exercise is validating that PcgFile/EditorBridge/the JS codecs are
      correct, which a from-scratch script would only prove about itself:
      - `tools/generate_setlist_test_matrix.js` -- a devtools-console script (pasted
        into the real running app's WebView inspector, `enableDebugMode` already on in
        `main.cpp`) that drives `getSongRecordBytes`/`putSongRecordBytes`/`saveFileAs`
        plus the real `setlist-comment.js`/`setlist-slot-params.js` codecs via a cached
        dynamic `import()`, same technique `pane.js` already uses.
      - `tools/generate_setlist_test_matrix.cpp` -- a second, standalone CLI executable
        (new CMake target, same minimal dependency set as `pcg_file_test`: only
        `PcgFile.cpp`/`ProgramDecoder.cpp`/`CombiDecoder.cpp`, no CHOC/WebView) that
        takes one argument (an input file path) and writes `<name>-test<ext>` next to
        it -- faster to actually run (no need to launch the GUI app first). Its
        Comment/Font-size/Volume ENCODING is necessarily a third expression of the same
        confirmed byte layout (`PcgFile.cpp`'s masks are private to that translation
        unit by design, and this tool has no WebView to run the JS codecs in) -- kept
        deliberately byte-for-byte mirroring the JS codecs, with an explicit comment
        flagging that if the confirmed encoding for these fields ever changes, this
        needs to change with it.
    - Both generate the same fixed matrix into one Set List's slots 010-029 (source
      entry at slot 000 -- these constants are edit-in-place if a different setlist/slot
      layout is ever needed): slots 10-14 vary Font size only (XS/S/M/L/XL, ascending),
      15-19 vary Volume only (0/1/10/100/127), 20-24 vary both together (the same 5
      pairings combined) -- each slot's Comment states exactly which value(s) it's
      testing, so the result is legible directly on the Kronos's own screen without
      cross-referencing anything.
    - **Group 4 (slots 25-29, ADDED 2026-08-06)**: a word-wrap probe, prompted directly
      by `frontend/readme-screen.txt` -- an untracked file with specific, confident-
      sounding claims about the Kronos's screen resolution (800x600) and rendering font
      (Helvetica-family), with zero citation for either and a resolution that doesn't
      match what's commonly documented for the Kronos's actual TouchView display
      (800x480). Treated as exactly what this project's whole methodology exists to
      reject -- a plausible-looking guess -- and answered the same way every other
      confirmed byte offset in this project has been: check it for real, don't debate
      it. All 5 slots carry the SAME Comment (`"01 02 03 ... 80"`, sequential 2-digit
      tokens, only Font size varies) -- unambiguous to read off the hardware screen and
      report back exactly where each size wraps ("line 1 ends at 09, line 2 at 19"),
      and directly comparable across sizes since the text itself never changes.
    - Verified end to end (not just "it compiles"): ran the CLI tool against a real
      minimal KORG-magic file, then independently re-decoded the output with a fresh
      Python script (not reusing any of this project's own code) -- confirmed all 20
      test slots' Font size/Volume/Comment bytes exactly match what was requested, and
      that the source slot and every other untouched slot in the file were unaffected.
    - **CONFIRMED on real hardware, 2026-08-06**: the generated file loaded onto a real
      Kronos with no issues at all -- meaningful beyond just this test, since it's the
      first real evidence that `PcgFile::save()`'s naive verbatim `data_` dump is
      actually accepted by the hardware, not just by this app's own reader. (Only
      tested so far against a minimal SDB1/SBK1-only file, not a full real backup with
      its original PRG1/CMB1/etc. content intact -- worth confirming that case too
      before trusting this broadly, see App/UI Blind Spot #9.) Every slot 010-024 read back
      exactly as written. Group 4's wrap points, read directly off the screen (each
      line's last token number, project owner's own report, one typo corrected in a
      follow-up): every Font size wraps at a perfectly constant tokens-per-line count,
      no exceptions across any line --
        XS=40, S=35, M=30, L=19, XL=8 (tokens of the probe's `"NN "` shape, 3 chars
        each, per line).
      Notably non-linear in a structured way: XS->S->M step by exactly -5 each time,
      then M->L->XL step by exactly -11 each time -- consistent with real integer
      pixel-width quantization (line pixel width / per-character pixel width, floored)
      rather than a simple linear formula, which is exactly the kind of texture real
      hardware measurements produce and a fabricated guess (see `readme-screen.txt`
      above) would not. This single-token-shape result doesn't by itself prove general
      proportional-font behavior (a real sentence's varying word lengths could still
      wrap differently) or reveal the actual screen resolution/font -- both remain
      unconfirmed; a longer single unbreakable "word" per size would be the natural next
      probe if that's ever needed for an in-app wrap-preview feature.
  - **Color names/hex CORRECTED against real hardware, 2026-08-06** -- both the
    biggest and most useful result from this whole test-matrix exercise so far. Added
    Group 5 (slots 030-045, `tools/generate_setlist_test_matrix.{js,cpp}`) writing one
    real Kronos color per slot 1-16, Comment stating which. Real-hardware result: the
    byte encoding itself was already correct (color N reliably showed as the Nth entry
    in the Kronos's own color list), but this project's working name list --
    `"Standard/Blue/Ivy/Gold/Rose/Azure/Red/Orange/Yellow/Green/Cyan/Purple/Magenta/
    Brown/Black/White"`, an unconfirmed guess -- was substantially wrong, in both name
    AND order. The real palette is Korg's own curated, muted set, not generic named
    colors at all (no Red/Green/Blue/Black/White exist as such):
      1=Default, 2=Charcoal, 3=Brick, 4=Burgundy, 5=Ivy, 6=Olive, 7=Gold, 8=Cacao,
      9=Indigo, 10=Navy, 11=Rose, 12=Lavender, 13=Azure, 14=Denim, 15=Silver, 16=Slate
    -- with hex values the project owner read directly off the device (`#494c55`,
    `#282b31`, `#af4350`, `#661b27`, `#929a33`, `#233519`, `#aa8c3e`, `#723d3f`,
    `#3759bf`, `#0410ab`, `#9478c7`, `#745ad2`, `#5588c2`, `#385f9c`, `#546180`,
    `#2a3149`, respectively) -- not pixel-sampled, but close enough to use directly per
    explicit instruction ("treat as proposal," not substituted for a "usual" hex for a
    similarly-named CSS color, since the whole point is showing what THIS hardware
    actually renders). Full table now in `docs/README.md`/`docs/content/format/
    index.md` §4.5, the canonical confirmed-format record.
    - **Open question, explicitly flagged by the project owner, not yet investigated**:
      whether this palette (names and/or hex) is identical across every Kronos
      hardware variant/revision, or whether it differs by model (e.g. an original unit
      vs. a limited/"silver edition" unit) -- confirmed so far only on the one unit
      this project has access to. No way to resolve this without testing a second,
      different unit (or finding it in Korg's own documentation) -- noted rather than
      guessed at either way.
    - `frontend/pane.js`'s `SETLIST_COLOR_NAMES`/`SETLIST_COLOR_HEX` updated to the
      confirmed real values (replacing the guess), kept as the literal hardware value
      -- unmodified, not re-brightened -- so it stays a trustworthy reference. A new
      `brightenHex()` helper applies a purely cosmetic ~1.3x per-channel brightening
      at the one point these values actually get rendered (`setlistColorHex()`, used
      by the "#" cell background and the Color editor's 16 buttons) -- per explicit
      request for more on-screen legibility, without letting that cosmetic choice
      contaminate the confirmed-value record itself. `setlist-slot-params.test.html`'s
      demo palette and both test-matrix tools' color-name labels updated to match.
    - Verified: full rebuild + `ctest` green; re-ran the CLI tool against a fresh
      minimal file and independently re-decoded the output (same Python cross-check
      approach as every other group) -- all 16 Group 5 slots' Color bytes and Comment
      labels match exactly, source slot and neighboring groups' slots unaffected.
  - **Comment editor: live Font-size wrap preview, CONFIRMED against real hardware
    2026-08-07** -- puts the Group 4 word-wrap data (tokens/line per Font size) to
    direct use in the UI, not just as a recorded fact. `pane.js`'s Comment textarea
    now scales its own `font-size` to match whichever Font size is selected (live,
    before Apply, same as the pending-state pattern the rest of that section already
    uses), so what the textarea shows approximates what the real device would show.
    - `commentEditorFontSizePx(fontSize, containerWidthPx)`: the confirmed tokens/line
      ratios (XS=40, S=35, M=30, L=19, XL=8) expressed as a font-size scale factor
      relative to S (13px, the byte encoding's own true baseline) -- a RATIO, not an
      absolute pixels-per-line target, since this pane's width is user-resizable
      (unlike the Kronos's fixed physical screen) and no single "characters per line"
      could be correct at every window size. The ratio itself stays correct at any
      width; CSS text reflow handles the actual wrap for whatever width is currently
      available.
    - Width-responsive on top of that: the 13px-at-S baseline was specifically
      confirmed correct at a ~600px textarea width (`COMMENT_EDITOR_REFERENCE_WIDTH_PX`),
      so `commentEditorFontSizePx()` also scales by `currentWidth / 600` -- resizing
      the main window keeps the preview calibrated instead of freezing at whatever
      width happened to be current when a slot's editor was opened. Recomputed via a
      `ResizeObserver` -- deliberately watching the row's flex CONTAINER, not the
      textarea itself, since observing the textarea directly would create a feedback
      loop (changing its own font-size changes its own height, which would
      immediately re-trigger the same observer).
    - Font changed from the previous monospace stack to `Helvetica, Arial, sans-serif`
      -- a monospace preview would wrap real prose at visually wrong positions
      relative to the real device even with the right font-SIZE ratio applied, since
      every character would take equal width here but the Kronos's own rendering
      (like most real UIs) is proportional-width. Still an unconfirmed guess at the
      exact real font (see `frontend/readme-screen.txt`'s own unverified claims), just
      a more realistic approximation than fixed-width.
    - Incidental bug fix while in here: `autoSizeCommentEditor()`'s "never shrink
      below 10 rows" floor used to be measured ONCE (via a `requestAnimationFrame`
      callback) and cached in `textarea.minHeightPx` -- correct at the time Font size
      was still fixed, but stale the moment Font size became switchable (10 rows at
      XL is much taller than 10 rows at XS, and the cached floor wouldn't know that).
      Fixed by re-deriving the floor fresh on every call instead: resetting
      `style.height` to `"auto"` first falls back to the `<textarea rows=10>`
      attribute's own intrinsic sizing, which already correctly reflects whatever
      font-size is currently applied -- no cached value needed at all, and the old
      `requestAnimationFrame`/`minHeightPx` machinery was removed entirely.
    - `frontend/mock_bridge.js` gained a real test paragraph (`MOCK_WRAP_TEST_COMMENT`,
      the project owner's own real Set List comment text) planted on Setlist 000 /
      Song 000 -- that slot was otherwise blank in every mock dataset (index 0 has no
      entry in `mockSongsByList`, deliberately, so it doubles as an always-empty
      example elsewhere) -- so the live preview can be exercised in mock/browser mode
      without the real native app.
    - **CONFIRMED against real hardware**: at a Comment textarea width of ~600px, the
      project owner reported the preview renders "nearly identical" to the same text
      on a real Kronos. First real validation that a UI feature built directly from
      this project's own confirmed hardware measurements (not a fabricated guess like
      `readme-screen.txt`) actually looks right side by side with the real device.
    - **Dev-environment note, not a code bug**: mock-mode testing hit stale-cache
      confusion in Chrome twice this session (a change appearing to not apply at all)
      -- both times resolved by testing in Firefox or an incognito window instead.
      Python's `http.server` (used for the no-native-build dev loop) sends no
      `Cache-Control` headers at all, so a plain reload isn't reliable; worth
      defaulting to a private/incognito window (guaranteed empty cache) when a
      frontend-only change seems to not be taking effect, before assuming it's a
      real bug.
  - **"Internals" pane -- read-only bank/chunk diagnostics, BUILT 2026-08-07**: a
    5th per-pane category (alongside Setlist/Programs/Combis/Duplicates),
    prompted by a real concern -- the project owner can apparently choose which
    data gets included when saving a backup from a real Kronos, so a loaded
    dataset might genuinely be missing whole banks, with nothing in the existing
    UI to reveal that (the Programs/Combis tables would just look thinner, with
    no way to tell "fewer patches exist" from "a whole bank is gone"). Phase 1
    only -- explicitly scoped down from also letting a user INITIALIZE a missing
    bank/patch with default data, which needs its own dedicated work first (see
    "Deferred" below).
    - **A significant finding surfaced while building this, not before**: bank
      "index" throughout this ENTIRE codebase turns out to be nothing more than
      file-order position among however many PRG1/CBK1 sub-bank chunks were
      found -- not a confirmed bank identity. See Format Blind Spot #9 for the
      full explanation; this is a real gap in EVERY feature that labels a bank
      (Programs/Combis tables, Timbre cross-referencing, Program copy), not
      something specific to this new pane -- the Internals pane is just the
      first place that had to be honest about it in the UI itself, rather than
      silently assuming position-equals-identity like everything before it.
    - Backend: `PcgFile::topLevelChunkTags()` (a new, simple non-recursive walk
      of PCG1's immediate children, reusing the existing `readChunk()` helper --
      this project never previously needed to ask "which top-level chunks exist
      at all," only "where's the ONE I already know how to parse"),
      `programBankInfo()`/`combiBankInfo()` (richer siblings of
      `programBankTypes()`, adding record count/stride, derived from the same
      already-retained `programBankLocations_`/`combiBankLocations_` -- no new
      parsing). All three carry doc comments spelling out the file-order-vs-
      identity caveat directly, not just in STATE.md.
    - Bridge: `EditorBridge::getDatasetInternals(datasetId)` bundles all three
      into one response, bound in `main.cpp`.
    - Frontend: new `frontend/internals.js` (`createInternalsPanel()`), a peer
      content renderer to `library.js`/`pane.js`'s `createSetlistPanel()` --
      same "read datasetId via a getter, own no dataset-select" contract as
      both. Loaded AFTER `pane.js` in `index.html` specifically so it can
      reference `PROGRAM_BANK_NAMES`/`COMBI_BANK_NAMES`/`NO_DATASET_MESSAGE`
      directly (classic `<script>` tags share one top-level `let`/`const`
      scope -- the same thing `app.js` already relies on to call `pane.js`'s
      `kronosNumber()`/`formatBankNumber()` with no import). First built as a
      flat pair of static tables; reshaped the same day after the project
      owner pointed out it wasn't following this project's own component-
      reuse architecture (Setlist/library.js already establish an Entry-row +
      `.editor-row` expand/collapse shape). Now: one Entry row per Topic
      ("Top-level chunks" / "Program banks (PRG1)" / "Combi banks (CMB1)"),
      each with a one-line summary ("N of M found"); clicking a topic toggles
      an `.editor-row` directly below it (several topics can be open at
      once, the same "independently toggleable" model a Setlist row's own
      Color/Comment/Volume sections use) holding that topic's detail --
      chunk badges (struck-through if missing), or (Program/Combi banks,
      reshaped again the same day, see below) a bank button grid. Each
      topic's detail is wrapped the same way a multi-section accordion
      would be, so a second section (e.g. an "Initialize bank" action once
      Phase 1.5 has real ground truth) can be added to a topic later
      without restructuring. The file-order caveat is still shown directly
      in the pane, and the "N of 20/14 expected banks found" summary still
      deliberately stops short of naming which specific bank(s) are
      missing (see the finding above).
    - **Bank detail reshaped a second time, same day**: from a table (name/
      Present-or-Missing/type/record-count columns) to the exact same
      bank-filter-button grid Programs/Combis already use
      (`library.js`'s `renderBankFilterRow()`: `.bank-filter-row` CSS
      grid, `.button.is-small.bank-filter-button`, "Bank (Engine)" caption
      when an engine type is known) -- reusing that look wholesale rather
      than a bespoke table, per direct request. Semantics differ from the
      real filter buttons on purpose, since this isn't a filter: **Present
      = deactivated** (`disabled`, plain -- nothing to do, the bank
      already has data) vs **Missing = enabled** (`is-warning`, a real
      clickable button) as a preview of a future "click to initialize this
      bank" action -- NOT YET implemented (Phase 1.5, still blocked on
      real Init-Program bytes), so today's click just calls the app's
      existing `showToast()` ("Initializing ... isn't built yet -- see
      STATE.md") rather than doing nothing silently or pretending to work.
      Buttons are grouped into 4 rows via `internals.js`'s new
      `bankNameRows()` (a small regex split, not new bridge/backend data):
      I-* (INT letter banks), the lone G(d) bank, U-* (single-letter USER
      banks), U-** (double-letter USER banks) -- a row that's empty for a
      given category (Combis have no G(d), no double-letter USER banks,
      see `docs/README.md` §5.1/§5.2) is dropped rather than shown blank.
      Record counts/columns dropped entirely as part of the cleanup --
      the caption already carries what mattered (name + engine). Removed
      the now-dead `.internals-status-present`/`.internals-status-missing`
      CSS this replaced; no other new CSS needed beyond one thin
      `.internals-bank-groups` spacing rule, everything else reuses
      `.bank-filter-row`/`.bank-filter-button`/Bulma's `.is-warning` as-is.
    - `frontend/mock_bridge.js` gained a fake `getDatasetInternals` returning
      only 2 of 20 Program banks / 2 of 14 Combi banks and a partial
      top-level-chunk list on purpose, so the "N of M found" shortfall
      messaging has something real to exercise in mock mode too.
    - Verified: `pcg_file_test.cpp` extended (`topLevelChunkTags()`'s exact file
      order, `programBankInfo()`/`combiBankInfo()`'s record counts per the
      synthetic fixture), deliberately broken then restored per this project's
      standard practice; full app + test target build clean; every touched JS
      file `node --check`ed. Both same-day reshapes (Entry-row, then the
      bank-button-grid) re-verified all of the above (backend untouched by
      either) plus a CSS brace-balance check and a `curl` confirmation
      against the running static dev server that it was actually serving
      the new `internals.js`/`style.css`.
    - **Deferred, explicitly out of scope this round** (per direct agreement):
      initializing a missing bank/patch with default data. Writing a default
      patch into an EXISTING bank's already-present-but-unused slot is a
      same-class operation to `copyProgramFrom()` (a fixed-size in-place
      overwrite) and could follow once real "Init Program" byte data is pulled
      from the project owner's own Kronos, the same ground-truth-first approach
      as everything else this session -- not attempted yet since that reference
      data doesn't exist in this project yet. Creating an ENTIRELY MISSING bank
      (a new chunk that doesn't exist in the file at all) is a different, much
      riskier class of operation -- it would need to grow the file and update
      chunk-size fields up the whole hierarchy, and directly collides with
      several still-unconfirmed structural facts (the `used`/count header
      field's real meaning, the unexplained 4-byte chunk prefix, and the
      file-header checksum flag) -- explicitly agreed to treat as its own later,
      dedicated research phase, not bundled into this one.
  - **Combi-usage counting extended from 4 to 8 confirmed banks, 2026-08-08**:
    prompted by a direct question -- "why only INT-A..D, we have all banks
    identified" -- while explaining why Duplicates showed "n/a" for Combi
    references outside that range (blind spot #18 above). The two "identified"
    senses turned out to be different: bank *names* (file-order index ->
    "USER-D") are fully identified for all 20/14 banks (§5.2/§5.1); a Combi
    Timbre's own *raw bank code* -- a completely separate number space -- is
    independently confirmed for only 8 individual banks (docs/README.md §6.2:
    INT-A..D = 0..3, plus USER-A=17/USER-D=20/USER-F=22/USER-AA=24). Checking
    the code turned up a real, fixable gap: `timbreBankName()` already knew
    all 8 codes, but `isConfirmedTimbreProgramBank()` only covered 4 (INT-A..D,
    where the file-order index and raw code happen to coincide) -- the other 4
    confirmed codes existed but were never wired into the Combi-usage-counting
    path at all.
    - `src/kronos/PcgFile.cpp`: replaced the three separate, easily-
      desynced encodings of this data (a switch in `timbreBankName()`, a
      range check in `isConfirmedTimbreProgramBank()`, and two ad hoc raw-
      code/file-order-index conflations in `combiUsagesForProgram()`/
      `combiUsageCounts()`) with one shared table, `kConfirmedTimbreBanks`
      (8 `{programBankIndex, rawBankCode, name}` rows), plus two small
      translation helpers (`confirmedTimbreCodeForProgramBank()`/
      `programBankForConfirmedTimbreCode()`, internal linkage, not in the
      header -- nothing outside this file needs the raw translation itself).
      `combiUsagesForProgram(bank, number)` now translates the file-order
      `bank` to its confirmed raw code before comparing against
      `TimbreRef::rawBankCode`, instead of assuming they're the same
      number (only true for INT-A..D); `combiUsageCounts()` translates the
      other direction. `isConfirmedTimbreProgramBank()`'s public signature/
      semantics are unchanged, so `EditorBridge.cpp` and the frontend
      needed no changes on the backend-boundary side.
    - `frontend/library.js`: found the *same* raw-code/file-order-index
      conflation independently re-implemented on the frontend, in
      `formatTimbreRef()` (the expanded Combi row's per-Timbre engine-
      type/Program-name lookup) -- its own `isConfirmedTimbreProgramBank()`
      mirror was a plain `bank <= 3` check applied directly to
      `t.rawBankCode`, and `getProgramBankType(t.rawBankCode)`/
      `programs.find(p => p.bank === t.rawBankCode ...)` both compared a
      raw Timbre code directly against file-order Program bank data. Fixed
      the same way as the backend: a mirrored `CONFIRMED_TIMBRE_BANKS`
      table plus `programBankForConfirmedTimbreCode()`, used throughout
      `formatTimbreRef()`. Every "INT-A..D so far" tooltip/note string in
      this file (Programs table's #CMB `n/a` tooltip, the Program usage
      panel's Combi-unavailable note, the expanded Combi row's own note,
      the new Duplicates button comment) updated to say "8 individually-
      verified banks (INT-A..D, USER-A/D/F/AA)" instead.
    - Verified: extended `tests/pcg_file_test.cpp`'s synthetic CBK1 fixture
      with a second Timbre (raw code 20 = USER-D, file-order index 11 --
      deliberately one of the 4 newly-wired banks, not one of the original
      INT-A..D range, so the test actually exercises the translation
      rather than a case where it's a no-op) plus new assertions:
      `isConfirmedTimbreProgramBank(11)` true, `(4)` (INT-E, unconfirmed)
      false, `combiUsagesForProgram(11, 7)` finds the synthetic reference,
      `combiUsagesForProgram(4, 7)` returns nothing rather than a guess,
      `combiUsageCounts()[11][7] == 1`. Deliberately broken (expected count
      2 instead of 1) then restored, confirming the test actually catches a
      real regression; full app + `pcg_file_test` build clean.
  - **Chunk header fixed to its real 12-byte shape, 2026-08-08 -- resolves
    this project's oldest open question, and the PRG1/CMB1/GLB1
    Internals-pane bug from the same day**: prompted by the project owner
    adding a large new folder of official Korg SysEx parameter
    documentation (`docs/external/KORG/`) and asking to cross-reference it
    against this project's own findings. `SetList.txt`'s per-field byte
    offsets, once compared precisely (a hand-arithmetic attempt got this
    wrong twice before switching to an actual script -- worth remembering:
    verify offset math by running it, not by reasoning through it), showed
    a *consistent* +4 discrepancy against every one of this project's own
    confirmed SBK1 field offsets. Separately,
    `docs/external/Synthify-Kronos-PCG-File-Structures.xlsx` -- already in
    this project's references, just not previously read for this detail --
    turned out to directly state the answer: "A Chunk has a header
    consisting of three 4-byte sized objects... TAG1... size... dwX...
    Data." This project's own model had the extra 4-byte field on the
    *wrong side* of the tag: not an ambiguous prefix sometimes appearing
    *before* the tag (the old `readChunk()`'s "try position 0, then
    position+4" logic), but a fixed, unambiguous third header word
    (`dwX`, meaning still unknown) always *after* `size` and *before*
    content. Every chunk's header is 12 bytes, full stop, no ambiguity.
    - `src/kronos/PcgFile.cpp`: `readChunk()` rewritten around the real,
      unambiguous structure -- tag always at `pos`, `contentStart` always
      `pos+12` (was `p+8`, missing `dwX`), the old dual-position trial
      loop removed entirely. `collectChunks()`'s and `topLevelChunkTags()`'s
      own loop guards (`pos+8 <= end`) updated to `pos+12` to match.
    - **Same root cause as the PRG1/CMB1/GLB1 Internals-pane bug reported
      this session**: a `contentStart` computed 4 bytes short silently
      self-corrected often enough in a *deep, unscoped* recursive search
      (finding `MBK1`/`PBK1` anywhere in the file, regardless of exact
      boundaries) that it went unnoticed, but a *strict* top-level walk
      (`topLevelChunkTags()`) has zero tolerance for drift -- one short
      chunk desyncs every sibling after it. This is why Program/Combi bank
      data always displayed correctly while the Internals pane's chunk
      badges did not.
    - `tests/pcg_file_test.cpp`: **the fixture itself was part of the
      problem** -- it built a flat sibling list (`SDB1`, `SBK1`, `PBK1`,
      ... all top-level, no nesting at all), which is not how a real file
      is shaped and never exercised the bug. Reshaped to the real
      hierarchy (`PCG1 > SLS1 > (SDB1, SBK1)`, `PCG1 > PRG1 > (PBK1,
      MBK1)`, `PCG1 > CMB1 > (CBK1)`, with a `DIV1` sibling first) --
      `appendChunk()` also gained the real `dwX` field (previously an
      8-byte tag+size-only header). `topLevelChunkTags()`'s own expected
      value updated from the old flat 5-tag list to the real 4 top-level
      tags (`DIV1`, `SLS1`, `PRG1`, `CMB1`) -- SDB1/SBK1/PBK1/MBK1/CBK1 are
      correctly nested now, not top-level.
    - **Verified two ways, not just "tests pass"**: (1) the reshaped
      fixture, decoded through the *old* buggy 8-byte-header math (a
      temporary revert), doesn't just fail one assertion -- it fails to
      load *at all* ("No SDB1 chunk found"), since the nested search never
      finds SDB1 inside SLS1's now-misaligned bounds. Confirms this was a
      real, load-bearing bug, not a cosmetic offset difference, and that
      the new fixture is a genuine regression test for it, not just a
      differently-shaped one. (2) The project owner's own "child sizes sum
      to the parent" description of the format was verified structurally:
      each wrapping chunk's declared `size` in the fixture is
      *constructed* as the exact byte-length of its children (via nested
      `appendChunk()` calls building up `sls1Content`/`prg1Content`/
      `cmb1Content` before wrapping each in its own chunk), and the whole
      file round-trips correctly end to end -- proving the parser
      correctly relies on that invariant, not just that the invariant is
      plausible.
    - Verified: deliberate-break-then-restore on the reshaped fixture
      (above); full `cmake --build build` (all three targets) clean;
      `ctest` clean.
  - **SECOND bug found the same day, immediately after attempting to
      commit the above**: the project owner interrupted a `commit and
      push` request -- "the app enters an infinity loop reading
      setlist_test_2.PCG" (a real 36MB file, located just outside the repo,
      first real-file access this project has had in this environment).
      This turned out to be exactly the right call: it caught a second,
      independent, more consequential bug the synthetic-only test suite
      could never have caught, because the synthetic fixture encoded the
      *same wrong assumption* on both the write and read side, so it could
      never disagree with itself.
    - **Root cause**: SDB1/SBK1/PBK1/MBK1/CBK1's own record-array header
      (the "count/numRecords/bytesPerRecord" 3-field, 12-byte shape this
      project had documented since early on) never actually existed as a
      3-field header -- it's 2 fields, 8 bytes (`numRecords`,
      `bytesPerRecord`), confirmed directly against the real 36MB file: at
      the corrected chunk-content boundary, SDB1's first 8 bytes read
      128/3612 (exactly the real numSetlists/bytesPerSetlist), and the
      real Set List name "Preload Set List" begins exactly 8 bytes in, not
      12. The old, wrong 3-field reading had been silently
      **canceling out** against the OTHER bug (the chunk-header fix above)
      the whole time: reading 4 bytes too early at the chunk level, then
      discarding an extra bogus 4-byte field at the record level, landed
      on the exact same real absolute byte position either way -- which is
      *why* real Set List names, Program names, and everything else this
      project had already verified against real data kept working despite
      neither individual assumption being correct. Fixing only the
      chunk-level bug broke that accidental cancellation and exposed this
      one. Confirmed via a battery of hand-written diagnostic probes
      against all 5 real `.PCG` files available locally (not committed,
      `.gitignore`'d, built ad hoc with `clang++` per this project's
      standard practice) -- after this fix: `programBankInfo()`=20 banks,
      `combiBankInfo()`=14 banks, `setlists()`=128, `programs()`=2,560,
      `combis()`=1,792, all exactly matching real physical maximums (before
      the fix: 4 banks, 6 banks, 0 setlists, 19,840/46,860 -- garbage,
      inflated by the same record-boundary corruption `collectChunks()`'s
      recursive-into-everything search was quietly absorbing). Load time
      for the 36MB file: ~3 seconds, not infinite -- the reported "infinite
      loop" was this corrupted, ever-growing false-positive chunk search,
      not a true non-terminating loop.
    - `topLevelChunkTags()` needed one more, separate fix on top: `PCG1`
      itself is a real chunk starting at byte 16 (its own declared size
      exactly spans the rest of the file, confirmed against real data),
      not implicitly consumed by the 16-byte file header as this project
      had assumed without ever checking -- `topLevelChunkTags()` now reads
      `PCG1` first and returns *its* children.
    - `src/kronos/PcgFile.cpp`: all four record-array header readers
      (SDB1, SBK1, the PBK1/MBK1 program-bank loop, CBK1) changed from
      reading a discarded field + two more at `contentStart+4`/`+8` (with
      `recordsStart`/`setlistsStart` = `contentStart+12`) to reading two
      fields directly at `contentStart+0`/`+4` (`recordsStart`/
      `setlistsStart` = `contentStart+8`). A consistent, still-unexplained
      4 bytes are left over between the last record and each chunk's own
      declared end in every case checked -- flagged, not guessed at.
    - `tests/pcg_file_test.cpp`: every synthetic bank/setlist header
      (`sdb1`, `sbk1`, `pbk1BankA`, `pbk1BankB`, `cbk1BankA`) had its
      bogus leading `pushU32BE` removed to match; `buildSyntheticPcgFile()`
      also gained a real `PCG1` wrapper around everything (previously
      `DIV1` sat directly at byte 16, which is not what a real file does).
      Deliberately broken (a wrong offset) then restored, confirming the
      test suite actually catches this class of bug now.
    - **Two more fixes landed the same round, from the project owner's own
      direct byte-level walkthrough of `SetList.txt`'s bit table** (not
      more cross-referencing guesswork -- explicit confirmation, field by
      field): (1) SBK1's Performance Type (byte +12) is genuinely 2 bits
      (`kSbkTypeMask = 0x03`), not the 1 bit this project originally
      decoded -- `isProgram` is now `(typeColor & 0x03) == 1`, correct for
      Program/Combi either way (bit 0 alone already distinguished those
      two) and no longer silently misreads the unused value 3 as Program;
      Song (value 2) still isn't separately represented anywhere in the
      app (nothing needs it yet), but the *read* is now correct rather
      than accidentally-right. (2) Comment is confirmed **512 bytes**, not
      524 (`kSbkRecordSize - 18`, this project's original assumption) --
      fixed in both `PcgFile.cpp`'s `readComment()`/`kSbkCommentMaxLength`
      and, critically, `frontend/components/kronos/setlist-comment.js`'s
      encoder, which previously both allowed writing up to 523 characters
      *and* zero-filled all the way to the record's own end (542) on every
      edit -- both of which would have clobbered the record's trailing 12
      bytes (of genuinely unknown purpose) on a sufficiently long comment.
      This was flagged as a real write-corruption risk earlier the same
      day and deliberately left unfixed pending exactly this kind of
      direct confirmation, per this project's standing rule.
    - `tests/pcg_file_test.cpp`'s synthetic fixture also needed a related
      fix: `garbageBit1`, a parameter that deliberately poked byte +12's
      bit 1 to prove Color-decoding correctly ignores bits it doesn't own,
      no longer makes sense now that bit 1 is a real part of Type --
      removed (every bit of that byte is now owned by something real, so
      there's nothing left to garbage-test there). Deliberately broken
      (wrong Type comparison) then restored to confirm the test still
      catches a real regression.
    - `frontend/components/kronos/setlist-comment.test.js`: one assertion
      ("shortening the comment zero-fills the rest of the record") updated
      to check only through the real 512-byte comment boundary, not the
      full 542-byte record -- the trailing 12 bytes are deliberately left
      untouched now, not zeroed.
    - Verified end to end: full `cmake --build build` clean, `ctest`
      clean, the headless `setlist-comment.test.js` clean, and a final
      re-run of the ad hoc real-file probes against all 5 locally-available
      `.PCG` files (36MB and 9.3MB alike) confirming correct, fast
      (`<3.1s` for the 36MB file), sane results throughout.
    - **Explicitly deferred, per direct agreement**: now that raw-byte
      editing (Setlist Color/Volume/Comment, Program copy) is on solid
      ground again, reordering/swapping entries -- Setlist slots first,
      later Programs/Combis -- looks straightforward as a *fixed-size,
      in-place* operation (swap two records' raw bytes, update whichever
      reference bytes point at them), no file growth involved. Deliberately
      not started: the explicit agreement is to keep proving out "edit what
      we already have" (this round's fixes included) before taking on
      anything more complex, matching this project's existing "small
      iterations, no guessing" discipline rather than a scope change.
  - **Setlist reorder/copy-over + multi-select groundwork (2026-08-08, built)**:
    the "reordering/swapping entries" work flagged as deliberately deferred just
    above is now built, following an explicit RFC (agreed in full): dropping one
    Setlist row directly ON another is "copy over" (target becomes an exact copy
    of source -- name + params -- source unchanged, a genuine behavior CHANGE
    from the old swap-based `moveEntry()`); dropping BETWEEN two rows (or before
    the first / after the last) is "insert with shift" -- a pure rotation of the
    same 128 slots, safe by construction since nothing is added or removed.
    - `src/kronos/PcgFile.h`/`.cpp`: new `nameRecordBytes()`/`putNameRecordBytes()`
      (the 28-byte SDB1 name record -- confirmed to live in a completely separate
      chunk/stride from a slot's SBK1 params, so any relocate/copy must move both
      together or the result mismatches a slot's displayed name against its
      actual content) and `reorderSong()` (relocates one slot's name AND params
      together within one Set List, shifting the intervening range by one -- one
      native call, not N bridge round-trips, matching this project's two-tier
      data-flow model for bulk operations). `moveEntry()` (the old same-Setlist
      swap, in-memory-struct-only, predating the real byte-level write path) is
      deleted, fully superseded.
    - `src/bridge/EditorBridge.h`/`.cpp`, `src/main.cpp`: `getNameRecordBytes()`/
      `putNameRecordBytes()`/`reorderSongEntry()` bound the same way as the
      existing Song-record pair; `copyEntry()` kept, now documented as cross-
      Set-List-only (same-Set-List dragging no longer uses it).
    - `frontend/pane.js`: `dropZoneForEvent()` reads a row's cursor Y-position
      (top/bottom 30% = insert before/after, middle = copy over) and drag
      handlers toggle `.drop-target` (the "copy over" full-row highlight,
      `background`/`outline` in `frontend/style.css`) for "on".
    - **Bug found during manual testing (2026-08-08), same day this landed**:
      the insert before/after gesture's visual feedback never actually
      rendered -- only "copy over" did. Root cause: the insert indicator was
      originally a `box-shadow` on the target `<tr>` itself
      (`.drop-target-before`/`.drop-target-after`), and Bulma's `.table` sets
      `border-collapse: collapse`, under which no current browser paints a
      `box-shadow` on a table row at all -- a real table-rendering
      limitation, not a specificity or z-index bug (confirmed by the "on"
      gesture working fine, since `background`/`outline` don't have this
      limitation). Fixed by replacing the `<tr>` box-shadow entirely with one
      floating `<div class="drop-indicator">` per Setlist panel, absolutely
      positioned against `.entries-scroll` (now `position: relative`) and
      moved via `showDropIndicator(tr, zone)`/`hideDropIndicator()` to
      straddle the boundary above/below the current insert target -- scrolls
      naturally with the table since it's a sibling inside the same
      scrolling element, and sidesteps table-layout quirks entirely rather
      than fighting them. `.multi-selected` (added the same day, see below)
      had the identical latent bug -- its own box-shadow was moved from the
      `<tr>` onto `tr.multi-selected > td:first-child` instead, since
      box-shadow on a `<td>` *does* render correctly under border-collapse
      (the standard workaround for this class of bug).
    - Ctrl/Cmd+click toggles a row into a `multiSelected` Set independent of
      `openPanels` (which rows have an editor open) -- styled via a new
      `.multi-selected` class (a left-edge inset box-shadow using a new
      `--multiselect-accent` token, deliberately additive rather than
      fighting `.is-selected`'s background or `.drop-target`'s outline for
      the same CSS property, since a row can carry any combination of the
      three states at once). Per direct agreement, multi-select has no
      further action wired to it yet ("2.X no further action right now" in
      the RFC) -- purely visual groundwork for a future bulk action.
    - `frontend/app.js`: `onDropEntry()` rewritten to dispatch on `zone`/
      `sameList` -- insert uses the one native `reorderSongEntry()` call; copy-
      over reads+writes both the params and name records (`Promise.all()` pairs)
      since they're separate SBK1/SDB1 records; cross-dataset stays blocked
      (unchanged reasoning, see the function's own doc comment) and cross-
      Set-List-same-dataset stays on the older in-memory-only `copyEntry()` --
      true cross-Set-List insert would have to evict something from an already-
      full 128-slot destination to make room, a real data-loss question still
      deliberately not tackled.
    - `frontend/mock_bridge.js`: `moveEntry()` fake removed; `reorderSongEntry()`
      fake mirrors the real splice-and-reindex semantics; `getNameRecordBytes()`/
      `putNameRecordBytes()` fakes synthesize/parse a 28-byte buffer from a mock
      entry's `label` field (mirroring the existing `makeFakeSlotBytes()` pattern
      for the 542-byte params record).
    - Backend verified: full `cmake --build build` clean, `ctest` clean
      (`PcgFile.cpp`'s `reorderSong()`/name-record methods have their own
      deliberate-break-then-restore coverage in `tests/pcg_file_test.cpp`).
      Frontend verified: `node --check` on every touched file, CSS brace-balance
      checked, and confirmed working in the real app (2026-08-08, manual test
      against a real backup) -- copy-over and insert both write real bytes, as
      intended (see EditorBridge.h's own class doc comment for the one
      exception: cross-Set-List dragging still uses the older in-memory-only
      `copyEntry()`, and nothing is written to disk until an explicit
      `saveFileAs()` call).
  - **"Copy all to opposite" + swap-panes button (2026-08-08, built)**: further
    UI polish requested the same day, once the reorder/copy-over gestures above
    were confirmed working end to end.
    - `src/kronos/PcgFile.h`/`.cpp`: new `copySetlist(srcSetlistIndex,
      dstSetlistIndex)` -- overwrites every one of a destination Set List's 128
      slots (both name and params records) with a source Set List's, reusing
      `nameRecordBytes()`/`songRecordBytes()`/`putNameRecordBytes()`/
      `putSongRecordBytes()` internally (same pattern as `reorderSong()`), one
      native call rather than 256 bridge round-trips. Same-file only (both
      indices are Set Lists within the one already-open `PcgFile`); leaves the
      destination Set List's own name (`Setlist::name`) untouched, only the
      song slots move. `src/bridge/EditorBridge.h`/`.cpp`, `src/main.cpp`:
      `copySetlistEntries(datasetId, srcSetlistIndex, dstSetlistIndex)` bound
      the same way as the other Setlist bridge methods.
    - `tests/pcg_file_test.cpp`: the synthetic fixture gained a SECOND Set
      List ("Gig Setlist", one pre-existing song "Old Song") specifically so
      `copySetlist()`'s test can tell a real overwrite apart from a no-op --
      deliberately broken (skipped the params write) then restored, confirming
      the new test genuinely catches a regression, not just checking the happy
      path.
    - `frontend/pane.js`: the Setlist panel's "Showing ..." row (`.setlist-
      info-row`, `style.css`) gained a "Copy all to opposite" button, enabled
      only when this pane and the opposite pane share one dataset with two
      different Set Lists selected. Reading the OPPOSITE pane's current
      dataset/Set List needed a small addition: `datasets.js` gained
      `notifyPaneSelectionChanged()`/`onPaneSelectionChanged()`, a plain
      broadcast (deliberately no stored state of its own, unlike the datasets-
      list cache above it in the same file) that tells a pane WHEN to re-read
      its sibling's already-authoritative state (`getCurrentDatasetId()`, and
      a new `getCurrentSetlistIndex()` exposed the same way) -- fired whenever
      any pane's own dataset/Set List selection changes, whether or not that
      pane owns the button being recomputed.
    - `frontend/app.js`: `onCopySetlist(source, target)` (mirrors
      `onDropEntry`'s shape) calls the new bridge method and refreshes
      whichever pane(s) show the destination dataset. A new `swapPanes()`
      physically reorders the two `<section class="pane">` DOM elements
      inside `.panes` -- deliberately NOT a data/state swap, since a pane's
      `paneId` ("A"/"B") is only ever used as a label (status log prefixes,
      the cross-pane slot-edit lock), never to decide which side is which, so
      moving the element is sufficient.
    - `frontend/style.css`: the swap button is `position: absolute` against
      `.panes` (now `position: relative`), floating over the 1px seam between
      the two panes rather than being a real third flex item -- keeps both
      panes' widths exactly as Bulma's `.columns` grid already computes them.
    - `frontend/mock_bridge.js`: `copySetlistEntries()` fake added, same
      "overwrite dst's slots while keeping dst's own position/index" contract
      as the real bridge method.
  - **Drag-and-drop insert indicator bug found during the SAME manual test
    session (2026-08-08)**: copy-over (drop directly on a row) visibly
    worked; insert (drop between two rows) showed no feedback at all. Root
    cause: the insert indicator was a `box-shadow` on the target `<tr>`
    itself (`.drop-target-before`/`.drop-target-after`), and Bulma's
    `.table` sets `border-collapse: collapse` -- no current browser paints a
    `box-shadow` on a table row under `border-collapse` at all, a genuine
    table-rendering limitation, not a specificity or z-index bug (confirmed
    by copy-over working fine, since its `.drop-target` uses
    `background`/`outline` instead, neither of which has this limitation).
    Fixed by replacing the `<tr>` box-shadow entirely with one floating
    `<div class="drop-indicator">` per Setlist panel, absolutely positioned
    against `.entries-scroll` (now `position: relative`) and moved via
    `showDropIndicator(tr, zone)`/`hideDropIndicator()` (`frontend/pane.js`)
    to straddle the boundary above/below the current insert target --
    scrolls naturally with the table since it's a sibling inside the same
    scrolling element, sidestepping the table-layout limitation entirely
    rather than fighting it. The `.multi-selected` marker (added the same
    day) had the identical latent bug -- moved from the `<tr>` onto
    `tr.multi-selected > td:first-child` instead, since box-shadow on a
    `<td>` *does* render correctly under `border-collapse` (the standard
    workaround for this class of bug).
  - **Drag-and-drop code reuse assessed, not (yet) refactored (2026-08-08)**:
    asked whether the Setlist drag-and-drop code (`pane.js`) is shared with
    or reusable by Programs' own drag-and-drop (`library.js`, copy-a-Program-
    onto-another-slot). It isn't -- the two are independently hand-written,
    each with its own `dragstart`/`dragend`/`dragover`/`dragleave`/`drop`
    listener set, own `dataTransfer` JSON payload shape, and own accept/
    reject rules (Setlist's is genuinely more complex: 3 drop zones via
    `dropZoneForEvent()` plus the floating indicator above; Programs' is a
    single zone, engine-type-gated). A shared `frontend/dragdrop.js` helper
    extracting just the common listener-wiring boilerplate (JSON
    serialize/parse, `drop-target` class toggling, the `preventDefault`/
    `dropEffect` dance) is a real, low-risk option if this need comes up a
    third time -- explicitly not built now, per this project's "don't build
    for hypothetical future needs" rule: two call sites don't yet justify the
    abstraction, and the domain-specific parts (zone detection, accept
    rules, indicator positioning) would stay per-caller regardless, so the
    win is real but modest.
  - **A-Z/Z-A sort buttons, FIRST VERSION (2026-08-09, built, then corrected
    the same day -- see below)**: two buttons beside the filter input,
    `frontend/pane.js`, toggle-style (`.is-link`) -- sorted the visible rows
    by name ascending/descending, or back to physical slot order if the
    active one was clicked again. Built display-only, exactly like the
    existing filter: a `sortOrder` (`null`/`"asc"`/`"desc"`) applied in
    `renderRows()` on top of the filtered list, right before rendering --
    never touched `entries`/any entry's own `.index`. This turned out to be
    the wrong call (see the next entry) and has been fully replaced, not
    kept alongside the new behavior.
  - **Slot-position-IS-order confirmed directly against `docs/external/
    KORG/SetList.txt` (2026-08-09)**: Korg's own SysEx documentation lays
    out Slot 0 at a fixed record offset and states Slots 1-127 simply
    follow at the same stride, "and the IDX is assigned to 1 ~ 127" -- no
    separate order/pointer field exists anywhere in the structure. Added to
    `docs/README.md`/`docs/content/format/index.md` §3.2 as confirmed
    ground truth: a slot's number IS its physical record position, nothing
    else stores or could store it.
  - **Sort buttons corrected to a real reorder (2026-08-09, same day)**: the
    first version's reasoning was backwards. Since a slot's number IS its
    physical position (previous entry) and nothing else stores order, a
    *display-only* sort can't mean anything to a real Kronos -- it never
    even sees the app's UI state, only the file's bytes. The project owner
    caught this directly: "DAD and SORTING is NOT [display-only]. It has to
    change the underlying rawdata." Rebuilt accordingly:
    - `src/kronos/PcgFile.h`/`.cpp`: new `sortSetlist(setlistIndex,
      ascending)` -- computes the target alphabetical order for all 128
      slots (empty-named slots always trail last, regardless of direction,
      matching the old display convention), snapshots every slot's raw
      name+params bytes up front (unlike `reorderSong()`'s single-range
      shift, this touches all 128 slots in a non-contiguous new order, so
      there's no safe read-then-write direction without snapshotting
      first), then writes them back via the existing
      `putNameRecordBytes()`/`putSongRecordBytes()`. Comparison is a plain
      byte-wise `std::string` compare, not locale-aware -- fine for the
      ASCII-range names seen so far, flagged in the doc comment as a
      known simplification.
    - `src/bridge/EditorBridge.h`/`.cpp`, `src/main.cpp`:
      `sortSetlistEntries(datasetId, setlistIndex, ascending)` bound the
      same way as the other Setlist bridge methods; a small `boolArg()`
      helper added alongside the existing `stringArg()`/`intArg()`.
    - `frontend/pane.js`: the entire client-side `sortOrder`/in-`renderRows()`
      sort is gone. The two buttons now call `sortSetlistEntries()` and
      `refreshEntries()` (re-reading the file's now-actually-different
      physical order) -- a one-shot immediate action, not a toggleable view
      mode, so the `.is-link` active-state styling is gone too (nothing to
      indicate as "currently active" once there's no persistent mode).
      Button tooltips rewritten to say plainly: real bytes, immediate,
      whole Set List, no undo.
    - `tests/pcg_file_test.cpp`: new `sortSetlist()` coverage on the
      existing 2-song fixture (ascending puts "Song One" before "Song
      Zero", empties trail; descending restores the original order, which
      doubles as the test's own cleanup) -- deliberately broken (flipped
      the empty-slots-trail-last comparison) then restored, confirming 8
      real assertions catch the regression.
    - `frontend/mock_bridge.js`: `sortSetlistEntries()` fake added (there
      was nothing to replace here -- the old display-only sort never called
      the bridge at all).
    - `docs/README.md`/`docs/content/format/index.md` §3.2, and the new
      Hugo User Guide's Setlist section, corrected to describe the sort
      buttons as a real, immediate, whole-Set-List rewrite -- explicitly
      flagged as a correction of the same day's earlier (wrong) claim that
      they were necessarily display-only.
    - Verified: full `cmake --build build` clean, `ctest` clean (including
      the deliberate-break-then-restore above), `node --check` clean on
      every touched frontend file.
  - **"Save As..." button (2026-08-09, built)**: written just before the
    sort-buttons correction above, to give the project owner a way to
    actually write a dataset's current in-memory bytes to disk for
    real-hardware testing. The underlying write path (`PcgFile::save()`,
    `EditorBridge::saveFileAs()`) already existed from earlier real-
    hardware validation work, but was only reachable via a devtools
    console script -- no UI. Added `EditorBridge::saveFileDialog(datasetId)`
    (`src/bridge/EditorBridge.h`/`.cpp`, bound in `src/main.cpp`): shows a
    native Save dialog (`NativeFileDialog.h`'s `showSaveFileDialog()`,
    already existed but was unused until now) pre-filled with the
    dataset's own filename, then writes to wherever the user picks via the
    same `PcgFile::save()` `saveFileAs()` already uses -- `saveFileAs()`
    itself is untouched, kept as the lower-level path-supplied building
    block. `frontend/pane.js`: a "Save As..." button next to each pane's
    own dataset selector (per-pane, not global, since each pane already
    knows exactly which dataset it's showing -- no ambiguity about which
    dataset to save the way a single global button would have).
    `frontend/mock_bridge.js`: a `window.saveFileDialog()` fake mirroring
    `openFileDialog()`'s existing `window.prompt()` stand-in pattern, since
    mock/browser mode can't write a real file either. Full `cmake --build
    build` clean, `ctest` clean (no C++ test added specifically for this --
    it's a thin dialog/path wrapper around the already-tested
    `PcgFile::save()`/`saveFileAs()`); `node --check` clean on both touched
    frontend files.
  - **`docs/README.md` demoted from a second full copy to a short pointer
    (2026-08-09)**: raised by the project owner directly -- "Maybe we should
    get rid of the README.md over time?" -- after noticing the dual-
    maintenance cost (`docs/README.md` and `docs/content/format/index.md`
    kept in sync by hand, per this file's own "Keep the docs in sync"
    convention) had already caused real drift: a stale, dangling half-
    paragraph in `docs/README.md`'s Transpose section (an old edit that
    removed a sentence but left its tail behind) that `docs/content/
    format/index.md` didn't have, found while reconciling the two before
    this change. Decided against deleting `docs/README.md` outright (it's
    the one file-format reference someone browsing the repo on GitHub, not
    the Hugo site, can read without leaving) or Hugo's `module.mounts`
    (would need frontmatter in the mounted file, which would show as raw
    text at the top of the GitHub-rendered README) -- settled on a short
    pointer instead. `docs/content/format/index.md` is now the single
    canonical file-format reference; `docs/README.md` is a short redirect
    plus a table of contents. Every in-repo citation of `docs/README.md`
    (§N.N section references throughout `src/`, `frontend/`, `tests/`,
    `tools/`, plus `CLAUDE.md`, the top-level `README.md`, `docs/external/
    README.md`, `docs/references/README.md`, `docs/HUGO-SITE.md`) updated
    to point at `docs/content/format/index.md` instead -- except STATE.md's
    own historical entries below this point, deliberately left as-is (a
    changelog records what was true at the time; the section numbers
    themselves didn't change, only which file holds them). Verified: full
    `cmake --build build` clean, `ctest` clean, `node --check` clean on
    every touched frontend file, and a local `hugo --minify` build clean
    (confirms the Hugo site itself, e.g. the `format`/`overview` pages'
    cross-links, wasn't broken by any of the file moves).
  - **Shared Hugo scaffold extracted to `github.com/jens-goes-mad/
    DIY-HUGO-SCAFFOLD.public` (2026-08-10)**: raised by the project owner --
    `DIY-KRONOS-EDITOR`, `DIY-MIDI-METRONOME.public`, and
    `DIY-PEDALBOARD.public` all run the same hand-copied Hugo/Stack-theme
    site scaffolding (`docs/HUGO-SITE.md` already said as much). Direct
    diffing confirmed `layouts/`, `assets/scss/`, `assets/icons/`, and
    `config/_default/{module,permalinks,markup}.toml` were byte-identical
    or near-identical across all three, and -- concretely -- that this
    session's own `docker-compose.yml` local-dev `baseURL` bugfix was
    already missing, unfixed, in the other two repos' copies. Planned
    (see the plan file this session used) and executed as a real Hugo
    Module import, the same mechanism already used for the Stack theme
    itself:
    - New repo populated from Kronos's own (already-bugfixed) copies of
      the shared pieces, plus a reference-only `docker-compose.yml`/
      `config/_default/{module,permalinks,markup}.toml` (Hugo's own config
      loading never reads `config/` from an imported module, only the main
      project -- these can't be truly shared the way layouts/assets can,
      just copied by hand and kept small).
    - **A real, verified-not-assumed gotcha caught before it shipped**:
      importing the scaffold module *after* the theme (the intuitive
      "site overrides come last" ordering) silently fell back to the
      theme's own placeholder `custom.scss` and default layouts for every
      overlapping file, with no build error at all -- caught only by
      diffing the built `public/` output before/after against a saved
      baseline, not by reading Hugo's docs. The fix, confirmed by that
      same diff going clean: import the scaffold *before* the theme.
      Hugo resolves an overlapping file to whichever import was declared
      *first*. Documented prominently in both `config/_default/
      module.toml`'s own comment and the scaffold repo's README, since
      it's the opposite of what anyone would guess.
    - `DIY-KRONOS-EDITOR/docs/go.mod` now imports the scaffold module
      (`hugo mod get`, pinned to a real pseudo-version); Kronos's own
      `layouts/`, `assets/scss/`, `assets/icons/` deleted, `config.toml`/
      `params.toml`/`menu.toml`/`content/`/`docker-compose.yml` kept local
      (genuinely per-project). `docs/HUGO-SITE.md` updated to explain the
      new structure and the corrected local dev URL (needs the
      `/DIY-KORG-KRONOS-EDITOR/` subpath, per the `--baseURL` fix from
      earlier this session).
    - Verified: `diff -rq` of the full built `public/` output against a
      pre-migration snapshot is byte-identical; local dev server spot-
      checked on `/`, `/overview/`, `/format/`, `/guide/`, `/building/`,
      `/components/`, all 200; `cmake --build`/`ctest` unaffected (docs-
      only change).
    - **Explicitly deferred, not part of this pass**: migrating
      `DIY-MIDI-METRONOME.public`/`DIY-PEDALBOARD.public` to the same
      shared module (they'd pick up tonight's `baseURL` fix and the
      missing `book.svg` icon for free) -- different repos, worth their
      own verification pass rather than bundling into this one. Also
      deferred: sharing `.github/workflows/hugo.yml` as a GitHub Actions
      reusable workflow -- lower value (it's short, changes rarely) and
      more risk (Metronome's workflow has one genuine extra build step) than
      the scaffold itself, not blocking.
  - **Explicitly not committed to being final**: both the project owner and this
    assistant agreed to revisit/rethink this shape as each piece (Program decoder now
    done; chunk-based component wiring next) proves itself against real tests and the
    real UI, rather than committing to it across the whole codebase up front.

--- EXPLORATION: SQLITE-BACKED PATCH DATA MODEL (branch: explore/sqlite-patch-datastore, 2026-08-02, NOT DECIDED) ---

Lives on its own branch, deliberately kept separate from `main` -- this is a genuine
architecture question, not yet a decision, and touches nothing that's currently
shipped. Started from a real, practical concern (memory/perf with several large
`.PCG` files open at once now that Datasets decouple "loaded file" from "pane," see
the ARCHITECTURE section above) and evolved into something bigger worth recording
even in its current unfinished state, so the reasoning isn't lost if this branch sits
for a while.

  - **Where the idea started**: instead of every open dataset holding its own
    full ~50-70MB raw byte buffer in `PcgFile::data_`, back Program (and
    potentially Combi/Set List) storage with SQLite -- an on-disk-backed file
    (not `:memory:`, which wouldn't help RAM at all), so the OS page cache can
    evict cold data instead of everything sitting permanently resident. Since
    `decodeProgramFields()`/`hashProgramRecord()` already operate on a bare
    byte pointer (not on `PcgFile` internals), swapping where that pointer's
    bytes come from -- offset math into `data_` vs. a `SELECT raw FROM
    programs WHERE bank=? AND number=?` -- is invisible to `EditorBridge`'s
    public shape and to any future JS-side component. That property held up
    well and isn't in question.
  - **Where it got more ambitious**: a Program table that persists *across
    sessions*, deduped globally by `content_hash` (not per-dataset) -- "every
    unique Program the editor has ever seen" -- plus a Combi-Timbre table
    modeled as an m:n join to Programs, plus Set List slots as private
    per-row data with a foreign key to a Program/Combi instead of resolving
    bank/number against array lookups by hand. Keeping the raw BLOB per row
    (not just derived columns) keeps this compatible with the project's core
    method -- derived fields stay honestly re-computable if a decoder's
    understanding improves later, rather than becoming a second, driftable
    source of truth.
  - **Where it hit a real wall (the reason this is a branch, not a merged
    change)**: Kronos Program/Combi data is NOT freely content-addressable
    the way the hash-dedup model assumes. A Combi's Timbre reference isn't
    "this Program's content" -- it's "whatever physically sits at raw bank
    code X, number Y," and each sound engine (HD-1, AL-1, CX-3, ...) owns its
    own dedicated bank ranges, so that physical slot has to both exist *and*
    belong to the right engine for the reference to mean anything. Copying a
    CMB bank without its dependent PRG bank(s) doesn't yield "a Combi with
    unknown sounds" -- it yields a structurally broken Combi on the receiving
    unit. (This matches why real commercial patch vendors ship one CMB bank
    plus the one or two specific PRG banks it actually depends on, never an
    arbitrary/whole-unit Program dump.) This is *the* mental block for
    building any "patch manager" on top of a hash-deduped model: content
    hash is a good *compatibility check* ("does bank X/number Y in the
    destination already hold byte-identical content to what this Combi
    expects?"), but it can't be the reference mechanism itself. Any real
    move/merge feature has to solve a physical-placement problem --
    allocating an engine-compatible bank slot in the destination and either
    confirming it already matches or copying the dependency there -- not
    just a content-copy-plus-FK-update problem.
  - **A prerequisite this surfaced that wasn't obvious before, since partly
    resolved (2026-08-02)**: safely moving/merging Combis depends on knowing
    a Program bank's engine. Research (see `docs/external/README.md`)
    turned up more than expected:
    - **Officially confirmed by Korg's own KRONOS Parameter Guide**: a bank
      is either HD-1 or EXi, never mixed -- "Banks can contain either HD-1
      Programs or EXi Programs, but not both" -- and the manual's factory
      table names the *specific* engine per bank by default (INT-D=AL-1,
      INT-E=AL-1 and CX-3, INT-F=STR-1, USER-A=MS-20EX & PolysixEX,
      USER-B=MOD-7, ...). Explicitly the factory *default* -- bank type (and
      by extension real contents) is user-reconfigurable per bank via Global
      mode, and a separately-found community document confirms real
      long-used units routinely drift from this layout. A strong
      default/fallback label set, not a per-file guarantee.
    - **Built and tested (this branch)**: `classifyProgramBankType()`
      (`src/kronos/ProgramDecoder.{h,cpp}`) derives the HD-1/EXi split from
      two signals already parsed at load time -- the bank's own chunk tag
      (`MBK1`=EXi/`PBK1`=HD-1, from `docs/references/PCG-Structure-Kronos-
      DaBlick.txt`) cross-checked against its declared per-record byte
      stride (HD-1=4960/EXi=3706 bytes, from a Synthify community
      spreadsheet, see `docs/external/README.md`) -- deliberately not a
      hardcoded per-bank-index table, since bank type is configurable.
      `ProgramInfo`/`decodeProgram()` now carry a `bankType` field; covered
      by a dedicated synthetic unit test (both match and mismatch cases) in
      `tests/pcg_file_test.cpp`, spot-checked with a deliberately broken
      assertion to confirm it fails loudly.
    - **Still open**: the byte-level mechanism itself (chunk tag + the
      4960/3706 stride figures) hasn't been cross-checked against a real
      backup's actual bytes -- no `.PCG` file was available in the
      environment this was built in. The underlying HD-1/EXi model is now
      officially confirmed; this project's specific *detection* of it from
      raw bytes is not, yet. Also still open: which *specific* EXi engine a
      given EXi bank holds isn't decoded anywhere yet (only the HD-1/EXi
      binary split is built) -- the Parameter Guide's factory table is real
      ground truth for that follow-up, whenever it's wanted.
  - **UI enforcement (BUILT 2026-08-03)**: the per-pane category navbar (see above)
    made it easy to point two panes at two *different* datasets and drag a Setlist
    slot between them -- which surfaced this problem concretely: `copyEntry` happily
    copied a Song's bank/number as-is into the other dataset, even though that
    bank/number is a physical-location reference meaningful only within its own
    dataset's Program/Combi tables. `app.js`'s `onDropEntry` now rejects any
    cross-dataset Setlist copy outright (same-dataset cross-Setlist-list copy, and
    same-list reorder, are unaffected -- both stay within one `PcgFile`, safe).
    `pane.js` also shows this during the drag itself (not just after dropping): a
    shared `draggedFromDatasetId` variable (a plain JS side channel, since
    DataTransfer's payload isn't readable during `dragover` for a same-page drag)
    lets a row being hovered skip `preventDefault()` when the drag came from a
    different dataset, so the browser shows its own "not allowed" cursor and no
    `drop` event fires there at all. Revisit once the physical-bank-position
    problem above is actually solved -- not before.
  - **Category: a second, parallel physical-reference problem (researched 2026-08-03,
    not built)**: prompted by wanting to know what has to be checked/reassigned when
    moving a Program or Combi between datasets. Category (Keyboard/Bass/Strings/etc.,
    used to browse sounds by type regardless of bank/number) turns out to be the SAME
    shape of problem as Bank type -- confirmed via the official Kronos Parameter Guide
    (see `docs/external/Korg-Kronos-Parameter-Guide-Category-excerpt.txt`): each
    Program and Combi (two SEPARATE 18-main x 8-sub-category tables, not shared) is
    assigned a small Main Category (0-17) + Sub Category (0-7) index, but the *names*
    for those indices -- including all 16 "factory" names, not just the 2 open User
    slots -- live in a per-unit-customizable table in Global settings ("Global P3:
    Category Name"), saved via "Write Global Setting." So a Program's category index
    is portable, but its *meaning* isn't guaranteed to match between two datasets whose
    category tables have been renamed differently -- confirms the project owner's
    instinct that moving Programs/Combis between datasets may need explicit user
    confirmation/reassignment when category tables don't match. Not solved, not built
    -- two concrete, not-yet-started follow-ups: (1) locate Category/Sub-Category's
    byte offset in a Program record and (separately) a Combi record -- not in this
    project's own findings, the Synthify spreadsheet, or DaBlick's notes, so genuinely
    unexplored, same as the rest of `GLB1`; needs the same purpose-built test file
    approach already used for Font size/Transpose/Combi Timbre refs. (2) Parse `GLB1`
    itself, at minimum enough to extract both category name tables -- `GLB1` has never
    been touched at all (see Blind Spot #5).
  - **Setlist Bank-jump button, refined (BUILT 2026-08-03)**: a Setlist row's Bank cell
    is a button (`frontend/pane.js`'s `.bank-jump-button`, `stopPropagation()`-ed so it
    doesn't also toggle that row's Comment editor), styled with the same `.lib-tab`
    class as the category nav itself (not a text link) per explicit feedback. Clicking
    it switches this pane to its Programs/Combis category and expands+scrolls to that
    exact entry (`createLibraryPanels()`'s `jumpToEntry()`, found via a `data-entry-key`
    attribute added to each rendered row). `createPane()`'s category-tab-click logic was
    factored into a shared `switchCategory()` so both the tab buttons and this jump
    action stay in sync through one place. The scroll itself computes an exact target
    position (`scrollRowBelowHeader()`, via `getBoundingClientRect()`) so the row always
    lands just below the table's sticky header instead of `scrollIntoView({block:
    "center"})`, which could leave a row (especially one near the top of the list) still
    partly hidden behind it.
  - **Bank-filter buttons (BUILT 2026-08-03)**: the Programs and Combis panels each get
    a row of toggle buttons (one per bank name, between the Name search and the table)
    reusing the bank-name arrays `pane.js` already has. Only buttons for banks actually
    present in the current dataset are enabled; all enabled banks start "pressed"
    (shown) on every fresh dataset load, independently toggleable per category after
    that -- an inclusive multi-select filter, not a single-bank picker. `jumpToEntry()`
    (above) force-presses the target bank before rendering, so a Bank-jump can never
    land on a row its own filter has hidden.
  - **Bank names shortened + button/column widths equalized (BUILT 2026-08-04,
    frontend-only)**: `pane.js`'s `PROGRAM_BANK_NAMES`/`COMBI_BANK_NAMES` now hold
    "I-A".."I-G"/"U-A".."U-FF" (was "INT-A"/"USER-AA" etc.) -- purely a UI-layer
    shortening, not a change to the ground-truth naming: `PcgFile.cpp`'s confirmed
    Timbre bank-code table (`INT-A`=0, `USER-D`=20, ...) and the docs are untouched, a
    new `abbreviateBankName()` helper in `pane.js` shortens a bridge-provided full name
    (e.g. a Timbre's `bankName`) only at render time (`library.js`'s
    `formatTimbreRef()`). `formatBankNumber()` also switched from `"I-C-000"` to
    `"I-C 000"` (space, not dash). Freed-up width let `.col-bank` shrink 9em -> 6.5em
    and `.col-refs` 10em -> 4.5em (paired with renaming the "Setlist refs"/"Combi refs"/
    "Setlist references"/"Combi references" column headers, inconsistent across the
    Programs/Combis/Duplicates tables, to a uniform `#STL`/`#CMB` with a title tooltip
    spelling out the full meaning). Buttons that used to size to their own label's
    length (bank-filter buttons, the Setlist Bank-jump button, the Setlist/Programs/
    Combis/Duplicates category tabs) now share a fixed width per group
    (`.bank-filter-button` 3.6em, `.bank-jump-button` 100% of its now-narrower table
    cell, `.pane-category-tabs .lib-tab` 6.5em) so each row of buttons lines up evenly
    instead of looking ragged.
  - **Setlist row columns trimmed (BUILT 2026-08-04, frontend-only)**: Hold Time
    dropped from the row entirely (still fetched/returned by `getEntries()` --
    `entry.holdTime` is just not rendered here -- planned to resurface in the Comment
    editor panel later, not lost). The separate Color column (a small swatch) is gone
    too -- its color now paints the "#" slot-number cell's own background instead
    (`idxTd.style.background`, same unverified-placeholder hue formula as before, see
    the code comment -- **not** Korg's real color palette, no ground truth for that
    exists yet). Song name column narrowed 15em -> 10.5em (`.col-song`, a 30% cut from
    the same 15em/24-char baseline `.col-name` uses for Program/Combi names -- Set List
    slot names are confirmed the same 24-byte field, docs/README.md §3) -- wraps rather
    than ellipsis-truncates, since this cell can also hold a second line (the
    cross-referenced instrument name) that a hard truncate would clip along with it.
  - **Horizontal scrollbar bug fixed + columns shrunk to fit 800px (BUILT 2026-08-04,
    frontend-only)**: root cause -- `.entries-scroll`/`.library-body` only set
    `overflow-y: auto`, and per spec, leaving `overflow-x` at its default `visible`
    while the other axis is non-`visible` makes it compute to `auto` too, not stay
    `visible`. With two panes side by side, the Setlist table's column widths summed to
    more than a pane's actual width at the app's own 800px minimum (`main.cpp:81`'s
    `setMinimumSize(800, 500)`, unchanged/already correct), so that implicit `auto`
    kicked in as a real horizontal scrollbar. Fixed both properly (`overflow-x: hidden`
    is now explicit on both, so a scrollbar can never reappear by accident) and by
    actually shrinking columns to fit: `.col-index` (`#`) 3.5em -> 1.75em (50%),
    `.col-narrow` (Type/Vol) 4.5em -> 3.15em and `.col-bank` 6.5em -> 4.55em (30% each,
    shared classes so this also affects the Programs/Combis library tables' Type/Bank
    columns, not just Setlist). `.col-bank`'s `white-space: nowrap` was removed too --
    at the new width a label like "U-AA 009" needs to wrap onto a second line rather
    than force the column wider than its declared width. Also added `min-width: 800px`
    to `html, body` -- mirrors the native window's own floor so a plain browser tab
    (mock_bridge.js's no-native-app testing mode) can't be resized narrower than what
    the real app ever allows, which is exactly the width this bug only showed up at.
  - **Real fix for the shrink-layout bugs (BUILT 2026-08-04, supersedes the previous
    attempt, frontend-only)**: the previous pass (`overflow-x: hidden` + `min-width:
    800px` on `html`/`body`) didn't actually work -- reported back as three symptoms:
    the native window's own 800px minimum appearing not to be respected, the right
    pane going partly invisible when shrinking, and columns not visibly shrinking at
    all. Root causes, actually isolated this time:
    - `min-width: 800px` on `html, body` was actively harmful, not just insufficient --
      it forces the PAGE to stay 800px wide regardless of the WebView's real viewport
      width, so the instant the actual window (or content view, mid-resize) is even
      slightly under that, the page overflows its own viewport: a scrollbar appears and
      whatever's past the edge (the right pane) is genuinely off-screen, not just
      visually squeezed. This is almost certainly what made the OS-level
      `setMinimumSize(800, 500)` (`main.cpp:81`, itself unchanged and believed correct
      -- a plain, standard `NSWindow setContentMinSize:` call, no custom resize delegate
      overriding it) look like it wasn't being respected. **Removed** -- the page must
      be able to shrink freely below 800px so it's never wider than its actual viewport
      under any circumstance.
    - The real cause of the right pane vanishing: `.panes` is a CSS grid, and a grid
      item's default `min-width` is `auto`, which resolves to its CONTENT's min-content
      size, not 0 -- so `.pane` (the grid item) could never shrink below whatever its
      content naturally needed, no matter how narrow the `1fr` track was asked to go.
      Same underlying issue one level deeper too: `.pane-category-content` is a flex
      item (in `.pane`'s column flex container) on its cross axis, subject to the exact
      same content-based-minimum default. Fixed by adding `min-width: 0` to both --
      `.entries-scroll`/`.library-body` already got this for free from their own
      `overflow-x: hidden` (per spec, non-visible overflow on an element makes ITS OWN
      automatic minimum resolve to 0), but that never helped the ANCESTOR grid/flex
      items further up the chain, which needed the same treatment explicitly.
    - Why columns weren't visibly shrinking: `.entries-table` never set `table-layout`,
      so it defaulted to `auto` -- in auto layout, a CSS `width` on a `<td>`/`<th>` is
      only a *hint* the browser can still override to fit content (a button's own
      minimum size, an unbroken label, ...), not a hard constraint. Switched to
      `table-layout: fixed`, which makes the first row's declared widths authoritative;
      content now wraps/clips to fit instead of pushing the column wider.
    - **Per explicit request, redesigned as "hard-locked narrow columns + exactly one
      resizable column"**, rather than the previous percentage-shrink pass: `.col-index`,
      `.col-narrow`, `.col-bank`, `.col-refs`, and (new) `.col-badges` now all set
      `width`/`min-width`/`max-width` to the identical value (a genuine lock, not a
      hint, now that layout is `fixed`) -- `.col-song` (Setlist) and `.col-name`
      (Programs/Combis) are the only columns left with no width at all, so they're the
      sole ones that grow/shrink with the pane. `.col-badges` used to be the
      Combis table's own "soaks up leftover space" column (`width: auto`); now fixed at
      10em so `.col-name` is unambiguously the only resizable one there too.
  - **Column widths redone via <colgroup> (BUILT 2026-08-04, supersedes the em-on-
    th/td attempt above, frontend-only)**: the `table-layout: fixed` + hard-locked
    em-width `.col-*` classes pass looked reasonable but was reported completely
    broken in practice (the `#` column rendering far smaller than declared, every
    fixed column refusing to actually resize, Name blowing out). Root cause: `table-
    layout: fixed` only takes each column's width from cells in the table's first
    row, and `.entries-table th` sets `font-size: 12px` while most `<td>`s inherit
    14px (or their own 12px override, inconsistently) -- so an `em`-based width meant
    two different pixel values depending on whether the browser happened to key off
    the `<th>` or a `<td>`, silently producing widths nothing close to intended.
    **Fixed properly with `<colgroup>`**: a new `colgroupHtml(widthsPx)` helper in
    `pane.js` builds `<col style="width:Npx">` per column (absolute px, `null` for
    the one column meant to flex) -- `<col>` width is font-size-independent, shared
    identically by every cell in that column, and is what `table-layout: fixed`
    actually expects to key off. Every `.entries-table` (Setlist in `pane.js`,
    Programs/Combis/Duplicates in `library.js`) now opens with a `colgroupHtml(...)`
    call; the `.col-*` CSS classes are cosmetic-only now (color, font-size, text
    wrapping) -- no width/min-width/max-width left on any of them. Widths chosen
    (px): Setlist `[21, flex, 38, 55, 38]` for #/Song/Type/Bank/Vol; Programs `[55,
    flex, 38, 54, 54]` for Bank/Name/Type/#STL/#CMB (also gave Programs' Name cell
    the `.col-name` class it was missing before, so it gets the same ellipsis
    treatment Combis' always had); Combis `[55, flex, 160, 54]` for Bank/Name/Set
    Lists/#STL (Set Lists -- `.col-badges` -- used to be the "soaks up leftover
    space" column here too, which meant TWO resizable columns competing for the same
    space; now fixed at 160px so Name is unambiguously the only one); Duplicates
    `[55, 54, 54]`, no flexible column (matches its original design -- the group name
    is shown above the table, not in a column).
  - **Missing links in the min-width: 0 chain (BUILT 2026-08-04, supersedes the
    <colgroup> pass above -- that pass was reported as making zero visible
    difference)**: after the <colgroup> fix still tested as completely unchanged,
    re-audited the full flex/grid ancestor chain from `body` down to the table and
    found it was fixed at the wrong end. `.pane` and `.pane-category-content` had
    `min-width: 0` (the previous round's fix), but `.view` (a flex item of `body`,
    column flex, cross axis = width) and `.panes` (a flex item of `.view`, same
    trap) did NOT -- and since a flex/grid item's content-based auto-minimum applies
    at EVERY level independently, leaving even one un-fixed level upstream stops the
    whole chain from ever reaching the levels that were already fixed. The window
    could never actually get narrow enough for the colgroup's px widths to matter,
    which is exactly why that pass changed nothing observable. Added `min-width: 0`
    to both `.view` and `.panes`, completing the chain: `body -> .view -> .panes ->
    .pane -> .pane-category-content` all now either set `min-width: 0` explicitly or
    (`.entries-scroll`/`.library-body`) get the same effect for free from their own
    `overflow-x: hidden`.
  - **Tables rebuilt as CSS Grid, not HTML table layout (BUILT 2026-08-04, supersedes
    both the em/th-td and <colgroup> attempts above -- per explicit "the layout is
    beyond repair, kick in a real grid system" request)**: after the min-width: 0 chain
    was completed and STILL reported as no better ("same"), decided to stop fighting
    HTML table-layout algorithms entirely rather than keep patching around their
    quirks (first-row-only column sizing, th/td font-size context mismatches, engine-
    specific auto/fixed differences). Every `.entries-table` (Setlist in `pane.js`,
    Programs/Combis/Duplicates in `library.js`) is now a genuine CSS Grid: `display:
    grid` on the `<table>`, `display: contents` on `<thead>`/`<tbody>`/`<tr>` (removes
    them from the box tree so their `<th>`/`<td>` children become direct grid items,
    while leaving them in the DOM untouched -- existing click/drag handlers on `<tr>`
    still work exactly as before). Column widths are one `grid-template-columns` value
    set as an inline style directly on each `<table>` (`pane.js`'s new
    `gridTemplateColumns(widthsPx)`, replacing the `<colgroup>` helper) -- the same
    solid, explicit mechanism `.panes` (the two-pane split) has used from the start
    without any of these problems. Two knock-on fixes this forced: (1) `colSpan`
    doesn't work under `display: grid` (it's a table-rendering feature), so the three
    full-width expandable rows (`pane.js`'s Comment editor, `library.js`'s Program
    usage panel and Combi Timbre list) now use `td.style.gridColumn = "1 / -1"`
    instead -- while fixing this, found the Program usage row's old `colSpan = 2` was
    stale (Programs has had 5 columns for a while, so it was only ever spanning 2 of
    them), a real pre-existing bug now moot since `1 / -1` always means "all of them."
    (2) `display: contents` rows have no box of their own to paint a background on, so
    row-level hover/drop-target/expanded state (`tbody tr:hover`, `tr.drop-target`,
    `tr.expanded`) now targets each row's `td` children directly instead (`tr:hover
    td`, etc.) -- the state still lives as a class on the `<tr>` in the DOM, unchanged
    in JS, these are just CSS selector adjustments to reach through it. `cursor: grab`
    stayed on `tr` itself since `cursor` inherits through `display: contents` fine
    (only non-inherited properties like background/outline needed moving).
  - **Adopted Bulma for layout/components (STARTED 2026-08-04, pane shell + Setlist
    table done, library.js/Programs/Combis/Duplicates intentionally NOT converted yet)
    -- supersedes hand-rolling our own grid**: after the CSS-Grid-tables rewrite still
    didn't fix things, we asked directly why not use an established framework instead
    of continuing to hand-roll layout primitives. Landed on Bulma over Bootstrap: CSS-
    only (no JS/Popper dependency, fits this app's all-vanilla-JS interaction model --
    we still hand-write every behavior, Bulma only supplies CSS classes), ~24KB
    gzipped, MIT, vendored as one file (`frontend/vendor/bulma.min.css` + a
    `BULMA_VERSION.txt` note, same pattern as `third_party/CHOC_VERSION.txt`) rather
    than CDN-linked, since this app has no general-internet-access assumption
    (`fetchResource` serves everything from local disk). `index.html` sets
    `data-theme="dark"` on `<html>` to force Bulma's dark palette unconditionally
    (checked the CSS -- Bulma auto-dark-mode is keyed off `prefers-color-scheme` by
    default, which would drift from our own hard-coded dark `:root` whenever the OS
    itself isn't in dark mode; `[data-theme=dark]` is a real, explicit override Bulma
    ships for exactly this).
    - **Corrected a wrong assumption before it shipped**: initially claimed Bulma's
      `.column` bakes in the `min-width: 0` fix this whole saga kept needing --
      actually checked the vendored CSS and it does NOT (`.column{flex-basis:0;
      flex-grow:1;flex-shrink:1}`, no min-width). Bulma doesn't make that bug go away
      for free; `.pane`'s own `min-width: 0` (already in style.css) is still what's
      doing the work, just now on an element that also carries Bulma's `.column`
      class. Surfaced this to the project owner mid-task rather than building on the
      wrong premise -- confirmed to proceed anyway, for the grid pattern + component
      value, not because it's automatic.
    - **`.panes`/`.pane` -> Bulma's real `.columns`/`.column`** (`index.html`), not a
      hand-rolled grid -- this was the actual "we need a row/col concept" ask. Both
      Bulma's inter-column gap and each `.column`'s own padding key off one CSS
      variable (`--bulma-column-gap`); zeroed it on `.panes.columns` and kept our own
      `gap: 1px` + `background: var(--border)` (the existing thin divider-line look)
      instead of fighting Bulma's spacing system or duplicating padding.
    - **`is-mobile` on both `.topbar.level` and `.panes.columns`**: Bulma is mobile-
      first -- `.level`/`.columns` only become row layouts past a 769px breakpoint,
      stacking below it. This app's window has an 800px floor (`main.cpp`), which
      clears that breakpoint, but relying on a 31px margin above a breakpoint is
      exactly the class of narrow-width edge case that broke things repeatedly before
      this rewrite -- `is-mobile` forces row layout unconditionally, no breakpoint.
    - **Topbar**: `.level`/`.level-left`/`.level-right` for the Open button + title +
      loading indicator row; Open button and the Setlist table's Bank-jump button are
      now real Bulma `.button.is-small` (was a hand-rolled `.lib-tab` class).
    - **Category nav (Setlist/Programs/Combis/Duplicates)**: Bulma's real
      `.tabs.is-boxed` component (`<ul><li><a>`, active state as `is-active` on the
      `<li>`), replacing the hand-rolled `.lib-tabs`/`.lib-tab` buttons entirely for
      this nav specifically -- `switchCategory()` updated to toggle `is-active`.
      `.lib-tab` itself is NOT deleted -- library.js's bank-filter-row buttons still
      use it, unconverted on purpose (explicit scope: pane/Setlist-table first).
    - **Form controls**: `.setlist-select`/`.dataset-select` wrapped in Bulma's
      `.select` (`.is-fullwidth` for the Setlist one), `.filter-input` gained Bulma's
      `.input` class, `.setlist-info` gained `.help`. Checked Bulma's actual CSS before
      deleting our own hand-rolled background/border/padding/disabled-opacity rules
      for these -- confirmed `.input`/`.select.is-fullwidth` already handle
      width:100% and disabled state on their own, so that hand-rolled CSS was dead
      weight once removed; the one thing Bulma can't infer on its own (which flex item
      should grow in a specific layout) stayed as a small `.dataset-select-wrap{flex:1}`
      rule.
    - **Explicitly deferred, not forgotten**: library.js's Programs/Combis/Duplicates
      panels (bank-filter-row buttons, the data tables themselves) are UNCONVERTED --
      still `.lib-tab`-styled buttons and the CSS-Grid-based `.entries-table` mechanism
      from the previous pass. Per explicit instruction, Bulma adoption lands here next,
      once the pane shell + Setlist table are confirmed working.
  - **Setlist rebuilt as a real Bulma table + Library (Programs/Combis/Duplicates)
    fully converted too (BUILT 2026-08-04)** -- two rounds landed together:
    - **Setlist**: back to a real `<table>` (Bulma's `.table.is-fullwidth.is-hoverable
      .is-narrow`), not the CSS-Grid-dressed-as-a-table hack from the previous pass --
      per explicit "remove all non-Bulma CSS from the tables and rows" request. Column
      widths moved from px to a genuine 12-based grid (`pane.js`'s `colgroupHtml()`,
      mirroring Bulma's own `.column.is-1..is-12` convention) expressed as percentages
      via `<colgroup><col style="width:N%">`, so the table scales with its container.
      `colSpan` is back to a plain attribute (was a `grid-column: 1/-1` workaround).
      Row highlight (Comment editor open) uses Bulma's real `tr.is-selected`, not a
      hand-rolled `.expanded` class; row hover comes from Bulma's `.is-hoverable`, no
      custom CSS needed at all. What's left as genuinely irreducible custom CSS:
      column-width locking itself (`table-layout: fixed`, since Bulma's `.table` has
      no column-layout system whatsoever), an opaque sticky-header background (Bulma's
      table head background is transparent by default), and two real app-specific
      states no framework has a concept for (drag-and-drop's drop-target hint, "this
      row has a Comment").
    - **Column-width floors**: `<col>` doesn't support `min-width` (ignored by
      browsers), and `table-layout: fixed` ignores per-cell width/min-width past the
      first row -- so a per-column minimum can only live on the `<col>` itself, via
      `calc(N% + Mpx)`. `colgroupHtml()` now accepts `{frac, extraPx}` per column;
      Setlist's #/Type/Vol get +5px, Bank +10px (the jump-button's "I-C 000" text
      needs more room).
    - **Library (Programs/Combis/Duplicates) converted the same way**, closing out the
      staged rollout: same real-`<table>` + `colgroupHtml()` + `tr.is-selected`
      pattern for all three. Bank-filter-row buttons moved from the hand-rolled
      `.lib-tab` to Bulma's `.button.is-small` (pressed/active state via `is-link`,
      the idiomatic Bulma way to show a toggle button as active -- Bulma has no
      dedicated toggle-button component). Set List reference pills (Combis) moved
      from a hand-rolled `.badge`/`.badge-list` to Bulma's real `.tags`/`.tag`
      component. With library.js off it, the entire old CSS-Grid-table mechanism
      (`.entries-table`, its `display: contents` thead/tbody/tr hack, `.col-narrow`/
      `.col-bank`/`.col-refs`/`.col-name`, `.badge-list`/`.badge`, `.library-table`,
      `.lib-tab`/`:hover`/`.active`, `pane.js`'s `gridTemplateColumns()`) had nothing
      left using it -- deleted outright rather than left as dead code.
    - **Fixed a real, unrelated bug found along the way**: the topbar loading spinner
      never actually hid. Root cause -- `.topbar-loading` also carries Bulma's
      `.level-right`, and since it lives inside `.level.is-mobile`, Bulma's actual
      matching rule is `.level.is-mobile .level-right{display:flex}` (3 combined
      classes, specificity 0,0,3,0), which beat our `.topbar-loading[hidden]{display:
      none}` (0,0,2,0) outright regardless of the `hidden` attribute. Fixed with a
      deliberate, narrow `!important` -- the right tool for "must win over a
      third-party framework's conflicting rule for a boolean visibility toggle."
  - **Real Bootstrap-style responsive breakpoints (BUILT 2026-08-04)**: two changes,
    both using Bulma's mobile-first `is-N-mobile`/`is-N-tablet` column-size classes
    (verified against the actual CSS before relying on them: `.column.is-12-mobile`
    is unconditional `width:100%`, `.column.is-6-tablet` only applies inside `@media
    (min-width:769px)` -- and critically, `.columns` WITHOUT `.is-mobile` isn't
    `display:flex` at all below that breakpoint, so `.column` children just stack as
    plain blocks with no wrapping logic needed).
    - **Dataset-select and the category tabs share one responsive row (BUILT
      2026-08-04, corrected same-day)**: the first attempt put them in two separate,
      always-stacked `.columns` rows -- wrong mechanism, per follow-up clarification
      the actual want was Bootstrap's `col-12 col-lg-6` pattern: ONE row that wraps
      into two stacked rows below a breakpoint and merges into one side-by-side row
      above it. Fixed: both back in a single `.columns.is-multiline` row, each
      `.column.is-12-mobile.is-6-desktop` -- full width (stacked, 12+12 doesn't fit
      one row) below Bulma's 1024px desktop breakpoint, half width each (side by
      side, 6+6=12 fits exactly) from there up. `.is-multiline` is load-bearing here
      specifically: the container's own flex-activation breakpoint (769px, tablet) is
      EARLIER than the width-override breakpoint (1024px, desktop) it's paired with,
      so without it, the 769-1024px range would be a flex row of two still-full-width
      items with nothing telling them to wrap -- they'd overflow instead of stacking.
      (`.panes` below doesn't have this mismatch -- its own flex-activation and
      width-override are both 769px, so no `.is-multiline` needed there.) The app's
      default window (1100px, `main.cpp`) clears 1024px, so this is genuinely visible
      in normal use -- shrink the window below ~1024px total (not per-pane, Bulma
      breakpoints are viewport-width-based) to see it stack.
    - **The two-pane split (.panes/.pane) is genuinely responsive now**, not forced
      side-by-side: `is-mobile` removed from `.panes`, `.pane` gained `is-12-mobile
      is-6-tablet` -- stacks full-width below 769px, side-by-side (half each) above
      it. The app's own window has an 800px floor (`main.cpp`), comfortably clearing
      769px, so the native app always renders side-by-side either way; the stacking
      only becomes externally visible testing in a plain browser tab narrower than
      that (mock_bridge.js's no-build mode). The topbar's `.level` keeps its
      unconditional `is-mobile` (nothing useful to stack a title bar into).
  - **Pane-header breakpoint written directly, not via Bulma's grid (BUILT
    2026-08-04, supersedes the .columns.is-multiline attempt above -- reported as
    still not responding to the breakpoint at all)**: reasoning through the Bulma
    mechanism (mismatched 769px container-flex-activation vs 1024px width-override
    breakpoints, `.is-multiline` needed to bridge them) held up on paper, but with no
    screen access to debug Bulma's own minified cascade against what actually
    rendered, kept relying on an unverifiable chain of reasoning wasn't going
    anywhere. Replaced with a small, self-contained rule: `.pane-header-row{display:
    flex;flex-wrap:wrap}`, `.pane-header-col{flex:1 1 100%}` (stacked by default),
    one `@media(min-width:1024px)` flipping to `flex:1 1 0` (side by side, shared
    equally). Same lesson as the entries-table saga a few rounds back -- when a
    framework mechanism can't be visually verified and keeps failing, a few lines of
    plain CSS beat continuing to reason about a library's internals blind.
  - **View-hint row removed, moved to a native hover tooltip (BUILT 2026-08-04)**:
    the always-visible `.view-hint` text row is gone; checked first whether Bulma has
    a tooltip component to use instead (it doesn't -- tooltips were historically a
    separate, unbundled Bulma extension, not part of core) -- used the plain HTML
    `title` attribute instead (a real native hover tooltip, zero JS/CSS/extra markup
    needed), attached to the topbar's own `<h1>`.
  - **Column-width regression: calc() on <col> suspected and removed (BUILT
    2026-08-04)**: reported as "only the Song/Name column shows, everything else is
    invisible" on all four tables (Setlist, Programs, Combis, Duplicates). Root cause
    not independently confirmed (no way to inspect actual rendered layout in this
    environment), but correlates exactly with the one thing that changed since the
    last confirmed-good state: the previous pass added a per-column pixel floor via
    `calc(pct% + Npx)` on each `<col>`'s width, to bake in a min-width `<col>` doesn't
    support natively. `<col>` elements have historically had weak, inconsistent
    cross-engine support for anything beyond a plain width value -- treating that as
    the likely cause even though `calc()` on a `<col>` width is spec-legal. Reverted
    `colgroupHtml()` to plain percentages only, no calc(), across all four tables
    (`pane.js`'s Setlist + `library.js`'s Programs/Combis/Duplicates all share the
    one helper). The "make some columns a bit wider" intent from before is now
    expressed as a non-integer fraction instead (e.g. `1.3` instead of `{frac:1,
    extraPx:5}`) -- same idea, no calc() involved, `colgroupHtml()` already accepted
    any number, not just whole ones.
  - **Bank-filter buttons moved out of the scrolling table area (BUILT 2026-08-04)**:
    they used to live inside `.library-body` (the same scroll container as the
    table), so they scrolled out of view along with the rows -- moved to a new
    `.bank-filter-area` sibling ABOVE `.library-body`, between the Name search input
    and the table, always visible regardless of scroll position. `showPanel()` now
    also toggles which category's bank-filter row (if either -- Duplicates has none)
    is shown, mirroring how it already toggled `panels`. While doing this, proactively
    fixed the same `[hidden]`-losing-to-our-own-unconditional-`display`-rule bug the
    topbar spinner had a few rounds back (`.bank-filter-row[hidden]{display:none}`,
    explicit rather than assumed) -- caught before it needed reporting a second time.
  - **Program bank type (HD-1/EXi) surfaced in the UI (BUILT 2026-08-05)**: was already
    classified and stored per-Program (`ProgramInfo::bankType`, exposed via
    `listPrograms()`), but nothing showed it except the Programs table's own dedicated
    Type column -- the Programs bank-filter buttons and the Setlist/Duplicates Bank
    labels had no way to show it without a specific Program row in hand. Added
    `PcgFile::programBankTypes()` (new, one entry per bank: `{bank, bankType}`,
    derived from the already-classified `programBankLocations_`, no new parsing) and a
    matching lightweight bridge call `getProgramBankTypes(datasetId)` -- much cheaper
    than fetching all ~2560 Program records just to label ~20 banks. Covered by a new
    `pcg_file_test` assertion (verified failing before restoring the correct expected
    value, this project's usual deliberate-break-then-restore check).
    `pane.js`'s `formatBankNumber(entry, bankType)` now takes an optional bank type and
    appends `" (HD-1)"`/`" (EXi)"` -- only ever for Programs (Combis have no engine type
    of their own, so it's a no-op there regardless of what's passed). Wired into: the
    Programs bank-filter buttons (`"U-A (EXi)"`), the Setlist Bank-jump button when it
    points at a Program (`"U-A 008 (EXi)"`), and the Duplicates table's Bank cell
    (which -- unlike Programs -- has no separate Type column, so this is genuinely new
    information there, not a repeat of an adjacent column). Deliberately NOT added to
    the Programs table's own Bank cell, since its Type column already shows the same
    fact right next to it -- would just be the same information twice in one row.
    `createPane()` now fetches+caches this once per dataset load (a `bank -> type` Map)
    and shares it with both `createSetlistPanel()` and `createLibraryPanels()`, so the
    bridge call happens once per dataset change, not once per renderer. Two follow-on
    CSS fixes this forced: `.bank-filter-button`'s old fixed 3.6em width (fine when
    every label was uniformly "I-A".."U-FF") had to go, since `"U-AA (EXi)"` varies too
    much in length now; and Bulma's `.button` forces `white-space: nowrap` by default,
    which would have made the Setlist Bank-jump button overflow its cell once the
    suffix made its text much longer -- overridden to `white-space: normal` so it wraps
    onto a second line instead, plus its column's colgroup fraction bumped 2.6 -> 3.5.
    Caveat carried through honestly, not smoothed over: `ProgramBankType` itself is
    per-file-derived (Kronos OS 3.0+ lets a user reassign INT Program Banks between
    HD-1/EXi, so it was never going to be a hardcoded table) and is flagged in
    `PcgFile.h`'s own doc comment as **not yet independently verified against a real
    Kronos backup** -- this UI surfaces that classification more prominently now, not
    a newly-confirmed fact.
  - **Program drag-and-drop: the first feature that writes immediately into the
    native buffer (BUILT 2026-08-05)**: per explicit direction, editor operations
    stop being purely in-memory-bookkeeping from here on -- this is the first one
    that actually writes into a dataset's retained raw bytes (`data_`), not just
    `setlists_`. Planned via `EnterPlanMode` given the scope (first real write path,
    touches the physical-bank-placement problem this branch exists to explore) --
    plan file preserved the full design reasoning; summary here.
    - **`PcgFile::copyProgramFrom(src, srcBank, srcNumber, dstBank, dstNumber)`**
      (new): copies one Program record's raw bytes from `src` (pass `*this` for a
      same-dataset copy) into this file's target slot, re-decoding the destination's
      `programs()` entry from the freshly-written bytes so cached fields never go
      stale. Five guards, checked in order, nothing written unless all pass:
      out-of-range bank/number, engine-type mismatch (HD-1 record into an EXi bank
      would corrupt it, or vice versa -- this is exactly why bank type was surfaced
      in the UI just before this), a record-size mismatch as a defensive second
      check on top of the type check, the target slot already holding a *different*
      Program (confirmed explicitly with the project owner as a real, no-undo
      action worth a hard block, not silently overwritten), and a byte-identical
      Program already existing anywhere in the destination file. New
      `programBankTypeAt()` (single-bank lookup) alongside it.
    - **Real bug caught and fixed by the test, not by inspection**: the
      duplicate-exists check initially compared the source record's hash against
      *every* entry in the destination's `programs()`, including the source's own
      slot -- which trivially matches itself, so every same-dataset copy rejected
      itself as "a duplicate of itself." Caught immediately because
      `testCopyProgramFrom()` (new, `tests/pcg_file_test.cpp`) actually exercises a
      real same-dataset copy, not just the rejection paths -- fixed by excluding
      only the exact source slot from that comparison (not its whole bank), and
      only when `&src == this`. Also deliberately broken-then-restored once more,
      per this project's usual practice.
    - **`EditorBridge::copyProgram(args)`**: `[srcDatasetId, srcBank, srcNumber,
      dstDatasetId, dstBank, dstNumber] -> {ok}` or `{ok:false, error}`, same or
      different dataset both work (`fileOf()` twice, no aliasing risk since
      distinct (bank, number) pairs never overlap in byte range even when
      `srcDatasetId == dstDatasetId`).
    - **Cross-dataset copying is allowed for Programs specifically** -- confirmed
      with the project owner this doesn't reopen the physical-bank-placement
      problem the SQLite exploration flagged as hard: unlike a Setlist slot or a
      Combi Timbre, nothing *outside* a dataset can already be pointing at one of
      its Program slots, so copying raw bytes into another dataset's slot doesn't
      leave any dangling/wrong reference behind. Setlist's own `onDropEntry`
      (`app.js`) stays same-dataset-only, unchanged.
    - **Frontend**: Programs rows are draggable now (`library.js`, was explicitly
      "not draggable yet" until this pass) -- drag one onto another (same pane or a
      different pane's dataset) to copy. `app.js`'s new `onDropProgram` mirrors
      `onDropEntry`'s shape but only refreshes the *destination* dataset's panes
      (a copy never touches the source, unlike Setlist's swap/copy). Live
      type-mismatch feedback during `dragover` (before drop, not just after) reuses
      each row's own already-known `bankType` -- no extra bridge call needed beyond
      what the bank-type-in-UI pass already wired up. `.programs-table` shares its
      `cursor:grab`/`.drop-target` CSS with `.setlist-table` rather than duplicating
      it (one rule, two selectors).
    - **Not built**: any UI for the reverse direction (does the source disappear?
      No -- copy, not move, confirmed explicitly) or a "target occupied, overwrite
      anyway?" confirmation -- the third guard above is a hard block instead, since
      this app has no modal UI anywhere yet and building one just for this would be
      scope creep beyond what was asked.
  - **Real Kronos Set List slot colors (BUILT 2026-08-05)**: the "#" cell's
    background color used to be a synthetic hue-spread placeholder
    (`hsl(color*47%360, ...)`), explicitly flagged as not Korg's real palette.
    Replaced with `pane.js`'s `SETLIST_COLOR_NAMES`/`SETLIST_COLOR_HEX` (16 real
    color names from the official Korg manual, English + German, provided by the
    project owner) -- **ordering not yet independently confirmed against a real
    Kronos** (noted explicitly in both the code comment and here, per this
    project's "no guessing" standard); the array order is the order they were
    given in (reads like the on-device menu order), a working assumption pending
    a real-hardware cross-check the project owner will do later. Hex values are
    this project's own muted approximation of each named color (not sampled from
    real hardware either), deliberately kept dark enough that the existing
    dim-gray cell text stays legible on top of any of them -- White specifically
    can't be real white for that reason, see the code comment. `mock_bridge.js`'s
    fake Setlist songs now cycle through all 16 colors (`(k % 16) + 1`) instead of
    a fixed `color: 1`, so mock mode can actually exercise the full palette
    visually without needing a real backup file.
  - **Bank-filter grid alignment + Timbre reference engine type/Program name (BUILT
    2026-08-05)**: three small follow-ups from real usage.
    - `.bank-filter-row` moved from `flex-wrap` to a real CSS grid
      (`grid-template-columns: repeat(auto-fill, minmax(6.5em, 1fr))`) -- flex-wrap
      left each button hugging its own label's width, which read as ragged once
      labels started varying in length ("I-A" vs "U-AA (EXi)"); the grid gives every
      column the same width regardless of label length.
    - Combi Timbre references now show engine type and the actual Program name
      (`library.js`'s `formatTimbreRef()`), not just bank/number -- the name was
      "already in memory anyway" per the request: `programs` is the exact array this
      panel already fetched for its own Programs table, just searched by
      (bank, number), no new bridge call. Both are gated to
      `isConfirmedTimbreProgramBank()`'s confirmed range (at the time, bank 0-3 /
      INT-A..D only -- **extended 2026-08-08 to 8 banks total, INT-A..D plus
      USER-A/D/F/AA, see the ARCHITECTURE entry below**)
      `EditorBridge::combiUsagesForProgram()`/`combiUsageCounts()` already use for
      the #CMB column -- outside that range, a Timbre's raw bank code isn't
      confirmed to mean the same thing as a Program's own `.bank` field at all, so
      showing a type/name for it would be a guess dressed up as a lookup, exactly
      what got flagged when a Combi showed `"Timbre 5: code 19 085"` with nothing
      else -- that's not a bug, it's the honest "don't know" case working as
      designed, and is the same reason a name/type can't safely be added for it
      either (mirrored in `mock_bridge.js`: one fake Timbre now deliberately matches
      a fake Program's bank/number so the new lookup has something to demonstrate
      in mock mode too).
  - **Indeterminate loading spinner (BUILT 2026-08-03)**: a pane-wide overlay
    (`.pane-loading`) shown for the duration of a file drop's base64-encode/decode+parse
    -- genuinely indeterminate, since no byte-level progress callback exists for that
    path yet (a real percentage bar would need the transfer itself restructured into
    chunks with progress events, a bigger follow-up, see Blind Spot #15).
  - **Global mode scope, when it's picked up (not started)**: intentionally narrow --
    for now, just pull the category name tables (Program's and Combi's, separately), not
    a general `GLB1` parse. Matches this project's "only extract what's currently
    needed" convention (see `docs/content/components/index.md`) -- other Global
    settings stay unparsed until something concrete needs them.
  - **Not decided, not scheduled**: no schema has been written, no SQLite
    dependency has been added, nothing here has touched `main`. This section
    exists so the reasoning survives even if this branch is set aside for a
    while -- update it in place as the exploration continues, rather than
    letting the thread live only in chat history.

--- NATIVE FILE DIALOG + PROGRESS (branch: explore/sqlite-patch-datastore, 2026-08-03, Phase 1 built) ---

A distinct thread of work from the SQLite exploration above (just landed on the same
branch) -- prompted by asking how the native side actually reads a file today, which
surfaced two real gaps: no working native Open/Save dialog (drag-and-drop is the only
mechanism, specifically *because* the native one is broken -- see Blind Spots), and no
real progress reporting (the loading spinner we built is necessarily indeterminate,
since the whole current pipeline is one monolithic base64-encode-in-JS /
base64-decode-and-parse-in-C++ step with no chunking at all).

  - **Root cause of the broken native dialog, confirmed by reading CHOC's own source**:
    `choc_WebView.h`'s `runOpenPanelWithParameters` delegate (triggered by an
    `<input type="file">` from inside the page) opens `NSOpenPanel` via
    `beginSheetModalForWindow:completionHandler:` -- a *sheet*, attached to the
    WKWebView's own window. That's the documented z-order bug. A directly-invoked,
    **app-modal** `runModal` call is a genuinely different code path -- not attached to
    any window at all, so there's no window for it to end up behind.
  - **BUILT (Phase 1)**: `src/platform/NativeFileDialog.h`/`.cpp` (new) -- macOS-only for
    now, calls `NSOpenPanel`/`NSSavePanel` directly via `choc::objc` (CHOC's own reusable
    Objective-C call helpers, `third_party/choc/choc/platform/choc_ObjectiveCHelpers.h`
    -- safe to include unconditionally, the whole file is `#if CHOC_APPLE`-guarded) and
    `runModal`, bypassing CHOC's own broken delegate entirely. `isNativeFileDialogSupported()`
    lets callers distinguish "not supported on this platform" from "user cancelled" (both
    would otherwise look like an empty result) -- Windows/Linux stub returns `false`/
    `nullopt` rather than untested `IFileOpenDialog`/`GtkFileChooserNative` code with no
    way to verify it here.
  - **BUILT (Phase 1)**: `EditorBridge::openFile()`'s body refactored into a shared
    private `openFileAtPath()`, reused by both the pre-existing `openFile(path)` (JS
    supplies the path -- still not wired to any UI control) and the new
    `openFileDialog()` (no args, calls `kronos::showOpenFileDialog()` then the same
    helper). Bound in `main.cpp`, and `pane.js` gets an "Open..." button next to each
    pane's dataset-select (opens into that specific pane, mirroring drag-and-drop).
    `mock_bridge.js` mirrors it with a `window.prompt()` stand-in, same spirit as its
    existing fabricated-data approach.
  - **Save dialog built but deliberately not wired to any UI yet**: `showSaveFileDialog()`
    exists (same mechanism, proven once Open works) but nothing calls it -- there's
    nothing meaningful to write yet. `moveEntry`/`copyEntry`/`setComment` only mutate the
    in-memory `setlists_` structs, never `data_` (no `putRecordBytes()`/encoder exists).
    Wiring a Save button now would either silently discard every edit (writing back
    unmodified original bytes) or need to be disabled/misleading -- worse than waiting.
  - **CONFIRMED WORKING in the real app (2026-08-03)**: the `runModal` fix does resolve
    the z-order bug -- the panel appears in front and a dataset loads successfully via
    "Open...". Blind Spot #11 updated accordingly. This was the actual gate for
    everything below.
  - **Drag-and-drop-to-open REMOVED (2026-08-03), now that "Open..." works**: the
    file-drop listeners (`dragenter`/`dragover`/`dragleave`/`drop` for *files*, as
    opposed to the still-present Setlist row drag-and-drop, which is unrelated and
    untouched) are gone from `pane.js`, along with `arrayBufferToBase64()`, the
    `EditorBridge::openFileBytes()` bridge method, its `main.cpp` binding, and the
    hand-rolled `decodeBase64()` helper -- all fully unreachable once "Open..." is the
    only way in, removed rather than left as dead code (per this project's usual
    convention). `mock_bridge.js`'s fake `openFileBytes` removed too; its
    no-native-bridge-detected check now looks for `window.openFileDialog` instead.
    `.pane.drag-over` (and the now-pointless `border: 2px dashed transparent` on `.pane`
    that only existed to support it) removed from `style.css`. The `.view-hint` text and
    this bridge's own doc comments updated to stop describing a mechanism that no longer
    exists.
  - **Can't open the same dataset twice (BUILT 2026-08-03)**: `openFileAtPath()` now
    checks `m_datasets` for an existing entry whose `displayName` (which, for every path-
    based open, *is* the real path) exactly matches before loading anything -- if found,
    returns that dataset's existing info (`alreadyOpen: true`) instead of loading a
    second redundant copy. This was only feasible once every open went through a real,
    stable path (drag-and-drop never had one to compare against, which is exactly why
    this wasn't attempted before). `pane.js` logs a distinct "already open" message when
    this happens, but otherwise needs no special handling -- `loadDataset()` just shows
    whichever `datasetId` came back either way. `finishOpen()` and this new check share
    a `datasetResultValue()` helper so the `{datasetId, displayName, setlistCount}` shape
    is built in exactly one place.
  - **Verified**: compiles and links clean (confirms `choc::objc` + the existing
    `-framework Cocoa`/`-framework WebKit` link flags are sufficient, no new link step
    needed), app launches without crashing, `ctest` unaffected (no `PcgFile`/decoder
    changes in Phase 1), all JS syntax-checked, a full grep swept for any leftover
    reference to the removed drag-and-drop/`openFileBytes` code (none found).
  - **"Open..." moved from per-pane to a single topbar button (2026-08-03,
    frontend-only, no C++ changes)**: opening a dataset was never really "for" a
    specific pane (a dataset is decoupled from panes -- see `EditorBridge.h`), so having
    two identical buttons was redundant. `index.html`'s `.topbar` now holds the one
    `.open-file-button` (left side, `h1` flows after it) plus a `.topbar-loading`
    spinner; `pane.js`'s per-pane button/spinner markup and click handler are gone,
    replaced by two small exports (`loadDataset`, `isEmpty`) that `app.js`'s new global
    click handler uses to land a freshly-opened dataset in the first empty pane (A
    checked before B); if both panes already show something, the dataset still becomes
    selectable from either dropdown via the usual `refreshDatasets()` broadcast, just
    not auto-shown. The same-path dedup (`alreadyOpen`) already existed in
    `EditorBridge::openFileAtPath()` from the change above -- confusion about "still
    being able to open the same file twice" turned out to be testing against a stale
    already-running process (native code had changed but the process hadn't been
    relaunched), not a real gap; `mock_bridge.js`'s fake `openFileDialog()` now mirrors
    the same dedup-by-displayName behavior for consistency in plain-browser mode.
    `datasets.js`'s `populateDatasetSelect()` now labels each option `#<id> — <full
    path>` instead of just the bare path, so the id and the complete name are both
    always visible, not just whichever was picked out via truncation/CSS ellipsis.
  - **Phase 2, designed but not built -- only once Phase 1's dialog fix is confirmed
    working**: chunked reading with real progress, reported natively -- `PcgFile::load()`
    gains an optional `std::function<void(size_t,size_t)>` progress callback (seek for
    total size upfront, read in fixed chunks, e.g. 4MB, invoking the callback after
    each). The read runs on a background `std::thread` (so it can't block the WebView);
    progress and the final result both cross back to the main thread via
    `choc::messageloop::postMessage()` (confirmed thread-safe for exactly this use, see
    `choc_MessageLoop.h:67`), which then calls `WebView::evaluateJavascript()` to push a
    `window.__onImportProgress(percent)` / `window.__onImportComplete(result)` call into
    the page -- the first *native-initiated* event this bridge will have used, versus
    every existing method's JS-initiated request/response shape. Only the final
    `m_datasets` insert happens on the main thread -- the read/parse itself touches no
    shared state, so it's safe to run in the background. `openFileDialog()`'s bound call
    becomes fire-and-forget (returns immediately once a path is chosen) rather than
    blocking for the whole read. This is *not* built yet.

--- EXPLORATION: PCG TOOLS FEATURE COMPARISON (2026-08-14, NOT DECIDED) ---

The project's own Overview page now names the gap this app is meant to close: PCG Tools
(Michel Keijzers, hosted at kronoshaven.com/pcgtools/) is a real, capable Windows-only
librarian/editor that's effectively unmaintained. Worth actually checking the comparison
holds up feature-for-feature, not just asserting it. Went through PCG Tools' own published
feature list (kronoshaven.com/pcgtools/) point by point against what this app does today.
Triaged into three buckets -- not a commitment to build anything, just recorded so the
gaps are deliberate choices, not accidental blind spots.

**Already have (some arguably better than a static list-based tool)**:
  - Loading/navigating Programs, Combis, Set List slots -- plus live cross-link jump
    buttons everywhere one references another, with per-pane Back/Forward history
    (PCG Tools has no equivalent to this -- it's a list/editor, not a browser with
    navigation).
  - Set List slot moving/sorting -- real drag-and-drop reorder/insert AND A-Z/Z-A physical
    sort, arguably more capable than PCG Tools' plain "move/sort."
  - Copying Programs between files (drag-and-drop, cross-dataset).
  - Editing Set List slot names/Comments, including multi-line Comments (`\r\n`) -- matches
    PCG Tools' own "editing set list slot descriptions with use of Return characters."
  - Opening multiple files simultaneously (any number of datasets, either pane can show
    any of them).
  - Finding duplicate Programs (byte-exact hash) -- and, unlike PCG Tools' list-only
    approach, actually resolving a duplicate group in the UI (keep one, clear the rest,
    repoint every reference) -- see entries 31-33 above.
  - Basic `.SNG` file support (same parser, same UI).

**Missing, worth looking at later** (real gaps, not obviously out of scope):
  - **Renaming Programs and Combis** -- Set List slot names are editable; Program/Combi
    names are not. A real, surprising gap given how core renaming is to cleanup work.
  - **Combi duplicate detection** -- `findDuplicatePrograms()`/`CombiInfo` deliberately has
    no `contentHash` field ("duplicate detection was only requested for Programs" --
    `PcgFile.h`'s own comment). Same mechanism could extend to Combis if wanted.
  - **Reordering Timbres within a Combi** (move up/down) -- Combi Timbres are read-only
    except for the new duplicate-resolution repoint (which changes a reference, not
    position). PCG Tools has this.
  - **Moving/sorting Programs and Combis within their own banks** -- only Set List slots
    have real reordering; Programs only have cross-slot *copy*, Combis have neither.
  - **"Compact empty slots to the end"** for Program/Combi banks specifically -- Set Lists
    already get this as a side effect of A-Z/Z-A sort; Program/Combi banks have no
    equivalent operation at all.
  - **List export** (CSV/text/HTML of bank usage, Program-to-Combi/Set-List cross-
    references) -- the data already exists and is shown interactively (Program usage rows,
    Combi Set List badges); exporting it as a static reference sheet is a relatively small
    incremental step on top of what's already built, and fits this project's own stated
    motivation (auditing/cleaning up a big backup) directly.
  - **Cross-file Set List slot copy** -- currently deliberately blocked (a slot's Program/
    Combi reference is a raw bank/number pointer, meaningless in a different file's bank
    layout without translation) -- PCG Tools apparently allows this; would need real
    thought about what "translate the reference" even means before attempting it, not a
    quick fix.
  - **Drum Kits** -- `DKT1` chunk exists in the container format but is entirely
    unparsed/unexplored (see [The file format](/format)'s open questions). PCG Tools
    navigates them; this app doesn't parse them at all yet.

**Deliberately out of scope / not planned** (silently ignored until now -- now explicit):
  - **Other Korg workstation models** (Krome, Oasys, etc. -- PCG Tools supports several;
    this project's own name/scope is Kronos-specific, and nothing suggests broadening
    that).
  - **Cubase-specific patch list export** -- too tool/DAW-specific a format to be worth
    building for this project's own stated goals, versus the more general list-export idea
    above.
  - **"Converting" Programs/Combis between models** -- PCG Tools explicitly does NOT do
    this either ("Copying... not converting"), so there's no gap here at all, just
    confirming the two projects agree on scope.

Not acted on yet -- this is a triage/comparison record, not a build plan. Worth revisiting
before picking the next feature to build.

--- EXPLORATION: CREATING A DATASET FROM NOTHING, AND WHOLE-BANK PROGRAM REORDER (2026-08-14, NOT DECIDED) ---

Two questions asked directly, answered here rather than guessed at.

**Can we synthesize a brand-new, empty dataset (Set Lists + a HD-1 bank + an EXi bank +
a Combi bank) without loading a real Kronos backup first? FURTHER INVESTIGATION
REQUIRED -- not confident enough yet, for real, specific reasons, not just caution:**
  - What we DO have solidly: real "Init Program"/"Init EXi Program" template bytes for a
    blank Program slot, both engine types (`resources/Init-Program-HD1.raw`/
    `Init-Program-EXi.raw`, entries 31-33 above) -- and an extensively-exercised
    "genuinely empty Set List slot" shape (every drag-and-drop/sort/copy operation
    already reads and writes these for real).
  - What we DON'T have: no extracted or verified **blank Combi** template at all --
    every Init Program effort so far was Programs-only. The chunk header's own `dwX`
    field ([The file format](/format) open question #1) has never had its *meaning*
    confirmed, only its position/width -- fine when copying it through unchanged from
    an existing valid file, not fine if we'd have to invent a value from nothing. The
    file-header **checksum flag** (open question #11) reads "checksum present" in our
    one real sample, but where that checksum would actually live and how it's computed
    has never been investigated -- if real hardware validates it, a synthesized file
    could be silently rejected. Whether a real backup can legally *omit* a bank at all
    is itself still an open question (#13), so we don't even know if "an empty PRG1
    bank with zero records" is a well-formed thing a real unit accepts.
  - Most fundamentally: every real-hardware verification this project has ever done
    started from an already-valid real file and either just *read* it, or made a
    *small edit* to already-valid bytes (see the User Guide's own "Save As, then load
    onto a Kronos" testing workflow). This project has never tried building a whole
    file's bytes from nothing and having a real Kronos accept it as a valid backup --
    that's a different, larger act of trust than editing one field of a file that was
    already known-good, even where every individual piece is well understood.

**Can we A-Z/Z-A reorder Programs within one bank, like Set Lists already do? Yes,
architecturally -- the primitives already exist, and the real challenge is exactly the
one flagged when asking: repointing every reference, file-wide, not just within the
bank.**
  - The key difference from Set List sorting: a Set List slot has no INCOMING
    references (nothing else in the file points at "Set List 5, slot 12" specifically),
    so `sortSetlist()` is a pure, local, 128-record shuffle. A Program is a heavily
    *referenced-by* target -- every Set List slot across all 128 lists that points at
    it, and every Combi Timbre across every Combi that references it (via the raw-code
    translation, see §6.2), would need to be found and repointed to its new position.
    Reordering a whole bank typically moves most of its records, not just two swapping
    -- so this is the same repointing mechanism `resolveDuplicates()` (entries 31-33
    above) already proved out, just applied to potentially all 128 Programs in one
    bank at once instead of one duplicate.
  - Real shape this would take: (1) snapshot all 128 records' raw bytes up front, same
    "read everything before writing" discipline `sortSetlist()` already uses so a
    record about to move never races its own read; (2) compute an old-position ->
    new-position mapping (same alphabetical/empties-at-the-end convention as Set List
    sort); (3) write all 128 records into their new positions; (4) sweep every Set List
    slot and every Combi Timbre reference in the *entire file* and repoint any hit
    against the mapping.
  - Not a reverse-engineering unknown like the question above -- no blocking "do we even
    know how" gap, just real, substantial engineering (and worth validating performance
    on a large real file before committing to it, though the existing usage-scanning
    code this app already runs on every load suggests it'd be fine).

Neither of these is being built now -- recorded so the reasoning isn't lost, matching
this project's own "note it, don't guess, don't silently build it either" pattern.

--- BLIND SPOTS / NOT YET TOUCHED ---

Format:
  1. SBK1 +17's bit3 and bits0-2 -- still unexplained now that bit4 and
     bits5-7 are confirmed as Font size/Transpose (see above). Real files
     show isolated non-zero values there independent of either confirmed
     field, so something real is still unaccounted for.
  2. What the `used`/count header field (present in SDB1/SBK1/CBK1/MBK1/
     PBK1 alike) actually counts.
  3. The 4-byte prefix field preceding every chunk header throughout the
     whole format.
  4. Exactly which of the 20 PRG1 banks maps to which display label --
     lookup mechanism confirmed, specific label-per-index is not.
  5. DKT1 (Drum Kits), WSQ1 (Wave Sequences), GLB1, DPI1 -- entirely
     unexplored. Unknown whether Set List slots can reference these
     directly (if so, instrument-name lookup has a gap there too).
  6. The older SoundQuest `.SQS` backup dialect (`LIST`/`FORM`/`BANK`
     wrapping) found under `~/Documents/Sound Quest/` -- structurally
     different from the `KORG`/`PCG1` dialect this parser targets; never
     tested against it.
  7. **UNCONFIRMED (project owner's own recollection, 2026-08-07, not yet
     independently tested by this project)**: a Set List slot's NAME (SDB1,
     the 24-byte field distinct from SBK1's Comment) must be a single line
     -- no embedded line breaks -- and names longer than 24 characters get
     truncated to fit on the real device. The 24-character figure matches
     this project's own already-confirmed byte layout exactly
     (`kRecordSize`(28) - `kMarkerSize`(4) = 24 bytes, see `PcgFile.cpp`'s
     `readRecordName()`), which is a good consistency sign, but the actual
     *behavior* on real hardware -- silent truncation vs. some other
     handling when a user types past the limit -- hasn't been checked.
  8. **UNCONFIRMED (same source/date as #7)**: the Comment field truncates
     after 512 characters on the real device. Doesn't cleanly match this
     project's own confirmed byte math: SBK1's Comment field spans
     `RECORD_SIZE`(542) - `COMMENT_OFFSET`(18) = 524 bytes, minus 1 for the
     NUL terminator (`setlist-comment.js`'s `encodeSetlistComment()` already
     enforces this exact `RECORD_SIZE - COMMENT_OFFSET - 1` = 523-byte cap)
     -- an 11-byte gap between the recalled 512 and the byte-derived 523
     worth resolving: either the real device enforces a tighter UI-level
     limit than the byte layout technically allows, or one of these two
     numbers is slightly off (a rounded recollection vs. an exact byte
     count). Not yet reconciled -- needs an isolated real-hardware test
     (e.g. write exactly 512 and exactly 523 ASCII characters via this
     project's own write path, see what the device actually shows/accepts)
     before either number goes in `docs/README.md` as confirmed.
  9. **NEWLY SURFACED, 2026-08-07, while building the Internals pane (see
     "ARCHITECTURE" below)**: every Program/Combi bank "index" this project uses
     anywhere (`ProgramInfo::bank`, `programBankTypes()`, the whole Programs/
     Combis tables, Timbre cross-referencing, `copyProgramFrom()`'s destination
     checks -- literally everywhere) is actually just that bank's POSITION among
     however many PRG1/CBK1 sub-bank chunks were found, in file order --
     `PcgFile.cpp`'s PRG1-parsing loop assigns `bankIdx` purely as a loop
     counter, with no reference to any per-chunk identity field. This has been
     silently correct so far only because every real file examined happened to
     contain a complete, canonically-ordered set of banks. The project owner's
     own observation (saving a backup apparently lets you choose which data to
     include) means that assumption may not hold in general: a file missing,
     say, canonical bank 4 would silently relabel every later bank as one
     position earlier than its real identity, everywhere in this app, with no
     way currently to detect it. Directly related to blind spot #4 above (label-
     per-index confidence) and #2 (the `used`/count header field) -- and each
     PRG1/CBK1 sub-bank chunk's own first 4 bytes (currently read and discarded,
     "meaning not understood yet") are a real candidate for a per-chunk bank-
     identity field that would fix this properly. Not yet investigated with
     real test data that's actually missing a known bank -- the Internals pane
     surfaces the current (possibly wrong, if anything's missing) file-order
     numbering honestly rather than hiding the uncertainty.
     **Confirmed directly, 2026-08-07** (real hardware behavior, see
     `docs/README.md` §5/§5.2): engine assignment (HD-1/EXi) is a global,
     per-bank setting -- a bank can never mix engines -- and bank storage is
     all-or-nothing, always exactly 128 slots, never partial. This doesn't
     resolve the file-order-vs-identity gap above, but it does sharpen it: a
     "missing bank" can only mean a whole PRG1/CBK1 sub-bank chunk absent
     entirely, never a partially-saved or mixed-engine one -- so whatever
     eventually identifies a bank properly (e.g. the unexplored first-4-bytes
     field above) only ever has to answer a binary present/absent question
     per bank, not a "how much of it is there" one.
  10. **FLAGGED 2026-08-14, not yet investigated**: a Program record's own
      "KARMA Common" section (Korg's own `Prog_HD-1.txt`/
      `Prog_EXi_Common.txt`) has eight each of "SwitchN Name ID"/"FaderN
      Name ID" fields (`0000~02FF`, a numeric index into up to 768
      entries), plus KARMA's separate GE (Generated Effect) module
      structure (`docs/external/KORG/KARMA_GE_RTP.txt`, its own name plus
      up to 32 named real-time parameters). Whether any of these are
      simple self-contained values (safe to copy verbatim) or references
      into a KARMA Scene/GE library that isn't necessarily identical
      between two files -- or even two banks of the same file -- is
      completely unknown. Real, practical consequence: `PcgFile::
      copyProgramFrom()` (used by same-dataset Program drag-copy AND the
      new cross-dataset Combi copy's own Program placement, entry 37
      above) copies a Program's entire raw record verbatim with no
      special handling for these fields -- if any turn out to be
      file/bank-relative references, a copied Program's KARMA behavior
      could silently end up wrong with no error at all. Noted in
      docs/content/format/index.md §8 #15 and the User Guide's Current
      Limitations, and flagged to real users as something worth reporting
      if hit -- not reproduced against a real file yet.

App/UI:
  8. Leading spaces reportedly disappearing from Comment text somewhere in
     the round-trip -- reported once, not yet reproduced. Neither
     `readComment()` nor `setComment()` does any trimming, so the cause
     isn't obvious from code inspection alone; repro steps needed (typed
     fresh via Apply vs. already present in the source file).
  9. **First piece BUILT 2026-08-06; real-hardware round-trip CONFIRMED the same
     day for a minimal file** -- still real gaps remain, see below.
     `PcgFile::save()`/`EditorBridge::saveFileAs()` (see "ARCHITECTURE" above)
     write the retained bytes straight to disk -- a naive verbatim `data_`
     dump, no Save UI wired up (no dialog, no dirty-tracking). The project
     owner's actual bar was higher than "this app can re-open the file":
     loading it back onto real hardware -- **confirmed working**: a file
     generated by `tools/generate_setlist_test_matrix.{js,cpp}` (a minimal
     SDB1/SBK1-only file, one real entry plus 20 generated test slots) loaded
     onto a real Kronos with no issues, and every generated slot (Comment/
     Color/Volume/Font size, plus the Group 4 word-wrap probe) read back
     exactly as written -- see the "ARCHITECTURE" entry above for the full
     wrap-point results. This is real evidence the naive verbatim dump is
     hardware-acceptable, not just accepted by this app's own reader.
     **Still open**: this was only tested against a minimal file with no
     PRG1/CMB1/etc. content -- a full real backup (this app's actual primary
     use case) round-tripped through load-edit-save hasn't been checked yet,
     and the file-header checksum flag (§1.1, byte offset 8, confirmed
     present as `0x01` on the one real sample checked so far, but where any
     such checksum would actually live and over what byte range is still
     completely uninvestigated, see Format blind spot list above) remains a
     real question mark for a larger/more complex file even though this
     minimal one worked.
  10. Filter/search and row drag-swap/drag-copy interactions have been
      exercised by the project owner in the real app for file-open and
      name-lookup verification, but not explicitly confirmed end-to-end
      for the swap/copy drag gestures themselves or the Set List picker
      dropdown -- worth a deliberate pass.
  11. **RESOLVED (2026-08-03)**: the NSOpenPanel-behind-the-window bug was
      specifically in CHOC's own WebView-triggered delegate
      (`beginSheetModalForWindow:completionHandler:`, a *sheet* attached to
      the WKWebView's window). `src/platform/NativeFileDialog.cpp` (see the
      NATIVE FILE DIALOG + PROGRESS section) calls `NSOpenPanel`/
      `NSSavePanel` directly via `runModal` -- app-modal, not sheet-attached,
      a genuinely different code path -- bypassing that delegate entirely.
      **Confirmed working in the real app**: the panel appears in front and
      loads a dataset successfully. Drag-and-drop-to-open has since been
      removed now that this is fixed -- "Open..." is the only way to load a
      file. Save dialog exists at the native layer but isn't wired to any
      UI yet (nothing meaningful to write -- no encoder exists).
  12. The sibling reference CHOC project (conventions, CI pipeline) still
      not linked in -- this scaffold's choices (no Bootstrap, plain CSS,
      specific file-open pattern) may get reconciled once it is.
  13. (resolved) CI now exists: `.github/workflows/hugo.yml` (docs site)
      and `.github/workflows/native-build.yml` (macOS arm64/Intel, Linux,
      Windows, path-filtered to skip docs/frontend-only pushes).
  14. **RESOLVED (2026-08-01)**: committed test infrastructure now exists
      on both sides. C++: `tests/pcg_file_test.cpp` + a scoped
      `pcg_file_test` CMake/`ctest` target, depending on *only*
      `PcgFile.cpp`/`ProgramDecoder.cpp`/`CombiDecoder.cpp` (not
      `main.cpp`/`EditorBridge.cpp`/CHOC) -- builds a small synthetic
      `.PCG` byte buffer in memory
      (real files are large and `.gitignore`'d) exercising
      `loadFromMemory()` end-to-end: Set List names, masked Font
      size/Transpose decoding (including deliberately-poked garbage bits
      in fields it doesn't own), Program bank cross-referencing,
      `findDuplicatePrograms()`, `programSetlistUsages()`, and
      `decodeProgram()`'s on-demand re-decode; extended same-day to cover
      the new Combi decoder too (`decodeCombiFields()` directly, plus
      `combis()`/`decodeCombi()` through a synthetic CBK1 bank). Runs via
      plain `ctest` in ~0.01s. Frontend: `setlist-comment.js` now has a headless,
      `node`-runnable `setlist-comment.test.js` alongside its existing
      `.test.html` browser harness, both importing the same real-byte
      fixture from a new shared `frontend/components/kronos/
      test-fixtures.js` (so they can't drift into testing different
      data) -- exits non-zero on any failed assertion, the shape
      CI/`ctest`-style automation needs. (A `frontend/components/
      package.json` with `"type": "module"` was also added -- without it
      Node mis-parses genuine ES module `export`/`import` syntax in a
      bare `.js` file and throws a confusing "does not provide an
      export" error.) Both suites were spot-checked with a deliberately
      broken assertion to confirm they fail loudly and non-zero, not just
      pass trivially. Next: proceed to the Combi decoder, per the
      already-agreed sequencing.
  15. **PARTIALLY RESOLVED (2026-08-03)**: an indeterminate spinner now covers
      the pane while a dropped file's base64-encode/decode+parse is in
      flight (see the EXPLORATION section's per-pane UI notes) -- still not
      a real percentage bar, which needs the encode/transfer restructured
      into chunks with progress callbacks rather than one monolithic step,
      not done.
  16. **RESOLVED (2026-08-01)**: the Library view has now been clicked
      through end to end in the real app (see the Datasets entry in
      "ARCHITECTURE" above) -- confirmed working.
  17. **RESOLVED (2026-08-02)**: drag-and-drop file loading -- a pane (or
      its sibling, if the drag passed over it on the way to the actual drop
      target) could stay visually marked as a drop target after a dataset
      had already loaded. Root cause confirmed as hypothesized: `pane.js`'s
      old `dragleave` handler only cleared the highlight when `ev.target
      === root`, but dragenter/dragleave fire per-element as the pointer
      crosses into/out of *child* elements too (the table, the
      dataset-select), so a `dragleave` targeting a child rather than
      `root` itself left the class stuck. Fixed with the standard
      enter/leave depth counter (immune to which descendant the event
      targets), plus every pane's highlight is now explicitly cleared in
      the `drop` handler (not just the pane that received the drop) to
      cover the sibling-pane case directly, regardless of exact event
      delivery order for a given drag session.
  18. **PARTIALLY RESOLVED (2026-08-08)**: Library's Duplicates tab showed
      "n/a" for a duplicate Program's Combi reference count -- noticed
      during an earlier click-through, and confirmed NOT a bug: reading
      `EditorBridge::findDuplicatePrograms()` showed no logic difference
      from the Programs tab's (also gated on
      `isConfirmedTimbreProgramBank(program.bank)`), and real duplicate
      Programs (often "Init Program"-style placeholders) do tend to sit
      outside the confirmed range -- the honest "don't know" case working
      as designed. Since then, the confirmed range itself was widened (see
      the ARCHITECTURE entry below) from INT-A..D (4 banks) to 8 banks
      total (INT-A..D plus USER-A/D/F/AA), using ground truth
      (docs/README.md §6.2) that was already independently confirmed but
      not yet wired into the Combi-usage-counting logic. **Still open**:
      every bank beyond those 8 remains genuinely unconfirmed, so "n/a"
      for a duplicate in, say, INT-E or USER-B is still correct, honest
      behavior, not a bug -- if a duplicate group ever shows "n/a" for a
      Program in one of the now-8 confirmed banks, that would indicate a
      real, distinct bug worth re-investigating.
  19. **Cosmetic, low-priority, unconfirmed**: possible padding-top
      inconsistency in the Internals pane (2026-08-08) -- the project
      owner reported the "Top-level chunks" topic's `.internals-chunk-row`
      shows the `padding-top: 8px` gap under its topic-row title
      correctly, but wasn't sure whether "Program banks"/"Combi banks"
      (`.internals-bank-groups`, same CSS property, same mechanism) show
      the same gap. Code inspection found no structural reason the two
      would differ, and this was folded into a broader debugging thread
      that turned up a real, confirmed, separate bug in the same report
      (`.internals-empty[hidden]` losing to Bulma's `.help{display:block}`,
      see App/UI blind spot list resolution history and `style.css`) --
      that real bug was fixed; this specific padding question was
      explicitly marked cosmetic/not worth chasing further right now.
      Revisit by comparing the two sections directly in the real app if it
      resurfaces.
  20. **RESOLVED (2026-08-10)**: USER-A/D/F/AA's `kConfirmedTimbreBanks`
      file-order indices (PcgFile.cpp and its frontend mirror,
      `library.js`'s `CONFIRMED_TIMBRE_BANKS`) were wrong -- 8/11/13/14
      instead of the real 6/9/11/13. Root cause: the original indices
      assumed `INT-A..G` was 7 letters (indices 0-6) with `USER-A` starting
      at 8; the project owner checked real hardware and confirmed there is
      no `INT-G` bank at all (what's shown after `INT-F` is `GM` then
      `g(d)`, neither stored per-file) and that `USER-A..G` is genuinely 7
      single-letter banks, not 6 -- so USER-A starts right after INT-F, at
      index 6. This is what had been causing Combi U-A 016 Timbre 2 (raw
      bank 17/USER-A, raw program 47) to resolve to the wrong Program
      ("Xfade StagePianoATK Kn5" instead of the correct "EXi Overdrive
      Organ") -- not a Program-number translation issue as first suspected,
      just the bank index. Confirmed via `setlist_test_2.PCG`: file-order
      index 6/record 47 reads "EXi Overdrive Organ" exactly; the project
      owner independently confirmed index 6/9/11/13's record 0 names
      ("Doubled Screamer"/"Vibraphone 2"/"Harmonic Bass/Lead"/"The Temple
      SW1") against real hardware too, plus index 6/record 16 = "Big
      Sleep". Also resolves the previously-flagged USER-G contradiction
      (docs/content/format/index.md §6.2/§8#8): it's a real 7th
      single-letter USER bank -- the project owner confirmed its position 0
      is "JB: Africa Drum" on real hardware, matching file-order index 12
      exactly, so `USER-G` (raw code 23) got a full index+code+name entry
      in `kConfirmedTimbreBanks`, promoted out of the name-only table.
      Raw code 30 was also resolved the same day: with `USER-AA..GG`
      understood to run file-order index 13-19, code 30 = `USER-GG`
      (index 19) -- confirmed two independent ways, both landing on "JMJ
      Theremin": the raw Combi Timbre bytes for Combi U-A 016 Timbre 3
      (program 15) point at index 19/record 15 in `setlist_test_2.PCG`,
      and the project owner separately confirmed by browsing directly to
      Program bank `USER-GG` position 15 on real hardware. `USER-GG` also
      got a full `kConfirmedTimbreBanks` entry. Fixed in `PcgFile.cpp`,
      `library.js`, `tests/pcg_file_test.cpp`, and docs/content/format/
      index.md §5.2/§6.2/§8. **Still open**: `USER-B/C/E` and
      `USER-BB/EE/FF` still lack independent file-order-index confirmation
      even though B/C are already name-confirmed.
  21. **RESOLVED (2026-08-11)**: entry 20 above turned out to be
      incomplete -- it fixed `PcgFile.cpp`'s `kConfirmedTimbreBanks` (used
      for Combi-usage counting and a Timbre's own bank-code lookup) but
      missed a second, independent hardcoded Program-bank-name list,
      `frontend/pane.js`'s `PROGRAM_BANK_NAMES` -- the array the Programs
      panel/bank filters/Internals pane actually use to label a Program's
      *own* bank. It still had the old wrong order (`"I-G"`/`"G(d)"` at
      index 6/7, everything from 6 onward off by 2), so the UI kept
      showing wrong bank labels even after entry 20 shipped. The project
      owner caught this by checking position-0 names for literally all 20
      Program banks against real hardware at once, which also fully
      confirms §5.2's bank order/labels end to end (not just the specific
      indices individually verified before -- see docs/content/format/
      index.md §5.2). Root-caused as an actual duplicate-mapping bug (two
      hardcoded name lists, C++ and JS, that could disagree with no test
      or build error catching it) and fixed structurally, not just
      re-synced: `kConfirmedTimbreBanks` no longer has a `name` field at
      all (removed, not just corrected) -- `frontend/library.js`'s
      `formatTimbreRef()` now derives a Timbre's bank label from
      `PROGRAM_BANK_NAMES[programBankIndex]` for any code with a confirmed
      index, and `PcgFile.cpp`'s `timbreBankName()` only ever resolves a
      name for the handful of codes that have NO confirmed index
      (`kConfirmedTimbreBankNamesOnly`) -- there is now exactly one place
      ("index 6 = USER-A") each fact is spelled out, never two. Also
      updated `frontend/mock_bridge.js`'s fake Timbre data (`bankName: ""`
      for confirmed-index codes) so mock-mode dev/testing exercises the
      same contract as the real bridge, and `EditorBridge.cpp`'s own doc
      comment. Fixed in `PcgFile.cpp`, `EditorBridge.cpp`, `library.js`,
      `pane.js`, `mock_bridge.js`, `tests/pcg_file_test.cpp`, and
      docs/content/format/index.md §5.2/§6.2.
  22. **RESOLVED (2026-08-11)**: reported immediately after entry 21 shipped
      -- Combi U-A 002 "Sex on Fire" Timbre 2 (raw bank 5/`INT-F`, raw
      number 71) showed the bank label but no Program name at all, even
      though the Programs panel itself showed "Vocal Dancing" for that
      exact bank/number. Cause: `INT-F` was one of five raw codes
      (`INT-F`/`USER-B`/`USER-C`/`USER-CC`/`USER-DD`) confirmed by name
      against real hardware but with no matching PBK1 file-order index, so
      `isConfirmedTimbreProgramBank()`/`programBankForConfirmedTimbreCode()`
      returned false/null for them and `library.js`'s `formatTimbreRef()`
      skipped its Program-name lookup entirely (that lookup is gated on a
      confirmed index, entry 21's fix). Once §5.2's full 20-bank order got
      confirmed (entry 21), all five turned out to already have a confirmed
      index too (5/7/8/15/16) -- promoted into `kConfirmedTimbreBanks`
      directly rather than just patching `INT-F` alone, since `USER-B/C/
      CC/DD` had the identical latent bug, just not yet reported. Verified
      against real bytes: index 5/record 71 in `setlist_test_2.PCG` reads
      "Vokal Dancing", matching "Vocal Dancing" on the real unit. The
      now-permanently-empty `kConfirmedTimbreBankNamesOnly` table was
      removed entirely (not left around empty) -- `timbreBankName()` is now
      a stub returning `""` always, kept only in case a future raw code is
      confirmed by name without landing on one of the 20 known bank
      positions. Fixed in `PcgFile.cpp`, `tests/pcg_file_test.cpp`,
      `library.js`, and docs/content/format/index.md §6.2.
  23. **BUILT (2026-08-12), not yet committed**: two Combi-panel UI
      features, both requested directly. (a) A Set List filter dropdown
      next to the Combis panel's None/All/Invert buttons
      (`library.js`'s `selectedSetlistIndex`/`buildSetlistFilterSelect()`)
      -- filters the Combi table to only Combis referenced by a chosen Set
      List, reusing `c.setlistUsages` (already loaded per Combi for the
      existing "Set Lists"/"#STL" columns, `PcgFile::setlistUsageCounts()`)
      with no new backend work -- purely a client-side re-slice of
      already-loaded data, not performance-sensitive even at max file size
      (16,384 Set List slots / 1,792 Combis). (b) A Combi Timbre row's bank
      reference (e.g. "I-A 017") is now a `.bank-jump-button`, same look as
      the Setlist table's own Bank button, only when the raw code has a
      confirmed Program-bank index -- clicking it reuses `pane.js`'s
      existing `jumpToInstrument`/`onJumpToInstrument` mechanism (already
      built for the Setlist table's Bank button) to switch that SAME pane
      to Programs and scroll to the entry -- no new navigation plumbing
      needed, just threading the existing per-pane closure into
      `createLibraryPanels()` too. Fixed a real edge case the Set List
      filter (a) introduced for existing Setlist-to-Combi jumps:
      `jumpToEntry()` now resets `selectedSetlistIndex` on any Combi jump,
      so an active filter can't hide the entry someone just navigated to.
      Verified via `node --check` and a headless page-load smoke test only
      -- no browser-automation tool (Puppeteer/jsdom/CDP client) available
      in this environment, so full interactive click-through hasn't been
      done; flagged explicitly rather than claimed. Files: `library.js`,
      `pane.js`, `style.css`.
  24. **RESOLVED (2026-08-12)**: three more Combi Timbre raw bank codes
      confirmed directly against real Combis in `setlist_test_2.PCG` --
      `USER-BB` (25) and `USER-EE` (28) fit the expected pattern exactly.
      `USER-E` did NOT: every other single-letter USER bank (A/B/C/D/F/G)
      sits contiguously at 17-23, so 21 was the obvious guess for E and was
      deliberately left unconfirmed rather than assumed -- real hardware
      says it's actually raw code **4**, confirmed via Combi I-A 000
      "K-Lab: Katja's House" Timbre 7 (program=61/bank=4, exact byte match).
      Why E alone breaks the pattern isn't understood -- recorded as an
      open oddity in docs/content/format/index.md §6.2, not explained away;
      explicitly flagged there that `USER-FF` (the one remaining
      unconfirmed double-letter code) shouldn't be assumed at 29 on pattern
      alone given this precedent. `kConfirmedTimbreBanks` now has 18
      entries. Fixed in `PcgFile.cpp`, `tests/pcg_file_test.cpp`,
      `library.js`, and docs/content/format/index.md §6.2/§8.
  25. **RESOLVED (2026-08-12)**: a genuinely new category of Combi Timbre
      raw bank code confirmed -- `GM` (raw code 6), via Combi U-A 030
      "Bad Name" Timbre 2 (program=91/bank=6 in `setlist_test_2.PCG`,
      exact match for the project owner's real-hardware report). Unlike
      every code in entries 20/21/22/24 above, `GM` is not "unconfirmed
      pending an index" -- it structurally can NEVER get one: `GM` is
      fixed MIDI-spec content, not one of the 20 stored PBK1/MBK1 Program
      banks (§5.2/§5.4), confirming the exact scenario entry 21's removed
      `kConfirmedTimbreBankNamesOnly` table had flagged as "unlikely but
      not structurally impossible." Reintroduced that table (now correctly
      documented as permanently-indexless, not temporary) with just this
      one entry -- `timbreBankName(6)` returns `"GM"`,
      `isConfirmedTimbreProgramBank()`/`kConfirmedTimbreBanks` correctly
      never gain an entry for it. No Program name is shown for a GM
      reference (nothing in this file's own data to look one up from --
      a real General-MIDI-instrument-name table would be a separate
      feature decision) and no jump-to-Program button (only renders for a
      confirmed index). Fixed in `PcgFile.cpp`, `EditorBridge.cpp`,
      `tests/pcg_file_test.cpp`, `library.js`, `mock_bridge.js` (added a
      GM fake-Timbre example), and docs/content/format/index.md §5.4/§6.2.
  26. **RESOLVED (2026-08-12)**: four more permanently-indexless Combi
      Timbre raw bank codes confirmed the same way as `GM` (entry 25) --
      `G(1)`/`G(2)`/`G(3)`/`G(4)` at raw codes 7/8/9/10, via Combi I-C 022
      "Rainbow Bridge" Timbres 1-4 (program=122/bank=7..10 in
      `setlist_test_2.PCG`, exact match for the project owner's
      real-hardware report -- also caught a typo in how the Combi name was
      reported, "Brodge" vs the real "Bridge"). These sit right after `GM`
      (6) as a contiguous block, consistent with §5.2's own note that the
      real Program bank browser shows "GM" then "g(d)" right after
      `INT-F` -- likely that same "g(d)" family, though this project
      doesn't know Korg's own official name/purpose for `G(1)`..`G(4)`
      and isn't guessing. `kConfirmedTimbreBankNamesOnly` now has 5
      entries. The project owner also reported specific Program names at
      each ("Rain"/"Thunder"/"Wind"/"Stream", suggestive of a GM2 SFX/
      nature-sound kit) -- not stored anywhere in this codebase, same
      "separate feature decision" as GM's own instrument names. No JS
      changes needed -- `library.js`'s name-only fallback path already
      handled this generically. Fixed in `PcgFile.cpp`,
      `tests/pcg_file_test.cpp`, and docs/content/format/index.md §6.2.
  27. **Diagnostic pass (2026-08-13)**: scanned every Combi Timbre in all 5
      real sample `.PCG` files for raw bank codes not yet in
      `kConfirmedTimbreBanks`/`kConfirmedTimbreBankNamesOnly` (throwaway
      probe, not committed). Only 5 distinct codes turned up: 21 (1,228
      occurrences -- overwhelmingly the most common, across ordinary
      orchestral/guitar/brass Combis, suggestive of a heavily-used normal
      bank like the still-unconfirmed `INT-E`) and 11/12/13/15 (2-6
      occurrences each). Reported back to the project owner with examples
      to check on hardware.
  28. **RESOLVED (partial) 2026-08-13**: `g(5)`/`g(6)`/`g(7)`/`g(9)` (codes
      11/12/13/15) confirmed the same permanently-indexless way as `GM`/
      `G(1)`..`G(4)` (entries 25/26) -- extend that same contiguous block
      with no gap (`g(8)`, code 14, not checked, not assumed).
      `kConfirmedTimbreBankNamesOnly` now has 9 entries. **FLAGGED, NOT
      resolved**: the project owner also reported `code 21 = USER-E`, but
      `USER-E` was already independently confirmed as raw code **4** in
      entry 24 (verified against real bytes at the time). Two codes can't
      both be `USER-E` under a consistent scheme -- neither code 4 nor
      code 21 was touched pending clarification; code 21's own real bytes
      (Combi I-A 001 "Stradivarius Goes POP" Timbre 7, program=73) and its
      unusually high occurrence count (1,228 -- see entry 27) are recorded
      in docs/content/format/index.md §6.2 as a clue for whoever resolves
      this next. Fixed in `PcgFile.cpp`, `tests/pcg_file_test.cpp`, and
      docs/content/format/index.md §6.2.
  29. **RESOLVED (2026-08-14), RETRACTING entry 24's `USER-E`=code-4
      finding**: the project owner re-checked the exact same real Combi
      (I-A 000 "K-Lab: Katja's House" Timbre 7, raw bytes byte-identical to
      before -- program=61/bank=4) and confirmed real hardware actually
      shows `INT-E`, not `USER-E`, for that reference. Entry 24's "genuine
      surprise" framing was itself the mistake -- a first-transcription
      error, caught only by re-verifying the specific hardware reading
      rather than trusting it. Corrected: `INT-E` is raw code 4 (index ==
      code, same coincidence as `INT-A..D`, no anomaly after all); `USER-E`
      is raw code 21 (entry 28's own flagged conflict), confirmed via a
      different real Combi (I-A 001 "Stradivarius Goes POP" Timbre 7,
      program=73) -- also exactly the "obvious" gap in `USER-A..G`'s 17-23
      block, resolving entry 28's flag too. Also fixed a real formatting
      bug in docs/content/format/index.md §6.2 from an earlier edit (a
      duplicated "Raw code 30" heading had orphaned the "One name, one
      place" section's own heading, leaving its body floating under the
      wrong title). `kConfirmedTimbreBanks` now has 19 entries, covering
      every one of the 20 Program bank indices except 18 (`USER-FF`) --
      the only remaining gap. Fixed in `PcgFile.cpp`,
      `tests/pcg_file_test.cpp`, `library.js`, and docs/content/format/
      index.md §5.2/§6.2/§8. Left as a methodology note rather than
      scrubbed from history, per this project's own standing practice
      (see §6.3's "A resolved anomaly" note) -- catching your own mistake
      is exactly what the verify-everything discipline is for.
  30. **RESOLVED (2026-08-14)**: `USER-FF` (raw code 29) confirmed via a
      real Combi (U-A 090 "Days like this" Timbre 1/2, program=87/bank=29,
      program=85/bank=29 in `setlist_test_2.PCG`), exactly matching the
      `+11`-offset pattern the rest of the double-letter series follows --
      unlike `INT-E`/`USER-E` (entry 29), this pattern-completion turned
      out correct, checked directly rather than assumed either way. This
      completes `kConfirmedTimbreBanks`: all 20 Program bank indices now
      have a confirmed raw Combi Timbre code, closing out the whole
      "unidentified codes" thread that started with entry 27's diagnostic
      scan. Fixed in `PcgFile.cpp`, `tests/pcg_file_test.cpp`,
      `library.js`, and docs/content/format/index.md §6.2/§8.
  31. **BUILT (2026-08-13)**: `resources/Init-Program-HD1.raw` and
      `resources/Init-Program-EXi.raw` -- real, cross-verified raw PBK1/
      MBK1 Program record bytes for a Kronos-factory "Init Program"/"Init
      EXi Program" slot, meant as this app's own known-good template for a
      future "clear a Program slot" write path (no such write path exists
      yet -- extraction only, this pass). Backing new `PcgFile::
      programRecordBytes(bank, number)` accessor (mirrors songRecordBytes()/
      nameRecordBytes()'s shape). Extracted from `setlist_test_2.PCG`
      (a representative HD-1 slot at bank 12/number 13, and EXi at bank
      19/number 21 -- first match of each by file order), then confirmed
      byte-identical against the same two names' records in the
      independently-different `test_1.PCG` -- genuinely stable Korg factory
      content, not something specific to one backup.
      - **Two real findings surfaced while verifying, both checked against
        two independent real files, neither yet acted on**:
        - `copyProgramFrom()`'s doc comment previously claimed HD-1=4960
          bytes / EXi=3706 bytes -- the EXi figure was never actually
          confirmed. Real data: every one of the 20 PRG1 sub-banks, HD-1 or
          EXi alike, uses 4960-byte records in both real files checked.
          Comment corrected in `PcgFile.h` (RECORD SIZE CORRECTION, not
          silently changed).
        - Every "Init Program"/"Init EXi Program" slot is byte-identical to
          every OTHER slot of the same name **within its own bank**, but
          bytes 2632-2633 differ consistently **across** banks (e.g. bank 12
          vs bank 17, both HD-1) -- first suspected as a per-bank identity
          tag. **RESOLVED same day**: cross-checked against Korg's own
          official parameter reference (`docs/external/KORG/Prog_HD-1.txt`
          and `Prog_EXi_Common.txt`, identical entry in both) -- it's "Tone
          Adjust" / "Switch8 On Value", a real Program parameter, nothing
          bank-identity-related. Still an open practical question, directly
          relevant to the actual reason this was asked for: a planned
          Duplicates-panel feature where clicking one copy of a duplicate
          Program keeps it and overwrites every OTHER duplicate slot with
          "the init program (according to the bank)" plus repoints their
          Combi/Set List references. A factory Init Program's Tone Adjust
          value isn't identical across every bank, so writing one bank's
          template into a different bank carries over whichever value the
          SOURCE bank's Init Program had -- not yet checked whether that's
          harmless on real hardware. Flagged in `PcgFile.h`'s
          `programRecordBytes()` doc comment so it isn't missed when that
          write path actually gets built.
  32. **BUILT (2026-08-13)**: the Duplicates-panel write path entry 31
      above was extracted for -- each duplicate copy's button (previously a
      "not built yet" toast) now really works. Clicking a copy makes it the
      only version: every OTHER duplicate in that group is cleared to its
      own bank's factory Init Program template, and every Set List slot /
      Combi Timbre that referenced a cleared duplicate is repointed to the
      kept one.
      - New `PcgFile::resolveDuplicates(keepBank, keepNumber, hd1InitBytes,
        exiInitBytes)`, all-or-nothing (template size checked against every
        affected bank before any write happens). Needed two new raw-record
        write pairs to support it: `putProgramRecordBytes()` (mirrors
        `putSongRecordBytes()`), and `combiRecordBytes()`/
        `putCombiRecordBytes()` -- the first real Combi write path this
        project has built (`CombiDecoder.h`'s own comment used to say "no
        encoder yet, every use is read-only"). `CombiDecoder.h` gained
        `timbreByteOffset()`/`writeTimbreProgramRef()` so the write side
        reuses `decodeCombiFields()`'s one copy of the 4806-base/188-stride
        derivation rather than a second one.
      - Combi Timbre repointing needs the PBK1-index -> raw-Timbre-code
        translation (`confirmedTimbreCodeForProgramBank()`, already used by
        `combiUsagesForProgram()`) -- now that all 20 Program banks have a
        confirmed code (entry 30), this basically never skips in practice,
        but the skip path (`combiRefsSkipped`) is still real, defensive
        code for a duplicate/kept bank without one.
      - `EditorBridge::resolveDuplicateProgram()` reads the two template
        files fresh off disk per call via a new `EDITOR_RESOURCES_DIR`
        macro (mirrors `EDITOR_FRONTEND_DIR`'s Debug-build pattern) --
        Release-build embedding via `tools/embed_resources.py` is a known,
        deliberately deferred gap (no Release build is packaged/shipped
        yet), not silently skipped.
      - Applies immediately with a toast reporting the counts, same
        convention as every other write in this app (drag-and-drop,
        Program copy, A-Z sort) -- no confirm dialog, no undo, confirmed
        directly rather than assumed.
      - Verified: `tests/pcg_file_test.cpp` gained `testResolveDuplicates()`
        (happy path incl. Set List + Combi repointing, no-such-keep-slot
        rejection, size-mismatch rejection writes nothing); full
        `pcg_file_test`/`kronos_editor`/`generate_setlist_test_matrix`
        rebuild clean; a throwaway probe against the real 36MB
        `setlist_test_2.PCG` resolved two genuine real duplicate groups
        (including one with an actual Combi Timbre reference, correctly
        repointed and confirmed via `combiUsagesForProgram()` before/after)
        -- not just the synthetic fixture.
  33. **RESOLVED (2026-08-14)**: two real bugs found after using entry 32's
      feature for real: (a) resolving a duplicate only refreshed the pane
      that triggered it -- the opposite pane's own separate copy of
      programs/combis/duplicateGroups never learned the file changed.
      Fixed with `refreshOppositeLibrary()` in `pane.js` (mirrors app.js's
      existing `onDropProgram` cross-pane-refresh pattern). (b) a cleared
      slot's Set List reference showed a stray/stale name after
      repointing, not the expected one -- traced (via a real-file byte
      dump, not guessed) to `Song::instrumentName`, a cross-reference
      cached once at load and never refreshed by `putSongRecordBytes()`;
      its own doc comment already flagged this as out-of-scope "since
      every editor using this path so far never touches bank/number" --
      `resolveDuplicates()` is the first one that does. Fixed by having
      `putSongRecordBytes()` re-resolve it via a new
      `PcgFile::resolveInstrumentName()` helper. Also: the Init Program
      template's real Korg name field turned out too subtle in the UI (a
      cleared slot looked identical to any other already-blank one) --
      `resources/Init-Program-HD1.raw`/`Init-Program-EXi.raw`'s name field
      now reads `"- Init Program (HD1) -"`/`"- Init Program (EXi) -"`
      (22 characters, fits the hard 24-byte limit) instead of Korg's own
      `"Init Program"`/`"Init EXi Program"` -- every other byte in both
      files is still the real, cross-verified extracted content, see
      docs/content/format/index.md §5.5. New pane-visibility toggle (Left
      only/Both/Right only, topbar `.level-right`, useful on small
      screens) added the same session, keyed to visual position
      (`:first-of-type`/`:last-of-type`) so it stays correct across
      `swapPanes()` either order.
  34. **BUILT (2026-08-14)**: Combi rearrangement -- swap, move within a
      bank, move to a different bank -- the last read-only table in
      `library.js` (Combis) is now draggable, mirroring what Set Lists and
      Programs already had. A Combi is only ever referenced by Set List
      slots (never by other Combis, unlike Programs which are also
      referenced by Combi Timbres), so this needed no Timbre-repointing
      dimension at all.
      - `PcgFile::repointSetlistReferences()` factored out of entry 32's
        inline `resolveDuplicates()` loop -- shared by all four write paths
        now instead of getting a fourth near-identical copy.
      - Three new `PcgFile` methods, all returning `{ok, error,
        setlistRefsRepointed}`: `swapCombis()` (same or different bank,
        never destroys anything so no restriction), `moveCombiWithinBank()`
        (shift, same mechanic as `sortSetlist()`), `moveCombiToBank()`
        (overwrites the destination -- refuses if the destination is still
        referenced by any Set List slot, a deliberate choice over silently
        orphaning or blanking those slots).
      - `moveCombiToBank()` needed a blank "Init Combi" filler for the
        vacated source slot, and this project had never extracted one for
        Combis. First search used the wrong signal (`name.empty()`) and
        found nothing -- caught directly: `U-A 100` in `setlist_test_2.PCG`
        is a real blank Combi literally named `"Init Combi"`, 1136-1178 of
        them across two real files. Within one bank every non-zero-numbered
        one is byte-identical, but across banks they differ by 40+
        unexplained bytes (open question, not investigated further this
        pass) and slot 0 of each bank carries one extra outlier byte at
        offset 3. Rather than ship a single resource file that would be
        wrong for 13 of 14 banks, the filler is sourced LIVE from another
        `"Init Combi"` slot in the SAME bank being vacated (name patched to
        `"- Init Combi -"`, same visibility convention as entry 33's Init
        Program templates) -- refuses the move if that bank has none.
      - Two real ordering bugs in `moveCombiWithinBank()`'s reference
        repointing, both only exposed by a real-file smoke probe (the
        synthetic unit-test fixture's shift distances were too short to hit
        either): doing the shift loop then a final repoint for the moved
        record's own referrers let the loop's first write collide with the
        final repoint's search; moving the final repoint before the loop
        instead just moved the same collision onto the loop's last step in
        the other direction. Fixed by snapshotting which Set List slots
        reference the record's ORIGINAL position via a pure search (no
        writes) before touching anything, then applying that repoint by
        identity after every other write is done -- immune to the collision
        because identity-based application never re-searches.
      - `EditorBridge`/`main.cpp` bindings and `mock_bridge.js` fakes follow
        entry 32's exact pattern. `library.js` drag gesture: drop onto
        another row = swap; drop before/after in the same bank = move
        within bank; drop before/after in a different bank = move to that
        bank (no shift concept spans two independent banks, so it collapses
        to the same as dropping onto that slot).
      - Verified: `tests/pcg_file_test.cpp` gained a dedicated fixture
        (`buildCombiRearrangeFixture()`, not the shared synthetic file,
        which asserts exactly one Combi record elsewhere) and
        `testCombiRearrange()` covering all three operations plus both
        refusal paths; full `pcg_file_test`/`kronos_editor`/
        `generate_setlist_test_matrix` rebuild clean; real-file smoke
        probes against `setlist_test_2.PCG` confirmed all three operations
        end to end, including `moveCombiToBank()`'s happy path (a
        genuinely-referenced Combi moved bank 7 -> bank 0, the destination
        inherited its 1 Set List usage, the vacated source read back as
        `"- Init Combi -"`) and its destination-referenced refusal
        (attempting to overwrite a bank whose only spare Combi -- an INT
        factory bank -- turned out to have zero "Init Combi" slots at all,
        correctly refused rather than fabricating bytes).
  35. **BUILT (2026-08-14)**: Combi copy, a fourth Combi rearrange gesture
      alongside entry 34's swap/move-within-bank/move-to-bank -- requested
      directly for a real, concrete use case (keeping two variations of the
      same Combi across two physical band setups -- e.g. one with a brass
      section and one without -- without ever editing the shared original).
      A prior reality-check question surfaced that dropping a Combi onto an
      empty ("Init Combi") slot was silently just running the SAME
      `swapCombis()` entry 34 uses for dropping onto any occupied Combi --
      correct per how it was built, but not a copy: the source's own slot
      would become "Init Combi" too, losing it. This is the real, separate
      operation that keeps the source untouched.
      - `PcgFile::copyCombi(srcBank, srcNumber, dstBank, dstNumber)`, same
        `CombiRearrangeResult` shape as the other three (`setlistRefsRepointed`
        is always 0 -- nothing is repointed, since the source keeps its own
        references and the destination had none). Refuses (writes nothing)
        unless the destination's name case-insensitively CONTAINS "init
        combi" -- a substring match, not exact-equals, specifically so it
        also accepts entry 34's own vacated-slot rename, `"- Init Combi -"`,
        not just Korg's literal `"Init Combi"`. Mirrors `copyProgramFrom()`'s
        own `TargetSlotOccupied` guard (Programs also only ever copy into an
        empty slot) -- new `looksLikeEmptyCombiName()` helper, deliberately
        separate from `moveCombiToBank()`'s own filler search (an exact,
        case-sensitive match answering a different question: finding a
        byte-identical DONOR to vacate a slot into, not "is this safe to
        overwrite"). Also refuses if the destination is still referenced by
        any Set List slot, same defensive reasoning as `moveCombiToBank()`
        even though a real reference to an empty placeholder isn't expected
        in practice.
      - Frontend gesture: the Combi row drop handler's "onto" branch
        (`pane-combi-editor.js`) now checks the target's name
        (`/init combi/i`) before deciding swap vs. copy -- copy if it
        matches, swap otherwise (unchanged). Before/after (move within/
        between banks) is untouched -- copy is only reachable via the
        direct-onto gesture, matching how it was actually requested.
      - `EditorBridge::copyCombi()`/`main.cpp` binding/`mock_bridge.js` fake
        follow entries 31-34's exact pattern.
      - Verified: `tests/pcg_file_test.cpp`'s Combi rearrange fixture grew a
        4th real Song (referencing bank0/4, "Init Combi") and a 3rd Combi in
        bank 1 (`"- iNit COMBI -"`, mixed case, unreferenced) specifically
        to exercise `copyCombi()`'s case-insensitive match, a cross-bank
        copy, and a destination-referenced refusal without disturbing any
        existing swap/move-within/move-to-bank assertions; `testCombiRearrange()`
        extended with a happy path (source untouched, including its own 1
        Set List reference) plus occupied-destination/referenced-destination/
        same-slot/out-of-range refusals -- full `pcg_file_test` rebuild
        clean. Real-file smoke probe against `setlist_test_2.PCG` confirmed
        the happy path end to end (a genuinely 3-Set-List-referenced Combi
        copied onto a real "Init Combi" slot, source's own 3 references
        unchanged after) and that re-copying onto the now-occupied
        destination correctly refuses.
      - User Guide (`docs/content/guide/index.md`) and the Hugo Overview
        page both updated the same day to describe all four Combi
        rearrange gestures, not just entry 34's three.
  36. **RESOLVED (2026-08-14)**: real-world use surfaced two bugs and one
      column-width complaint after entries 34/35 shipped, all reported
      directly rather than caught by this project's own tests (the Combi
      rearrange fixture is real Set List repointing behavior, correctly
      verified server-side -- these were both purely frontend gaps that
      synthetic backend tests can't see).
      - **Combis table column widths**: Bank/Name/#STL all stretched too
        wide, per direct report -- `renderCombisPanel()`'s colgroup was
        `[2.6, null, 4, 1.3]` (Name the flexible one). Combis have no
        engine-type suffix to make room for the way Programs' own Bank
        column does, so Bank/Name/#STL all got narrow FIXED widths instead
        (`[1.6, 3, null, 0.9]`), moving the flexible slot to Set Lists --
        the column whose content genuinely varies most (a handful of pill
        badges vs. a dozen), matching the request to stretch there instead.
      - **Set List references went stale after a Combi swap/move
        (real bug, not a report of intended behavior)**: after swapping two
        Combis, a Setlist row that referenced the moved content kept
        showing its OLD bank/number -- confirmed the *data* was correct
        (PcgFile::swapCombis() writes the right bytes, per entry 34's own
        test coverage and real-file smoke probe) but the Setlist PANE's own
        cached `entries` (bank/number/instrumentName per slot, fetched once
        via `getEntries()`) was never told to re-fetch. `onNeedsFullReload`/
        `onRefreshOppositeLibrary` (entries 32-35) only ever refreshed the
        Programs/Combis/Duplicates tables, never the separate Setlist panel
        in either pane -- the exact same staleness *class* entry 33 already
        fixed once for the opposite pane's Library view, just never
        extended to the Setlist view at all, in either pane. Same root
        cause additionally affected `resolveDuplicateProgram()` (entry 33)
        -- it also repoints real Set List references and had the identical
        gap, just never reported before now.
      - Fixed with one new function, `refreshSetlistEverywhere(datasetId)`
        in `pane.js`'s `createPane()` (refreshes THIS pane's own
        `setlistPanel.refreshEntries()` AND the opposite pane's, if it's
        showing the same dataset -- mirrors `refreshOppositeLibrary()`'s own
        shape, just for Setlist instead of Library, and covering both
        panes, not just the opposite one), threaded down through
        `createLibraryPanels()` as `onSetlistRefsRepointed` to
        `createCombisPanel()` (`pane-combi-editor.js`) and
        `createDuplicatesPanel()` (`pane-program-editor.js`). Both call it
        (guarded on `result.setlistRefsRepointed > 0`, so a Combi copy --
        which never repoints anything, entry 35 -- doesn't trigger a wasted
        refetch) right after their existing `onNeedsFullReload()`/
        `onRefreshOppositeLibrary()` calls.
      - Verified: `node --check` on all three touched files, full
        `pcg_file_test`/`kronos_editor`/`generate_setlist_test_matrix`
        rebuild clean, confirmed the new callback is correctly wired end to
        end (grepped the built binary's embedded assets for the new
        function/parameter names). No backend change needed or made --
        `pcg_file_test` was already green and stays green, since the actual
        byte-level repointing was already correct; this was purely a
        missing frontend refresh call.
  37. **BUILT (2026-08-14)**: cross-dataset Combi copy, with Program
      dependency resolution -- the first Combi rearrange operation that
      crosses datasets at all (entries 34/35's swap/move/copy are all
      same-dataset only, and cross-dataset was explicitly refused before
      this). Planned via Plan Mode, not built ad hoc, given the size (a new
      backend analysis+apply pair, a new bridge round-trip, and this app's
      first-ever modal-like UI). Two decisions made directly with the user
      rather than guessed, before writing any code:
      - **Scope**: only the drop-onto-empty-slot (copy) gesture goes
        cross-dataset. Swap/overwrite-move stay same-dataset-only -- a
        cross-dataset swap would need this same resolution run
        bidirectionally, real but separate future work. This also explains
        why entries 34/35 never needed this at all: within ONE file a
        Timbre's raw `(rawBankCode, number)` pointer keeps meaning the same
        Program regardless of where the Combi itself moves -- only crossing
        files can land that same pointer on a completely different Program,
        or nothing.
      - **UI shape**: not a modal (this app has none) -- a sliding side
        panel (a tablet/mobile drawer pattern), sliding in from whichever
        screen edge is nearest wherever the Combi was actually dropped. Only
        appears when a real decision is needed -- if every one of the
        Combi's Program dependencies already exists byte-identical in the
        destination, it applies immediately with no panel, same "write
        immediately, no confirmation" convention as every other edit here.
      - `PcgFile::analyzeCombiCrossDatasetCopy(src, srcBank, srcNumber,
        dstBank, dstNumber)` (read-only, called on the DESTINATION file):
        for each of the source Combi's active Timbres with a CONFIRMED raw
        bank code (now all 20 Program banks, per entry 30), checks whether
        an identical Program (by `contentHash` -- already existed on
        `ProgramInfo`, never previously exposed to JS, stays backend-only
        here too, same as `findDuplicatePrograms()`) already exists
        anywhere in the destination, and for each UNIQUE one that doesn't,
        which destination banks (matching engine type, >=1 slot with
        `name.empty()`) could receive it. GM (raw code 6, permanently
        indexless) and any genuinely unidentified raw code aren't
        resolved at all -- not file-specific data, copied through
        unchanged by the apply step instead of being guessed at. Validates
        the destination Combi slot up front (reusing entry 35's own
        `looksLikeEmptyCombiName()`/Set-List-reference checks via a new
        shared `checkCombiCopyDestination()` helper) so a caller never
        opens the panel only to fail at Apply time.
      - `PcgFile::applyCombiCrossDatasetCopy(src, srcBank, srcNumber,
        dstBank, dstNumber, placements)` (called on DEST): re-resolves
        fresh rather than trusting an earlier analyze() call (the
        destination could have changed via the opposite pane in between) --
        a two-pass, all-or-nothing structure mirroring `resolveDuplicates()`'s
        own validate-then-write discipline: pass 1 resolves every
        dependency (existing match, or the caller's chosen bank's first
        free slot right now) without writing anything, refusing outright if
        any dependency has neither; pass 2 copies each genuinely-new
        Program via the EXISTING `copyProgramFrom()` (already supported
        cross-file copying, never needed new Program-copy logic), then
        rewrites a COPY of the source Combi's raw bytes
        (`writeTimbreProgramRef()`, the same helper `resolveDuplicates()`
        already uses) so every real dependency points at its resolved
        destination, leaves GM/unknown-code/default Timbres' bytes
        untouched, and writes the result -- `src` itself is never read
        again after snapshotting its Combi/Program bytes, matching entry
        35's own "source stays untouched" contract.
      - `EditorBridge::analyzeCombiCrossDatasetCopy()`/
        `applyCombiCrossDatasetCopy()`, `main.cpp` bindings, and
        `mock_bridge.js` fakes (name-equality as the mock stand-in for
        `contentHash` comparison, same convention `findDuplicatePrograms()`'s
        own mock already uses -- flagged in its own comment that every mock
        dataset shares one generator, so two freshly-opened mock files will
        always resolve as fully "found" and never exercise the panel's
        bank-picker path; that path is covered by the real backend test and
        real-file smoke probe instead, not mock/manual testing).
      - `pane-combi-editor.js`'s Combi row drop handler: the blanket
        same-dataset-only guard now only applies to swap/before-after; the
        `zone === "on" && looks-like-empty` branch additionally allows a
        different `source.datasetId`, routing to a new orchestrating
        function instead of calling `window.copyCombi()` directly.
      - New `frontend/combi-cross-dataset-panel.js`: the sliding panel
        itself, mounted once at the app level (`index.html`, alongside
        `toastContainer` -- spans both panes, like `onDropProgram()`/
        `onCopySetlist()` in `app.js`, not owned by either pane's own
        closure). Slide direction read from the destination pane's actual
        DOM position (`.pane:first-of-type` vs `:last-of-type`), NOT
        `paneId` -- `swapPanes()` already means those aren't the same
        thing. Lists every dependency (found ones grayed out,
        informational only) plus one radio-button-bar bank picker per
        unresolved Program (reuses the same `.is-link`-active button look
        `renderBankFilterRow()`/`createSelectControlRow()` already use,
        single-select instead of their own toggle semantics) -- Apply
        stays disabled until every unresolved Program has a selection; one
        with zero candidate banks shows that inline with nothing to
        select, Cancel the only path. New CSS in `style.css` for the
        slide-in/backdrop (z-index below the toast container, so an error
        toast still reads on top).
      - Verified: `tests/pcg_file_test.cpp` gained a genuinely two-file
        fixture pair (`buildCrossDatasetSrcFixture()`/
        `buildCrossDatasetDstFixture()` -- every other Combi test uses one
        shared fixture, which can't exercise a cross-file operation) and
        `testCombiCrossDatasetCopy()`, covering: found-by-hash-at-a-
        DIFFERENT-position (proving matching is by content, not position),
        an unresolved Program with a real candidate bank, GM/default
        Timbres passed through untouched, all three destination refusals
        (not empty / still referenced / missing placement), zero-candidate-
        banks reported honestly, and the source file byte-for-byte
        untouched after a successful apply. Hit the SAME (bank=0,
        number=0) all-zero Set-List-slot collision entry 34's own fixture
        already had to work around (docs/content/format/index.md §5.4) --
        fixed the identical way, a dummy "Unused" record at number 0 so no
        real target ever sits where every unused slot in the fixture
        also decodes to. Real-file smoke probes against
        `setlist_test_2.PCG`/`test_1.PCG` (two independent real backups):
        confirmed a real 16-Timbre Combi copies cross-file with everything
        found (all 652 real Combis in one file turned out fully resolvable
        against the other -- both apparently share the same factory
        content) and, after deliberately blanking one destination Program
        in memory only (never saved) to force a genuine unresolved case,
        confirmed the full analyze -> placement -> apply -> Timbre-rewrite
        path end to end against real 7810-byte Combi records, with the
        source file's own Combi bytes verified byte-identical before and
        after. Full `pcg_file_test`/`kronos_editor`/
        `generate_setlist_test_matrix` rebuild clean, `node --check` on
        every touched/new JS file.
  38. **FIXED (2026-08-14)**: files could not be loaded at all unless they
      contained at least one Set List -- `loadFromMemory()` treated a
      missing SDB1 (Set List database) chunk as a fatal error
      (`"No SDB1 (Set List database) chunk found in this file"`, load
      refused outright). Reported directly: two real third-party PCG
      sound-bank distributions (donated for testing -- `HALEN-SPLIT.PCG`,
      `JMJ KRONOS 2.PCG`) wouldn't open at all. Walked both files' real
      chunk hierarchy by hand (a throwaway Python script, not guessed) and
      confirmed neither contains SLS1/SDB1/SBK1 *anywhere* -- just
      `PCG1 > (DIV1, PRG1 > (PBK1[, MBK1]), CMB1 > CBK1)`, one also with
      `WSQ1`/`DPI1` Drum Sample data. Root cause: every real file this
      project had tested against so far happened to include at least one
      Set List, so the hard requirement went unnoticed -- but Set Lists are
      just one of several categories the Kronos's own backup dialog lets
      you include/exclude; a sound-bank-only PCG (the common case for
      sharing/distributing Programs and Combis, as opposed to a personal
      backup) has no reason to include any.
      - Fix: removed the early-return -- an empty `sdbChunks` already made
        the SDB1-parsing loop below it a correct no-op (zero Set Lists),
        nothing else needed to change. Programs/Combis/etc. parse
        completely independently of Set Lists already.
      - Verified: a standalone smoke-test binary (`clang++` against
        `PcgFile.cpp` directly, this project's usual pattern for a quick
        real-bytes check outside the full CMake build) loaded both real
        donated files after the fix -- `HALEN-SPLIT.PCG`: 0 Set Lists, 128
        Programs, 128 Combis; `JMJ KRONOS 2.PCG`: 0 Set Lists, 256
        Programs (two Program banks), 128 Combis. Both failed outright
        before the fix. Added `testPcgFileNoSetlists()` to
        `tests/pcg_file_test.cpp` as a permanent regression test -- a
        fixture shaped like the real donated files (PRG1/CBK1 present, no
        SLS1 sibling at all, not just an SLS1 with empty SDB1/SBK1
        children -- the real files omit the wrapper chunk itself). Full
        `pcg_file_test`/`kronos_editor` rebuild clean, `ctest` clean.
  39. **BUILT**: Shift+Cmd+click, a second cross-pane jump gesture alongside
      plain Shift+click (`toOpposite`, all 5 jump/navigation buttons across
      `pane-setlist-editor.js`/`pane-combi-editor.js`/`pane-program-
      editor.js` -- built earlier, `pane.js`'s `jumpToOppositePane()`, never
      previously given its own STATE.md entry). Plain Shift+click jumps to
      the opposite pane AND switches its dataset to match this one first if
      needed; Shift+Cmd+click jumps to the same bank/number coordinate in
      the opposite pane WITHOUT touching its dataset, even if that's a
      completely different file. Real motivating case, reported directly:
      a donated foreign PCG's Combi whose Timbres only reference default/
      GM-ish Programs -- nothing distinctive to identify by content -- so
      with a reference dataset (the project owner's own original Kronos
      backup) already open in the opposite pane, Shift+Cmd+click peeks at
      whatever that reference dataset already has at the exact same
      coordinate, which a dataset-switching jump can't do (it would replace
      the reference dataset with the foreign one before jumping).
      - `pane.js`: `jumpToOppositePane(to, from, keepDataset)` gained the
        third param -- skips the `loadDataset()` call entirely when set,
        toasting instead of jumping if the opposite pane has no dataset
        open at all (nothing to jump to there). `jumpToInstrument`/
        `jumpToSetlistEntry` gained a matching `keepOppositeDataset` field,
        threaded straight through.
      - All 5 call sites now pass `keepOppositeDataset: ev.metaKey`
        alongside the existing `toOpposite: ev.shiftKey`, with tooltips
        updated to describe both gestures.
      - One real conflict found and fixed:
        `pane-setlist-editor.js`'s `handleMultiSelectClick()` (the Setlist
        table's own Ctrl/Cmd+click "toggle multi-select" gesture, checked
        BEFORE the jump buttons' own toOpposite/keepOppositeDataset logic
        at every one of its 4 call sites) fired on ANY Ctrl/Cmd+click,
        including a Shift+Cmd+click meant for the new gesture -- it would
        have toggled multi-select and swallowed the click before
        `onJumpToInstrument()` ever ran, on the Setlist row's own Bank/jump
        button specifically. Fixed by making `handleMultiSelectClick()`
        return `false` immediately whenever `ev.shiftKey` is set, on the
        reasoning that Shift is now reserved for the jump-gesture family on
        that row; plain Ctrl/Cmd (no Shift) still toggles multi-select
        exactly as before everywhere else that calls it.
      - Verified: `node --check` on all four touched files, full
        `kronos_editor` rebuild clean, grepped the embedded binary for the
        new `keepOppositeDataset` identifier (9 occurrences, matching the
        3 `pane.js` internal references + one per call site x 5 plus the
        object-literal key itself once more -- consistent with the actual
        edit, not a stray/missing one).
  40. **BUILT (2026-08-15)**: cross-dataset Combi copy's unresolved-Program
      picker gained exact-slot placement -- previously (entry 37) the panel
      only let the user pick a destination BANK per unresolved Program;
      `applyCombiCrossDatasetCopy()` always auto-picked the first empty
      slot in that bank. The plan's own "out of scope this pass" note
      flagged a slot-number picker as a "possible future refinement, not
      requested" -- now requested directly, with a concrete UI spec.
      - `PcgFile::ProgramPlacement` gained `int dstNumber = -1` (sentinel:
        "let apply() auto-pick", preserving every existing caller's
        behavior unchanged). `applyCombiCrossDatasetCopy()`'s resolution
        pass: when `dstNumber >= 0`, uses it directly after a FRESH
        re-validation that it's still actually empty right now (refuses
        with a clear error if not, same "never trust a stale earlier read"
        discipline the rest of this function already follows) instead of
        scanning for the first free slot.
      - `EditorBridge.cpp`'s `placementsArg()` reads an optional
        `dstNumber` field (defaults to -1, so an older-shaped `{srcBank,
        srcNumber, dstBank}` placement object still works unchanged) --
        `mock_bridge.js`'s fake mirrors the same exact-slot-vs-auto-pick
        branch.
      - `combi-cross-dataset-panel.js`: replaced the per-Program bank-only
        radio-bar with a two-column row-editor -- one row per candidate
        bank (column 1: bank ID) with that bank's own empty Program slots
        in a dropdown (column 2), built from a
        `window.listPrograms(dstDatasetId)` snapshot fetched once when the
        panel opens (`candidateBanks` only ever said WHICH banks have
        room, never the actual free slot numbers -- reused the existing
        Programs-table bridge call rather than adding a new one, since the
        data was already there). Selections now store `{bank, number}`
        per unresolved Program instead of just a bank; Apply sends the
        exact `dstNumber` through.
      - **Regression found and fixed the same day, reported directly**: a
        real `<table>`/`<tr>`/`<td>` was tried first for the row-editor,
        nested three lit-html template-literal levels deep (per-Program row
        > per-bank row > `<option>` list) -- the panel opened but showed no
        banks/dropdown at all for any unresolved Program, silently. Root
        cause never fully pinned down in isolation (no browser devtools
        available mid-session to inspect it directly), but table-context
        HTML parsing is stricter about which elements can appear where
        (foster-parenting) than a plain element, and that friction is a
        known rough edge with lit-html's per-template-literal isolated
        parsing once nesting gets this deep -- consistent with the
        symptom. Fixed by dropping `<table>` entirely for plain flex-row
        `<div>`s (same two-column look, none of the parsing risk). Also
        added a try/catch around the panel's whole render path that shows
        a visible in-panel error message instead of silently leaving it
        blank if this class of bug recurs -- this codebase's testing
        environment has no browser console access mid-session, so a
        swallowed exception here would otherwise be invisible until
        reported by hand.
      - Verified: `tests/pcg_file_test.cpp` gained
        `testCombiCrossDatasetCopyExactSlot()` -- a dedicated small fixture
        pair (not the shared src/dst from entry 37's own test, whose state
        evolves across its sub-tests) covering: an exact NON-lowest slot
        (2, with 0/1 also free) is honored rather than silently falling
        back to "first free," and a stale exact-slot placement (chosen
        slot no longer empty by apply time) is refused with nothing
        written. Full `pcg_file_test`/`kronos_editor` rebuild clean,
        `node --check` on every touched JS file, grepped the embedded
        binary for the new UI strings.
  41. **FIXED (2026-08-15)**: every "is this Program slot free" check in the
      whole app used `name.empty()` -- reported directly against a real
      personal Kronos backup, whose cross-dataset Combi copy claimed ZERO
      free destination banks anywhere despite genuinely having room. Root
      cause: this project ALREADY confirmed, independently, days earlier
      (§5.5 in the file-format doc) that a genuinely untouched Program slot
      on real hardware is named Korg's own factory `"Init Program"`/`"Init
      EXi Program"`, never a blank string -- but that finding was only ever
      applied to the Duplicates panel's "clear a slot" write path
      (`resources/Init-Program-*.raw`), never fed back into any of the
      "is this slot free to write INTO" checks. Every one of this project's
      OWN synthetic test fixtures happened to use a literal blank name for
      "free," which is exactly why this went unnoticed until tested against
      a real file.
      - New shared helper `looksLikeEmptyProgramName()` in `PcgFile.cpp`
        (mirrors `looksLikeEmptyCombiName()`'s existing shape) -- empty
        string, or a case-insensitive match on `"init exi program"`
        (Korg's real EXi factory name doesn't contain "init program" as a
        contiguous substring, so it needs its own exact check) or
        containing `"init program"` (catches Korg's real HD-1 factory name
        AND this app's own two customized "cleared slot" template names,
        entry from 2026-08-14's docs). Replaces `p.name.empty()` at all 4
        real call sites: `copyProgramFrom()`'s `TargetSlotOccupied` check,
        `analyzeCombiCrossDatasetCopy()`'s `candidateBanks` scan, and both
        branches of `applyCombiCrossDatasetCopy()`'s slot resolution (exact
        `dstNumber` re-validation and auto-pick).
      - `combi-cross-dataset-panel.js` gained a JS mirror of the same
        function for its own per-bank slot dropdown (entry 40's own
        `dstPrograms.filter(...)` call) -- the backend fix alone wasn't
        enough, since the dropdown itself was independently filtering by
        `!p.name`. Dropdown option labels also now show the slot's real
        name (`"012 (Init Program)"`) instead of a hardcoded `"(empty)"`,
        so a genuinely-blank slot and a real-factory-named one read
        differently. `mock_bridge.js`'s fake mirrors the same check at its
        3 equivalent call sites (Duplicates' own "skip blank names" mock
        check, unrelated to this, deliberately left alone -- see its own
        comment).
      - Verified: `tests/pcg_file_test.cpp` gained
        `testProgramCopyRecognizesRealFactoryEmptyNames()` -- deliberately
        builds destination banks whose ONLY "free" slots are real-factory-
        named (zero blank-named slots anywhere), so the test can only pass
        if the real name is actually recognized, not by accidentally still
        matching `name.empty()`. Covers `copyProgramFrom()` directly (both
        `"Init Program"` and `"Init EXi Program"`) and the full
        `analyzeCombiCrossDatasetCopy()` -> `applyCombiCrossDatasetCopy()`
        path end to end. Full `pcg_file_test`/`kronos_editor` rebuild
        clean, `node --check` on both touched JS files.
  42. **IDEA, NOT DECIDED (2026-08-15)**: a persistent log file to capture
      exceptions, plus a new header button (left side, beneath the right
      tab bar) to open a sidebar showing it -- suggested after entry 40's
      own render-failure try/catch only helps if devtools happen to be open
      at the moment something breaks; a log file would survive across runs
      and be inspectable after the fact, which is the actual gap. The
      second half of the suggestion -- "the sidebar becomes context aware,"
      i.e. reusing the cross-dataset copy panel (entry 37/40,
      `combi-cross-dataset-panel.js`) as a general multi-mode sidebar
      (combi-picker mode vs. log-viewer mode) rather than building a
      separate, simpler affordance for logs -- is a real architectural
      shift (that panel is currently a single-purpose global overlay), not
      a small addition, and was flagged back to the project rather than
      assumed: one real use (the cross-dataset picker) doesn't yet justify
      generalizing the panel into a shared shell. Recommended sequencing if
      this gets picked up: build the log file first (small, immediately
      useful on its own), decide the sidebar-reuse question separately
      once there's a second real consumer, not bundled into one task.
      Explicitly deferred -- "We look at it later," not committed to yet.
  43. **BUILT (2026-08-15)**: "Unload" a dataset (per-pane header, between the
      dataset dropdown and "Save As..."), the first real dirty-tracking in
      this app -- STATE.md previously listed "no dirty-tracking/undo" as a
      standing limitation. `EditorBridge::closeDataset(datasetId)` already
      existed and already frees a dataset unconditionally; Unload is the
      first UI wired to it, gated on a real "would this lose anything?"
      confirmation.
      - First design (dirty-tracking marked individually at each of
        `EditorBridge.cpp`'s 12 write methods) was reconsidered after
        review: `PcgFile.cpp` was grepped for every actual
        `std::copy(..., data_.begin() + offset)` write and turned out to
        have only 5 of them (`copyProgramFrom()`, `putCombiRecordBytes()`,
        `putProgramRecordBytes()`, `putSongRecordBytes()`,
        `putNameRecordBytes()`) -- every one of the 12 `EditorBridge`
        methods bottoms out in one of these 5. Centralized instead: a new
        private `PcgFile::writeIntoData(offset, src, length)` does the
        actual `std::copy` AND sets a new `dirty_` member; all 5 call sites
        route through it instead of writing `data_` directly. Result: zero
        changes needed anywhere in `EditorBridge.cpp`'s 12 write methods --
        `PcgFile::isDirty()` is automatically correct for all of them,
        including any FUTURE write method, with no per-caller
        "remember to mark dirty" discipline required at all.
      - `copyEntry()`/`setComment()` correctly excluded -- both only mutate
        the in-memory `Setlist` struct, confirmed by their own doc comments,
        never `data_`, so an edit through either can't be saved anyway.
      - `PcgFile::save()` changed from `const` to non-const (confirmed safe
        -- every real call site already only ever calls it on a non-const
        object) so it can clear `dirty_` on a successful write, entirely
        internally -- `EditorBridge`'s `saveFileAs()`/`saveFileDialog()`
        needed no changes for the clear-on-save side either.
      - `EditorBridge::listDatasets()`/`datasetResultValue()` each gained
        one line exposing `dataset.file.isDirty()` as `dirty` -- the
        `Dataset` struct itself gained no new field.
      - `pane.js`: new `.unload-dataset-button`, enabled/disabled exactly
        like the existing `saveFileButton`. Click handler reads this
        pane's own dataset's `dirty` flag from the already-cached
        `knownDatasets` (no extra bridge round-trip), shows a plain native
        `window.confirm()` if dirty (project owner's own explicit choice --
        this app has no modal dialogs today, a native confirm needs zero
        new UI code), then calls `closeDataset()` + the existing
        `refreshDatasets()` -- every pane showing that dataset (this one or
        the opposite one) already resets itself via its own
        `onDatasetsChanged` listener, no new pane-reset logic needed at
        all.
      - `mock_bridge.js` mirrors the real bridge's write surface 1:1 (12
        fake write functions) but has no equivalent shared low-level
        primitive to hook once -- each of the 12 sets
        `datasets[datasetId].dirty = true` on its own success path instead
        (the two cross-dataset ones, `copyProgram`/
        `applyCombiCrossDatasetCopy`, mark the destination dataset only).
      - **Deferred, not built this pass**: the project owner's own larger
        idea -- collapsing `putSongRecordBytes`/`putNameRecordBytes`/
        `putCombiRecordBytes`/`putProgramRecordBytes` into fewer (or one)
        generic put method(s), with `getSongRecordBytes()`/etc. also
        returning the record's own offset so JS echoes it back instead of
        re-supplying indices every time -- agreed as a real, worthwhile
        simplification (all 4 are already "get raw bytes, decode/mutate in
        JS via the existing codec files, re-encode, write the exact bytes
        back" round trips), but a separate, larger refactor (offset-vs-
        index addressing, per-record-type size validation, and cache-
        refresh logic all need to move together, plus the JS call sites in
        `pane-setlist-editor.js`). Noted here so it isn't lost, not
        bundled into this pass.
      - Verified: `tests/pcg_file_test.cpp` gained `testDirtyTracking()`
        (`PcgFile`-only, so unlike the original per-`EditorBridge`-method
        design this is now actually exercisable by the existing CHOC-free
        test target) -- covers a fresh load never being dirty, each of the
        5 `writeIntoData()` call sites setting it, a REJECTED write (wrong
        byte length) leaving it false, and `save()` clearing it on
        success. Full `pcg_file_test`/`kronos_editor` rebuild clean,
        `node --check` on `pane.js`/`mock_bridge.js`.
      - **Bug found and fixed the same day, reported directly**: the
        Unload confirmation above read `dirty` from `knownDatasets`
        (`pane.js`'s own cache, populated only by the global
        `refreshDatasets()`/`onDatasetsChanged()` broadcast) -- but a
        Combi swap/move/copy (and every other write across the app) only
        refreshes its OWN pane's view (`onNeedsFullReload()`/
        `refreshEntries()`), never that broadcast, so the cached `dirty`
        went stale the instant any edit happened anywhere and never caught
        up. Root design issue, not a one-off bug: the dirty flag lives on
        the raw data itself and is meant to be asked fresh, not cached
        alongside a dataset-selector list built for a different purpose.
        Fixed properly instead of patched: new
        `EditorBridge::isDatasetDirty(datasetId)` (`main.cpp`-bound,
        `mock_bridge.js`-mirrored) -- a direct point query straight from
        `PcgFile::isDirty()`, no cache involved at all. The Unload click
        handler now calls this instead of `refreshDatasets()`/
        `knownDatasets`. `listDatasets()`'s own `dirty` field is left in
        place (harmless, matches the dataset-info cluster already there,
        available for a possible future dirty indicator) but is no longer
        what Unload itself relies on.
  44. **BUILT (2026-08-15)**: `EditorBridge` no longer formats any value to
      a display string -- `fontSizeName()`, `timbreStatusName()`, and
      `programBankTypeName()` (three small enum->string switch/ternary
      helpers) are gone, per direct architectural instruction: "The only
      reason for native C++ handling is speed or bulk modifications.
      Anything else belongs to encoder/decoder components" -- i.e. this
      project's own JS. `songToValue()`/`programToValue()`/`combiToValue()`/
      `getProgramBankTypes()`/`getDatasetInternals()`/
      `analyzeCombiCrossDatasetCopy()` now send the RAW `kronos::FontSize`/
      `TimbreStatus`/`ProgramBankType` enum values (`static_cast<int>`)
      instead.
      - `fontSizeName()` was a CONFIRMED real duplicate before this --
        `frontend/components/kronos/setlist-comment.js` already had its own
        independent `FONT_SIZE_BY_VALUE` table decoding the exact same
        field for the real byte-level editor; the C++ copy only ever fed a
        read-only summary label. `pane-setlist-editor.js` gained its own
        small `FONT_SIZE_NAMES` array (deliberately a SEPARATE array from
        the codec's, not a shared import -- the codec is lazily loaded on
        first editor-panel open, but the row-summary label needs a name
        the instant rows first render; `refreshEntries()` now converts the
        raw value to a name once, right at the data boundary, so every
        display site downstream is unaffected).
      - `timbreStatusName()`/`programBankTypeName()` had NO existing JS
        duplicate (checked directly, not assumed) -- `programBankTypeName()`
        specifically has no raw per-record byte to decode in the first
        place, since `ProgramBankType` comes from classifying an entire
        BANK by its chunk tag once at load time, not a per-record field.
        New homes: `pane.js` gained `PROGRAM_BANK_TYPE_NAMES`/
        `programBankTypeName()` (used everywhere a "HD-1"/"EXi" string is
        shown, including inside `formatBankNumber()` itself now, so most
        callers needed no change at all); `pane-combi-editor.js` gained a
        single `TIMBRE_STATUS_OFF = 0` constant (the ONLY TimbreStatus
        value ever actually branched on anywhere in this app -- Internal/
        External/Ex2 are never shown as text -- so no full name table was
        needed there at all).
      - **Real bug caught and fixed while migrating, not yet reported
        live**: several existing display sites checked `bankType`
        truthily (`bankType ? ... : ...`, `p.bankType || ""`) -- harmless
        while `bankType` was a non-empty STRING, but `kronos::
        ProgramBankType::Hd1 == 0`, and `0` is falsy in JS. Left as-is,
        this would have silently dropped "(HD-1)" labels/Type column text
        specifically for HD-1 banks the moment the raw value replaced the
        string. Fixed at every site found (`pane.js`'s `formatBankNumber()`,
        `internals.js`, `pane-program-editor.js`) by checking `!= null`
        instead.
      - `mock_bridge.js` updated to match the new contract exactly, not
        just superficially -- `entry.fontSize`'s OWN internal
        representation changed from a string to the raw value throughout
        (not just at the `getEntries()` boundary), which incidentally
        simplified `makeFakeSlotBytes()`/`putSongRecordBytes()`'s mock (the
        `FONT_SIZE_VALUE`/`FONT_SIZE_BY_VALUE` strings-to-bits lookup
        tables it used are gone -- the raw value already IS the bit-pattern
        index). Every `"HD-1"`/`"EXi"`/`"Off"`/`"Internal"` literal
        replaced with its raw `0`/`1` equivalent at all 9 mock call sites.
      - **Also removed** (flagged in the same review, same principle):
        `EditorBridge::setComment()` -- an older in-memory-only Comment
        setter, confirmed genuinely dead (grepped every frontend file: no
        real UI ever called `window.setComment`, only its own doc comment
        and mock stub referenced it, both already saying it was superseded
        by `getSongRecordBytes()`/`putSongRecordBytes()`). Deleted outright
        (header, implementation, `main.cpp` binding, mock stub) rather than
        migrated, since there was no live functionality to preserve.
      - Verified: full `pcg_file_test`/`kronos_editor` rebuild clean
        (`pcg_file_test` itself is unaffected -- confirmed it has zero
        `EditorBridge`/CHOC dependency, per its own `CMakeLists.txt`
        scoping), `node --check` on all 6 touched JS files, grepped the
        embedded binary to confirm `setComment` is fully gone and the new
        JS constants are present.
      - **One truthy-`bankType` site missed in the first pass, reported
        directly the same day**: `pane.js`'s `renderBankFilterRow()` (the
        Programs bank-filter buttons) still had the exact falsy-zero bug
        described above -- `const bankType = getBankType && getBankType(bank)`
        then `bankType ? ... : name`, so HD-1 banks (raw value 0) showed
        no type marker at all ("nothing"), and EXi banks (raw value 1)
        showed the un-named raw value literally, `"(1)"`. Fixed the same
        way as the other three sites: `!= null` instead of truthiness, and
        `programBankTypeName(bankType)` instead of interpolating the raw
        value directly. All `bankType`-truthiness and raw-`${bankType}`
        interpolation patterns re-grepped across the whole frontend
        afterward to confirm no fourth site was still hiding.
      - **Follow-up consolidation, same day, direct instruction**: two
        "is this slot empty" checks were independently reimplemented at
        multiple call sites instead of sharing one definition --
        `looksLikeEmptyCombiName()` (a case-insensitive "init combi"
        substring match) was inlined 5 times across `pane-combi-editor.js`
        (a plain regex, twice) and `mock_bridge.js` (a
        `.toLowerCase().includes(...)` call, three times);
        `looksLikeEmptyProgramName()` already existed as a named function
        but was DEFINED independently, with an identical body, in both
        `combi-cross-dataset-panel.js` and `mock_bridge.js`. Both are now
        single shared definitions in `pane.js` (the established home for
        cross-file display helpers -- `PROGRAM_BANK_NAMES`/
        `formatBankNumber()`/`programBankTypeName()` already live there),
        with every other file's own copy deleted and its call sites
        switched to the shared one -- including `mock_bridge.js`, which
        can reach these safely since `pane.js` loads before it in
        `index.html`'s script order.
      - Also found and fixed the same way: `mock_bridge.js` had its OWN
        internal duplication, unrelated to the real bridge -- the "which
        type is this bank" rule (`bank === 0 ? 0 : 1`, this mock's fixed
        2-bank world) was written out independently at 5 call sites across
        `makeFakePrograms()`/`copyProgram()`/`getProgramBankTypes()`/
        `getDatasetInternals()`. Consolidated into one local
        `mockBankType(bank)` helper, used at all 5.
      - Verified: re-grepped the whole frontend afterward for the removed
        patterns (`/init combi/i.test`, `.includes("init combi")`, a
        second `function looksLikeEmptyProgramName`/
        `looksLikeEmptyCombiName` definition, any remaining
        `bank === 0 ? 0 : 1` outside `mockBankType()` itself) -- all
        confirmed at exactly zero/one occurrence as expected. Full
        `pcg_file_test`/`kronos_editor` rebuild clean, `node --check` on
        all 4 touched files.
  45. **BUILT (2026-08-15)**: category tabs (Setlist/Programs/Combis/
      Duplicates) grey out and become unclickable when the current dataset
      has zero rows for that category, checked after every dataset
      selection change (`pane.js`'s `loadDataset()`/`resetToEmpty()`) --
      per direct request. Internals stays exempt (always relevant, even to
      show "0 of everything"). Each sub-panel gained a small count
      accessor (`getSetlistCount()`/`getProgramCount()`/`getCombiCount()`/
      `getGroupCount()`) reading its own already-fetched array -- no new
      bridge calls needed. New `.is-tab-disabled` CSS class
      (opacity + `pointer-events: none`); the tab click handler also
      checks the class directly, not just CSS, since a tab can become
      disabled programmatically after being opened. If the active tab
      becomes disabled, falls back to the first available one (Internals
      as the final fallback).
      - **Real, pre-existing bug found the same day, reported directly**:
        HALEN-SPLIT.PCG (a real donated file, confirmed earlier this
        session to have a real Combi) showed its Combis tab disabled
        despite having real content. Root cause: `createLibraryPanels()`'s
        own `onDatasetChanged()` called `load({resetFilters: true})`
        without `async`/`return` -- so `await
        libraryPanels.onDatasetChanged()` (`loadDataset()`/
        `resetToEmpty()`) resolved immediately, before Programs/Combis/
        Duplicates had actually finished fetching. Harmless before now --
        nothing previously depended on that completion timing, the UI just
        caught up a moment later once `load()` itself finished and called
        `renderCurrentTab()` -- until `updateCategoryTabAvailability()`
        became the first caller that needed the counts to be accurate the
        instant it ran, reading stale (usually empty, pre-fetch) data as a
        result. Fixed by returning `load()`'s own promise.
        `setlistPanel`/`internalsPanel`'s own `onDatasetChanged()` were
        checked too and are correctly `async` already -- this was the one
        gap.
  46. **BUILT (2026-08-15)**: error messages now show as a red toast
      (`showToast(message, {isError: true})`, Bulma's semantic `--bulma-
      danger`/`-invert` pair, same "reuse Bulma's real color system"
      reasoning the existing default warning-yellow toast already used) --
      per direct request, after the persistent status bar (`setStatus()`,
      whose own doc comment already admitted "easy to miss, and
      overwritten by the next status message") turned out to be where
      EVERY error in `app.js` (Setlist move/copy, Program copy, Set List
      copy, Open file -- 9 call sites) was still going, while newer files
      (`pane-combi-editor.js`/`pane-program-editor.js`/`combi-cross-
      dataset-panel.js`) already used `showToast()` for errors but with no
      color distinction from a success message (5 more call sites
      switched to the new red variant for consistency). Success/
      informational `setStatus()` calls untouched -- only genuine failure
      paths moved.
      - `EditorBridge::programCopyErrorMessage()` (also flagged the same
        session, same "should this be JS's job" principle that removed
        `fontSizeName()`/`timbreStatusName()`/`programBankTypeName()`
        earlier) was DELIBERATELY KEPT in C++, unlike those three: this
        app's entire error-handling convention (~40+ bridge call sites)
        assumes `result.error` always arrives as a ready-to-show string:
        sending a raw `ProgramCopyError` code instead would mean this one
        call site alone needs its own JS-side code-to-text table, a
        special-cased shape used nowhere else in the app -- more
        inconsistency introduced than removed. Reasoning shared directly
        rather than silently complying or silently ignoring the
        suggestion.
      - Verified: `node --check` on all touched JS files, full
        `kronos_editor` rebuild clean.
  47. **FIXED (2026-08-15)**: the cross-dataset Combi copy panel's per-bank
      slot dropdowns (entry 40) didn't behave as a radio group -- picking a
      slot in a SECOND bank never visually cleared the first bank's own
      dropdown, so both looked selected at once. Root cause: resetting the
      other dropdown used `?selected` on its `<option>` elements -- a
      lit-html boolean-ATTRIBUTE binding, which only affects an `<option>`'s
      initial parse state, not a live `<select>`'s actual displayed value
      once the browser has already rendered it (attribute vs. the real
      `.value` PROPERTY diverge for a `<select>` that already exists in the
      DOM). `selections` itself was already correctly single-valued
      underneath the whole time -- this was purely a display bug. Fixed by
      binding the `<select>`'s own `.value` PROPERTY directly (lit-html's
      leading-dot property-binding syntax) instead, which forces the
      browser's actual selection to match on every render regardless of
      prior user interaction. Also added true radio-group behavior per
      direct request: once one bank has a selection, every OTHER bank's
      dropdown for that Program is now disabled too (not just reset) until
      the selection is cleared back to the placeholder.
  48. **BUILT (2026-08-15)**: `PcgFile::swapPrograms()` -- a Program swap,
      mirroring `swapCombis()`'s own shape but repointing TWO kinds of
      reference instead of one (Combis are only ever referenced by Set
      List slots; Programs are referenced by Set List slots AND Combi
      Timbres). Built specifically because `copyProgramFrom()`'s own
      `DuplicateExists` guard makes a plain drag-and-drop copy meaningless
      between two slots that are BOTH genuinely empty ("Init Program") --
      every Init Program is byte-identical to every other one in the same
      bank type, so copying one onto another always trips that guard, even
      though nothing is actually wrong -- reported directly against a real
      same-dataset drag (I-A 108 -> I-A 100). A swap never creates a new
      copy of anything, so `DuplicateExists` doesn't apply to it at all.
      - Same-dataset only, matching `swapCombis()`'s own existing scope
        decision (a cross-dataset swap raises the same "resolve
        dependencies first" question cross-dataset Combi copy already
        needed a whole panel for -- out of scope here). Rejects a bank-type
        (HD-1/EXi) or record-size mismatch, same two guards
        `copyProgramFrom()` already has. A no-op (nothing written) for the
        same slot twice.
      - Set List repoint: reuses `swapCombis()`'s own single-pass, both-
        directions-at-once shape (checking each song against BOTH original
        positions in one loop) -- NOT two sequential calls, which would
        have the second one immediately re-catch and undo what the first
        just wrote.
      - Combi Timbre repoint (the genuinely new part, Combis never needed
        this): same single-pass-both-directions idea, per Timbre instead
        of per Set List slot, gated per-direction on the DESTINATION
        bank's own confirmed raw Timbre code
        (`confirmedTimbreCodeForProgramBank()`) -- mirrors
        `resolveDuplicates()`'s own `combiRefsSkipped` reasoning
        (structurally can't happen today, all 20 Program banks are
        confirmed, kept for the same defensive reason).
      - `EditorBridge::swapProgram()`/`main.cpp` binding/`mock_bridge.js`
        fake (same-dataset-only, engine-type guard, Set-List + Combi-
        Timbre repoint, all mirroring the real backend) all added the same
        shape as `copyProgram()`/`swapCombis()` already established.
      - Frontend: Shift+drag on a Program row now swaps instead of copies
        (`pane-program-editor.js`) -- `effectAllowed` changed from `"copy"`
        to `"copyMove"` at dragstart (required for `dropEffect = "move"` to
        actually take effect during a Shift-held dragover; `effectAllowed`
        set at dragstart caps which `dropEffect` values the browser honors
        later), `ev.shiftKey` read fresh at both dragover (cursor hint) and
        drop (which bridge call to make) since the key can be pressed/
        released mid-drag. New `app.js` `onSwapProgram()` mirrors
        `onDropProgram()`'s own shape -- red-toast on failure, refreshes
        every pane showing the affected dataset's Library tables, plus the
        Setlist tab too if `setlistRefsRepointed > 0` (a swap can repoint
        Set List slots, unlike a copy, which never does). Cross-dataset
        Shift-drop is rejected client-side first, with a clear message,
        before even reaching the bridge.
      - Verified: `tests/pcg_file_test.cpp` gained `testProgramSwap()` --
        happy path swaps two Programs each referenced by a DIFFERENT kind
        of pointer (a Set List slot vs. a Combi Timbre) so both repoint
        directions are exercised in one pass, confirming each reference
        followed its CONTENT (not stayed at its original position) to the
        new location; plus a same-slot no-op, a bank-type-mismatch
        rejection, and an out-of-range rejection. Full
        `pcg_file_test`/`kronos_editor` rebuild clean, `node --check` on
        all touched JS files.
  49. **FIXED (2026-08-15)**: Unload's "unsaved changes" confirmation never
      appeared for a dirty dataset -- the click just silently did nothing.
      Reported directly ("no dialog appears, z-ordering issue?"). Root
      cause wasn't z-ordering at all: on macOS, WKWebView only shows a
      native JS `confirm()`/`alert()` dialog if its UIDelegate implements
      `runJavaScriptConfirmPanelWithMessage:initiatedByFrame:
      completionHandler:` -- CHOC's WebView
      (`third_party/choc/choc/gui/choc_WebView.h`) registers a UIDelegate
      for the open-file panel only
      (`runOpenPanelWithParameters:initiatedByFrame:completionHandler:`)
      and never implements that method, so WKWebView just drops the call
      entirely: `confirm()` resolves immediately to `undefined` (falsy),
      and Unload's own `if (!confirmed) return;` no-opped every time with
      nothing visibly wrong. This was the app's only `window.confirm()`
      call site (grepped to confirm) -- fixed by never relying on a native
      dialog anywhere in this app instead of patching around this one
      call site. New `frontend/confirm-dialog.js`: a generic
      `window.showConfirmDialog(message, {confirmLabel, cancelLabel,
      isDanger})` -> `Promise<boolean>`, rendered as an in-page Bulma
      `.modal` (CSS already shipped by `vendor/bulma.min.css`, no new
      library) via lit-html -- same pilot pattern
      `combi-cross-dataset-panel.js` established, second file to use it.
      Mounted at the app level (`#confirmDialogRoot` in `index.html`),
      same reasoning as `toastContainer`/`combiCrossDatasetPanelRoot`.
      Bulma's own modal-card background variables
      (`--bulma-modal-card-*-background-color`) only turn dark under a
      real OS-level `prefers-color-scheme: dark`, but this app forces a
      dark look regardless of OS setting -- `style.css`'s
      `.confirm-dialog-modal` overrides them explicitly with the same
      `var(--bulma-scheme-main, #1b1d22)` fallback the cross-dataset
      panel already uses, rather than trusting Bulma's own scheme
      variables. `pane.js`'s Unload handler now awaits
      `showConfirmDialog()` instead of calling `window.confirm()`.
      Verified: full `kronos_editor` rebuild clean (confirms the new file
      is picked up by the embedded-assets glob automatically), `node
      --check` on all touched JS files.
  50. **BUILT (2026-08-15)**: cross-dataset Combi copy now warns about
      Timbre references it can't identify at all, instead of silently
      carrying them through. Reported directly: "In case a timbre
      references a bank which does not exist in the destination dataset
      at all, ... apply creates a non working copy in the destination,
      but why not" -- i.e. don't block, just tell the user. Root cause
      wasn't really "a bank absent from the destination" -- it's a raw
      Combi Timbre bank code this project has never identified at all
      (`programBankForConfirmedTimbreCode()` returns -1 for it), distinct
      from the ALREADY-handled GM/G(1..4)/g(5..7)/g(9) codes (permanently
      indexless, hardware-builtin, universal across every unit --
      `kronos::timbreBankName()` has a name for those, correctly no
      warning needed). Both cases were previously lumped into the same
      silent `continue` in `analyzeCombiCrossDatasetCopy()`'s Timbre loop.
      - `PcgFile.h`: new `CombiCrossDatasetAnalysis::unmappableTimbres`
        (a `UnmappableTimbre { timbreIndex, rawBankCode, rawNumber }`
        list) -- populated only when `programBankForConfirmedTimbreCode()`
        returns -1 AND `timbreBankName()` is also empty (i.e. genuinely
        unidentified, not GM/G(n)/g(n)).
      - `EditorBridge.cpp`: serializes the new list alongside
        `dependencies`/`unresolved`.
      - `mock_bridge.js`: mirrors the same distinction via each fake
        Timbre's own `bankName` field (populated = GM-like, empty =
        unmappable) -- reuses `makeFakeTimbres()`'s existing raw-code-20
        entry (already had no `bankName`) as the mock's stand-in.
      - `combi-cross-dataset-panel.js`: new "Unrecognized references"
        section, amber (Bulma's warning color, same "reuse Bulma's real
        color system" convention the toast-error styling already
        follows) -- explicitly NOT part of `applyDisabled`'s check, since
        this warns without blocking (apply still copies the raw Timbre
        bytes through unchanged, exactly as it already does for GM).
      - Verified: `tests/pcg_file_test.cpp`'s
        `testCombiCrossDatasetCopy()` fixture gained a 4th source Timbre
        (raw code 16, a real, genuinely-unconfirmed gap between the
        confirmed `g(n)` block and `USER-A`) -- asserts it's absent from
        both `dependencies` and `unresolved`, present in
        `unmappableTimbres` with its raw code/number preserved, and that
        `applyCombiCrossDatasetCopy()` still copies its raw bytes through
        unchanged (same as the existing GM Timbre assertion). Full
        `pcg_file_test`/`kronos_editor` rebuild clean, `node --check` on
        all touched JS files.
  51. **CONFIRMED + BUILT (2026-08-16)**: which specific EXi synthesis
      engine (AL-1/CX-3/STR-1/MS-20EX/PolysixEX/MOD-7/SGX-2/EP-1) an
      individual EXi Program uses -- a genuinely new field, finer-grained
      than the already-confirmed per-bank HD-1/EXi split (§5.2/
      ProgramBankType). Full derivation and real-byte verification written
      up in docs/content/format/index.md's new §5.6 -- short version:
      Korg's own `docs/external/KORG/Prog_EXi.txt` gives an explicit
      0-9 legend (Off/HD-1/AL-1/CX-3/STR-1/MS-20EX/PolysixEX/MOD-7/SGX-2/
      EP-1), `Prog_EXi_Common.txt` places the byte at SysEx offset 2857
      within a documented 4960-byte record (matching this project's own
      independently-confirmed real Program record size, §5.5), and the
      predicted `sysexOffset + 4` shift (this format's established 4-byte-
      marker convention) was verified directly against the two real
      templates already in `resources/`: `Init-Program-HD1.raw` reads Off
      at file offset 2861, `Init-Program-EXi.raw` reads AL-1 there, with
      the adjacent Transpose byte also reading a plausible value in the
      same template -- three consistent real-byte checks, not a guess.
      - `ProgramDecoder.h`/`.cpp`: `ProgramFields`/decode gains
        `exiAlgorithmType` (raw 0-9 int, decoded unconditionally at a
        fixed offset -- reads Off/0 harmlessly on an HD-1 record, same
        "C++ decodes raw data" convention as everything else here). Also
        fixed a stale `kExiProgramRecordSize = 3706` left over from the
        §5.5 correction (dead in practice -- nothing reads
        `tagMatchesStride`, but the constant itself was wrong) to the
        confirmed 4960.
      - `PcgFile.h`: `ProgramInfo` gains the same field; all 3 construction
        sites in `PcgFile.cpp` updated (bulk scan, `decodeProgram()`,
        `refreshProgramInfo()`).
      - `EditorBridge.cpp`: `programToValue()` serializes the raw int only
        -- naming the engine is a JS-layer job, same "C++ = speed/bulk,
        JS = presentation" principle as everywhere else this session.
      - `pane.js`: new `EXI_ALGORITHM_NAMES`/`exiEngineName()`, same shape
        as `PROGRAM_BANK_TYPE_NAMES`/`programBankTypeName()`.
        `pane-program-editor.js`'s Programs table Type column now shows
        "EXi (AL-1)" etc. instead of a bare "EXi" when `bankType` is Exi.
        Only this one display site wired up for now (per-bank-only views
        like bank-filter buttons/jump labels don't have per-Program
        granularity to show) -- `mock_bridge.js` mirrors the field
        end-to-end (initial data, `copyProgram`/`swapProgram`).
      - Verified: `tests/pcg_file_test.cpp` gained
        `testExiAlgorithmTypeRealTemplates()` -- loads the real
        `resources/Init-Program-*.raw` files directly (new
        `EDITOR_RESOURCES_DIR` compile definition on the `pcg_file_test`
        CMake target) and asserts the decoded value against real bytes,
        not synthetic fixtures; `testClassifyProgramBankType()`'s stride
        expectations updated for the 3706->4960 fix. Full
        `pcg_file_test`/`kronos_editor` rebuild clean, `node --check` on
        all touched JS files.
      - NOT done yet, explicitly deferred: a second "EXi2 Common"
        Algorithm Type field exists too (a Program can apparently layer
        two independent EXi engines) -- offset confirmed the same way,
        but not decoded/wired in until something actually needs it. Also
        not yet started: an actual parameter EDITOR for any EXi engine
        (the next real goal discussed -- SGX-2 has by far the fewest
        parameters of the 8, per Prog_EXi.txt's own per-engine
        `Number Of Param.` counts, a natural first candidate).
  52. **RENAMED + SPLIT (2026-08-16)**: the Setlist row editor's codec
      family, per direct request ("I do not like the names of the JS
      files... lets name them accurate right now, maybe we have to
      refactor them"). Old names were inconsistent (`setlist-comment.js`,
      `setlist-slot-params.js`, `setlist-slot-name.js`) and one file
      (`setlist-slot-params.js`) bundled two unrelated fields (Color,
      Volume) under a name too generic to tell which. New
      `frontend/components/kronos/setlist-editor-*.js` family, one file
      per field/field-group, each renamed for exactly what it edits:
      - `setlist-comment.js` -> `setlist-editor-comment-and-font.js`
        (unchanged content -- Comment + Font size, packed into the same
        bytes/codec call together already).
      - `setlist-slot-params.js` -> SPLIT into `setlist-editor-color.js`
        (Color only) and `setlist-editor-volume.js` (Volume only) --
        anticipates more per-field codecs being added later (the EXi
        engine editor discussed as the next goal will need several), where
        "one file per concern" scales better than one growing file.
      - `setlist-slot-name.js` -> `setlist-editor-name.js`.
      - Each `.css`/`.test.html`/`.test.js` sibling renamed/split the same
        way; internal CSS class names (e.g. `.setlist-comment-editor`) left
        untouched -- out of scope, the request was about file names
        specifically, and grepped confirmed-contained (only referenced
        within their own file pair, no cross-file coupling to fix).
      - Every consumer updated: `pane-setlist-editor.js`'s
        `loadSlotCodecs()` (now 4 dynamic imports instead of 3),
        `tools/generate_setlist_test_matrix.js`'s 2 import lines (now 3),
        plus every doc-comment mention across `mock_bridge.js`,
        `style.css`, `tools/generate_setlist_test_matrix.cpp`,
        `src/bridge/EditorBridge.{h,cpp}`, `src/kronos/PcgFile.{h,cpp}`,
        `tests/pcg_file_test.cpp`, `CLAUDE.md`, `README.md`,
        `docs/content/building/index.md`, `docs/content/components/
        index.md` -- grepped the whole repo afterward to confirm nothing
        stale remained (a handful of intentional "(originally X, renamed
        2026-08-16)" historical notes kept on purpose, this file's own
        history log left untouched as always).
      - Verified: `node --check` on every touched/new JS file; the 3 (was
        2) headless `.test.js` files all still pass against the same real
        `ROLLING_IN_THE_DEEP_RECORD_HEX` fixture, split assertions
        included; full `pcg_file_test`/`kronos_editor` rebuild clean.
      - NOTED, not fixed (out of scope for this rename): while grepping
        `docs/content/components/index.md`, found several OTHER stale
        `pane.js` references that predate this session's own
        `pane.js`->`pane-setlist-editor.js` split (only the one line this
        rename directly touched was corrected). Worth a dedicated doc pass
        later.
  53. **BUILT + VERIFIED (2026-08-16), on branch `feature/sgx2-editor-
      window`, not on `main` yet**: multi-window scaffolding -- this app
      can now open more than one native window in the same process, all
      sharing one `EditorBridge` instance (so the exact same in-memory
      dataset is visible/editable from any window, live, no copying). Built
      as the first real step toward a dedicated SGX-2 parameter editor
      window (see entries above) -- deliberately just the plumbing, no
      actual SGX-2 controls yet (those still need real byte offsets, §5.6's
      open question).
      - `src/main.cpp`: extracted `bindEditorBridgeFunctions()` (the ~30
        `view.bind()` calls, previously inline in `main()`) so every window
        gets the identical full bridge surface, not a per-window whitelist.
        New `createEditorWindow(entryHtml, title, w, h, minW, minH)`
        (a `std::function` capturing itself by reference -- the standard
        recursive-lambda idiom, safe here since nothing invokes it until
        `webviewIsReady` fires asynchronously, well after the assignment
        completes) creates a `DesktopWindow`+`WebView` pair and tracks it in
        `openWindows`. **The one real behavior change**: `windowClosed` used
        to unconditionally call `choc::messageloop::stop()` -- now it only
        removes itself from `openWindows` and stops the loop once THAT list
        is empty, so closing a secondary window no longer kills the main
        window (verified below). A new `openSgx2EditorWindow` bind (on
        every window, calling `createEditorWindow` again with `/sgx2-
        editor.html`) is the only way to open a second window today.
      - `EditorBridge.h`/`.cpp`: new `addDatasetsChangedListener()` (NOT
        bound to JS -- a native-only hook) + private `notifyDatasetsChanged()`,
        called from the exact two places `m_datasets` gains/loses an entry
        (`finishOpen()`, `closeDataset()` when it actually erased something).
        `main.cpp` registers one listener per window that pushes
        `view.evaluateJavascript("window.refreshDatasets()")` into THAT
        window -- solves the real gap multi-window introduces: each window
        is a separate JS context (unlike this app's two PANES, which share
        one page), so without this, Window B would never learn Window A
        opened/closed a file. `EditorBridge` itself stays CHOC-unaware
        (only holds `std::function<void()>` callbacks), same "testable with
        zero CHOC dependency" reasoning `PcgFile` already follows.
      - `frontend/index.html`/`app.js`: new "SGX-2 Editor WIP" topbar
        button, calling `window.openSgx2EditorWindow()`. `mock_bridge.js`
        gets a stand-in that explains multi-window is native-app-only
        (plain-browser mode has no CHOC, no second window possible).
      - New `frontend/sgx2-editor.html`/`.js`: a genuinely minimal
        placeholder page (not the real editor) that lists currently-open
        datasets and re-renders live -- exists purely to prove the whole
        loop works before any real SGX-2 UI is built on top of it.
      - **Verified against the actual running app, not just compiled**:
        screenshots don't render any window content at all in this sandbox
        (confirmed systemic -- even already-running unrelated apps like
        Chrome/Terminal show nothing in `screencapture` output here), so
        verification went through macOS's accessibility API instead
        (`osascript -l JavaScript` walking/clicking the real `AXButton`/
        `AXWindow` tree) -- clicked the real "SGX-2 Editor WIP" button,
        confirmed a second window opened titled exactly "SGX-2 Editor
        (experimental) -- DIY Kronos Editor"; confirmed its initial
        "No files open." state; opened a real file
        (`test_1.PCG`) via the MAIN window's native Open dialog (also
        driven via accessibility, since it's a real NSOpenPanel) and
        confirmed the SGX-2 window's own list updated to show it live,
        with zero interaction on that window -- the actual cross-window
        broadcast working end to end, not just present in the diff. Closed
        the SGX-2 window and confirmed the process + main window survived;
        then closed the main window too and confirmed the whole process
        exited cleanly -- both halves of the `windowClosed` fix confirmed,
        not just one.
      - Full `pcg_file_test`/`kronos_editor` rebuild clean, `node --check`
        on all touched/new JS files.
  54. **REFINED (2026-08-16), same branch, per direct request**: the SGX-2
      window is now opened per-Program, not via a single generic topbar
      button.
      - `pane-program-editor.js`'s Programs table: Bank column narrowed 30%
        (2.6->1.82 of 12), Type column widened 50% (1.3->1.95) --
        `colgroupHtml()`'s existing 12-unit fraction system. A row whose
        Type is SGX-2 (`bankType===1 && exiAlgorithmType===8`) now renders
        that cell as a real button ("EXi (SGX-2)", `ev.stopPropagation()`
        so it doesn't also trigger the row's own expand-on-click) instead
        of plain text -- click opens/refocuses that Program's SGX-2 window.
        The topbar's standalone "SGX-2 Editor WIP" button (entry 53) is
        gone -- removed from `index.html`/`app.js` now that every SGX-2
        window is tied to a specific Program instead of being generic.
      - `main.cpp`'s `openSgx2EditorWindow` bind now takes
        `[datasetId, bank, number, label]` (`label` is pane-program-
        editor.js's own `formatBankNumber()` output, deliberately called
        WITHOUT a `bankType` arg so it excludes the "(EXi)" suffix the Type
        button itself already shows) -- becomes the window title, between
        "Editor" and "(experimental)" (e.g. `SGX-2 Editor I-B 000
        (experimental) -- DIY Kronos Editor`), per direct request.
      - **Dedup by Program, per direct request**: new `sgx2WindowsByRef`
        (`std::map<std::tuple<int,int,int>, EditorWindowInstance*>`, keyed
        by datasetId/bank/number) in `main()` -- a second
        `openSgx2EditorWindow` call for a Program that already has a window
        open calls `.toFront()` on the EXISTING window instead of creating
        a duplicate, returning `{ok:true, broughtToFront:true}`. Prevents a
        real editing hazard: two windows independently read-modify-writing
        the same raw bytes would silently "last write wins" whichever
        saves/closes second. `createEditorWindow()` gained two new optional
        parameters to support this generically: `extraBindings` (lets a
        caller add window-TYPE-specific JS bindings -- used here for a new
        per-window `getSgx2ProgramRef()`, letting `sgx2-editor.js` learn
        which exact Program it's for) and `onClosed` (lets a caller hook
        additional cleanup when THIS window closes -- used here to erase
        the closing window's own `sgx2WindowsByRef` entry).
      - `mock_bridge.js`: `openSgx2EditorWindow` stand-in now accepts and
        logs the new args; `makeFakePrograms()`'s bank1/number0 ("Berlin
        Grand SW2 U.C.") is deliberately SGX-2 instead of the mock's usual
        AL-1 default, giving the Programs table's new button a real row to
        exercise in mock/browser mode too.
      - Verified: the JS->native argument CONTRACT directly (a standalone
        Node simulation of `openSgx2Editor()`'s exact call shape confirmed
        `(datasetId, bank, number, label)` arrive in main.cpp's expected
        order, and that `label` correctly excludes the "(EXi)" suffix) and
        the full window mechanism itself (createEditorWindow/
        addDatasetsChangedListener/windowClosed) against the running app,
        same accessibility-driven approach as entry 53. **Honest limit,
        not glossed over**: didn't click a real SGX-2 button end-to-end
        this round -- `test_1.PCG`'s real Programs table has ~1500+
        accessibility nodes once loaded, and every attempt to search it via
        `osascript`/System Events (recursive walk, `whose()` filtering
        including a sanity check against the always-present "Unload"
        button, `entireContents()`) either timed out or silently failed to
        recurse into the WKWebView's content at all -- confirmed as a tool
        limitation, not specific to this feature (a plain title/bank-filter
        button dump from the SAME window worked fine and did visibly
        confirm the bank-type-per-button data, I-A(EXi)/I-B(HD-1)/etc.,
        rendering correctly). Full `pcg_file_test`/`kronos_editor` rebuild
        clean, `node --check` on all touched JS files.
  55. **REPO SPLIT (2026-08-16), same branch**: per direct request ("maybe
      this is too much effort for a free open source version") -- the
      SGX-2/EXi parameter editors and the MIDI SysEx transport layer (to be
      pulled from `DIY-MIDI-METRONOME`) will live in a separate PRIVATE
      companion repo, not this public one, pulled in as a git submodule --
      too large/open-ended a maintenance surface (MOD-7 alone is 1108
      confirmed params) to bundle into a free, from-scratch OSS
      reverse-engineering project, but still built into the SAME
      `kronos_editor` binary/process so it keeps sharing the exact
      `EditorBridge`/dataset state the multi-window work (entries 53/54)
      was built for.
      - This repo itself renamed on GitHub:
        `DIY-KORG-KRONOS-EDITOR` -> `DIY-KORG-KRONOS-EDITOR.public`,
        matching the `.public`-suffix convention the sibling
        `DIY-MIDI-METRONOME.public` project already uses. `gh` isn't
        installed in this environment, so the actual rename was done by
        the project owner via GitHub's Settings UI; local `origin` remote
        updated to match afterward (`git remote set-url`), confirmed
        reachable via `git ls-remote`.
      - GitHub's own redirect covers `github.com/.../blob/...`-style
        links automatically (confirmed no changes needed in
        `.github/workflows/*.yml`, neither hardcodes the repo name) --
        but does NOT cover GitHub Pages URLs, which actually change and
        404 without a manual fix. Updated the two files that control the
        Pages build (`docs/config/_default/config.toml`'s `baseurl`,
        `docs/docker-compose.yml`'s local-preview `--baseURL`) plus
        `README.md`'s 6 hardcoded links straight to the docs site (a
        scope correction mid-conversation -- initially miscategorized
        these as "cosmetic, redirects fine" alongside the `github.com`
        links, which was wrong: they're on the same no-redirect `.io`
        Pages domain as the two required files). Left alone on purpose:
        `STATE.md`'s own historical mentions (not rewritten), and
        `docs/README.md`'s one mention (already a separate, deliberately-
        left-as-is inconsistency from entry 52's CLEAN UP item 2 -- now
        doubly stale, still just tracked there, not fixed here).
      - New private repo `diy-korg-kronos-editor` (project owner created
        it; genuinely empty otherwise) seeded with a minimal scaffold
        (`README.md` explaining the split, a comment-only `CMakeLists.txt`
        placeholder -- no real editor/SysEx code yet) so `git submodule
        add` had a real commit to reference. Added to this repo as
        `private/diy-korg-kronos-editor` (new `.gitmodules`).
      - Root `CMakeLists.txt`: `add_subdirectory(private/diy-korg-kronos-
        editor)`, guarded on that submodule's own `CMakeLists.txt`
        actually existing (not just the directory -- `git submodule add`
        creates the directory immediately, but a clone without
        `--recurse-submodules`, which is EVERY public contributor here
        since the submodule is private, leaves it empty until `git
        submodule update --init` runs). Verified both real cases, not
        just reasoned about them: copied the whole repo to `/tmp`, deleted
        `private/` entirely (simulating a public contributor's clone) --
        `cmake -B build` configured clean, `kronos_editor`/`pcg_file_test`
        both built and passed with zero submodule present. Then rebuilt
        the real working copy (submodule present, empty placeholder) --
        also clean, `ninja: no work to do` (no actual kronos_editor source
        changed, only CMakeLists.txt + the new empty submodule dir).
      - **Follow-up, same session, per direct request**: a build WITHOUT
        the private submodule now fails visibly and helpfully in the UI
        instead of just quietly lacking a feature. Root `CMakeLists.txt`
        sets `EDITOR_HAS_SGX2_MODULE=1` on the `kronos_editor` target only
        inside the same `if (EXISTS .../CMakeLists.txt)` guard already
        used for `add_subdirectory()`; `main.cpp` wraps its
        `openSgx2EditorWindow` bind (and the `sgx2WindowsByRef` map/its
        lambda capture) in `#ifdef EDITOR_HAS_SGX2_MODULE`, so a build
        without the private module never exposes
        `window.openSgx2EditorWindow` to JS at all -- not present-but-
        broken, genuinely undefined. `pane-program-editor.js`'s
        `openSgx2Editor()` checks `typeof window.openSgx2EditorWindow !==
        "function"` before calling it and shows a red toast ("SGX-2
        editor: feature not available in this build.") instead;
        `showToast` newly threaded through `createPane()` ->
        `createLibraryPanels()` -> `createProgramsPanel()` to make this
        possible (previously only `log()`/`setStatus` reached that deep --
        the existing `result.ok === false` path was upgraded from `log()`
        to `showToast` too while threading it through, for consistency).
        Verified both configurations compile clean from a fresh `/tmp`
        copy (with and without `private/` present) after this change, not
        just the one that was already being iterated on.
  56. **MOVED INTO THE PRIVATE SUBMODULE (2026-08-16), same branch, per
      direct request**: the SGX-2 window-opening code (previously in this
      repo's own `main.cpp`, entry 53/54) now lives entirely in
      `private/diy-korg-kronos-editor/src` -- this repo exposes only a
      generic extension point, never anything SGX-2-specific.
      - New `src/bridge/EditorExtension.h`: `EditorWindowHandle` (a
        `{ok, bringToFront}` value, NOT a pointer to `main.cpp`'s own
        file-local `EditorWindowInstance` type) and `EditorExtensionContext`
        (`{bridge, createWindow}`) -- the ENTIRE surface this repo exposes
        to an optional private module. `registerPrivateEditorExtensions()`
        is declared here, `#ifdef EDITOR_HAS_PRIVATE_MODULE`-guarded, and
        implemented ONLY in the private repo.
      - `EditorBridge` gained four genuinely generic additions (none know
        anything about SGX-2): JS-bound `getProgramRecordBytes`/
        `putProgramRecordBytes` (exact mirror of the existing
        `getSongRecordBytes`/`putSongRecordBytes`, thin wrappers around
        already-existing `PcgFile::programRecordBytes()`/
        `putProgramRecordBytes()`); native-only (not JS-bound)
        `getProgramRecordBytesRaw`/`putProgramRecordBytesRaw` for a C++
        caller that already holds `EditorBridge&` and shouldn't need to
        hand-construct a `choc::value::Value` array just to call the
        JS-shaped versions; native-only `lockProgramRecord`/
        `unlockProgramRecord`/`isProgramRecordLocked` (a
        `std::set<std::tuple<int,int,int>>`) -- **per direct request,
        blocks Move/Copy of a Program while its editor is open**:
        `copyProgram()`/`swapProgram()` now refuse (existing
        `frontend/app.js` error-toast handling surfaces this with zero
        frontend changes) if EITHER side of the operation is locked.
      - Renamed the compile flag `EDITOR_HAS_SGX2_MODULE` ->
        `EDITOR_HAS_PRIVATE_MODULE` mid-pass -- it's a generic "is any
        private module compiled in" gate, not SGX-2-specific, and the old
        name itself was leaking "SGX-2" into the public repo's own
        `CMakeLists.txt`/`main.cpp`, contradicting the whole point of this
        change.
      - `main.cpp`: `createEditorWindow()` gained a `resourceDir` parameter
        (a real signature change, not just an addition -- `fetchResource`'s
        capture of the old fixed `frontendDir` had to become a by-value
        capture of the new per-call parameter). One `EditorExtensionContext
        ctx` built once, declared (default-constructed) BEFORE
        `createEditorWindow` so its lambda can capture `ctx` by reference,
        then `ctx.createWindow` assigned right after `createEditorWindow`
        itself exists -- resolves what would otherwise be a circular
        dependency between the two (a first draft hit this as a real
        compile-order problem, not just a naming choice).
      - `private/diy-korg-kronos-editor/src/Sgx2Editor.cpp`: implements
        `registerPrivateEditorExtensions()` -- the `openSgx2EditorWindow`
        bind, title formatting, and the per-Program dedup map (now
        file-local here, storing `EditorWindowHandle` instead of a raw
        pointer) all moved here verbatim from the deleted `main.cpp` code.
        Binds two new generic per-window functions, `getRecordBytes()`/
        `putRecordBytes(bytes)`, replacing the old `getSgx2ProgramRef()` --
        the window never learns its own `(datasetId,bank,number)` at all
        now, only ever sees bytes (the window title, native-side, is the
        only place that triple is still shown). Calls
        `lockProgramRecord()`/`unlockProgramRecord()` around the window's
        own open/close lifecycle.
      - `private/diy-korg-kronos-editor/src/Sgx2EditorStandaloneMain.cpp`
        (new) + `CMakeLists.txt` gains a second executable target,
        `sgx2_editor_standalone` -- no `kronos_editor`, no `EditorBridge`,
        no `PcgFile`, no dataset model at all; loads a single exported
        `.bin` chunk into memory and binds `getRecordBytes()`/
        `putRecordBytes()` directly against it, so `frontend/sgx2-
        editor.html`/`.js` (moved from the public repo into the private
        repo's own `frontend/`, adapted to the new byte-primitive names,
        dropped the now-redundant per-window dataset-ref display) runs
        completely unmodified against either this tool or the real app.
        Platform-link block duplicated from the parent `CMakeLists.txt`
        (matching this codebase's own established "small duplication over
        premature sharing" convention) rather than a shared CMake function.
      - **Verified for real, not just compiled** -- the real-app UI check
        planned (click the actual Programs-table SGX-2 button) turned out
        impractical: `test_1.PCG`'s real Programs table made every
        accessibility-automation approach either time out or fail to
        recurse into the WKWebView content at all (same class of tool
        limitation hit in entry 53, worse here since even a bare
        `AXTable`-search timed out this time). Pivoted to a **direct C++
        smoke test** instead (a throwaway `clang++`-compiled binary linking
        `EditorBridge.cpp` directly, per CLAUDE.md's own sanctioned
        pattern) run against the real `test_1.PCG` -- confirmed, against
        real file bytes: `getProgramRecordBytesRaw` returns real 4960-byte
        Program records; `lockProgramRecord`/`isProgramRecordLocked` work;
        `copyProgram` refuses a locked DESTINATION; `swapProgram` refuses
        when EITHER side is locked; both stop refusing (for that reason)
        once `unlockProgramRecord` is called. That same smoke test then
        exported one real Program's 4960 bytes to a real `.bin` file, which
        `sgx2_editor_standalone` was run against directly (accessibility
        automation against ITS much smaller single-window UI worked fine,
        unlike the full app) -- confirmed "4960 bytes loaded.", clicked
        "Write back unchanged bytes", confirmed the UI's own "Wrote back
        successfully." AND that the written `-edited.bin` file is
        byte-for-byte identical (`cmp`) to the original export. Full
        `pcg_file_test`/`kronos_editor`/`sgx2_editor_standalone` rebuild
        clean, both with and without `private/` present (fresh `/tmp`
        copies, as in entry 53). `grep -ri sgx2 src/ CMakeLists.txt` in the
        public repo is NOT literally empty -- the remaining hits are the
        already-confirmed engine-name enum in `ProgramDecoder.h` (real
        file-format knowledge, correctly public, predates and is unrelated
        to this split) and a few explanatory comments describing what the
        generic extension point is currently used for; no actual
        SGX-2-specific logic/window-management code remains anywhere in
        this repo.
  57. **BUILT (2026-08-20)**: Setlist slot copy-over ("on" drop zone) is now
      save-durable across two DIFFERENT Set Lists in the same dataset, not
      just within one -- closes part of the User Guide's "Current
      Limitations" gap (the cross-Set-List `copyEntry()` bullet). `app.js`'s
      `onDropEntry()` used to branch on `sameList` first, so a copy-over onto
      a different Set List fell all the way through to the older in-memory-
      only `copyEntry()` even though the "on" gesture is a direct 1:1 slot
      overwrite with no eviction question -- only "insert" (before/after,
      shifting the intervening range into an already-full destination list)
      has the real data-loss problem. Re-branched on `target.zone === "on"`
      first instead: that path now always does the same real byte-level
      `getSongRecordBytes`/`getNameRecordBytes`/`putSongRecordBytes`/
      `putNameRecordBytes` round trip regardless of `sameList`, since those
      bridge calls already take source/target Set List index independently
      and needed zero backend changes. Cross-Set-List INSERT still falls
      through to `copyEntry()` unchanged -- that data-loss question is still
      not tackled.
      - **Not verified live this pass**: this environment has no `node`
        installed (couldn't run `node --check` or the headless
        `.test.js` suites) and the app itself is a macOS-only native build
        this sandbox can't launch -- checked by inspection only (brace/paren
        balance, and confirming `mock_bridge.js`'s `getSongRecordBytes`/
        `putSongRecordBytes`/`getNameRecordBytes`/`putNameRecordBytes`
        signatures are already generic on `(datasetId, setlistIndex,
        songIndex[, bytes])`, unchanged by this edit). Needs a real smoke
        test before being trusted: drag-copy a slot onto a different Set
        List of the same dataset, Save As, reload, confirm the copy
        persisted.
  58. **BUILT (2026-08-20)**: "Reset entry" -- right-click a Programs-table
      row for a local menu with one action, which writes that slot's
      bank-matching Init Program template (HD-1 or EXi) straight over it.
      Per direct discussion: a per-bank-header button was considered and
      rejected first -- the bank filter buttons already make it unclear
      which banks are even showing, so a per-ROW menu (sits on the exact
      slot it affects, filter-state-independent) was chosen instead, and
      "..." per row was explicitly rejected as visual clutter -- a
      right-click local menu costs zero extra always-visible UI.
      - `PcgFile::resetProgram(bank, number, hd1InitBytes, exiInitBytes)`
        (`PcgFile.h`/`.cpp`) -- the single-slot "clear to Init Program" half
        of `resolveDuplicates()`, with none of its multi-duplicate/
        repointing machinery: nothing else in the file is touched, and
        anything already referencing (bank, number) keeps pointing at it
        (shows the reset content now) rather than getting repointed away,
        which is the actual behavioral difference from Duplicates'
        "resolve" action -- this is "reset this slot", not "delete this
        Program and preserve its references elsewhere".
      - **Bug found by the new test actually running, fixed same pass**:
        a Set List slot referencing the reset (bank, number) kept showing
        its OLD `instrumentName` -- that field is a cache on the `Song`
        struct, only re-derived by `putSongRecordBytes()` when the SLOT's
        own record is rewritten, never when the Program it points to
        changes elsewhere (`putProgramRecordBytes()`/`refreshProgramInfo()`
        only refresh `programs_`'s own entry). Fixed by having
        `resetProgram()` call the existing `repointSetlistReferences(true,
        bank, number, bank, number)` -- repointing every referencing slot
        to the SAME (bank, number) it already has, a no-op for what's
        stored, but it routes through `repointOneSetlistSlot()` ->
        `putSongRecordBytes()`, which re-derives `instrumentName` fresh.
        Reused already-tested code instead of a second bespoke refresh
        path. Combi Timbre display needed no equivalent fix -- `TimbreRef`
        (`PcgFile.h`) caches no name at all (just rawBankCode/number/
        status), names are resolved fresh wherever they're shown.
      - `EditorBridge::resetProgram(datasetId, bank, number)` (`.h`/`.cpp`)
        and `main.cpp`'s binding mirror `resolveDuplicateProgram()`'s shape
        exactly (same `readResourceFile()` template loads, same
        `EDITOR_RESOURCES_DIR`) -- deliberately no `isProgramRecordLocked()`
        check, matching `resolveDuplicateProgram()`'s own precedent (only
        `copyProgram()`/`swapProgram()` check that lock today).
      - `mock_bridge.js`: `window.resetProgram()` mirrors the real bridge's
        write surface the same way every other mock write function here
        does (sets `.dirty`, updates the fake `program.name` to "Init
        Program"/"Init EXi Program" by `bankType`).
      - `pane-program-editor.js`: new `contextmenu` listener per Programs
        row opens a plain-DOM (no lit-html -- nothing else in this file
        uses it) Bulma `.dropdown-content` positioned at the click point,
        dismissed on outside click/Escape/scroll like a native context menu.
        Its one item, clicked, shows `showConfirmDialog(...,
        {isDanger: true})` (same pattern as Unload's dirty-check
        confirmation) before calling `window.resetProgram()`, then
        `refresh()`s the panel on success.
      - **Verified**: this environment turned out to have `clang++` (though
        still no `cmake`/`node`) -- compiled `pcg_file_test.cpp` +
        `PcgFile.cpp`/`ProgramDecoder.cpp`/`CombiDecoder.cpp` directly
        (bypassing CMake) and ran it for real: new `testResetProgram()`
        (happy path incl. the stale-`instrumentName` regression above,
        no-such-bank, no-such-slot, template-size-mismatch rejections) plus
        every pre-existing test, all passing. `EditorBridge.cpp`/`main.cpp`
        both `clang++ -fsyntax-only` clean against the vendored CHOC headers
        (no link/run -- that needs the real WebView frameworks). The JS
        side (`pane-program-editor.js`'s context menu, `mock_bridge.js`) is
        NOT verified live -- no `node` here either, same gap as entry 57.
        Needs a real smoke test: right-click a Program row, Reset entry,
        confirm the row now shows "Init Program"/"Init EXi Program" and any
        Set List slot referencing it updates too.
      - **Verified live (2026-08-21), two real bugs found and fixed** --
        the "Not verified live" gap above got a real smoke test once entries
        59/60 below unblocked it:
        - The menu rendered in the DOM (confirmed via the now-working
          Inspector, entry 59) but was completely invisible -- Bulma's real
          `.dropdown-menu` rule is `display: none; position: absolute; top:
          100%; ...` by default, only overridden to `display: block` as a
          DESCENDANT of `.dropdown.is-active`/`.dropdown.is-hoverable:hover`
          (`vendor/bulma.min.css`) -- a naive `grep` for `.dropdown-menu`
          earlier had truncated that compound selector and missed this
          entirely. This menu is positioned freely at the click point, not
          wrapped in that structure, so `display`/`position` now also get
          set inline in `openRowMenu()` (`pane-program-editor.js`),
          overriding the stylesheet directly.
        - Per direct request, the menu item also had no background
          (unreadable) -- `.program-row-menu` (`style.css`) now gives it
          the same dark floating-panel treatment as `.cross-dataset-panel`/
          `.modal-card` (`var(--bg)`, `var(--border)`, the same box-shadow),
          hover in `var(--editor-accent)` (darkorange) -- this app's one
          existing orange token, reused rather than a new hardcoded color.

  59. **FIXED (2026-08-21)**: `third_party/choc/choc/gui/choc_WebView.h`'s
      macOS WKWebView setup only ever set the legacy `developerExtrasEnabled`
      preferences key (KVC on `WKPreferences`) when `enableDebugMode` is on.
      As of macOS 13.3/iOS 16.4, that's no longer enough on its own -- a
      WKWebView must also opt in via the real `inspectable` BOOL property for
      Safari's Develop menu to see it at all. Surfaced directly: after moving
      this checkout to a new machine (see entry 60's own opening for the
      broader "moved by ZIP" context) and getting a real build running, the
      app plain didn't appear as a connectable application in Safari's
      Develop menu, blocking `docs/content/building/index.md`'s own
      documented "Real DevTools attached to the running app" workflow
      entirely -- this had likely never worked reliably on any machine
      running current macOS. Fixed by also calling `setInspectable:` on the
      `webview` object itself once it exists (same `enableDebugMode` guard),
      via a `respondsToSelector:` check first since the selector doesn't
      exist on older macOS/WebKit this header still otherwise supports.
      Verified: `cmake --build build` clean, app launches without crashing,
      and DID appear in Safari's Develop menu afterward -- which is what
      surfaced entry 60 below in the first place.
  60. **FIXED (2026-08-21)**: `SyntaxError: Can't create duplicate variable:
      'litHtmlPromise'` -- only visible once entry 59 above actually made
      DevTools reachable. Root cause: `confirm-dialog.js` and
      `combi-cross-dataset-panel.js` each independently declared their own
      top-level `let lit`/`let litHtmlPromise` for their own lazy lit-html
      import (see each file's own "PILOT"/lazy-load comment) -- but classic
      (non-`type="module"`) `<script>` tags on one page share ONE global
      lexical scope for `let`/`const`, so the SECOND file to declare the
      same name throws a SyntaxError and never runs AT ALL.
      `index.html` loads `combi-cross-dataset-panel.js` before
      `confirm-dialog.js`, so confirm-dialog.js was the one silently broken
      -- `window.showConfirmDialog` has apparently never actually existed
      since it was added (entry 43), meaning the Unload "unsaved changes"
      warning that entry was built specifically to fix has likely never
      shown a real dialog on any machine running this app with both files
      present. `node --check` (this project's usual per-file syntax check)
      can't catch this class of bug at all -- each file is independently
      valid syntax; the collision only exists once both are loaded together
      in a real browser/WebView. Checked `kronos-envelope.js` for the same
      pattern (it also declares `let lit`/`let litHtmlPromise`) -- it's
      SAFE, since it's a genuine ES module (`export async function
      createKronosEnvelope`, loaded via `type="module"` in its own
      `.test.html`), which gets its own isolated module scope regardless of
      what any classic script on the same page declares.
      - Fixed by wrapping each affected file's entire body in an IIFE, so
        their private `let`/`function` declarations become closure-local
        instead of page-global. Grepped each file first for which of its
        top-level names are actually called from OTHER files as bare
        identifiers (not via `window.X`) -- only `startCombiCrossDatasetCopy`
        (called bare from `pane-combi-editor.js`) qualified, so it's the one
        name given an explicit `window.startCombiCrossDatasetCopy = ...`
        assignment inside the IIFE; `confirm-dialog.js`'s public surface was
        already exposed the same way (`window.showConfirmDialog = ...`), so
        it needed no further change beyond the wrap itself.
      - Verified: reloaded the running app (Debug build reads `frontend/`
        live off disk, no rebuild needed) -- console SyntaxError gone.
      - **Left as duplication, not consolidated**: both files still each
        carry their own near-identical lazy-lit-html-loader block --
        flagged per this project's own "flag duplicate code for discussion"
        convention (see CLAUDE.md), not unilaterally extracted into a
        shared helper. Worth a deliberate look if a third file ever needs
        lit-html the same way (`kronos-envelope.js`'s own copy is a module,
        so it's a different, non-colliding case).

  61. **RESTRUCTURED (2026-08-21)**: the User Guide (`docs/content/guide/`) split from one
      flat page into a generic overview (`_index.md`) plus three sub-pages
      (`setlist/index.md`, `combi/index.md`, `prog/index.md` -- the last also absorbs
      Duplicates, since it's Program-specific), per direct request. Cross-cutting content
      (opening a file, the dual-pane layout, the shared Programs/Combis browsing
      mechanics, jumping/Back-Forward, Internals, saving) stayed on the overview page;
      each old "Current limitations" bullet moved onto whichever page it's actually about,
      folded into that feature's own paragraph instead of a separate bolted-on list. The
      new "Reset entry" feature (entry 58) is now documented, on the Programs page.
      - **Required `index.md` -> `_index.md`**: Hugo's page-bundle model treats a plain
        `index.md` as a LEAF bundle -- terminal, can't have child pages; anything nested
        under one becomes a bundle RESOURCE, not a separate page. A branch bundle
        (`_index.md`) was required for `setlist/`/`combi/`/`prog/` to render as real
        sibling pages at all.
      - **Verified for real, not guessed**: `docs/config/_default/permalinks.toml`'s
        `page = "/:slug/"` looked like it could flatten every nested page to a bare
        `/setlist/` instead of `/guide/setlist/` -- rather than reason it out from config
        alone, ran a real one-shot `hugo --minify` via the project's own
        `docs/docker-compose.yml` image (`hugomods/hugo:exts-non-root`, no long-running
        server needed) against a throwaway test sub-page first. Confirmed nested URLs work
        correctly (`public/guide/setlist/index.html` etc.) -- that permalinks entry
        apparently doesn't flatten section-nested branch-bundle children the way it looked
        like it might.
      - **Bug found by the same real build, fixed same pass**: markdown links written as
        bare relative slugs (`[Setlist](setlist)`, on `_index.md` and once on
        `setlist/index.md`) rendered as literal `href=setlist` -- NOT canonified into an
        absolute URL the way root-relative links (`/format`, `/components`) are, since
        `canonifyURLs` only rewrites root-relative (leading-`/`) paths. Whether a bare
        relative link like that actually resolves correctly depends on the browser also
        being given the current page's own URL with its trailing slash intact, which is
        fragile compared to the already-proven `/format`-style absolute-path pattern this
        page already used successfully. Rewritten as `/guide/setlist`, `/guide/combi`,
        `/guide/prog`, `/guide/prog#duplicates` throughout -- matches the existing pattern,
        confirmed correct against the real rebuild (canonified, trailing slash present).
      - Also confirmed via the real build: every `#anchor` link's target heading ID matches
        goldmark's actual auto-generated ID (`#jumping-to-a-program-combi-or-set-list-slot`,
        `#browsing-programs-and-combis`, `#saving-your-changes`, `#duplicates`).
      - **Screenshots NOT moved**, per direct request ("I will move/add screenshots
        later") -- `DIY-KE-004-FilterSort.png`/`DIY-KE-005-SetlistItem.png` are referenced
        by `setlist/index.md`, `DIY-KE-006-CombiReferences.png` by `combi/index.md`, but
        all three physical files still sit in the parent `docs/content/guide/` from the old
        flat layout. Confirmed via the same real build exactly what this does in the
        meantime: since Hugo's page-bundle image processing only engages when the file
        actually exists alongside the page, these three renders as bare, unprocessed
        `<img>` tags rooted at the SITE ROOT (not `/guide/`, not `/guide/setlist/|combi/`)
        -- broken until the project owner moves each file into its new page's own
        directory, at which point normal page-bundle processing (responsive srcset, real
        width/height) should just start working with no markdown change needed.
      - `docs/content/guide/index.md` deleted (`git rm`), content fully absorbed into the
        four new files above.
  62. **BUILT (2026-08-23)**: real distribution, in two parts, both prompted by testing
      the actual published `v0.1.0`/`v0.1.1` releases (entries about the tag-triggered
      release job itself aren't in this log -- built directly in conversation, not a
      separate STATE.md-worthy pass on their own -- but the two real bugs that testing
      surfaced are).
      - **`resources/` now embedded into Release builds**, closing a gap this file used
        to flag directly ("no Release build is packaged/shipped yet"). `EditorBridge::
        readResourceFile()` (backs "Reset entry" and Duplicates resolution) used to read
        `EDITOR_RESOURCES_DIR`, a compile-time absolute path into wherever the CI runner's
        checkout was -- meaningless once the binary is handed to someone else, so both
        features would have silently found nothing on a real user's machine. Reused
        `tools/embed_resources.py` (already existed for `frontend/`) rather than writing a
        second mechanism -- generalized it to take a namespace argument, so `resources/`
        gets its own independent `editor_embedded_resources` table
        (`generated/EmbeddedResources.h/.cpp`) instead of merging into `frontend/`'s. Only
        2 files today (`Init-Program-HD1.raw`/`-EXi.raw`), verified byte-for-byte via a
        throwaway smoke test (`clang++` against the generated `.cpp` directly, printed
        each embedded file's size, matched `ls -la resources/*.raw` exactly).
      - **Real macOS `.app` bundle**, Release builds only (gated on the SAME
        `EDITOR_EMBED_RESOURCES` flag, so a Debug build stays a plain `build/kronos_editor`
        binary -- the documented local dev loop is unaffected). Reported directly: the
        published v0.1.1 macOS Intel binary, once downloaded, opened a Terminal.app window
        before the editor's own window appeared -- a bare Unix executable has no way to
        tell Finder it's a windowed GUI app, so double-clicking one launches it INSIDE
        Terminal instead. `CMakeLists.txt`'s new `APPLE` block sets `MACOSX_BUNDLE TRUE` +
        a real `MACOSX_BUNDLE_INFO_PLIST` (`platform/macos/Info.plist.in`, `configure_file`'d
        with `project()`'s own `VERSION` -- bumped by hand alongside a new git tag, not
        auto-derived). No `CFBundleIconFile` -- no icon asset exists in this repo at all,
        ships with the generic system icon for now, a real fix just a smaller scope than
        this pass, not a placeholder left half-done.
      - `.github/workflows/native-build.yml`: macOS jobs now upload
        `build/kronos_editor.app` (a directory) instead of the raw binary; the release
        job's zip step switched from `zip -j` (single file) to `zip -r` (whole bundle,
        preserving its internal `Contents/MacOS/...` structure) for macOS specifically,
        `chmod +x` retargeted at the binary's new nested path inside the bundle.
      - **Verified end-to-end for real, not just compiled**: full `cmake --build build`
        (Ninja, Release) clean, `pcg_file_test` still all-passing, confirmed
        `build/kronos_editor.app/Contents/Info.plist` has the right substituted version
        string, and launched the actual bundle via `open build/kronos_editor.app` (the
        same path a Finder double-click takes) -- process came up directly, no new
        Terminal.app process spawned (checked `ps aux` before/after, only the pre-existing
        session's own Terminal was present).
  63. **BUILT (2026-08-23)**: `choc::ui::WebView::Options::enableDebugMode` (`main.cpp`)
      now off for Release builds -- was unconditionally `true`, meaning every shipped
      binary had Safari's Develop menu, right-click "Inspect Element", and the legacy
      `developerExtrasEnabled`/`isInspectable` machinery (entry 59) all reachable, not
      just this project's own dev builds. Reused `EDITOR_EMBED_RESOURCES` (already this
      project's "is this a real packaged/Release build" marker, see entry 62) as the
      `#ifdef` guard rather than a new flag; a Debug build keeps debug mode on exactly as
      before. `private/diy-korg-kronos-editor/src/Sgx2EditorStandaloneMain.cpp` has its
      own separate `enableDebugMode = true` -- left alone, that binary is a private dev
      tool this repo's release workflow never builds or ships at all.
      - Verified: both a Release build (this repo's own `build/`, `EDITOR_EMBED_RESOURCES`
        already confirmed active there per entry 62's embedded-resources check) and a
        fresh Debug build (`/tmp/kronos_debug_build`, scratch dir) compiled clean --
        confirms both `#ifdef` branches are reachable and correct, not just the one
        currently configured.
  64. **FIXED (2026-08-23)**: the `v0.1.2` tag's release job failed outright (not just
      skipped) -- `chmod: cannot access 'artifacts/kronos-editor-macos-arm64/
      kronos_editor.app/Contents/MacOS/kronos_editor': No such file or directory`.
      Root cause: `actions/upload-artifact@v4`, given a single directory path (the
      macos-arm64/macos-x86_64 jobs' `path: build/kronos_editor.app`, entry 62), uploads
      that directory's CONTENTS, not the directory itself -- so each downloaded macOS
      artifact lands as a bare `Contents/` folder, missing the `kronos_editor.app`
      wrapper a real bundle needs. Not assumed from the action's docs -- pulled the
      actual failed run's real log directly (`actions/jobs/{id}/logs` via a token grabbed
      from the same `osxkeychain` credential `git push` already uses -- the anonymous
      `/actions/jobs/.../logs` and `/actions/artifacts/.../zip` endpoints both 403/401
      even on this public repo, authenticated is the only way to read them) and saw the
      exact failing path. Fixed by reconstructing the `kronos_editor.app/Contents/...`
      wrapper by hand in the release job before zipping, rather than guessing at a
      different upload-side fix. `v0.1.2` itself is a dead tag now -- no release object
      was ever created for it (the job failed before reaching the actual release-creation
      step) -- superseded by whatever tag gets pushed next, entries 62/63's fixes plus
      this one all need a fresh tag to actually ship together.
      - **`v0.1.3` shipped successfully** (all 4 jobs + release green, verified end-to-end
        again: downloaded the real macos-arm64 `.zip`, confirmed the bundle structure,
        exec bit, and a real Mach-O binary) -- but its `Info.plist` still said
        `CFBundleVersion 0.1.1`, not `0.1.3`. `CMakeLists.txt`'s `project(... VERSION
        0.1.1 ...)` was a hand-bumped constant (entry 62's own doc comment already said
        "bump by hand alongside a new git tag") that nobody actually bumped for either
        `v0.1.2` or `v0.1.3` -- demonstrated drift, not hypothetical, so worth fixing at
        the source rather than bumping the number a third time. Added
        `EDITOR_VERSION_OVERRIDE` (CMake cache var, only read by the `APPLE`/
        `EDITOR_EMBED_RESOURCES` block) -- `native-build.yml`'s two macOS jobs now pass
        `-DEDITOR_VERSION_OVERRIDE=<tag, v-stripped>` at Configure time on an actual tag
        build (`$GITHUB_REF` matches `refs/tags/v*`), read from `$GITHUB_REF_NAME`; a
        plain push to main has no tag, so it's left unset and falls back to
        `PROJECT_VERSION` same as before. Verified directly: a real local configure+build
        with `-DEDITOR_VERSION_OVERRIDE=9.9.9` produced a generated `Info.plist` with
        exactly `<string>9.9.9</string>` for `CFBundleVersion`.
  65. **FIXED (2026-08-23)**: reported directly, "You can't use this version of the
      application 'kronos_editor' with this version of macOS" -- on a real macOS 14.7.7
      machine. Root cause confirmed by inspecting the actual released binary, not
      guessed: `otool -l build/kronos_editor.app/Contents/MacOS/kronos_editor | grep -A5
      LC_BUILD_VERSION` on both `v0.1.3` macOS release assets showed `minos 15.0` --
      GitHub's `macos-15`/`macos-15-intel` runners default the binary's minimum-OS load
      command to their own SDK version (15.5) when `CMAKE_OSX_DEPLOYMENT_TARGET` isn't
      set explicitly, which this project never had. A local build on this same machine
      (SDK 15.2, but a real Command Line Tools install, not exactly the CI runner's own)
      had come out `minos 14.0` -- close enough to this machine's own OS to go unnoticed
      until testing the real CI-built asset specifically. Fixed with
      `CMAKE_OSX_DEPLOYMENT_TARGET` set to `11.0` (Big Sur) before `project()` (required
      -- has no effect set later), chosen as a broadly compatible floor with nothing in
      this codebase known to need newer (the one genuinely version-gated call,
      `isInspectable` in the vendored `choc_WebView.h`, entry 59, is already reached via
      `respondsToSelector:` at runtime, not a compile-time availability attribute, so it
      doesn't push this floor up). Verified directly: a real local rebuild produced
      `minos 11.0` (confirmed via the same `otool -l` check), and the resulting
      `.app` still launches cleanly on this machine via `open` (same path a Finder
      double-click takes).
  66. **FIXED (2026-08-24)**: reported directly, the real `v0.1.4` arm64 release refused
      to open at all on macOS Sequoia (15.7.7) -- "Kronos-editor ist beschädigt und kann
      nicht geöffnet werden" ("...is damaged and can't be opened"), a stricter, non-
      bypassable message distinct from the ordinary "unidentified developer" warning
      (which a right-click *can* override, per the README's existing note). Root cause
      confirmed directly, not assumed: `codesign -dv` on a real local build said "code
      object is not signed at all" -- this app has never been signed at all, at any
      point, and Apple Silicon's kernel-level code-integrity enforcement (AMFI) requires
      SOME signature to run anything -- unlike Intel, which is exactly why this was
      arm64-specific (an x86_64 build with the same zero signature still launches, just
      behind the milder, bypassable warning).
      - Fixed with an ad-hoc `codesign --force --deep --sign -` (no Apple Developer ID
        involved -- `-s -` is literally "sign with no identity"), applied in the
        `release` job to the FINAL reconstructed bundle (after entry 64's `Contents/`
        wrapper fix), not back in the `macos-arm64`/`macos-x86_64` build jobs -- covers
        exactly the bytes that actually ship, unaffected by whatever the artifact
        upload/download round-trip does to file layout in between. Required moving the
        `release` job off `ubuntu-latest` onto `macos-latest` -- `codesign` doesn't exist
        on Linux at all; everything else that job does (zip, cp,
        `softprops/action-gh-release`) works identically on macOS.
      - **Verified end-to-end locally before shipping, not assumed to work**: ad-hoc-
        signed a real local build, wrote a synthetic `com.apple.quarantine` xattr onto
        it matching exactly what a real browser download sets, and confirmed `open`
        actually launches it (via Gatekeeper's normal translocation path for an ad-hoc-
        signed-but-unnotarized app) instead of refusing -- `spctl -a` alone still says
        "rejected" for an ad-hoc signature (that's expected, it's checking full
        Developer-ID+notarization policy, a stricter bar than what actually gates a
        real `open`/double-click launch) so `spctl` alone would have been a misleading
        thing to test against.
      - **Immediate workaround for the already-downloaded `v0.1.4` copy**, verified the
        same way (unsigned + quarantined -> `xattr -cr Kronos-editor.app` alone, no
        signing needed for an already-local copy -> launches fine): quarantine is what
        actually triggers the strict check on a fresh browser download, not the missing
        signature by itself.
  67. **BUILT (2026-08-24)**: "Duplicates" now covers the inverse question too, per direct
      request -- Programs AND Combis sharing a **name** but NOT byte-identical (e.g. two
      Programs both called "Bass 1" that turned out to actually be different), alongside
      the original byte-exact check, which stays exactly as it was.
      - **Combi got content hashing for the first time** -- `CombiDecoder.h`'s own doc
        comment used to say "No contentHash -- byte-exact duplicate detection was only
        requested for Programs." New `hashCombiRecord()` (`CombiDecoder.h`/`.cpp`,
        identical FNV-1a algorithm to `hashProgramRecord()`, kept as its own function
        rather than calling that one on Combi bytes -- this project's usual duplication-
        over-a-premature-shared-abstraction convention between the two otherwise-
        independent decoders) + new `CombiInfo::contentHash` (`PcgFile.h`), computed at
        load (the CBK1 parse loop) and in the existing `refreshCombiInfo()` (already the
        one central refresh point every Combi write path already routes through --
        `swapCombis`/`moveCombiWithinBank`/`moveCombiToBank`/`copyCombi`/cross-dataset
        copy all needed zero additional changes).
      - New `PcgFile::NameCollisionGroup`/`NameCollisionVariant` (`PcgFile.h`) + two
        finders, `findProgramNameCollisions()`/`findCombiNameCollisions()` -- group by
        name, then by contentHash within each name; a name where every entry shares ONE
        hash is a plain duplicate (already covered by `findDuplicatePrograms()`), not a
        collision, so only names with 2+ distinct hashes are returned. Placeholder-named
        slots (`looksLikeEmptyProgramName()`/`looksLikeEmptyCombiName()`, already existed)
        are excluded first -- every untouched slot shares the same generic name, which
        would otherwise drown every real collision in one meaningless giant group. The
        actual grouping mechanics (shared, entity-agnostic) live in one internal
        `groupNameCollisions()` helper; only which entries feed in and which empty-name
        filter applies differs between the two public finders, kept as thin wrappers
        rather than duplicating the grouping logic itself a second time.
      - `EditorBridge::findProgramNameCollisions()`/`findCombiNameCollisions()`
        (`[datasetId] -> [{name, variants: [{members: [ProgramInfo/CombiInfo...]}]}]`) +
        `main.cpp` bindings mirror `findDuplicatePrograms()`'s existing shape -- each
        member is a full `programToValue()`/`combiToValue()` object (re-decoded via the
        existing `decodeProgram()`/`decodeCombi()`), not a bare bank/number pair, so the
        frontend can render them the same way.
      - **UI, per direct discussion on placement**: two vertical sub-tabs inside the
        existing Duplicates category tab (not a new top-level tab, not a second table
        bolted onto the old one) -- "Programs" (both checks: the original byte-exact
        table, and the new name-collision one) and "Combi" (name-collision only, since
        Combi never had a byte-exact check requested). Rotated 90 degrees
        (`writing-mode: vertical-rl` + `rotate(180deg)`, style.css's `.duplicates-subtab-
        button`) per direct follow-up request -- the pane is already tight on horizontal
        width (two side-by-side panes), and a normal horizontal tab row would compete
        with the table itself for it. The name-collision table (`pane-program-
        editor.js`'s `renderNameCollisionTable()`/`buildNameCollisionGroupRow()`) is
        deliberately **read-only** -- unlike a byte-exact duplicate, there's no safe way
        to auto-resolve two entries that genuinely have different content, so each
        variant renders as its own visually separated cluster (a dashed divider between
        clusters, `.name-collision-variant`) with plain, non-interactive labels instead
        of a write-triggering button.
      - `mock_bridge.js`: new `findProgramNameCollisions()`/`findCombiNameCollisions()`,
        sharing one internal `findNameCollisions()` helper. Mock data has no real bytes
        to hash, so `bank` stands in as the "different content" signal one level past
        what the existing `findDuplicatePrograms()` mock already does (name-only) --
        `makeFakePrograms()`/`makeFakeCombis()` both already reuse the identical name
        list per bank, so real bank0-vs-bank1 collisions exist in the fixture data with
        no changes needed there.
      - **Verified for real**: new `testFindNameCollisions()` (`tests/pcg_file_test.cpp`)
        against a dedicated fixture (`buildNameCollisionFixture()`) built the same
        "poke a byte outside the name field" way `buildSyntheticPcgFile()`'s own
        duplicate-Program setup already does -- asserts exactly one Program collision
        and one Combi collision, each with the right 2-variant/{2,1}-member shape, and
        that the unique-named and placeholder-named entries are correctly excluded. Full
        `cmake --build` (both the real `pcg_file_test` target and `kronos_editor` itself)
        clean, `pcg_file_test` all passing. `EditorBridge.cpp`/`main.cpp` both
        `-fsyntax-only` clean against the real CHOC headers. A real Debug build was
        launched and confirmed it starts up without crashing; the frontend JS itself
        (`pane-program-editor.js`'s new sub-tab rendering, `mock_bridge.js`) is **not**
        verified live against actual UI interaction -- no `node` in this environment
        either, same gap as every other frontend-only change this project has hit.
        Needs a real smoke test: open a file with a genuine name collision, confirm the
        vertical sub-tabs render correctly, expand a collision group, confirm the
        variant clusters are visually distinct.
  68. **BUILT (2026-08-25)**: follow-up to #67 above, per direct 4-part request -- Combi
      got its own byte-exact duplicate check for symmetry with Programs, the Duplicates
      panel's two views became a dropdown instead of always-stacked sections, every entry
      in every Duplicates table became a real jump button, and the vertical sub-tab strip's
      active-button color switched from Bulma's default blue to this app's own
      `--editor-accent` (darkorange).
      - **Combi byte-exact duplicates, backend**: `PcgFile::findDuplicateCombis()`
        (`PcgFile.h`/`.cpp`) groups by the `contentHash` #67 already added to `CombiInfo`,
        same shape as the existing `findDuplicatePrograms()`. `PcgFile::
        resolveDuplicateCombis(keepBank, keepNumber)` is **deliberately NOT symmetric**
        with `resolveDuplicates()` (Programs): it only repoints Set List references to the
        kept copy, and never clears the *other* duplicates' own bytes -- there's no
        confirmed "Init Combi" byte template to reset them to yet (unlike Programs, which
        has real captured `Init-Program-HD1.raw`/`Init-Program-EXi.raw` files), and this
        project's own "no guessing, ever" rule rules out fabricating one. `EditorBridge::
        findDuplicateCombis()`/`resolveDuplicateCombis()` + `main.cpp` bindings mirror the
        Program versions' shape (`combiToValue()` + `setlistReferenceCount`/
        `setlistUsages`, matching `listCombis()`'s own augmentation pattern).
      - **Verified for real**: new `testFindAndResolveDuplicateCombis()` (`tests/
        pcg_file_test.cpp`) against `buildCombiDuplicateFixture()`. Hit and fixed a real
        bug while building the fixture: an all-zero/padding Set List slot decodes as
        `isProgram=false, bank=0, number=0` -- identical to a genuine reference to Combi
        bank0/number0 -- so an initial fixture using bank0/number0 as the "kept" target
        got `setlistRefsRepointed=127` (126 zero-padding slots + 1 real reference) instead
        of the expected `1`. Fixed by moving the fixture off bank0/number0 entirely,
        matching a convention already established (if previously undocumented) elsewhere
        in this same test file (`testResolveDuplicates()`, `buildCombiRearrangeFixture()`).
        Also hit a real compile error (`unknown type name 'CombiRearrangeResult'`) from
        declaring `resolveDuplicateCombis()` before that nested struct's own definition in
        `PcgFile.h` -- fixed by moving the declaration after it. Full `cmake --build`
        (`pcg_file_test` + `kronos_editor`) clean, `pcg_file_test`: "All checks passed".
      - **Frontend redesign** (`pane-program-editor.js`'s `createDuplicatesPanel()`): the
        two views per sub-tab (Programs/Combi) -- "Same content, different location" /
        "Same name, different content" -- used to render as two always-visible stacked
        sections; now a single `<select>` dropdown (Bulma's own `.select` wrapper, same
        pattern as `pane-combi-editor.js`'s existing Set List filter dropdown) picks one at
        a time, per explicit request, tracked per-sub-tab (`activeView = { programs, combi
        }`) so switching sub-tabs doesn't reset which view you had open. Every entry in
        both kinds of table (byte-exact copies AND name-collision variants) is now a real
        navigation button wired to `onJumpToInstrument` (newly threaded through from
        `pane.js`'s `createLibraryPanels()` -- it was being built and passed to
        `createProgramsPanel()`/`createCombisPanel()` already, but never to
        `createDuplicatesPanel()`), same click/Shift+click (opposite pane)/Shift+Cmd+click
        (opposite pane, same coordinate, keep its dataset) convention as every other
        cross-reference in this app, with `from: null` (there's no single originating row
        to record -- a duplicate-group listing isn't itself a jump target the way a
        Setlist song or Combi Timbre row is). The byte-exact tables' old "click a copy to
        resolve" gesture is now **two separate buttons per copy** (`.duplicate-copy-
        actions` in style.css) -- the copy's own label navigates, a distinct **"Keep only
        this"** button is the actual (still immediate, no-confirm) write action -- so
        Duplicates' navigation isn't itself a destructive click, unlike before.
      - **Color**: `.duplicates-subtab-button.is-link` added to style.css's existing
        shared override rule (the same one that already remaps `.bank-filter-button.is-
        link`/`.pane-visibility-button.is-link` off Bulma's default blue) rather than a new
        rule of its own -- one comment block now documents all three scoped uses of
        `--editor-accent` together instead of duplicating the reasoning a third time.
      - `mock_bridge.js`: new `findDuplicateCombis()` (mirrors `findDuplicatePrograms()`'s
        own mock simplification -- `name` stands in for real byte-identical content, since
        mock data has no real bytes to hash) and `resolveDuplicateCombis()` (mirrors
        `resolveDuplicateProgram()`'s mock but Set-List-repoint-only, matching the real
        backend's deliberate asymmetry above).
      - **Verified for real**: full `cmake --build` (`pcg_file_test` + `kronos_editor`)
        clean, `pcg_file_test`: "All checks passed". All three touched frontend files
        (`pane.js`, `pane-program-editor.js`, `mock_bridge.js`) parsed clean via `osascript
        -l JavaScript`'s `new Function(...)` (no `node` in this environment, same
        workaround used before). A real Debug build was launched and confirmed it starts
        up without crashing. **Not yet verified live against actual UI interaction** --
        the dropdown's rendering, the new jump/Keep-only-this button split, and the
        vertical sub-tab color all still need a real hands-on smoke test in the running
        app, same gap #67 above flagged and never got a follow-up check on either.
      - Docs updated to match: `docs/content/guide/prog/index.md`'s Duplicates section
        (both sub-sections now describe the jump-button/Keep-only-this split), `docs/
        content/guide/combi/index.md`'s Duplicates section rewritten from "same name only"
        to cover both checks (its own byte-exact one is new), `docs/content/guide/
        _index.md`'s one-line Duplicates summary broadened from "Programs" to "Programs
        *and* Combis".
  69. **BUILT (2026-08-25)**: same-day follow-up to #68 above, confirmed working by the
      project owner ("Itried it and it works perfect") who then proposed a UI change --
      the per-copy "Keep only this" button (#68) always resolved the WHOLE group at once;
      replaced with a resolve-picker **side panel** that lets the user choose exactly
      WHICH duplicates to fold in, per group, leaving any others deliberately untouched
      (e.g. an intentional backup copy someone doesn't want auto-cleared).
      - **Backend: selective resolve, not whole-group**. `PcgFile::resolveDuplicates()`/
        `resolveDuplicateCombis()` (`PcgFile.h`/`.cpp`) both gained a new `const
        std::vector<std::pair<int, int>>& targets` parameter, REPLACING their old "search
        the whole file for everything sharing this hash" behavior -- the caller now names
        exactly which duplicates to act on. Both validate every named target up front,
        all-or-nothing: it must exist, AND its `contentHash` must actually match the kept
        copy's own -- **the real trust boundary against the JS frontend** (CLAUDE.md's
        "validate at system boundaries" rule), not merely trusted because the picker UI
        only ever offers same-group entries. A single bad target rejects the whole call,
        nothing written, consistent with the existing template-size all-or-nothing check.
        `EditorBridge::resolveDuplicateProgram()`/`resolveDuplicateCombis()` gained a
        matching 4th JS arg (`targets`, an array of `{bank, number}`), parsed by a new
        `targetsArg()` helper (`EditorBridge.cpp`) mirroring the existing `placementsArg()`
        pattern for the cross-dataset Combi copy panel's own array-of-objects arg.
      - Hit and fixed a real portability issue mid-build: capturing a structured binding
        (`for (const auto& [bank, number] : targets)`) inside a lambda is a C++20
        extension, not valid C++17 -- `clang++` warned (`-Wc++20-extensions`) on first
        compile. Fixed by unpacking into two plain named locals (`targetBank`/
        `targetNumber`) before the lambda, in both `resolveDuplicates()` and
        `resolveDuplicateCombis()`; rebuilt clean, zero warnings.
      - **Verified for real**: existing `testResolveDuplicates()`/
        `testFindAndResolveDuplicateCombis()` call sites updated to the new signature
        (passing the SAME targets the old auto-search would have found, preserving prior
        coverage) plus two genuinely new checks: `testResolveDuplicates()` gained a
        hash-mismatch-rejection case (bundling a real duplicate with one non-duplicate
        target in the same call -- confirms NEITHER gets touched, all-or-nothing). A new
        dedicated fixture, `buildCombiDuplicateTrioFixture()` (three byte-identical
        Combis, not two -- the existing `buildCombiDuplicateFixture()` had no room to
        prove "leaves an un-named duplicate alone"), backs a new
        `testResolveDuplicateCombisSelective()`: resolving only ONE of two duplicates
        confirms the other's Set List reference AND content are both untouched, and that
        `findDuplicateCombis()` afterward still reports it as part of a byte-exact group
        (Combi resolve never clears bytes, so content-hash grouping is unaffected by
        which references have moved -- confirmed by an assertion that initially FAILED
        with the wrong expected group size until this was reasoned through, not guessed).
        Full `cmake --build` (`pcg_file_test` + `kronos_editor`) clean, `pcg_file_test`:
        "All checks passed".
      - **Frontend redesign** (`pane-program-editor.js`'s `createDuplicatesPanel()`): the
        byte-exact tables' per-copy row is back to a single jump button (no write action
        on a row at all now) -- `buildDuplicateGroupRow()` lost its "Keep only this"
        button entirely. In its place, each group's own title row (`renderExact-
        DuplicatesTable()`) gained a "⋯" button (visible whether the group is expanded or
        not) that opens a new resolve-picker side panel: one row per copy in that group,
        a radio (`Src` -- the copy to keep) and a checkbox (`Dupl` -- disabled on
        whichever row is currently Src) per row, a `Resolve` button that only appears once
        a Src and 1+ Dupl are chosen. Built with **plain DOM**, not lit-html -- deliberate
        call: `combi-cross-dataset-panel.js` established lit-html for its own slide-in
        panel (see its own PILOT comment), but everything else in this already-plain-DOM
        Duplicates panel would have needed a THIRD near-duplicate lazy-lit-html-loader
        copy (STATE.md's own entry 60 flagged "worth a deliberate look if a third file
        ever needs lit-html the same way" -- this file hitting that exact trigger is
        exactly why it's flagged here, not silently done either way) to gain nothing --
        the panel's actual behavior (radio/checkbox state, conditional Resolve button) is
        no harder to express with this file's own existing imperative
        createElement()-plus-`render()`-on-change style than with lit-html. **Reused the
        cross-dataset panel's own CSS shell as-is** (`.cross-dataset-panel`/`-backdrop`/
        `-header`/`-title`/`-close`/`-body`/`-footer`, style.css) rather than inventing a
        second one -- only a few new rules were needed (`.duplicate-group-count-cell`,
        `.duplicate-resolve-menu-button`, `.duplicate-resolve-table`'s narrow Src/Dupl
        columns), since the shell itself is generic (full-viewport slide-in, not tied to
        the cross-dataset copy's own two-pane concept) and this feature reuses its own
        `slideDirectionFor()`-equivalent logic (nearest edge of THIS pane, via
        `panel.closest(".pane")`) rather than that file's two-pane version. After a
        resolve, the picker re-syncs itself against the freshly re-fetched
        `duplicateGroups`/`combiDuplicateGroups` (not a locally-patched guess) and stays
        open -- per explicit request -- closing itself only if the kept copy no longer
        shows up as a duplicate of anything at all. Closes automatically on a genuine
        dataset switch (`onDatasetChanged()`, not `refresh()` -- confirmed these are
        different call paths in `pane.js`'s own `load({resetFilters})`, so a resolve's
        own reload-and-stay-open never gets undone by this).
      - `mock_bridge.js`: `resolveDuplicateProgram()`/`resolveDuplicateCombis()` mocks
        updated to accept and honor the same explicit `targets` array (only the named
        entries get touched, same hash/existence validation in mock terms as the real
        backend), replacing their old "clear every same-name entry" version.
      - **Verified for real**: full `cmake --build` (`pcg_file_test` + `kronos_editor`)
        clean, tests passing, all three touched frontend files parsed clean via
        `osascript -l JavaScript` (no `node` in this environment). A real Debug build was
        launched twice (before and after the frontend changes) and confirmed it starts up
        without crashing both times. **Live UI interaction still not verified** in this
        session (no browser/screenshot tooling available here) -- the resolve picker's
        actual rendering, radio/checkbox behavior, and slide-in positioning all still need
        a real hands-on smoke test, same repeatedly-noted gap as #67/#68 above.
      - Docs updated again: `docs/content/guide/prog/index.md`'s "Same content, different
        location" section rewritten for the picker (no more "Keep only this" per copy),
        `docs/content/guide/combi/index.md`'s own version updated the same way plus its
        Combi-specific "nothing gets cleared, so it can show up again for a later pass"
        caveat.
      - **Private-repo note only, nothing built**: per direct request, recorded a new
        "OPEN:" idea at the end of `private/diy-korg-kronos-editor/STATE.md` -- once that
        repo's own (not-yet-built) MIDI SysEx transport layer exists, let the user send a
        single note to a real KRONOS for either variant of a "Same name, different
        content" entry, to decide by ear which one is genuine rather than by eye alone.
        Explicitly recorded as an unshaped idea (no message format, no UI, no decision on
        which repo it would live in), not a design.
  70. **BUILT (2026-08-25)**: same-day follow-up to #69 above, from two more pieces of
      direct feedback. First: "Why uses 'Same name, different content' a different UI...
      can we treat both the same?" -- answered (grouping by name alone mixes genuinely-
      different content across variant clusters, unsafe to offer the same picker on
      without scoping it correctly), which led directly to the second, larger ask: "For
      all other programs 'Same name, different content'... sometimes can be consolidated
      in the same way like 'Same content different location'" -- confirmed via
      `AskUserQuestion` that consolidating a genuinely-different variant must NEVER clear
      the non-kept variant's own bytes (only #68's byte-exact flow gets to do that, since
      it's provably lossless there) -- "Leave slot untouched" chosen over full parity.
      Also, independently: "Same name, different content should consider the underlying
      category (EXi, HD-1) too... it's clearly a different sound and name matches by
      accident" -- a real bug in #67's original grouping.
      - **Bug fix: name-collision grouping is now bank-type-aware for Programs**.
        `PcgFile::NameCollisionGroup` (`PcgFile.h`) gained an `int bankType = -1` field
        (a real `kronos::ProgramBankType` for a Program group, always -1 -- meaning "no
        such distinction" -- for a Combi group). The shared internal `groupNameCollisions()`
        helper (`PcgFile.cpp`) now groups by `std::map<std::pair<std::string, int>, ...>`
        (name + bankType) instead of name alone -- an HD-1 "Bass 1" and an EXi "Bass 1" are
        now two INDEPENDENT groups (each still needs its own 2+ distinct hashes to be
        reported at all), not one spurious cross-engine collision. `findCombiNameCollisions()`
        passes `bankType=-1` for every entry, so Combis keep grouping by name alone,
        unaffected. `EditorBridge::findProgramNameCollisions()`/`findCombiNameCollisions()`
        now also set `bankType` on the returned group value. `mock_bridge.js`'s own
        `findNameCollisions()` had the EXACT same bug in mock terms (`makeFakePrograms()`'s
        bank 0 = HD-1 / bank 1 = EXi convention reuses one name list per bank, so nearly
        every fake Program name used to register as a spurious collision) -- fixed the same
        way, keyed by `${name} ${bankType ?? -1}` (the `?? -1` makes it a no-op for Combi
        entries, which have no `bankType` field at all). **Net effect in mock/browser
        mode**: the Programs "Same name, different content" table now correctly shows
        (near-)empty for the stock demo data, since the fixture's only same-name repeats
        were exactly this cross-engine coincidence -- not a regression, the intended fix.
      - **Verified for real**: `buildNameCollisionFixture()` (`tests/pcg_file_test.cpp`)
        gained a second Program bank (tagged `MBK1`/EXi, one "Lead" record) alongside the
        existing HD-1 "Lead" 2-variant collision -- `testFindNameCollisions()` now asserts
        the HD-1 group is still exactly 2 variants (not 3, which a regression would produce
        by pulling the EXi entry in) and carries `bankType == 0`. Full `cmake --build`
        (`pcg_file_test` + `kronos_editor`) clean, `pcg_file_test`: "All checks passed".
      - **Backend: `requireByteExactMatch`, a new shared parameter on both resolve
        methods**. `PcgFile::resolveDuplicates()`/`resolveDuplicateCombis()` each gained a
        `bool requireByteExactMatch` parameter that deliberately gates TWO behaviors
        together (chosen this way, not independently toggleable, per the "leave untouched"
        decision above): `true` (the existing "Same content, different location" flow) --
        validates every target's `contentHash` matches the kept copy's own, THEN clears it
        (Programs only; Combis never clear regardless, see #68's own note); `false` (new
        "Same name, different content" consolidate flow) -- skips the hash check entirely
        (targets are EXPECTED to differ) and never clears anything, `clearedPrograms` stays
        0. All-or-nothing target-existence validation still applies in either mode.
        `EditorBridge::resolveDuplicateProgram()`/`resolveDuplicateCombis()` gained a
        matching 5th JS arg (defaults `true` via `boolArg(args, 4, true)` if omitted, so
        an older-shaped 4-arg call still resolves byte-exact duplicates exactly as before)
        -- the Program version also skips reading `Init-Program-HD1.raw`/`-EXi.raw`
        entirely when `false`, so an empty/missing `resources/` dir can't block a
        consolidate that was never going to touch those templates anyway.
      - **Verified for real**: two new tests, `testResolveDuplicatesConsolidateDifferentContent()`
        and `testResolveDuplicateCombisConsolidateDifferentContent()`, both reusing
        existing fixtures' own genuinely-different-content pairs (`buildSyntheticPcgFile()`'s
        bank0/number2 "Unique Program" vs. "Test Program A"; `buildCombiDuplicateFixture()`'s
        "Solo" vs. "Twin") that `testResolveDuplicates()`/`testFindAndResolveDuplicateCombis()`
        already prove get REJECTED when `requireByteExactMatch=true` -- these prove the
        exact same pairs get ACCEPTED, left byte-for-byte untouched, and (for the Program
        case) have their real Set List/Combi Timbre references actually repointed, when
        `false`. Full `cmake --build` clean, `pcg_file_test`: "All checks passed".
      - **Frontend: the resolve-picker sidebar (#69) now opens from the name-collision
        table too**. `renderNameCollisionTable()` (`pane-program-editor.js`) gained the
        same "⋯" trigger `renderExactDuplicatesTable()` already has, opening
        `openResolvePicker()` with `requireByteExactMatch=false` and the group FLATTENED
        across ALL its variants (`group.variants.flatMap(v => v.members)`) -- consolidating
        across variant clusters, not just within one, is the entire point of this mode.
        `resolvePicker`'s own state gained `requireByteExactMatch` and (for the name-mode
        case) `nameGroupKey` (`{name, bankType}`) -- the picker's title/footer-button text
        now read "Consolidate variants"/"Consolidate N into Src" rather than "Resolve
        duplicates"/"Resolve N into Src" in this mode, and the per-row Dupl checkbox's
        tooltip is reworded to make clear nothing gets cleared, ever, in this mode.
        `applyResolvePicker()`'s post-resolve re-sync is now mode-aware too: a byte-exact
        group genuinely shrinks as members get cleared (re-synced by bank/number, as
        before), but a name-collision group's own members NEVER disappear from a
        consolidate (nothing about their content -- or hash -- changes, only references
        move), so it's re-synced by re-finding the same `{name, bankType}` group instead.
        Also added Program groups' own bank-type suffix to their displayed name (new
        `nameCollisionGroupLabel()` helper, e.g. "Bass 1 (HD-1)") now that two groups can
        legitimately share a bare name, and fixed `expandedCollisionKeys`' own key (used
        to be name-only, now `${p|c}:${bankType}:${name}`) for the same reason.
      - Fixed a real bug found while extending the Program resolve mock: `resolveDuplicateProgram()`'s
        mock only marked the dataset dirty when `clearedPrograms > 0` -- missed the new
        case where `requireByteExactMatch=false` clears nothing but still repoints real
        Set List/Combi Timbre references (a genuine write). `mock_bridge.js` also gained
        `requireByteExactMatch` support on both resolve mocks (default `true`, matching
        the real bridge), and `findNameCollisions()`'s own fix above.
      - **Verified for real**: full `cmake --build` (`pcg_file_test` + `kronos_editor`)
        clean, `pcg_file_test`: "All checks passed" (6 tests added/extended this round).
        All three touched frontend files parsed clean via `osascript -l JavaScript` (still
        no `node` in this environment). A real Debug build was launched and confirmed it
        starts up without crashing. **Live UI interaction still not verified** -- same
        repeatedly-noted gap as #67/#68/#69 above; the consolidate mode's picker title/
        wording, the bank-type-suffixed group labels, and the "⋯" trigger on the name-
        collision table all still need a real hands-on smoke test.
      - Docs updated same-session: `docs/content/guide/prog/index.md`'s "Same name,
        different content" section rewritten for the Consolidate picker (bank-type-labeled
        groups, cross-variant Src/Dupl selection, "never clears" emphasized); `docs/
        content/guide/combi/index.md`'s own version updated the same way, cross-linking
        back to the Programs page rather than re-explaining the shared mechanics.
        `docker run hugomods/hugo` itself was unreliable in this environment this round
        (hung/failed to even start a container on repeated attempts, unrelated to any
        content change -- a working build of this exact docs tree from earlier in this
        same session is still the most recent real confirmation) -- the new heading anchor
        (`/guide/prog#same-name-different-content`) was checked by hand instead, against
        Hugo's own slug algorithm as already confirmed by this file's existing
        `#jumping-to-a-program-combi-or-set-list-slot` link (lowercase, punctuation
        stripped, spaces to hyphens) rather than a real build.
  71. **BUILT (2026-08-26)**: two more pieces of direct feedback on #70's resolve picker,
      plus a substantial new feature -- a real "you have unsaved changes, quit anyway?"
      guard, which turned out to require patching vendored third_party/choc code.
      - **Picking Src auto-checks every other copy as Dupl** (`pane-program-editor.js`'s
        `srcRadio`'s own `change` listener) -- the common case is folding in everything
        except the kept copy, so that's now the one-click default; the user un-checks any
        specific entry to leave it alone instead. Picking a DIFFERENT Src resets the whole
        selection back to "everyone else," rather than trying to preserve a prior partial
        selection that no longer has a clear meaning against the new Src.
      - **Blue replaced with orange** in the resolve picker: `accent-color: var(--editor-
        accent)` on the Src/Dupl native radio/checkbox inputs (`.duplicate-resolve-table`,
        style.css) -- the standard, WebKit-supported way to recolor a native control without
        replacing it with a custom fake one -- plus the picker's own "Resolve"/"Consolidate"
        button, which #69/#70 had deliberately left Bulma-blue ("a genuine write action, not
        a toggled state") -- overridden per this direct request via its own new
        `.duplicate-resolve-apply-button` class (not folded into style.css's shared
        `.is-link` override list, since this button never uses `.is-link` at all).
      - **Guard against quitting with unsaved changes** -- reported directly: "all unsaved
        changes are lost without warning the user," wants a "you have unsaved changes, quit
        without saving?" [yes]/[no] dialog. Investigation found this app's native windowing
        (`choc::ui::DesktopWindow`, vendored `third_party/choc/`) has NO way to veto or
        delay a close at all -- macOS's `windowShouldClose:` was hardcoded `return TRUE`,
        Linux never connected to GTK's vetoable `"delete-event"` signal (only the
        post-destruction `"destroy"`), and Windows' WM_CLOSE handler never destroyed
        anything itself anyway (incidentally already "vetoable" by construction). Asked the
        user via `AskUserQuestion` whether to scope this to macOS-only (the only platform
        buildable/testable here) or write all three now, unverified on Windows/Linux --
        chose all three now.
        - **CHOC patch** (`choc_DesktopWindow.h`, every addition marked "DIY-KRONOS-EDITOR
          local addition" with a date, so a future CHOC upgrade doesn't silently eat them):
          new `DesktopWindow::closeRequested` (set = suppresses the window's own default
          close entirely, notifies this callback instead of `windowClosed`) and
          `DesktopWindow::forceClose()` (bypasses `closeRequested`, actually closes for
          real, fires `windowClosed` as before). macOS: `windowShouldClose:` now checks
          `closeRequested`/a new `forcingClose` re-entrancy flag (`forceClose()`'s own
          `"close"` call would otherwise just re-trigger the same handler and veto itself).
          Linux: added the missing `"delete-event"` connection (UNVERIFIED, no GTK
          toolchain available -- `"destroy"` alone is provably too late to veto, per GTK's
          own documented signal semantics, but never compiled). Windows: `handleClose()`
          (WM_CLOSE) now calls `closeRequested` instead of `windowClosed` when set;
          `forceClose()` reuses the existing `HWNDHolder::reset()` (UNVERIFIED, no Windows
          toolchain available). Real bug hit and fixed on the FIRST macOS compile attempt:
          `CHOC_AUTORELEASE_BEGIN`/`END` are a literal `{`/`}` pair (an `@autoreleasepool`
          block) wrapping `windowShouldClose:`'s body -- an early `return` nested inside an
          `if` block with `CHOC_AUTORELEASE_END` placed INSIDE that `if` closed the wrong
          brace (the `if`'s own, not the autoreleasepool's), a real brace-mismatch compile
          error, not a logic bug -- fixed by deciding the veto via a plain `bool shouldVeto`
          declared BEFORE the autoreleasepool (so it's still in scope for one unified
          `return` AFTER it closes), never nesting a return inside it at all.
        - **A second, completely separate native gate, found the hard way**: after the CHOC
          patch compiled and `EditorBridge::anyDatasetDirty()` (new -- `bool`, iterates
          every open dataset's own already-existing `isDirty()`, no JS shape to bridge,
          same "direct C++ caller" convention `getProgramRecordBytesRaw()` already
          established) + `main.cpp`'s own `closeRequested` wiring were built and believed
          complete, a REAL live test (a genuine `.app` bundle build, launched via `open` so
          AppleScript could address it by bundle ID -- a raw `./kronos_editor` binary isn't
          addressable that way at all, confirmed as a dead end first) sent a real `quit`
          Apple Event and the app closed INSTANTLY anyway, despite the window-level veto.
          Root cause, confirmed by reading Cocoa's own documented default: Cmd+Q / the Quit
          menu item / Dock "Quit" / an AppleScript `quit` event all go through
          `-[NSApplication terminate:]`, a COMPLETELY SEPARATE gate from any window's own
          `windowShouldClose:` -- with no `NSApplicationDelegate` installed at all (true of
          this app before this session), Cocoa's documented default is to just terminate
          immediately, never consulting any window whatsoever. Fixed with a second, genuinely
          new mechanism: a minimal `NSApplicationDelegate` (`main.cpp`'s own
          `installAppTerminateDelegate()`/`AppTerminateContext`, macOS-only, `#if
          CHOC_APPLE` -- NOT folded into the vendored CHOC patch, since this is an app-level,
          not per-window, concern) implementing `applicationShouldTerminate:`, built with
          the exact same raw-ObjC-runtime pattern (`createDelegateClass`,
          `class_addMethod` with a stateless captureless `+[]` IMP,
          `objc_setAssociatedObject`/`objc_getAssociatedObject` to reach real C++ context)
          CHOC's own per-window delegate already establishes. Returns `NSTerminateNow` (1)
          immediately if nothing's dirty; `NSTerminateLater` (2) otherwise, triggering the
          SAME confirm-dialog round trip and leaving Cocoa waiting on an explicit later
          `replyToApplicationShouldTerminate:` (YES, confirmed -- then
          `choc::messageloop::stop()` directly, the same mechanism every normal window
          close already uses to end `main()`'s `[NSApp run]` loop; NO, cancelled -- Cocoa is
          genuinely BLOCKED waiting on exactly one reply once `NSTerminateLater` is
          returned, so unlike the per-window veto, doing nothing here is NOT enough).
        - **Frontend**: `app.js` gained `window.confirmQuitRequested()` (the per-window
          path, called from `closeRequested`) and `window.confirmAppQuitRequested()` (the
          app-level path, called from `applicationShouldTerminate:`) -- both show the exact
          same dialog via `window.showConfirmDialog()` (confirm-dialog.js) -- NOT
          `window.confirm()`, same reason `pane.js`'s existing Unload button already uses
          it: WKWebView silently drops native JS `confirm()` under this app's WebView --
          but reply differently afterward (`confirmQuitAndClose()`/`confirmAppQuitAndTerminate()`
          + `cancelAppQuitReply()`, all bound per-window in `main.cpp` alongside the
          existing `bindEditorBridgeFunctions()` call, `#if CHOC_APPLE`-guarded for the
          app-level pair).
        - **Verified for real, iteratively, catching two real bugs live**: a temporary
          `TEMP_FORCE_DIRTY_FOR_MANUAL_TEST` constant (removed before finishing) simulated
          a dirty dataset without needing a real `.PCG` file. First live test of the
          per-window veto alone: worked (process stayed alive on a plain `./kronos_editor`
          quit attempt). First live test of a real `quit` Apple Event against a proper
          `.app` bundle: FAILED (app closed instantly) -- this is what surfaced the missing
          `applicationShouldTerminate:` gate above. After adding it: temporary
          `std::cerr` tracing confirmed the new delegate installs and IS invoked, but
          initially still returned `NSTerminateNow` -- a real test-harness bug, not a code
          bug (the temporary force-dirty flag had only been wired into the per-window path,
          not the new app-level one) -- fixed, then a live `quit` Apple Event against the
          dirty app correctly returned `NSTerminateLater` and the process stayed alive
          waiting on the confirm dialog. (One false alarm along the way: forcibly killing
          `osascript` mid-flight while it was blocked waiting for the Apple Event reply
          appeared to kill the target app too -- running `osascript` detached instead
          resolved this; not a real bug in this app, an artifact of the test harness
          itself.) Full `cmake --build` (`pcg_file_test` + `kronos_editor`, both the normal
          Debug `build/` dir and a separate, deliberately temporary `build_bundle_test/`
          configured with `-DEDITOR_EMBED_RESOURCES=ON` purely to get a real `.app` bundle
          for this live testing, removed afterward) clean throughout, `pcg_file_test`: "All
          checks passed" (untouched by this feature, confirms no regression). All touched
          frontend files parsed clean via `osascript -l JavaScript`.
        - **Still open**: Linux/Windows halves of the CHOC patch are unverified (never
          compiled) -- see their own doc comments in `choc_DesktopWindow.h` for exactly
          what's untested there. The confirm→terminate and cancel→stay-open sub-paths
          (`confirmAppQuitAndTerminate()`/`cancelAppQuitReply()`) were verified by code
          review only, not a live click-through -- no UI automation/Accessibility
          permission available in this environment (confirmed via a failed `osascript`
          keystroke-simulation attempt) to actually click the dialog's own buttons.

CLEAN UP -- noted 2026-08-15:

  1. RESOLVED (2026-08-15): `docs/content/building/index.md` had two sections
     covering the same macOS ground -- this session's own "Real DevTools
     attached to the running app" (prose, all 3 platforms) and a separately-
     added "Debugging (setup) > MacOSX / Safari" (screenshots). Merged into
     one "## Debugging, especially the JavaScript side" section (Headless
     tests -> Plain-browser mode -> Real DevTools, the last with a per-
     platform macOS/Windows/Linux breakdown, macOS's including the
     screenshots) -- fixed two references left dangling by the reorder
     ("the first option"/"each of these" no longer pointed at the right
     thing) while merging.
  2. NOT YET ACTED ON: `docs/README.md`'s file-format pointer was repointed
     from
     `content/format/index.md` to `content/overview/index.md` (display text
     included) -- flagged directly as an inconsistency against this
     project's own documented convention (`content/format/index.md` = the
     canonical file-format reference, `content/overview/index.md` = a
     separate Hugo Overview summary, per this file's CLAUDE.md pointer and
     STATE.md's own "Keep the docs in sync by hand" section). Project owner
     chose to keep it as edited rather than revert. Worth reconciling
     later: either this is the start of retiring/merging the standalone
     format page into Overview (in which case CLAUDE.md's own description
     of the docs layout needs updating to match), or it should eventually
     move back -- not resolved either way yet.
  3. NOT YET ACTED ON (found 2026-08-16, while renaming the Setlist codec
     family, entry 52): `docs/content/components/index.md` has several
     stale `pane.js` references (e.g. "A Set List slot's Color/Comment/
     Volume editors (`pane.js`)...") that predate this project's own
     `pane.js` -> `pane-setlist-editor.js` split -- that Setlist-specific
     logic moved out of `pane.js` at some earlier point this doc was never
     updated for. Only the one line the codec rename directly touched got
     corrected; a dedicated pass over the rest of that page is still
     needed.

=== END STATE BLOCK ===
