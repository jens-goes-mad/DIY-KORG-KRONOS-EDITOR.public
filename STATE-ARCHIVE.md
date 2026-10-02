# STATE.md archive -- resolved and built entries

Moved out of STATE.md on 2026-09-30 to keep STATE.md short. This is the
former "BLIND SPOTS / NOT YET TOUCHED" section **verbatim** (Format list,
App/UI entries #8-#109, CLEAN UP), plus entries added since. Entry numbers
are unchanged -- code comments that cite "STATE.md entry N" mean the entry
with that number here. What is still open lives in STATE.md's compact
"BLIND SPOTS / STILL OPEN" section (and, for the file format, in
`docs/content/format/index.md` §8); new finished work gets its full text
appended here and a one-line index entry in STATE.md.

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
  72. **FIXED (2026-08-27)**, two more pieces of direct feedback on #71's work:
      - **Duplicates panel: the whole pane was scrolling, not just the table.** The
        vertical sub-tab strip and the view dropdown (`pane-program-editor.js`'s
        `render()`) were both plain children of `.duplicates-content`, which is itself
        just a child of `.lib-panel[data-panel="duplicates"]` inside `.library-body`
        (`pane.js`) -- and `.lib-panel` has no CSS rule of its own anywhere (a plain
        block, `height: auto`), so `.duplicates-body`'s own `height: 100%` (already
        present) resolved against nothing and did nothing; `.library-body`'s OWN
        `overflow-y: auto` ended up scrolling the entire thing together instead.
        Fixed with a real height/overflow chain: `.lib-panel[data-panel="duplicates"]`
        gained `height: 100%` (scoped to just this panel -- Programs/Combis
        deliberately keep relying on `.library-body`'s own scroll, unaffected),
        `.duplicates-content` became a flex COLUMN with `min-height: 0` (the actual
        fix -- a flex item's default `min-height: auto` silently defeats ANY overflow
        rule on it or its children until this is set, the same well-known gotcha
        `min-width: 0` on the same element already existed to solve along the other
        axis), and the table now lives in a NEW `.duplicates-table-scroll` wrapper
        (`overflow-y: auto`, `flex: 1`, `min-height: 0`) separate from the dropdown,
        which stays a fixed, non-scrolling header (`flex-shrink: 0`) above it. Verified
        via balanced-brace and syntax checks only (no browser/screenshot tooling in
        this environment) -- the reasoning is a standard, well-understood flexbox
        pattern, not a guess, but genuinely not seen rendered.
      - **Real bug, reported directly: "Closing the window leaves an artifact in the
        process list."** Root cause: `DesktopWindow::forceClose()`'s own macOS
        implementation (#71's CHOC patch) called plain `"close"` on the NSWindow, on
        the INCORRECT, never-actually-verified assumption that it would re-invoke
        `windowShouldClose:` the same way a user-initiated close does -- a real "no
        guessing" violation, caught the hard way instead of checked first. Per Apple's
        own documented contract, `-[NSWindow close]` deliberately does NOT consult the
        delegate's `windowShouldClose:` at all (only the informational,
        after-the-fact `windowWillClose:`) -- only `-performClose:` does, simulating a
        real user-initiated close end to end. Calling plain `"close"` meant `p.window`
        was never cleared and `windowClosed()` (what actually runs
        `openWindows.erase()`/checks `openWindows.empty()`/stops the message loop in
        `main.cpp`) never fired -- the NSWindow itself still visibly closed (`"close"`
        DOES do that part), but the whole process silently never exited. Fixed by
        switching to `"performClose:"`.
        - **Root cause of never catching this earlier**: every one of #71's own live
          tests exercised the APP-level `applicationShouldTerminate:` gate (Cmd+Q /
          AppleScript `quit`, reached via a real `.app` bundle so AppleScript could
          address it), never the PER-WINDOW `closeRequested`/`forceClose()` path this
          bug actually lived in -- confirmed by re-reading that session's own test
          trace, not assumed.
        - **Verified for real this time, with an actual positive AND negative
          control** -- no Accessibility/UI-scripting permission exists in this
          environment to click a real title-bar close button (confirmed again), so a
          TEMPORARY background `std::thread` (removed once done) posted a real
          `closeRequested()` call onto the main message loop 3 seconds after launch,
          simulating the exact gesture. With the bug still in place (`"close"`): the
          process was confirmed STILL RUNNING 5+ seconds later (`ps`) -- a genuine
          negative control, not just re-reading the same passing case. Switched to
          `"performClose:"`: the same live test now showed the process genuinely GONE
          within 4 seconds, no crash, log clean. `cmake --build` (`pcg_file_test` +
          `kronos_editor`) clean throughout both directions, `pcg_file_test`: "All
          checks passed" (untouched, confirms no regression). All temporary test code
          (the background thread, its now-unneeded `<thread>`/`<chrono>` includes)
          fully removed afterward -- confirmed via a final clean rebuild with none of
          it present.

  73. **FIXED (2026-09-04)**, two drag-and-drop bugs reported directly against the
      Combis table (`pane-combi-editor.js`):
      - **"Sometimes the Combi is inserted AFTER the drop target."** Real off-by-one
        in the before/after "move within bank" gesture. `window.moveCombiWithinBank()`
        (and `reorderSongEntry()` for Set List slots, same bug) takes the moving
        record's FINAL resting index -- `PcgFile::moveCombiWithinBank()`/`reorderSong()`
        shift the whole intervening range, then drop the record at exactly that index.
        The frontend passed `c.number` / `c.number + 1` unadjusted, but when the source
        sits ABOVE the target, pulling it out first slides the target (and everything
        between) down one slot, so the raw value overshoots by one and the Combi lands
        one past where the cursor showed. Fixed in both places by computing
        `insertPos` (where it should land in the CURRENT numbering) then subtracting one
        iff `source < insertPos`; added the matching "adjacent drop = no real move"
        guard the Combi path was missing. Downward drags now land exactly where the
        before/after line indicated. Frontend-only -- the native final-index semantics
        were already correct. Not run in a browser (no JS runtime in this environment);
        the index arithmetic was traced by hand against both native shift loops.
      - **"Hard to distinguish a copy from a move mid-drag."** The Combis table's four
        gestures (swap onto occupied / copy onto empty / move before / move after) all
        showed the same blue `.drop-target` hover and a `dropEffect = "move"` cursor.
        Now `dragover` computes the gesture live and shows it: `.drop-copy` (green,
        `--copy-accent`, plus the OS "+" cursor badge via `dropEffect = "copy"` --
        needed `effectAllowed = "copyMove"` at dragstart, same as
        `pane-program-editor.js`) for copy-onto-empty, the existing blue for swap, and
        a line along the row's top/bottom edge (`.drop-before`/`.drop-after`, an inset
        `<td>` box-shadow -- a `<tr>` one doesn't paint in a `border-collapse` table,
        same limitation `.drop-indicator`/`.multi-selected` work around) for the two
        move gestures. All four hover classes clear together on
        dragleave/dragend/drop. Cross-dataset drops (copy-only) now also treat the
        whole row as the "on" zone in the `drop` handler, matching what `dragover`
        shows.
      - **Then, per direct follow-up ("full consistency, remove redundant code to
        dedicated drag-and-drop.js"): extracted the shared engine.** The Set List,
        Programs and Combis tables each hand-rolled their own
        dragstart/dragover/dragleave/dragend/drop skeleton -- near-identical, and
        drifted (only Combis set a copy cursor, only Set List showed an insert line,
        only Programs rejected an engine-type mismatch on hover; the off-by-one above
        had to be fixed twice). New `frontend/drag-and-drop.js` (a plain global
        script, loaded before the pane editors, no module system -- like `pane.js`'s
        shared helpers) owns `makeRowDraggable(tr, { zones, getPayload, classify,
        onDrop })` plus `dropZoneForEvent()` (moved out of `pane.js`) and the one
        module-level "row being dragged" variable (was three: `draggedCombi`,
        `draggedProgram`, `draggedFromDatasetId`). Each call site keeps only what
        genuinely differs: `getPayload()`, `classify({dragged,zone,shiftKey}) => null
        | {effect:"copy"|"move", zone?}` (validity + the copy/move judgement the hover
        needs, called on every dragover, must be pure), and `onDrop({source,zone,
        shiftKey})`. `classify` may return a coerced `zone` for gestures that force one
        (cross-dataset Combi copy is always "onto").
      - **Consistency changes that fell out of unifying it:** the Set List "onto"
        drop (a copy-over) now shows the green `.drop-copy` + "+" cursor like every
        other copy (was a bare `dropEffect = "move"`); the Programs copy hover is
        green too (was blue -- blue now means move/swap everywhere); the Set List
        insert line moved from a floating `.drop-indicator` div (deleted, along with
        `showDropIndicator`/`hideDropIndicator` and the `position: relative` it needed
        on `.entries-scroll`) to the same row-edge `<td>` box-shadow the Combis table
        uses. `.drop-copy`/`.drop-before`/`.drop-after` CSS generalised from
        `.combis-table` to all three tables. Guide docs (setlist/prog) updated for the
        new visual language.
      - **Then, "swap not working" reported directly.** The `drop` handler had (since
        forever, in every version) recomputed the drop zone with a fresh
        `dropZoneForEvent(tr, dropEvent)`. A `drop` event's cursor coords aren't as
        reliable as a `dragover`'s -- seen arriving as `clientY: 0` in a WebView, which
        `dropZoneForEvent` reads as `relativeY` far below 0 => `"before"`, so EVERY
        Combi drop silently resolved to a move/insert and the swap ("onto") gesture
        could never fire. Fixed by having `makeRowDraggable` remember the last
        dragover's zone + `classify` verdict and act on THOSE at drop time -- the drop
        now does exactly what the hover just promised, and never depends on the drop
        event's own coordinates. Confirmed with a jsc harness (JavaScriptCore is on
        this Mac even though node isn't): loads the real `drag-and-drop.js`, simulates
        dragstart -> dragover(valid Y) -> drop(clientY 0), and the swap now fires where
        before the fix it became `moveCombiToBank`.
      - **Same round, per direct choice: dropping a USED Combi onto an EMPTY slot now
        MOVES it (Shift = copy).** Was copy-only, which meant the intuitive
        drag-X-onto-empty-slot gesture duplicated instead of moving, and the only way
        to actually move was the backwards drag-the-empty-slot-onto-X. Now: plain drop
        onto an "Init Combi" slot = `swapCombis` (the target's Init Combi record lands
        in the source slot, i.e. a move that vacates the source); hold Shift =
        `copyCombi` (the old behaviour, kept for the "two variations of one patch"
        workflow). Hover is blue "will move" by default, green "+" under Shift. Note
        this makes the Shift meaning OPPOSITE to the Programs table (there, plain =
        copy, Shift = swap) -- deliberate: a Program's bank/number is its identity and
        heavily referenced so copy is the safe default, a Combi is only referenced by
        Set List slots (all repointed) so move is safe and intuitive.
      - **Then "shift+copy not working, no + cursor" reported.** Root cause: **WebKit
        (WKWebView on macOS -- CHOC's engine there) does not populate modifier-key
        flags on drag events** -- `dragover.shiftKey` / `drop.shiftKey` are always
        `false`. Long-standing WebKit behaviour; means the Programs Shift+drag-swap
        (entry 48, 2026-08-15) had also silently never worked on Mac (only on Windows /
        WebView2 / Chromium). Fixed in `drag-and-drop.js` by tracking Shift from real
        `document` keydown/keyup (`capture: true`, `window` blur clears a stuck hold)
        into a module-level `shiftKeyDown`, and reading `shiftHeld(ev) = ev.shiftKey ||
        shiftKeyDown` everywhere the engine needs it -- covers WebKit (keydown fires,
        event flag doesn't) and Chromium (both). The UX rule is now "hold Shift BEFORE
        you start dragging" (like Option-drag in Finder): once a drag's own modal loop
        starts, WebKit suppresses keyboard events, so a Shift pressed mid-drag isn't
        seen -- but one held from before the mousedown already set `shiftKeyDown`.
        Guide docs (combi/prog) + inline comments updated to say so. jsc harness
        (which now stubs `document`) drives Shift via a real keydown and confirms the
        copy path + green `.drop-copy` + `dropEffect="copy"`; full matrix
        (empty/used x empty/used x shift x edge/middle x clientY-0 drop) passes.
  74. **BUILT (2026-09-04)**: Combi "Reset entry" -- reported directly missing ("no
      local menu or Hamburger in opened combi"), mirroring the Programs table's own
      right-click reset (entry 58, 2026-08-20), which Combis never got.
      - **Why it didn't exist yet**: `resetProgram()` writes a shipped factory
        template (`resources/Init-Program-HD1.raw`/`-EXi.raw`). No such template
        exists for Combis -- real "Init Combi" bytes differ across the 14 banks by
        40+ unexplained bytes (entry 34), so one resource file would be wrong for 13
        of 14, and this project's "no guessing, ever" rule rules out fabricating one.
      - **How it works anyway**: `moveCombiToBank()` already solves the identical
        problem for its own vacate step by sourcing a real "Init Combi" record LIVE
        from elsewhere in the SAME bank, patching its name to "- Init Combi -" for
        visibility. New `PcgFile::resetCombi(bank, number)` (`PcgFile.h`/`.cpp`)
        reuses that exact donor search (exact, case-sensitive "Init Combi" match) and
        a new shared `patchCombiNameToVacated()` helper (factored out of
        `moveCombiToBank()`'s own inline version, itself unchanged in behavior) to
        COPY the donor's bytes over the target instead of relocating them. Refuses
        (nothing written) if the bank has no other exact "Init Combi" to draw from,
        or the slot doesn't exist. Never repoints anything -- mirrors
        `resetProgram()`'s own "existing references keep pointing, now showing the
        reset content" contract exactly, including discarding the
        `repointSetlistReferences()` call's return value rather than reporting it as
        a `setlistRefsRepointed` count (which would misleadingly read as a real
        redirect). `EditorBridge::resetCombi()`/`main.cpp` binding follow
        `resetProgram()`'s own `[datasetId, bank, number] -> {ok}` shape.
      - **Frontend**: the Programs table's right-click menu scaffolding
        (`openRowMenu()`'s DOM/dismiss plumbing, previously hand-rolled once in
        `pane-program-editor.js`) extracted into `pane.js`'s
        `showRowContextMenu(ev, items)` -- both Programs' and Combis' own
        "Reset entry" now call it, `.program-row-menu` CSS renamed
        `.row-context-menu` to match. Combis' own `resetEntry(c)`
        (`pane-combi-editor.js`) mirrors Programs' shape (confirm dialog,
        `isDanger: true`, no undo) but refreshes MORE than Programs' own version
        does: `onNeedsFullReload()` + `onRefreshOppositeLibrary()` (this file's own
        drag-gesture pattern) AND `onSetlistRefsRepointed()` (unconditionally, not
        gated on a repoint count that doesn't exist) -- a reset changes what an
        existing Set List reference DISPLAYS without repointing it, and the Setlist
        tab keeps its own separate JS-side name cache that only a real refresh
        invalidates. **Flagging, not fixing**: Programs' own `resetEntry()` only
        calls its local `refresh()`, so a Set List slot referencing a just-reset
        Program can show a stale name until something else refreshes that tab --
        same gap, not touched here since it wasn't what was reported.
      - **Verified**: new `testResetCombi()` (`tests/pcg_file_test.cpp`) against the
        existing `buildCombiRearrangeFixture()` -- happy path (bank0's real "Init
        Combi" donor reached bank0/1, the donor itself left untouched, an unrelated
        slot untouched, the referencing Set List slot's `params` unchanged but its
        cached `instrumentName` now reads "- Init Combi -"), the "no donor in this
        bank" refusal (bank1 has only "- iNit COMBI -", which the donor search's
        exact match deliberately doesn't accept even though `looksLikeEmptyCombiName()`
        would), and out-of-range bank/number refusals. Full `cmake --build`
        (`pcg_file_test` + `kronos_editor`, private submodule included) clean,
        `pcg_file_test`: "All checks passed". Frontend not run in a browser (still no
        JS runtime/headless setup in this environment) -- `jsc` parse-checked the
        touched files and the diff was reviewed by hand against the Programs
        equivalent it mirrors.

  75. **FIXED (2026-09-04)**: "Combi copy not working, no plus sign appears with
      neither key combination" -- entry 73's Shift-tracked-via-keydown fix (built
      specifically to solve this exact gesture) turned out not to work either.
      Directly confirmed by trying all three ways it could plausibly fail: Shift
      held BEFORE starting the drag (the documented, supposedly-reliable rule),
      Shift pressed mid-drag, and Option/Alt instead of Shift -- none of it
      registered. No further root-cause available without a real device/Web
      Inspector session (not available in this environment) -- so rather than a
      third unverified WebKit-modifier theory, Combi's onto-an-empty-slot gesture
      DROPPED the modifier requirement entirely: it's now an unconditional COPY
      (`classify`/`onDrop` in `pane-combi-editor.js` no longer read `shiftKey` at
      all for this branch). Moving a Combi INTO an empty slot instead reuses the
      swap gesture in the OTHER direction (drag the empty slot onto the used one --
      already modifier-free, unchanged, still works) rather than needing a second
      new mechanism. `drag-and-drop.js`'s `shiftHeld()`/`shiftKeyDown` machinery
      stays in place -- the Programs table's Shift+drag-swap is now its only
      caller -- with its doc comment updated to record that this failed under
      conditions that should have worked, so its reliability is genuinely in
      question, not just theoretically fragile.
      **Flagged, not fixed**: Programs' Shift+drag-swap (entry 48) almost certainly
      has the identical failure and hasn't been re-verified or changed -- noted
      directly in both `drag-and-drop.js`'s comment and the Programs guide page,
      since it wasn't what was reported and (unlike Combi) has no equally-simple
      modifier-free replacement gesture sitting right next to it. Combi guide
      updated to the unconditional-copy wording and the direction-reversed move
      path; STATE.md's own prior entry 73 write-up for the Shift mechanism is
      superseded by this one for the Combi half specifically. jsc harness re-run:
      used-over-empty now copies regardless of a simulated Shift keydown; every
      other case (swap, before/after move, the clientY-0 robustness case)
      unchanged. Frontend not run in a browser -- `jsc` parse-checked, and this
      whole feature is now exactly what it originally was in entry 73's FIRST
      version before the Shift request, so behaviourally it's a revert plus
      updated docs, not new untested logic.
  76. **FIXED (2026-09-04)**: "Hamburger Menu misses in opened editor, CTRL+click
      open menu but nothing happens" -- entry 74's Combi (and the pre-existing
      Programs) "Reset entry" menu was right-click-only, no visible affordance at
      all. Reported directly that it wasn't discoverable, and separately that
      Ctrl+click DID open something but selecting an item had no effect --
      plausibly WKWebView's own native context menu rather than this app's: a
      genuine `contextmenu` DOM event (which `showRowContextMenu()`
      `preventDefault()`s to suppress the native one) isn't guaranteed to fire for
      every input method that traditionally means "right-click," and this project
      has hit a WKWebView-silently-drops-something-JS-never-sees bug in exactly
      this shape before (entry 49, `window.confirm()`). Rather than chase a third
      unverifiable WebKit theory in a row (see entry 75 immediately above), added
      a real, always-visible **⋯** button beside each Program's and Combi's own
      row name (`pane-program-editor.js`/`pane-combi-editor.js`, new shared
      `.row-name-cell`/`.row-menu-button` CSS) that opens the exact same
      `showRowContextMenu()` menu a click, not a right-click or a modifier,
      sidestepping the uncertainty entirely. Same visual pattern
      `.duplicate-group-count-cell`'s own "⋯" button (entry 68) already
      established for "label + more-actions button" in one cell. Right-click
      still works too (both rows' own `contextmenu` listener kept, now sharing
      the same `rowMenuItems` array the button uses, one list instead of two
      near-copies) for anyone whose right-click genuinely does route through the
      DOM. Guide docs (prog/combi) updated to lead with the button. Frontend-only,
      no backend change -- `jsc` parse-checked the touched files; the actual
      button-vs-native-menu behavior is, like the rest of this app's UI, not
      run in a real browser here.
  77. **REFACTORED (2026-09-04)**, same-day follow-up to entry 76: two direct pieces
      of feedback -- move the "⋯" button out of the Name cell into its own trailing
      column (after `#STL` for Combis, after `#CMB` for Programs -- whichever is
      each table's actual last column), and eliminate the duplication between the
      two tables' row-building before it compounds further as the project grows.
      - **New `pane.js` `menuCell(items)`** is now the ONE place the button+column
        exist -- builds a `<td class="row-menu-cell">` holding the "⋯"
        `showRowContextMenu()` trigger, called identically by
        `pane-program-editor.js` and `pane-combi-editor.js` (each just builds its
        own one-line `rowMenuItems` array locally and hands it to both
        `menuCell()` and its row's own `contextmenu` listener -- one array, two
        call sites, not two separately-typed-out item lists). Both tables' own
        `colgroupHtml()`/`<thead>` gained a matching narrow trailing column+`<th>`;
        both tables' own expand-row `colSpan` (`buildTimbreRow()`/
        `buildUsageRow()`) bumped by one to match -- missed on the first pass,
        caught rereading the row-loop end to end before calling this done.
      - **Removed, not left behind**: `pane-program-editor.js`'s `openRowMenu()`
        wrapper (now redundant -- its one line of logic, "build the Reset entry
        item list and call showRowContextMenu()", is exactly what the row loop's
        own `rowMenuItems` + the shared `menuCell()`/`contextmenu` listener already
        do) and the now-unused `.row-name-cell` CSS from entry 76 (replaced by
        `.row-menu-cell`, a plain centered cell -- text overflow/ellipsis handling
        the flex layout needed is gone too, since Name is plain text again).
      - **Verified**: `jsc` parse-checked all touched files, `pcg_file_test`
        rebuilt and passes unchanged (frontend-only, no backend touched). Guide
        docs (prog/combi) updated to say "the row's own last column" instead of
        "beside the name." Still not run in a real browser.
  78. **BUILT (2026-09-04)**, same-day follow-up, per direct request ("make sure
      PROG works exactly in the same way... code duplication is not an option any
      longer"): Programs gained the same before/after "move between rows" gesture
      Setlist and Combi already have (previously onto-only -- copy, or Shift+swap),
      full parity chosen over same-bank-only when asked directly. Also: the "⋯"
      menu's own overflow-off-the-right-edge bug, fixed.
      - **Backend, `PcgFile::moveProgramWithinBank()`/`moveProgramToBank()`** --
        Programs' own `moveCombiWithinBank()`/`moveCombiToBank()`, extended to
        repoint BOTH reference kinds a Program (unlike a Combi) can have: Set List
        slots AND Combi Timbres. `moveProgramToBank()` refuses across engine types
        (HD-1/EXi, same guard `swapPrograms()` already has -- a Program's raw bytes
        are engine-specific) and refills the vacated source with the shipped Init
        Program template, needing no live-donor search at all unlike Combi's own
        version (a real factory template already exists for Programs).
      - **New shared repoint helpers, not a third inlined copy**:
        `findCombiTimbreReferences()`/`repointOneCombiTimbre()`/
        `repointCombiTimbreReferences()` (`PcgFile.h`/`.cpp`) are the Combi-Timbre
        equivalent of the existing `findSetlistReferences()`/
        `repointOneSetlistSlot()`/`repointSetlistReferences()` trio, extracted from
        what used to be `resolveDuplicates()`'s own inlined loop (refactored to call
        the new helper -- behaviour-preserving, confirmed by `testResolveDuplicates()`
        still passing unchanged) and reused by both new move methods.
        `swapPrograms()` keeps its own hand-written BIDIRECTIONAL single pass instead
        (calling the one-directional helper twice would hit the exact same
        A→B-then-B→A collision `repointSetlistReferences()`'s own doc comment
        already documents for the Set List side).
      - **`EditorBridge::moveProgramWithinBank()`/`moveProgramToBank()`** +
        `main.cpp` bindings mirror `swapProgram()`'s own shape (the
        `isProgramRecordLocked()` open-editor guard included), `moveProgramToBank()`
        also reading `Init-Program-HD1.raw`/`-EXi.raw` the same way
        `resetProgram()`/`resolveDuplicateProgram()` already do.
      - **Frontend, `pane-program-editor.js`**: `makeRowDraggable()` now passes
        `zones: true` (was `false` -- Programs used to be the one onto-only table);
        `classify()`/`onDrop()` grew the before/after branch (same-dataset only,
        rejects a bank-type mismatch in every zone including before/after, matching
        the backend). New `onMoveProgram()` (`app.js`) picks
        `moveProgramWithinBank()` vs `moveProgramToBank()` by comparing
        source/target bank, threaded through the same
        app.js→createPane()→createLibraryPanels()→createProgramsPanel() callback
        chain `onDropProgram`/`onSwapProgram` already use (four call sites, all
        updated).
      - **New shared `finalIndexForInsert(zone, targetPosition, sourcePosition)`**
        (`drag-and-drop.js`) -- the before/after "final resting index" math
        (entries 73/75's own bugfix) had been independently written out in TWO
        places (Setlist's `onDropEntry` in `app.js`, Combi's `onDrop` in
        `pane-combi-editor.js`); both refactored to call this instead of adding a
        THIRD copy for Programs' own `onMoveProgram()`.
      - **"⋯" menu overflow, reported directly** ("opens the hover menu to the
        right too, so it goes out of the window"): `showRowContextMenu()`
        (`pane.js`) used to position with a bare `left: ev.clientX` (grows
        right/down, no viewport awareness) -- fine when the trigger was anywhere
        inland, broke once entry 77 moved the "⋯" button into the table's own
        rightmost column, right where the menu had the least room to grow into.
        Now positions AFTER appending to the DOM (so its real rendered size is
        known) and clamps both axes to stay inside the viewport with an 8px
        margin -- opens normally near the left/top edge, opens leftward/upward
        near the right/bottom edge.
      - **Verified**: `cmake --build` (`pcg_file_test` + `kronos_editor`, private
        submodule included) clean throughout. New `testProgramMove()`
        (`tests/pcg_file_test.cpp`) against a new dedicated `buildProgramMoveFixture()`
        (3 Program banks -- two HD-1 for same-type cross-bank, one EXi for the
        type-mismatch refusal -- one Set-List-referenced Program and one
        Combi-Timbre-referenced Program so the shift loop's OWN per-step repoint of
        both kinds gets exercised, not just the moving record's): happy-path
        within-bank shift (repoints both kinds on the SHIFTED record, not the
        dragged one, isolating the genuinely new code), the same no-op convention
        every other move/swap here uses, happy-path cross-bank move (repoint +
        template refill verified), and four refusal paths (destination-referenced,
        same-bank via the wrong method, engine-type mismatch, out-of-range).
        `pcg_file_test`: "All checks passed" -- and confirmed the harness itself
        isn't a silent no-op by deliberately breaking one assertion, rebuilding,
        watching it FAIL at the right line, then restoring and rebuilding clean
        again (a `mv`-restored file kept its pre-edit mtime, which ninja read as
        "not newer than the last build" and skipped recompiling on first retry --
        caught by re-checking the actual file content, not just trusting a green
        build log; fixed with `touch`). Frontend verified with a jsc harness
        driving the real `makeRowDraggable()`/classify()/onDrop() through all 8
        Program-table cases (same-bank edge/middle/Shift, cross-bank same-type,
        cross-bank different-type rejection, and all three cross-dataset variants)
        -- every one resolved to the expected call or the expected rejection.
        Still not run in an actual browser.
  79. **FIXED (2026-09-04)**, same-day follow-up to #78: "PROG move still does not
      work" + "COMBI shows the insert-position blue line, this is not existing in
      PROG" -- both symptoms, one root cause. `style.css`'s `.drop-before`/
      `.drop-after` rules (the before/after insert-line hover) were still scoped
      to only `.setlist-table`/`.combis-table`, a leftover from when Programs was
      the one onto-only table (pre-#78) -- entry 78 turned on the SAME zone/class
      logic for Programs (confirmed correct again this round with the same jsc
      harness from #78, unchanged, still resolving all 8 cases right) but never
      updated the CSS selectors to match, so `.programs-table tr.drop-before`/
      `.drop-after` painted nothing at all: right classes on the right elements,
      no rule anywhere to render them. The "move doesn't work" report is very
      likely the SAME bug wearing a different face -- with zero visual feedback
      for the before/after zones (no highlight at all, unlike the "onto" zone's
      still-working blue/green background), a user has no way to tell where that
      target even is and no confirmation a drop there did anything, so a drag
      that in fact worked reads as "nothing happened." Fixed by adding
      `.programs-table` to both selectors, matching `.drop-target`/`.drop-copy`
      just above them (which WERE already scoped to all three). Also directly
      answered: the before/after "move" gesture and the "on" zone's Shift-toggles-
      copy/swap are two independent, already-coexisting mechanisms, not
      alternatives to pick between -- Shift's role on the "on" (middle) zone is
      unchanged by any of this.
      - **Verified**: CSS brace-balanced, `jsc` parse-checked every touched file,
        the #78 jsc harness re-run unchanged (still all 8 cases correct -- this
        was never a logic bug). No C++ touched this round. Still not run in an
        actual browser, so the visual fix itself -- unlike the logic, which the
        harness can actually execute -- rests on reading the now-matching
        selector list, not a rendered screenshot.
  80. **FIXED (2026-09-04)**: "COPY shows repointing messages" -- reported directly
      once #78/#79's Combi+Program drag-and-drop was confirmed working. Found in
      exactly one place: `pane-combi-editor.js`'s drop handler appended
      `-- repointed N Set List slot(s).` UNCONDITIONALLY after its whole
      copy/swap/move-within-bank/move-to-bank if-chain, including for the copy
      branch -- where `setlistRefsRepointed` is always 0 by construction
      (`copyCombi()`'s own doc comment: a copy never touches anything else in the
      file), so every Combi copy toast read "Copied X -> Y -- repointed 0 Set
      List slot(s)," true but meaningless noise. Fixed by moving the suffix into
      each of the three genuinely-repointing branches' own `description` string
      instead of appending it after the fact -- the copy branch's own message
      (`Copied X -> Y.`) never mentions repointing at all now, matching
      `onDropProgram()`'s (`app.js`) plain-copy message, which never showed this
      in the first place (checked directly -- swept every `repointed` site in the
      frontend; the other five are `onSwapProgram()`/`onMoveProgram()`, both
      genuine swaps/moves that DO repoint, and the Duplicates panel's
      resolve/consolidate messages, a different operation that legitimately
      repoints by design -- none needed a change).
      - **Verified**: `jsc` parse-checked the touched file. No C++/backend change
        (frontend message text only). Still not run in a real browser.
  81. **CHANGED (2026-09-04)**, two pieces of direct feedback:
      - **"PROG copy does not show message 'copied' as COMBI does"**: `onDropProgram()`
        (`app.js`) used `setStatus()` (the persistent bottom status bar -- its OWN doc
        comment already calls this "easy to miss") for its success message, while
        Combi's own `onDrop()` always used `showToast()` (the transient, hard-to-miss
        popup) for every one of its outcomes. Switched Program's plain-copy message
        to `showToast()` to match.
      - **"Copying a PROG several times is it not possible (byte-identical) ...
        Remove restriction"**: `PcgFile::copyProgramFrom()`'s `DuplicateExists` guard
        -- reject a copy if a byte-identical Program already exists ANYWHERE in the
        destination file, even when the actual destination slot is a genuinely empty
        "Init Program" completely unrelated to wherever the duplicate lives --
        **removed entirely**, per direct request, after confirming it was a pure
        app-level opinion with no file-format constraint behind it (nothing about the
        format requires Program content to be unique; a real factory backup routinely
        has many byte-identical "Init Program" slots). `ProgramCopyError` lost the
        enum value; `EditorBridge`'s error-message `switch` lost the case.
        - **One real dependent found and fixed, not just deleted blind**: `swapPrograms()`
          (entry 48, 2026-08-15) was originally built SPECIFICALLY because this guard
          made copying meaningless between two Init Program slots -- its own doc
          comment, `EditorBridge::swapProgram()`'s, and the frontend's
          (`app.js`/`pane-program-editor.js`) all cited that as the reason it exists.
          Rewrote all four to the reason that's STILL true post-removal: `copyProgramFrom()`
          still refuses to overwrite a slot holding a genuinely DIFFERENT real Program
          (`TargetSlotOccupied`, unchanged), so swap remains the one non-destructive way
          to exchange two occupied, non-identical slots -- the feature itself wasn't
          touched, only why it still matters.
        - Also updated (comment-only, no behavior change): `applyCombiCrossDatasetCopy()`'s
          own "skip a redundant copy" comment, which cited DuplicateExists as WHY the
          skip was safe -- the skip itself never actually depended on that guard firing
          (it's driven by the earlier content-match resolution, not by
          `copyProgramFrom()`'s own rejection), so this was a stale justification, not a
          real dependency; reworded to the actual reason (avoid a redundant no-op write).
      - **Verified**: `cmake --build` (`pcg_file_test` + `kronos_editor`) clean.
        `testCopyProgramFrom()`'s own former "rejected: byte-identical duplicate exists
        elsewhere" case rewritten to assert the opposite -- a third byte-identical copy
        now succeeds and is readable back -- confirmed the harness still catches a real
        failure here too (deliberately broke the new assertion, rebuilt with `touch`
        this time to sidestep the mtime gotcha entry 78 hit, watched it FAIL at the
        right line, restored, rebuilt clean again). `pcg_file_test`: "All checks passed".
        Guide docs (`docs/content/guide/prog/index.md`) updated: the copy section now
        says duplicate content is fine, the swap section's own "why" rewritten to match
        the code comments. Frontend-only pieces `jsc` parse-checked; still not run in a
        real browser.
  82. **BUILT (2026-09-04)**, two pieces of direct feedback bringing Setlist up to the
      same rules Programs/Combis already follow -- both frontend-only, no backend
      change needed at all.
      - **"Copying a setlist item over a used entry should be blocked."** Setlist's
        onto-a-row drop (`app.js`'s `onDropEntry`) used to overwrite whatever was there
        unconditionally -- no equivalent of Combi's/Programs' own onto-occupied
        refusal existed for it. New `pane.js` `looksLikeEmptySetlistName(name)` --
        genuinely simpler than its Combi/Program siblings: a real, never-touched Set
        List slot's SDB1 name record is CONFIRMED blank on real hardware (docs/content/
        format/index.md §3.2: "Unpopulated song slots are empty strings"), not a Korg
        factory placeholder string the way "Init Program"/"Init Combi" are, so this only
        needs the blank check plus this app's own new "- Init Setlist -" marker (below)
        -- no third factory-name table to maintain. Enforced in TWO places, same
        defense-in-depth as Combi/Programs: `pane-setlist-editor.js`'s own `classify()`
        rejects the "on" zone outright (row never lights up as a target, cursor shows
        "not allowed") using the row's own already-known `entry.label` -- no extra
        bridge round-trip -- and `onDropEntry` re-checks the same thing from a new
        `target.label` field (threaded through the drop payload) before writing
        anything, with a clear toast if a stale render let a now-invalid drop through
        anyway. Same-day, also fixed while touching this exact function: the copy
        success message switched from `setStatus()` to `showToast()`, matching the
        visibility fix entry 81 already gave `onDropProgram()`.
      - **"We need the ⋯ button to reset a setlist too ('- Init Setlist -')."** New
        `resetEntry()` in `pane-setlist-editor.js`, reached via the SAME shared
        `menuCell()`/`showRowContextMenu()` column Programs/Combis already use (their
        table gained its own trailing "⋯" column + a matching colSpan bump on the
        editor row, same shape as entries 76/77). Genuinely simpler than Program's
        (a shipped Init Program template) or Combi's (a live-donor search for a real
        "Init Combi" elsewhere in the same bank): since a real unpopulated Set List
        slot's own bytes are CONFIRMED blank (see above), writing blank bytes for
        BOTH records already matches the real hardware-confirmed empty state exactly
        -- no template file needed, and no "raw byte file is missing" fallback to
        build, because there was never a file to be missing in the first place. The
        ONE thing that's this app's own invention, not Korg's, is the visible
        "- Init Setlist -" name patched in afterward (same "a deliberately-cleared
        slot should look different from an untouched one" reasoning "- Init Combi -"/
        "- Init Program (HD1) -" already exist for) -- written via the EXISTING,
        already-marker-preserving `setlist-editor-name.js` codec (`encodeSlotName()`,
        built 2026-08-16 for the General section's own Name field, reused as-is here,
        not duplicated) so the 4-byte SDB1 marker byte is never touched -- correctness-
        critical specifically for a Set List's FIRST slot, whose marker is documented
        as "the *only* way to find where one Set List's name ends and its 128 songs
        begin" (§3.2); blindly zeroing the whole 28-byte record would have corrupted
        that. Reuses the EXISTING `commitSlotBytes()`/`commitNameBytes()` write path
        every other Setlist edit (Color/Volume/Comment/Name) already goes through, not
        a second one -- both grew a `return true/false` (previously nothing) so
        `resetEntry()` can tell a write failed and show a toast, a small, backward-
        compatible addition (every existing caller already ignored the return value).
      - **Explicitly flagged, not fixed**: `PcgFile::sortSetlist()`'s own "empty slots
        sort to the end" check (`name.empty()`) doesn't know about the new
        "- Init Setlist -" marker, so a freshly-reset slot would sort by that literal
        string (starting with "-") instead of landing with the other blanks -- a real
        but minor inconsistency, left alone rather than reaching into backend C++ for
        a cosmetic sort-order nuance nobody asked about this round.
      - **Verified**: `jsc` parse-checked every touched file. A dedicated harness
        (stripping `export`/loading the real `setlist-editor-name.js` codec as a plain
        script) confirmed `looksLikeEmptySetlistName()`'s five cases (blank, undefined,
        a real name, the marker, and a case-insensitive match), `encodeSlotName()`
        preserving a real confirmed marker byte pattern while patching the name, and
        the `classify()` rejection logic across four scenarios (onto a used slot ->
        rejected, onto blank -> copy, onto a reset slot -> copy again, before/after a
        used slot -> still allowed to reorder). No backend touched, so no C++ rebuild
        needed this round. Guide docs (`docs/content/guide/setlist/index.md`) updated:
        the drag-and-drop section notes the new refusal and links to the new "Resetting
        a slot" section; also corrected a stale claim in the same paragraph ("a blue row
        means it will move the slot" -- blue has only ever meant Combi's/Programs' own
        swap gesture in this app, never a Setlist outcome, predating this session).
        Still not run in a real browser.

  83. **FIXED (2026-09-04)**, closing the gap entry 82 explicitly flagged the same
      round: `PcgFile::sortSetlist()`'s "empty slots sort to the end" check was a bare
      `name.empty()`, so a freshly-`resetEntry()`-ed slot -- whose name is the literal
      marker string `"- Init Setlist -"`, not blank -- sorted ALPHABETICALLY among real
      content (leading `-` sorts ahead of everything under ascending) instead of landing
      with the genuinely untouched slots at the end. New `PcgFile.cpp` helper
      `looksLikeEmptySetlistName()` (C++ mirror of `pane.js`'s own function of the same
      name, entry 82) -- blank string OR a case-insensitive "init setlist" match --
      used in `sortSetlist()`'s comparator in place of the old check.
      - **Verified**: new `pcg_file_test` case writes the marker directly into slot 2's
        name record (bypassing `resetEntry()`, which is frontend-only), sorts ascending,
        and checks the marker landed at the END alongside the other empty slots rather
        than ahead of `"Song One"`/`"Song Zero"` -- confirmed non-vacuous with the usual
        deliberate-break discipline (reverted the fix, rebuilt with `touch` to dodge the
        entry-78 mtime gotcha, watched 3 CHECK_EQs FAIL at the right lines, restored,
        rebuilt clean). `pcg_file_test`: "All checks passed". `kronos_editor` also
        rebuilt clean (no frontend change this round -- backend-only fix).
      - **Docs**: `docs/content/format/index.md` §3.2 (Set List block) gained a
        paragraph documenting the "- Init Setlist -" marker as this app's own invention
        (not Korg's) alongside the already-confirmed "unpopulated slots are blank"
        finding, and pointing `looksLikeEmptySetlistName()` readers at both the JS and
        C++ copies. While writing that paragraph, caught it citing `"- Init Combi -"`
        as documented "below" in the same file -- checked with a plain grep, found zero
        hits, so that marker has actually never been written up in `format/index.md`
        despite predating this session by weeks (entries 34/74) -- corrected the wording
        to point at `PcgFile.cpp`/STATE.md instead of a nonexistent section rather than
        adding the missing writeup itself, which is its own separate piece of work,
        not part of this round's ask. `docs/content/overview/index.md`'s "What's built
        so far" section (the page CLAUDE.md calls out by name to keep current after a
        refactor, separately from STATE.md) updated to mention the onto-occupied
        refusal, empty/reset-slots-sort-last, unrestricted Program re-copying, the
        shared Reset entry action, and Program/Combi drag-and-drop gesture parity --
        all shipped across entries 76-82 but never reflected there until now.

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

  84. **CHANGED (2026-09-05)**: moved the Combis tab between Setlist and
      Programs (`pane.js`, a plain `<li>` reorder, direct request, no logic
      depends on tab DOM order). Separately, reported directly that the
      gear-icon Settings button (entry from 2026-09-03's "Generic sliding
      sidebar component, MIDI Settings UI" work) was missing -- root cause
      wasn't a private-repo-linking problem as guessed, but that this whole
      feature had only ever landed on the `midi-sysex-templates-cli` branch,
      which had diverged from `main` by four commits (the Combi/Program
      drag-and-drop parity round, entries 80-83, plus the Windows build
      fix). **Merged that branch into `main`** per direct choice (over
      rebasing or leaving it split) -- one real conflict in `style.css`
      (`main`'s already-generalized `.row-context-menu` vs. that branch's
      older `.program-row-menu`/`openRowMenu()`; kept `main`'s version and
      fixed a stale `.cross-dataset-panel` comment reference the branch's
      own `.sidebar-panel*` rename had left behind). Verified: `cmake
      --build` (`kronos_editor` + `pcg_file_test`) clean post-merge, private
      module compiling in (this machine's own submodule checkout is
      populated), `pcg_file_test` all checks passed.
  85. **BUILT (2026-09-05)**: a Usage Guide window -- a new "i" button next
      to the Left/Both/Right pane-visibility toggle (reported directly),
      opening `frontend/help.html` in its own top-level native window (not
      a modal) via a new `openUsageGuideWindow` binding (`main.cpp`, using
      the existing `createEditorWindow()` multi-window machinery, singleton
      -- refocuses rather than stacking a second window). Content covers
      the dual-pane layout, table structure, drag-and-drop copy/move/swap
      rules per table, the Shift trick, Reset entry, and cross-pane
      (Shift+click) navigation -- meant to grow incrementally, not a one-
      time dump. New `frontend/markdown-lite.js`: a small hand-rolled
      Markdown-to-HTML renderer (headings, paragraphs, bold/italic/inline
      code, fenced code blocks, one-level lists with wrapped-line
      continuations, links, hr) covering exactly what the guide's own
      content (`frontend/usage-guide-content.js`, a plain JS template
      string, not a fetched `.md` asset) uses -- built rather than vendoring
      a real Markdown library, see that file's own doc comment for why and
      when to revisit. Verified: `cmake --build` clean, `pcg_file_test` all
      checks passed, every new/touched JS file `jsc` parse-checked, and the
      renderer functionally exercised end to end against the real guide
      content via a `jsc` script -- caught and fixed a real bug this way
      (wrapped list-item continuation lines were falling out of their list
      into a stray paragraph) before it ever shipped. NOT verified: actually
      clicking the button in a running window -- no display available in
      this environment, so the real click-through is still owed.
  86. **BUILT (2026-09-05)**: a public Release Notes page
      (`docs/content/release-notes/index.md`, weight 7 -- between the User
      Guide/format/building/components/midi cluster and the "me, myself and
      I and legal stuff" page at weight 1000, per direct request to place it
      "between app and legal"). A short, user-facing bullet-point summary
      per tagged version (Initial/0.1.0, 0.1.8, 0.1.9 so far), not a commit
      log -- grounded in the real `git log`/tag history (`git tag
      --sort=-creatordate`, then `git log v0.1.0..v0.1.8` and
      `v0.1.8..v0.1.9`) rather than invented, per this project's own "no
      guessing" standard applied to release history instead of file-format
      bytes. Update this page by hand alongside future version tags, the
      same "keep the docs in sync by hand" discipline as the format page
      and the Hugo Overview page.

  87. **FIXED (2026-09-05)**: reported directly that a push wasn't producing
      the expected new Hugo nav item -- the site had actually been failing
      to build/deploy since the `midi-sysex-templates-cli` merge (entry 84),
      three pushes in a row, entirely silently (nothing surfaces a failed
      GitHub Pages deploy back into this session on its own). Root cause,
      confirmed from the real Actions run log the user pasted directly (this
      environment has no `gh` CLI and the Actions API's job-log endpoint
      needs admin rights even on a public repo, so that paste was the only
      way to see it): `ERROR Error: icon 'broadcast.svg' is not found under
      'assets/icons' folder` and the same for `'history.svg'` -- `docs/
      content/midi/index.md`'s `icon: broadcast` (added by that merge) and
      this session's own new `icon: history` (entry 86) both named Tabler
      icons that are real (confirmed by fetching each from tabler-icons'
      own GitHub repo directly) but were never actually vendored into
      `assets/icons` in the shared `DIY-HUGO-SCAFFOLD.public` module this
      project's docs import (`docs/go.mod` pins an exact commit, not a
      floating branch) -- that module only ships a small hand-picked subset
      of icons per-name, confirmed directly against its own `assets/icons`
      listing (a sibling checkout on this machine, `../DIY-HUGO-
      SCAFFOLD.public`) rather than assumed. Fixed by swapping both pages to
      icons that already exist at the CURRENTLY PINNED scaffold commit
      (confirmed via `git ls-tree` at that exact commit, not just the
      scaffold's own latest) -- `music` for the MIDI page, `calendar-stats`
      for Release Notes -- rather than bumping the module pin itself: this
      environment has no working `go`/`hugo` binary (Homebrew's own API
      fetch and a direct golang.org tarball download both failed/timed out
      against this sandbox's network), so hand-editing `go.mod`'s pseudo-
      version and `go.sum`'s checksums without a toolchain to actually
      verify them would trade one build failure for a likely different one
      (a checksum mismatch) -- exactly the kind of unverified change this
      project's own methodology exists to avoid. Separately (real, but not
      this fix): added `broadcast.svg`/`history.svg` to the scaffold repo
      itself anyway (real Tabler icons, MIT, fetched verbatim same as its
      existing `book.svg`/`sitemap.svg`) since they're genuinely useful for
      a future page on any of the DIY-* sites that import it -- committed
      and pushed there, but its pin in THIS repo's `go.mod` is untouched, so
      it has no effect here yet.
      - **Open follow-up, not resolved this round**: `docs/go.mod` still
        pins the OLDER scaffold commit (`505cbe62405b`) -- bumping it to
        pick up the two new icons for real (if a future page actually wants
        them, rather than `music`/`calendar-stats`) needs a real `go`/`hugo`
        toolchain to regenerate `go.sum` correctly, which this environment
        doesn't have. Do this from a machine with working Go tooling:
        `hugo mod get -u github.com/jens-goes-mad/DIY-HUGO-SCAFFOLD.public`
        (or `go get` the same, from `docs/`), then commit the updated
        `go.mod`/`go.sum` together.
      - **Also unresolved**: nothing in this app/workflow surfaces a failed
        Pages deploy back to wherever pushes are made from -- this failure
        sat for three pushes before it was noticed by chance. Worth a real
        notification (e-mail from GitHub already fires for failed workflow
        runs by default, unless disabled in the user's own GitHub
        notification settings -- not this repo's to fix) rather than relying
        on someone checking the live site.

  88. **BUILT (2026-09-07)**: hardened the shippable (resource-embedding)
      build -- `EDITOR_RELEASE_HARDENED`, auto-on with `EDITOR_EMBED_RESOURCES`
      (so `CMAKE_BUILD_TYPE=Release` or an explicit `-DEDITOR_EMBED_RESOURCES=ON`).
      Three parts, all verified against a clean `-DCMAKE_BUILD_TYPE=Release`
      build on this machine:
      - **Embedded frontend is obfuscated**, not plain byte arrays. Before:
        `strings build/kronos_editor.app/Contents/MacOS/kronos_editor` dumped
        every line of JS/HTML/CSS and every source comment verbatim. Now each
        file is `zlib.compress(9)` then run through a dependency-free keystream
        cipher (`key[i]` arithmetic + keyed byte-rotate + xorshift64 XOR, then
        a position-dependent rotate) before it lands in the generated arrays.
        `tools/gen_asset_key.py` mints a fresh 32-byte key per build tree
        (a CMake custom command with no `DEPENDS` -- regenerates only when its
        outputs are missing, so reconfigures don't churn every asset) and
        writes it two ways: `generated/AssetKey.h` (scattered into four
        misleadingly-named 8-byte arrays + a `uint32` mask seed, reassembled
        at runtime by `src/kronos/AssetKey.cpp` -- never a contiguous 32-byte
        literal) and `generated/asset_key.bin` (raw, for the Python embed
        step). `tools/embed_resources.py` gained `--obfuscate --key <file>`;
        `src/kronos/AssetObfuscation.cpp` reverses the transform (cipher +
        `choc::zlib::InflaterStream`) at load time in `main.cpp`'s
        `loadFrontendResource()`. **Explicitly obfuscation, not encryption** --
        the key is compiled in (the app has no user to get one from), a
        determined RE can always recover it; the goal was "hours not seconds",
        and the honest caveat is written into `docs/content/building/index.md`.
        Verified: a throwaway harness deobfuscated all 38 public + 32 private
        embedded assets with the real per-build key, `/index.html` round-trips
        to `<!doctype html>`, `strings` on the release binary finds zero
        recognisable frontend tokens (only the small inline JS literals that
        live in `main.cpp` itself). New `ctest` target `asset_obfuscation_test`
        round-trips `deobfuscateAssetWithKey()` against a committed fixture
        (`tests/fixtures/asset_obfuscation.{key,plain,obf}`, regenerable via
        `tests/fixtures/gen_asset_obfuscation_fixture.py` which imports the
        SAME Python `_obfuscate()` the build uses -- so encoder/decoder drift
        fails the test).
      - **The private module's frontend is now actually embedded** (closes the
        gap entry in `main.cpp` / `src/main.cpp`'s old KNOWN GAP comment: a
        Release build opened the SGX-2 window then failed every resource
        request for it). `private/.../CMakeLists.txt` embeds its `frontend/`
        into its own `editor_embedded_sgx2` table with the SAME key;
        `private/.../src/Sgx2AssetRegistration.cpp` registers a resolver on the
        new `src/bridge/EmbeddedAssetRegistry.{h,cpp}` that `loadFrontendResource()`
        falls through to when its own table misses (path namespaces are
        disjoint: `/index.html`,`/pane.js`,`/components/*` vs `/sgx-2/*`,
        `/common/*`). `Sgx2AssetRegistration.cpp` is compiled unconditionally
        with a no-op `#else` branch so a Debug build (which reads that frontend
        off disk via `EDITOR_SGX2_FRONTEND_DIR`) still links.
      - **CHOC debug mode locked down** beyond entry 63's `enableDebugMode =
        false`: a `static_assert` in `main.cpp` makes a hardened build fail to
        compile if the flag could be true (verified by deliberately forcing it
        -- build fails with "hardened build must not enable WebView debug
        mode"); every window gets a `view.addInitScript` + inline-`evaluate`
        that swallows F12 / Ctrl+Shift+I / Cmd+Opt+I and the native context
        menu (the app's own `showRowContextMenu()` builds its own DOM dropdown
        and is unaffected); the Release binary is symbol-stripped
        (`strip -x` post-build on Apple, `-s`/`--gc-sections` on Linux,
        `/OPT:REF /OPT:ICF` on MSVC) + `-fvisibility=hidden` -- `nm` on the
        stripped binary shows ~211 symbols vs thousands. `private/.../src/Sgx2EditorStandaloneMain.cpp`
        (a dev-only target CI never ships) had its hardcoded
        `enableDebugMode = true` changed to gate on the `KRONOS_SGX2_DEBUG`
        env var it already uses elsewhere.
      - Not done, deliberately: **JS minification**. The brief asked for
        "minify + compress"; there is no JS toolchain in this repo (no `node`),
        and a hand-rolled minifier is exactly the fragile guess this project's
        methodology forbids. Compression already destroys readability of
        strings/comments in the binary, so that half is covered. True
        minification stays a future step if a JS build step is ever added
        (e.g. a vendored `esbuild`).
      - Not done, deliberately: **obfuscating the asset PATH strings** -- the
        generated table still has `{ "/sgx-2/sgx2-editor.js", ... }` readable
        via `strings`. Routes aren't secret and leaving them helps diagnose a
        broken build; only the content is obfuscated.

  89. **BUILT (2026-09-11)**: cross-dataset Program duplicate finder -- a
      global (not per-pane) tool that finds byte-exact duplicate Programs
      across ALL currently-open datasets, complementing the existing
      per-dataset Duplicates tab (`findDuplicatePrograms()`, entry 67 and
      earlier), which only ever looks inside one file. RFC discussed and
      settled first, recorded here, then built the same session per direct
      request ("record first, than start").
      - **Backend**: `PcgFile::findDuplicateProgramsAcrossFiles(files,
        bankFilter)` (`src/kronos/PcgFile.h`/`.cpp`) -- a STATIC method (it
        inherently spans several already-loaded files, none of which "owns"
        the operation the way a single `PcgFile` owns its own
        `findDuplicatePrograms()`). Unions every file's Programs, drops
        anything matching `looksLikeEmptyProgramName()` (confirmed: without
        this, every unused "Init Program" slot across N files collapses
        into one useless giant group), restricts to `bankFilter` if
        non-empty, groups by the existing FNV-1a `contentHash` (already a
        pure function of record bytes, directly comparable across files
        with no new hashing scheme), keeps only groups whose members span
        2+ DISTINCT files (a same-file-only match is already covered by
        that file's own `findDuplicatePrograms()`). Returns
        `CrossFileDuplicateGroup{contentHash, members:
        [CrossFileProgramMatch{fileIndex, bank, number, name, bankType}]}`
        -- `fileIndex` is just the position in the `files` vector the
        caller passed, meaningless outside that one call; the caller maps
        it back to a real identity. New `EditorBridge::
        findDuplicateProgramsAcrossDatasets([datasetIds], [bankFilter])`
        resolves each id to a live file (silently skipping one no longer
        open), calls the above, and marshals each member to
        `{datasetId, filename, bank, number, name, bankType}` --
        `contentHash` itself is deliberately never serialized to JS, same
        convention `programToValue()`/`combiToValue()` already use (it's
        this project's own bookkeeping, not a Kronos field, and the
        frontend never needs to compare hashes itself, only render groups
        this method already grouped). `filename` is a NEW small
        `basenameOf()` helper -- `Dataset::displayName` is the full path
        everywhere else in this app (`datasets.js`'s own comment: "not a
        truncated/basename-only"), but a per-row Datasource column's whole
        point is distinguishing WHICH open file a match lives in, where a
        full path is needless noise. Bound as
        `window.findDuplicateProgramsAcrossDatasets` in `main.cpp`.
      - **Verified**: `testFindDuplicateProgramsAcrossFiles()`
        (`tests/pcg_file_test.cpp`) -- loads `buildSyntheticPcgFile()`
        TWICE into two independent `PcgFile`s rather than a dedicated
        fixture (since both decode identical bytes, every non-empty
        Program in one is byte-identical to its counterpart in the other by
        construction). Confirms: a single-file call finds nothing even
        though that file has a REAL intra-file duplicate pair (the actual
        regression check for the "2+ distinct files" rule); a Program
        unique within each file but shared ACROSS files forms a correct
        2-member cross-file group; an intra-file duplicate pair becomes a
        4-member group across two files (2 copies x 2 files), not two
        separate groups; the bank filter includes/excludes correctly; a
        bank filter matching nothing yields no groups, not a crash; a null
        file entry (mirrors a stale datasetId) is silently skipped. Full
        `pcg_file_test`/`kronos_editor` (incl. the private submodule)
        rebuild clean, zero warnings, `ctest` green.
      - **Frontend**: new `frontend/cross-dataset-duplicates-panel.js`, a
        NEW topbar icon (⧉, index.html, beside the pane-visibility
        [left|both|right] buttons -- always visible, no private-module
        gate) toggling ONE `createSidebarPanel()` instance with two views
        instead of a separate results overlay stacked on top of it: RFC's
        own "in-window overlay, not a second native window" decision is
        already satisfied by the sidebar's own slide-in shell, so a SECOND
        overlay mechanism on top of that would just be more machinery for
        no real benefit. "filters" view: one checkbox per open dataset
        (persisted across sidebar re-opens, re-defaulted to "everything
        open" whenever every previous choice has closed out from under it)
        + a bank-filter row reusing `pane.js`'s existing `PROGRAM_BANK_NAMES`/
        `renderBankFilterRow()` (no new hardcoded bank list, no new "which
        20 banks" guess) with `present` = the UNION of every OPEN dataset's
        own `getProgramBankTypes()` banks, deliberately NOT restricted to
        just the checked datasets (per direct decision) so toggling a
        dataset checkbox never reshuffles which bank buttons are enabled +
        a "Find" button (disabled under 2 selected datasets, with an
        explanatory title, since no group could ever span 2+ files with
        fewer than that). "results" view: grouped table (group header +
        member rows, NOT a flat list -- a flat list would make it
        impossible to see which rows are actual twins), columns Name /
        Datasource (the bridge's own basename) / ID (`formatBankNumber()`,
        the same "INT-A 001" / "U-A 008 (EXi)" formatting the per-file
        Duplicates tab already uses), a "← New search" button back to
        filters. No `getBankType` coloring on the FILTER row's own buttons
        (unlike `pane.js`'s single-dataset one) -- a bank's engine type is
        a per-FILE fact that could disagree between files sharing that
        checkbox, so showing one file's answer next to a checkbox not
        scoped to that file would mislead rather than help.
      - **Navigation**: click a result row -> right pane; Shift+click ->
        left pane; sidebar closes either way. Resolved by DOM position
        (`.panes .pane` first/last element), NOT by paneId "A"/"B" -- same
        convention `app.js`'s `setPaneVisibility()`/`swapPanes()` already
        rely on. Reuses each target pane's own existing `loadDataset()` (if
        it isn't already showing that dataset) then `jumpToInstrument()` --
        no new navigation primitive needed. Referencing `panes`/
        `renderBankFilterRow`/`PROGRAM_BANK_NAMES`/`formatBankNumber` (all
        defined in `app.js`/`pane.js`) as bare globals from a file loaded
        earlier in `index.html` works the same way `pane-program-editor.js`
        already calls `pane.js`'s `renderBankFilterRow()` despite loading
        before it -- classic `<script>` tags share one lexical scope, and
        none of these are referenced until a later user action, by which
        point every script has already run its own top level.
      - **Caching**: the last search's result stays displayed across
        sidebar close/reopen (`cache`, keyed on the exact sorted dataset-id
        + bank-filter selection) so repeat open/close is instant, no bridge
        round-trip -- invalidated the COARSE way agreed in the RFC (b:
        "dropped on ANY write", not a new per-dataset version counter), but
        implemented WITHOUT touching every write call site across
        `app.js`/`pane-program-editor.js`/`pane-combi-editor.js` (which
        would have meant a much larger, more invasive change for one
        sidebar): each cache entry snapshots `listDatasets()`'s own `dirty`
        flag per involved dataset at search time, and a reopen re-fetches
        `listDatasets()` and treats the cache as stale the moment ANY of
        those flags has flipped, or a dataset closed. `isDatasetDirty()`
        latches (never resets false once true), so this can miss a SECOND
        write after the first already flipped it dirty within one session
        -- acceptable under the RFC's own "coarse" mandate, and verified in
        isolation (`osascript -l JavaScript`, 6 scenarios: usable right
        after search, stale after a tracked dataset goes dirty/closes,
        stale after the selection itself changes, not usable with no
        cache). New search always recomputes and overwrites the cache
        regardless of staleness.
      - **Scope, as agreed**: Programs only (Combi cross-dataset duplicates
        explicitly deferred -- "different story afterwards"); no
        resolution/write action of any kind from this view (a Program's
        bank/number pointer is meaningless translated into a different
        file's bank layout, the same reason cross-file Set List slot copy
        is already blocked) -- read-only jump-to is the whole feature;
        cross-file NAME collisions (as opposed to byte-exact) also out of
        scope.
      - **Not done, deliberately**: no `mock_bridge.js` fake for
        `findDuplicateProgramsAcrossDatasets()` -- checked first, and NONE
        of this app's existing Duplicates-family bridge calls
        (`findDuplicatePrograms`, `resolveDuplicateProgram`, etc.) have one
        either; `pane-program-editor.js`'s own `safeFetch()` already
        treats a missing binding as "nothing found" rather than an error,
        so plain-browser/mock mode just shows an empty result, consistent
        with how every sibling Duplicates feature already behaves there.
      - **Adjacent gap surfaced, not yet fixed**: opening the exact same
        file twice as two separate datasets is already blocked for the
        Open dialog / programmatic `openFile` path
        (`EditorBridge::openFileAtPath()` dedups by path, returns
        `alreadyOpen:true`) -- but its own comment says drag-and-drop
        "never had one to compare." Worth closing at some point, since this
        feature's per-row Datasource column implicitly assumes one dataset
        == one real file; not blocking, since the existing dedup already
        covers the common case (the Open dialog/`openFile`).

  90. **FIXED (2026-09-11)**: a real, reported crash (`EXC_BAD_ACCESS` inside
      `choc::ui::WebView::evaluateJavascript`, hit while testing entry 89
      above) -- PRE-EXISTING, already present in the last commit
      (`a3b640e`), unrelated to entry 89's own new code: closing ANY
      secondary window (Usage Guide, or an optional private module's own
      window) left its `EditorBridge::addDatasetsChangedListener()`
      registration in place forever, capturing that window's `WebView*` by
      raw pointer (`main.cpp`'s `createEditorWindow()`). The next dataset
      change ANYWHERE (opening another file, closing a dataset) called
      `notifyDatasetsChanged()`, which invoked every registered listener
      including the now-dangling one -- `viewPtr->evaluateJavascript(...)`
      on a destroyed WebView, straight into `SIGSEGV`.
      - Fixed by giving each listener an identity: `addDatasetsChangedListener`
        now takes a `const void* key` (the caller's own `WebView*`, which is
        already unique for exactly the listener's intended lifetime) alongside
        the callback, and a new `removeDatasetsChangedListener(key)` erases by
        that key. `m_datasetsChangedListeners` is now
        `vector<pair<const void*, DatasetsChangedListener>>`, not a bare
        `vector<DatasetsChangedListener>`. `notifyDatasetsChanged()` iterates
        a COPY of the vector, not the live one, so a listener that itself
        closes a window (and thus calls `removeDatasetsChangedListener()`
        reentrantly) can't invalidate the iteration.
      - `main.cpp`'s `createEditorWindow()`: `windowClosed` now calls
        `bridge.removeDatasetsChangedListener(viewPtr)` before its existing
        `openWindows` cleanup -- the only call site of
        `addDatasetsChangedListener()` in the whole tree (checked, including
        the private submodule), so nothing else needed updating for the new
        2-arg signature.
      - Verified: full `pcg_file_test`/`kronos_editor` (incl. private
        submodule) rebuild clean, zero new warnings, `ctest` green. Not
        re-reproduced interactively (would need driving real window open/
        close through the native app) -- the fix is a direct, mechanical
        match for the crash's own stack trace (dangling `viewPtr` inside
        `evaluateJavascript`, called from `notifyDatasetsChanged()`), not a
        guess; worth the user re-confirming by hand (open a secondary
        window, close it, then open another file) now that it's fixed.

  91. **BUILT (2026-09-19)**: "Compare two files" -- a new mode in the
      cross-dataset tools sidebar (entry 89's `⧉` icon/`cross-dataset-
      duplicates-panel.js`), the inverse question from entry 89's own
      duplicate finder: pick exactly 2 open datasets (not N) + a bank
      filter, and find every (bank, number) slot BOTH files actually have a
      Program OR Combi in whose content DIFFERS between them ("same slot,
      different content", vs. entry 89's "same content, different
      location"). Built for comparing two snapshots of what's meant to be
      the same rig to see what's drifted apart. Direct request; two
      decisions made explicitly before starting (see the RFC exchange this
      session): Setlist comparison deferred (Programs/Combis both already
      have a `contentHash` to reuse directly; Setlist slots have none, and
      building one wasn't going to happen without checking it against real
      bytes first, this project's own rule), and the new mode reuses the
      EXISTING sidebar (a mode toggle at the top) rather than a third
      near-identical shell.
      - **Backend**: `PcgFile::findDivergentProgramsAcrossFiles(fileA,
        fileB, bankFilter)` / `findDivergentCombisAcrossFiles(fileA,
        fileB)` (static, exactly 2 files -- "diverged from each other" has
        no N-way generalization the way "duplicate somewhere" does).
        Indexes fileB's Programs/Combis by (bank, number) for an O(1)
        lookup per fileA entry, reports a `ProgramDivergence{bank, number,
        nameA, nameB, bankType}` / `CombiDivergence{bank, number, nameA,
        nameB}` wherever both files have an entry at that exact slot AND
        their `contentHash` differs. Deliberately NOT filtered by
        `looksLikeEmptyProgramName()` the way entry 89's duplicate finder
        is -- an Init-Program slot in one file that's been filled with a
        real sound in the other IS a genuine divergence signal here, not
        noise to hide (the "one giant useless group" failure mode that
        filter exists for doesn't apply to a position-matched pairwise
        comparison). A slot only ONE file has at all is out of scope --
        not a "divergence" in this tool's sense. New
        `EditorBridge::findDivergentProgramsAcrossDatasets`/
        `findDivergentCombisAcrossDatasets` resolve dataset ids to files
        (either missing -> empty array, not an error) and marshal results;
        bound in `main.cpp`.
      - **Verified**: `testFindDivergentAcrossFiles()` (`tests/
        pcg_file_test.cpp`) -- two identical loads of the existing
        synthetic fixture start with zero divergences anywhere (baseline);
        one Program slot and one Combi slot are then mutated in fileB via
        REAL writes (`putProgramRecordBytes()`/`putCombiRecordBytes()`,
        not a hand-built fixture) to create controlled divergences, and the
        test confirms exactly those two slots are reported, every other
        slot (including a byte-exact intra-file duplicate pair) is NOT,
        the bank filter correctly includes/excludes, and a file never
        diverges from itself. Caught and fixed a real bug in the TEST
        ITSELF while writing it: the first mutation attempt only
        overwrote the new name's own 6 bytes, leaving stale tail bytes
        from the original longer name past that point, corrupting the
        decoded name -- fixed by clearing the whole 24-byte name field
        first (`pushNameRecord()`'s own documented convention), same
        discipline this project applies to production code. Full
        `pcg_file_test`/`kronos_editor` (incl. private submodule) rebuild
        clean, zero warnings, `ctest` green.
      - **Frontend**: `cross-dataset-duplicates-panel.js` gained a mode
        toggle ("Find duplicates" / "Compare two files") at the top of the
        sidebar body, reusing `refreshOpenDatasets()`'s existing
        open-dataset list and bank-union fetch for both modes. "Compare"
        filters view: two `<select>` dropdowns (not N checkboxes -- exactly
        2 datasets, order matters here since A opens in the LEFT pane and
        B in the RIGHT) + the same bank-filter-button row (Programs only,
        Combis have no bank concept) + a "Compare" button (disabled with an
        explanatory title if A and B are the same dataset). Results view:
        two stacked sections (Programs, Combis), each a 3-column table (ID
        / that dataset's own basename as the column header for each side)
        -- reuses the shared `formatBankNumber()`/`renderBankFilterRow()`
        helpers, no new table-rendering code. New `basenameOfPath()` (JS
        side, mirrors the bridge's own `basenameOf()` reasoning from
        entry 89) for the column headers, since `displayName` is a full
        path everywhere else in this app.
      - **Navigation, deliberately different from entry 89's own
        click/shift+click**: a single click on a divergence row jumps BOTH
        panes at once -- dataset A's slot into the LEFT pane, dataset B's
        into the RIGHT, both via each pane's existing `loadDataset()`/
        `jumpToInstrument()` -- since the whole point of this tool is a
        live side-by-side look at what changed, and this app already has
        two panes built for exactly that.
      - **Caching**: same coarse, per-mode dirty-flag-snapshot pattern as
        entry 89 (`compareCache`, keyed on the exact A/B/bankFilter
        selection, invalidated the moment either dataset's own `dirty` flag
        flips or either closes). A real bug was caught and fixed HERE too,
        before it shipped: the A/B "re-default when a pick closes" logic
        picked A's fallback first without checking against whatever B had
        ALREADY survived as, which could silently re-collide the two onto
        the same dataset (e.g. A closes while B=7 survives, and the first
        remaining open dataset also happens to be 7) -- caught by testing
        the exact scenario in isolation (`osascript -l JavaScript`, the
        same no-`node`-here workaround used throughout this project),
        fixed by picking each side's fallback EXCLUDING whatever the other
        side currently holds. 6 default-picking scenarios + 6 cache-
        invalidation scenarios verified this way, all passing after the
        fix.
      - **Not done, deliberately**: Setlist slot comparison (needs a new
        Setlist-record content hash, or a field-level compare, neither
        built/confirmed yet -- explicit follow-up, not forgotten); no
        `mock_bridge.js` fake for either new bridge call, consistent with
        entry 89's own finding that NONE of this app's Duplicates-family
        bridge calls have one (plain-browser mode just shows an empty
        result via the existing "missing binding -> nothing found"
        convention); no resolve/reconcile action from either divergence
        table -- read-only jump-to-both-panes is the whole feature, same
        "no well-defined cross-file write target" reasoning entry 89's own
        duplicate finder already documents.

  92. **BUILT (2026-09-20)**: readable Combi divergence descriptions --
      direct RFC ("Currently it is impossible to resolve conflicts because
      reason is unknown"): entry 91's Combi divergence rows previously only
      said THAT two slots differ, never WHY. Real capability audit done
      first, before any design: `ProgramFields`/`ProgramInfo` in this repo
      decode NOTHING of a Program's internals beyond name + EXi Algorithm
      Type -- there is no offset reference for Program internals in this
      repo at all (`Prog_HD-1.txt`/`Prog_EXi_Common.txt` exist only in the
      private submodule's own `docs/external/KORG/`). Combis are the
      opposite: `docs/external/KORG/CombiAndSongTimbreSet.txt` (8626 lines,
      already in this repo, previously unused by `CombiDecoder.h` beyond
      name + the 16 Timbre-to-Program refs) is a full SysEx-offset map of
      the ENTIRE 7810-byte Combi record -- Master Volume, all 16 Timbres'
      own Volume/Pan/Sends/Bus routing/EQ, Insert Effect1-12, Total/Master
      Effect wrappers, KARMA modules, etc. Scope for this pass, per direct
      decision: Combis only, two tiers -- a short list of NAMED fields with
      real before/after VALUES, and coarser NAMED-ONLY categories (which
      raw byte range differs, not what specifically inside it changed) for
      everything else already positionally understood; full per-field
      Program decoding stays a private-submodule concern, not attempted
      here.
      - **The "+4 byte shift" rule, reused rather than re-derived**: this
        project already established (`ProgramFields::exiAlgorithmType`'s
        own doc comment) that Korg's own SysEx-offset docs are consistently
        4 bytes behind this project's real file-record offsets (a leading
        4-byte marker before every field block). Applied here too, and
        cross-checked TWICE independently before trusting it for a third
        table: `CombiDecoder.cpp`'s existing `kTimbreBaseOffset=4806` is
        exactly `CombiAndSongTimbreSet.txt`'s own "Timbre1 > Program
        Number" SysEx offset (4802) + 4; `TimbreStatus`'s own `+2` is
        exactly that same file's "Timbre1 > Status" SysEx offset (4804) +
        4. Two independent hits on the same shift, from an ENTIRELY
        different confirmation path (real Combi samples) than the one that
        first found it (Program templates) -- strong enough to trust for
        the new offsets this entry adds (Master Volume, IFX/MFX/TFX/EQ
        ranges), not a fresh guess.
      - **New**: `CombiDecoder.h`/`.cpp` gained
        `decodeCombiMasterVolume()` (file offset 1192, `0~127`, one clean
        new confirmed field) and `describeCombiDivergence(a, b, recordA,
        recordB) -> vector<string>` --
        - **Named, with values**: each Timbre's Program reference/on-off
          status (zero new byte work -- `CombiInfo::timbres` was already
          fully decoded) and Master Volume.
        - **Named-only, no value**: `IFX1`.."IFX12"` (each Insert Effect
          slot's own wrapper -- Effect Type/Channel/Switch/Panpot/Bus
          routing/Sends; confirmed stride exactly 74 bytes/slot from
          `(976-88)/12`), `MFX` (Master Effect 1+2 combined, deliberately
          NOT split by 1/2 per direct decision -- the two slots are treated
          as one functional pair here), `TFX` (Total Effect 1+2 combined,
          same reasoning, EXCLUDING the Master Volume byte so it isn't
          double-reported both as a named value AND a vague "TFX differs"),
          `EQ` (each Timbre's own "(Track EQ)" Trim/Bypass/Frequency/Gain
          bytes, pooled across all 16 Timbres into one category).
        - **Catch-all**: "Other section differs" fires only if the two
          records' `contentHash` actually differs but nothing above caught
          it -- nothing is silently dropped, matching the direct request
          to "detect changes in raw data blocks we already have detected"
          without parsing every parameter.
        - **Deliberately NOT built this pass, a real conflict flagged
          rather than silently resolved either way**: a `MIDI` category.
          Korg's own doc lists a per-Timbre "MIDI Channel" at the EXACT
          SAME byte `TimbreStatus` already reads (bits 4~0 of the status
          byte) -- but this project's OWN prior, independently-confirmed
          finding (`TimbreStatus`'s own doc comment in `PcgFile.h`) is that
          those bits are a Timbre's 0-based index, watched counting 0..15
          across a real Combi's 16 Timbres, not a MIDI channel. Two sources
          genuinely disagree; picking one silently would violate this
          project's core method. Needs a real Combi that breaks the
          "counts 0..15" pattern, or an independent second reference, to
          settle either way.
      - **`PcgFile::CombiDivergence`** gained a `changes: vector<string>`
        field, computed inside `findDivergentCombisAcrossFiles()` (which
        already fetches both slots' raw bytes via `combiRecordBytes()` for
        exactly this). `ProgramDivergence` gets no equivalent field --
        there's nothing to compute it from yet.
        `EditorBridge::findDivergentCombisAcrossDatasets()` marshals
        `changes` as a plain JS string array.
      - **Verified**: `testDescribeCombiDivergence()` (new,
        `tests/pcg_file_test.cpp`) -- 9 sub-cases, each reloading a FRESH
        identical file pair and poking exactly ONE byte in `fileB`'s Combi
        record for clean attribution: Master Volume (with real values),
        IFX1's first byte, IFX2's LAST byte (confirms the 74-byte stride,
        not just IFX1's own start), the byte immediately AFTER IFX12 (a
        boundary check -- lands in MFX, doesn't spill into IFX12), TFX
        start (and confirms Master Volume, untouched, does NOT also fire),
        Timbre1's own EQ byte, Timbre5's own EQ byte (confirms EQ pools
        across ALL 16 Timbres, not just the first), two categories poked
        together in one call (both reported, independently), and a real
        Timbre reference change via `writeTimbreProgramRef()` (the actual
        write path `resolveDuplicates()` itself uses, not a raw poke).
        Also strengthened the existing name-only-mutation divergence test
        to assert it now returns exactly `["Other section differs"]`
        -- extended, not just left passing by coincidence. Full
        `pcg_file_test`/`kronos_editor` (incl. private submodule) rebuild
        clean, zero warnings, `ctest` green.
      - **Frontend**: `cross-dataset-duplicates-panel.js`'s Combi
        divergence table gains a sub-row directly under each entry showing
        `d.changes.join(" · ")`, always visible (not collapsed behind a
        click) and NOT truncated the way the table's other cells are --
        this text is the whole point of the tool. Program rows have no
        `changes` field yet, so no sub-row renders for those (silently
        omitted, not a blank row).

  93. **REFINED (2026-09-20), same day as entry 92**: the owner added two
      real Kronos backups specifically to test entry 92 against real data
      (`K1_20260418.PCG`/`K2_20260401.PCG`, `KRONOS-SOUNDS/`, not in this
      repo) and reported "U-A 097 shows 'Other section differs' -- try to
      examine which sections differ, maybe we have to be a bit more fine
      grained." Investigated with a throwaway `clang++`-compiled smoke test
      against `PcgFile.cpp` (this project's own standard verification
      method) rather than guessed at.
      - **Finding**: comparing the two real files found 29 Combi
        divergences total. EVERY ONE of the 20 that showed "Other section
        differs" (including U-A 097 "Pianos") turned out to be the EXACT
        SAME single byte -- Timbre1's own "Volume" field (file offset
        4811, Korg SysEx offset 4807 -- the same `+4` shift rule entry 92
        already established), just with different before/after VALUES per
        Combi (confirmed genuinely per-Combi, not a suspicious shared
        constant, by histogramming that byte across all 1792 Combi records
        in both files: a real spread with 127/max as the common default,
        exactly what a real Volume field looks like -- not, say, a
        mis-attributed bit that happens to be identical everywhere).
      - **Fix**: `CombiDecoder.h`/`.cpp` gained
        `decodeCombiTimbreVolume(record, recordSize, timbreIndex)` (relative
        offset 5 from that Timbre's own base) as a THIRD named-with-values
        field alongside Timbre reference/status and Master Volume, plus a
        new pooled coarse category, **"Mixer"** -- every OTHER per-Timbre
        byte between its own 3-byte reference/status block and its EQ bytes
        (Pan, Send1/2, Bend Range, Transpose, Detune, Delay settings, Drum
        Kit Patch IFX1-12, Bus/Rec-Bus/Chord/Max-Notes bits, (Filter)
        transmit toggles), pooled across all 16 Timbres the same way EQ
        already is, EXCLUDING the Volume byte so a Volume-only change isn't
        ALSO reported as a vaguer "Mixer differs".
      - **Re-ran the same real-file probe after the fix**: all 29
        divergences now resolve to specific, named lines -- ZERO "Other
        section differs" remain. Every "Other" case became one or more
        "Timbre N Volume X -> Y" lines (U-A 097 "Pianos": "Timbre 1 Volume
        111 -> 103", confirmed exactly matching the raw byte diff). Two
        Combis previously invisible (`bank=7 number=27` "Runaway",
        `number=41` "I want it All") now show "Mixer differs" -- spot-
        checked "Runaway"'s own raw bytes directly: a single bit (top bit
        of Korg offset 4834, a packed "(Filter) Program Change/After
        Touch/Damper/Portamento" toggle byte) flipped identically across
        8 consecutive Timbres, a real, coherent global toggle change, not
        noise -- correctly attributed to "Mixer" even without decoding
        that specific bit's own name yet, exactly the tiered "coarse now,
        precise later if needed" approach entry 92 established.
      - **Verified**: `testDescribeCombiDivergence()` gained 3 more
        sub-cases (Timbre Volume with real values at the exact real-world
        offset; Timbre Pan falling into "Mixer" not "EQ"/Volume; a second
        Timbre's own Mixer byte, confirming the pool spans all 16) -- all
        passing, plus the full existing suite unaffected. Full
        `pcg_file_test`/`kronos_editor` (incl. private submodule) rebuild
        clean, zero warnings, `ctest` green.
      - **Not done, deliberately, still open**: individual names for any
        of the fields now folded into "Mixer" (Pan/Send/Bus/etc.) -- this
        pass only needed ONE more named field (Volume) to clear every real
        divergence found in the two test backups; further per-field
        decoding waits for an actual need, matching this project's own
        "don't build for hypothetical needs" norm.

  94. **BUILT (2026-09-20)**: resolving a Combi divergence for real, per
      direct RFC spec -- entries 92/93 could only ever DESCRIBE what
      changed; this makes it actually FIXABLE. "Double click still jumps
      both panes like before; a single click expands the row inline (same
      interaction/coloring as this app's Setlist row editors) into one row
      per `changes` entry, each with a "←"/"→" button -- ← copies the
      RIGHT column's raw bytes into the LEFT, → the reverse. 'Left'/'right'
      always mean this sidebar's own A/B columns, never whichever Norton
      pane currently shows which side (those can be independently
      swapped)." A resolved change is deleted from the list; if the
      written-to dataset is visible in a Norton pane, that pane refreshes.
      - **Backend, the real enabling change**: `describeCombiDivergence()`
        (CombiDecoder.h/.cpp) now returns `vector<CombiChange>` --
        `{description, ranges: [{offset, length}]}` -- instead of bare
        strings. Every entry's `ranges` is EXACTLY the bytes its own
        detection logic already scans (no new tracking needed): a named
        field's own 1-2 bytes; a coarse category's WHOLE declared section
        (e.g. resolving "EQ differs" copies all 16 Timbres' own EQ bytes,
        not just whichever one(s) actually differ -- these categories only
        ever detected "differs somewhere", never narrowed further, so
        that's the only honest resolve granularity available); the
        catch-all "Other section differs" gets the ENTIRE record (the only
        accurate answer when nothing more specific was ever identified).
        `PcgFile::CombiDivergence::changes` (the JS-facing field) is
        unchanged (`vector<string>`) -- populated by pulling `.description`
        out of the richer result, so display-only consumers need zero
        changes; a NEW `EditorBridge::resolveCombiDivergenceChange()`
        recomputes `describeCombiDivergence()` FRESH (always in the same
        A,B order the frontend's list was fetched in -- descriptions are
        directional text, e.g. "111 -> 103", so recomputing in the wrong
        order would silently mismatch every string) to look the requested
        change back up by its exact description text, then copies its
        ranges from source into a mutable copy of the target record and
        writes it via the already-public `putCombiRecordBytes()`. No new
        PcgFile method needed -- everything this required was already
        public.
      - **Real bug found and fixed while restructuring this**: the
        Timbre reference/status check used to be `if (refChanged) {...}
        else if (statusChanged) {...}` -- a Timbre whose reference AND
        status BOTH changed in the same comparison silently reported only
        the reference, making the status change invisible and (now that
        resolving is real) unresolvable. Changed to two independent `if`s,
        each with its own distinct byte range, so both surface and either
        can be resolved without touching the other.
      - **Verified**: existing `describeCombiDivergence()` call sites in
        `pcg_file_test.cpp` updated to read `.description`; every relevant
        sub-case ALSO gained a direct assertion on its own `.ranges` (exact
        offset/length for named fields; correct pooled-range COUNT for the
        coarse categories, confirming Volume is genuinely excluded from
        Mixer's own range, not just from its boolean check) -- a wrong
        range would silently corrupt data on resolve, so these are load-
        bearing, not decorative. New case: reference AND status changed
        together, confirming both now report independently with distinct,
        non-overlapping ranges (the bug fix above). Then verified the
        WHOLE resolve mechanism against the two real backups
        (`K1_20260418.PCG`/`K2_20260401.PCG`) with a throwaway `clang++`
        smoke test, entirely in-memory: found U-A 097's real change,
        applied its range A-to-B, confirmed the target now matches the
        source EXACTLY within that range and NOTHING else changed, and
        that a fresh `describeCombiDivergence()` call afterward reports
        ZERO remaining changes for that slot. A second smoke test drove
        the REAL write path (`putCombiRecordBytes()`, not a manual byte
        poke) end-to-end: confirmed the file's own `isDirty()` flips
        false->true, a fresh re-decode from the file's own retained data
        (not the local mutated copy) shows the resolved state, and --
        confirmed directly via `md5` before/after -- the real `.PCG` file
        on disk is completely untouched (this project's write model keeps
        every edit in memory until an explicit Save, exactly as intended;
        this was checked, not assumed, before running write tests against
        real backups). Full `pcg_file_test`/`kronos_editor` (incl. private
        submodule) rebuild clean, zero warnings, `ctest` green throughout.
      - **Frontend**: `cross-dataset-duplicates-panel.js`'s Combi
        divergence rows gained click/dblclick disambiguation (a short
        delay on the single-click handler, cancelled if a `dblclick`
        follows -- the standard way two gestures share one element without
        the browser's own click-click-then-dblclick sequence firing the
        single-click action twice first) and a chevron (▸/▾) hinting the
        expand state; expanding renders `buildCombiChangeRows()` --one row
        per change, description + "←"/"→" (tooltips name the actual
        dataset file, not bare "A"/"B", so it's clear which file is about
        to be overwritten). `resolveCombiChange()` calls the new bridge
        method, refreshes any Norton pane currently showing the
        WRITTEN-TO dataset (`pane.refreshLibrary()`, the same "did a write
        land somewhere visible" pattern `app.js`'s own `onDropProgram`
        already uses), then re-runs the SAME comparison rather than
        hand-editing the cached list -- the resolved change (and the whole
        Combi row, once nothing about it diverges any more) disappears
        naturally from the fresh result, never at risk of drifting out of
        sync with what the datasets actually contain. Program rows are
        UNCHANGED (single click still jumps both panes) -- they have no
        `changes` to expand.
      - **CSS**: new `.cross-dataset-dup-change-row`/`-actions`/
        `-resolve-button` classes use `--accent` (the Setlist accordion's
        own blue), not `--editor-accent` (this app's orange "is-link
        pressed" convention used elsewhere in this same file), per direct
        request to match the Setlist editors' own look.
      - **Not done, deliberately**: no `mock_bridge.js` fake for the new
        bridge call, consistent with every other Duplicates-family
        function in this app (STATE.md entry 89's own finding, still
        holding); no confirm dialog before a resolve -- applies
        immediately, matching every other write in this app (Resolve
        Duplicates, Reset entry, drag-and-drop, ...); no busy/disabled
        state on the ←/→ buttons while a resolve is in flight -- checked
        first that no existing resolve button elsewhere in this app has
        one either, so this stays consistent rather than introducing a new
        convention for just one feature.

  95. **UI POLISH (2026-09-20)**: entry 94's resolve rows, refined per
      direct request -- pure CSS/JS, no backend changes.
      - Sidebar 50px wider (`min(470px, 90vw)`, was 420px) -- scoped to
        `#crossDatasetDuplicatesPanelRoot .sidebar-panel` specifically, NOT
        the shared `.sidebar-panel` shell also used by MIDI Settings and
        the Duplicates resolve-picker (those stay at the base 420px).
      - The main divergence row gets an `.is-open` class while expanded --
        orange title text + a left orange border on its first cell + (via
        the expanded change rows themselves) a gray/brown row background,
        composed from the SAME tokens the Setlist row-editor system
        already defines (`--editor-accent`, `--panel`) per direct request
        to reuse that CSS, rather than a byte-for-byte copy of any single
        existing rule (the exact split asked for -- orange TEXT on a
        gray/brown background -- differs from the Setlist's own
        `.table tr.is-selected`, which is solid-orange-background with
        white text). Caught a real contrast bug while composing it:
        hovering an already-open row would have shown orange text (from
        `.is-open`) on the hover rule's own orange background -- same
        specificity, so source order alone decided it, and `.is-open`
        being later would have won, reading as illegible orange-on-orange.
        Fixed with an explicit `.is-open:hover` restoring the hover rule's
        own dark text.
      - Each change row: `colspan="2"` merges the ID + NameA columns into
        one cell for the description text (per direct request), leaving
        only the actions cell separate.
      - The actions cell (and its two buttons) now stretch to the row's
        own full height (`height: 100%` + `align-items: stretch`) rather
        than sitting at their own natural content height -- matters once a
        longer description wraps to two lines and the row grows taller
        than the buttons' own intrinsic size would otherwise be.

  96. **FIXED + BUILT (2026-09-20)**: a real bug reported directly ("the
      hover color in the sidebar is orange, in setlist its a dark gray and
      opened collapsible has a orange title instead, while its unchanged
      in the sidebar"), plus mouse-drag sidebar resize.
      - **Root cause of the hover bug**: `.cross-dataset-dup-table` never
        got Bulma's own `is-hoverable` class (which the Setlist/Programs/
        Combis tables all use, a subtle dark overlay) -- instead it had a
        hand-rolled `:hover`/`:focus-visible` rule using a SOLID
        `--editor-accent` (orange) background, predating entry 95's own
        "reuse the Setlist's orange-title-when-open" work. That old orange
        hover was ALSO the reason an opened row's own new orange title
        (entry 95) never looked like it changed in practice: right after
        clicking a row the mouse is still sitting on it, so it's ALWAYS
        also `:hover` at the exact moment you'd look -- orange text on an
        orange background is illegible, which is exactly why entry 95 had
        to add a defensive `.is-open:hover { color: #1b1d22 }` override in
        the first place (masking the title back to dark whenever hovered,
        i.e. almost always). Fixed at the root: added `is-hoverable` to
        both `cross-dataset-duplicates-panel.js` tables (Find duplicates
        AND Compare two files -- they share the same row class, so both
        get the fix), removed the old hand-rolled hover rule entirely, and
        removed the now-unnecessary `.is-open:hover` masking override --
        Bulma's own subtle gray hover no longer clashes with an open row's
        orange title, so both are visible together as intended.
      - **Mouse-drag sidebar resize**, direct request ("is it possible to
        resize the sidebar by mouse dragging?") -- answer: yes, and built
        into the SHARED `sidebar-panel.js` component rather than one-off
        for this panel, since MIDI Settings and the Duplicates
        resolve-picker sidebar use the exact same shell and benefit
        identically. A thin drag handle sits on the panel's INNER edge
        (opposite whichever side `edge` docks it to -- left-edge handle
        for a right-docked panel and vice versa, so this works for either
        orientation `createSidebarPanel()` supports), clamped to
        [280px, min(900px, 90vw)]. Sets `panelEl.style.width` directly, an
        inline style that overrides ANY class-based width (including a
        caller's own scoped default, e.g. entry 95's
        `#crossDatasetDuplicatesPanelRoot .sidebar-panel` 470px override)
        regardless of specificity, so no per-sidebar-caller changes were
        needed at all. Session-only, per direct scope -- resets to each
        panel's own CSS default width on next app launch, not persisted;
        not asked for, not built preemptively.
      - **Verified**: the resize math (which direction widens vs. narrows
        for each `edge`, and both clamp bounds) checked in isolation via
        the `osascript -l JavaScript` workaround (no node in this
        environment) -- 7 scenarios, all passing, before trusting it in
        the browser. Both touched files re-syntax-checked. No C++ touched
        this pass; full `ctest` suite re-confirmed green regardless.

  97. **DOCS (2026-09-20)**: two doc updates, per direct request.
      - **New Hugo page, `docs/content/experimental/`** -- a deliberately
        brief, detail-free heads-up (per direct instruction: "avoid
        details... screenshots later") that the private companion module's
        Insert Effect decoding + a much bigger parameter-editing UI (knobs/
        toggles/generic tables) are in progress, not yet stable, and not
        documented byte-level anywhere on the site yet -- explicitly NOT
        added to the Overview page's own "what's confirmed" section, since
        none of it is confirmed yet. `menu.main.weight: 8` (after Release
        Notes, before the catch-all "me" page at 1000); `icon:
        circuit-diode` -- checked directly against the scaffold module's
        OWN pinned commit (`git show <the exact hash docs/go.mod pins>:
        assets/icons`, not just its current HEAD, which already differs --
        the same "icon not found" trap entry 88's own history hit twice
        with `broadcast`/`history`) before using it, confirmed actually
        present there rather than guessed.
      - **In-app Usage Guide** (`frontend/usage-guide-content.js`) gained a
        new "Cross-dataset tools (⧉)" section describing entries 89-96:
        where the icon lives, a one-line mention of "Find duplicates" for
        context, and a fuller description of "Compare two files" -- the
        A/B-vs-Norton-pane distinction, click-to-expand-and-resolve for
        Combis with real values where known, double-click to jump both
        panes. Rendered through the actual `renderMarkdownToHtml()` (the
        app's own hand-rolled Markdown subset, not full CommonMark) to
        confirm the bold/italic/list markup used actually produces the
        intended HTML rather than assuming the syntax is supported.

  98. **DOCS (2026-09-21)**: entry 97 only added the cross-dataset tools
      (entries 89-96) to the IN-APP Usage Guide, per that entry's own
      literal instruction ("update the help text in the application
      itself"). Reported directly after entry 97's Hugo deploy went live
      ("where is the new Diverged section?") -- the PUBLIC Hugo User
      Guide (`docs/content/guide/`) had no mention of it at all, unlike
      the existing detailed per-dataset Duplicates section
      (`docs/content/guide/prog/index.md#duplicates`), a real, reasonable
      gap now closed:
      - New page `docs/content/guide/cross-dataset/index.md` -- "Find
        duplicates" (brief) and "Compare two files" at the same depth as
        the existing per-dataset Duplicates section: the A/B-vs-Norton-
        pane distinction stated as plainly as entry 97's own Usage Guide
        wording, double-click to jump both panes, and a full breakdown of
        the click-to-expand-and-resolve Combi UI (named-with-values vs.
        named-only vs. the catch-all, the ←/→ semantics, immediate
        no-confirm writes, auto-refresh). No screenshots (none exist yet
        for this feature) -- deliberately no `![...]` image reference
        added instead of a placeholder, since a missing image file would
        break the Hugo build.
      - `docs/content/guide/_index.md` gained a "Key features" bullet, a
        new "### Cross-dataset tools (⧉)" subsection alongside the other
        topbar-level controls (pane visibility, swap), and a "Where to go
        next" link -- the same three touch-points every other guide
        sub-page already gets.
      - Not committed/pushed yet as of this entry -- the question that
        prompted this was asked separately from a commit request.

  99. **"FIND DIFFERENCES" -- third cross-file mode, Programs only
      (2026-09-25, from the RFC logged under IDEAS below).** Built per the
      approved plan: a content-keyed, slot-independent set difference
      between two open files, for two backups of "mostly the same rig"
      (the project owner's two Kronos: ~90% shared patches, different
      slots, ~10% edited). Complements "Find duplicates" (only what
      matches) and "Compare two files" (same slot only).
      - `hashProgramRecordIgnoringName()` (ProgramDecoder) -- FNV-1a
        skipping the 24-byte name field (bytes 4..27), so a rename-only
        change is detectable ("same sound, different name" -- the owner's
        addition to the RFC). Bytes 0-3 stay in the hash: checked on real
        files (Narf K1X vs K2) that they don't vary between otherwise-
        identical Programs at different slots (638 cross-file groups, 0
        with differing bytes 0-3).
      - `PcgFile::findProgramDifferencesAcrossFiles()` -- classifies each
        non-empty Program (looksLikeEmptyProgramName skipped) against the
        other file, strongest match first: identical at the same slot (not
        listed) / Moved (identical, different slot) / Renamed (same
        content ignoring name) / ModifiedTwin (same name, different
        content) / OnlyInA / OnlyInB. Presence-based; same slot wins, else
        lowest (bank, number); each pair reported once. bankFilter limits
        what is LISTED, the whole other file is always searched.
      - Bridge `findProgramDifferencesAcrossDatasets` -> string `kind` +
        per-side coordinates (-1 / "" on a side with no partner); bound in
        main.cpp.
      - UI: third mode in `cross-dataset-duplicates-panel.js`, SHARING
        compare's A/B picks and bank filter (`buildCompareFiltersView` now
        takes a `forDifferences` flag rather than duplicating it; the cache
        check is generalized with an optional cache argument). Sections:
        Only in A/B, Renamed, Modified twins, Moved (collapsed by default,
        `diffShowMoved`). Row click jumps each pane to ITS OWN slot
        (`jumpToDifference`), since the sides differ, unlike
        `jumpToDivergence`.
      - Verified: `pcg_file_test` "All checks passed" including the new
        `testFindProgramDifferencesAcrossFiles` (masked-hash unit case;
        baseline/self-compare = 0 rows; Renamed reported once + bank
        filter; ModifiedTwin; Moved reported once from one side; OnlyInB
        with untouched empty slots ignored). The synthetic fixture's
        Program bodies are all zero (records differ only by name), which
        made every pair look "renamed" until per-slot body bytes were
        stamped -- a fixture artifact, not a logic bug. Real-file smoke
        runs: the owner's own K1_20260418.PCG vs K2_20260401.PCG report 0
        differences (their 2,560 Programs are identical slot for slot --
        a clean no-false-positive check); Narf K1X vs K2: 12 Renamed, 1555
        ModifiedTwin, 256 OnlyInB, 0 Moved.
      - **Finding worth knowing**: in the Narf pair, same-named factory
        Programs (e.g. "KRONOS German Grand") differ in ~86 of 4960 bytes,
        mostly rotating bit-patterns (`40 20 10 08 04 02 81`) in unused
        regions plus header byte 3 -- so identical-looking Programs saved
        from different instrument states are often NOT byte-identical, and
        "Modified twin" can mean a trivially small (even garbage-byte)
        difference. A "how many bytes differ" hint per twin would help
        triage; not built (no field-level decode of Programs exists in
        this repo). The UI build and this panel's layout (three toggle
        buttons in the sidebar) were NOT run in the real app in this
        session -- the JS was syntax-checked only; needs a real UI pass.
      - Docs synced: in-app guide, `docs/content/guide/cross-dataset/`,
        `docs/content/guide/_index.md`. Not committed as of this entry.

  100. **CROSS DATASET ANALYSIS REVISED (2026-09-26, per direct report after
      trying entry 99 on real files).** Reported: the sidebar got stuck when
      a third dataset was opened, its dropdowns didn't refresh on open/
      unload, "Find duplicates" found nothing for K1_20260418.PCG + INIT.PCG,
      and "Find differences" showed nothing.
      - **Root cause of the "nothing found" reports (verified on the real
        files, not guessed)**: cross-file matching used `contentHash` -- a
        hash of the WHOLE record -- but a Program record carries bytes that
        depend on WHERE it sits / which instrument saved it: (a) bytes 0-3,
        the record header (slot 0 of each bank holds bank-level metadata
        there -- 0000 0000, 0000 0001, 0000 0002... counting up per bank --
        other slots hold stale leftovers), and (b) bytes 2692-2693, "Drum
        Track > Program Number/Bank" (Prog_HD-1.txt / Prog_EXi_Common.txt
        offsets 2688/2689 + the +4 shift): a REFERENCE to another Program
        slot. Same-named Programs in K1 vs INIT differed in exactly those
        bytes and nothing else in 1,244 of 1,711 pairs. So slot-shifted
        identical sounds never matched, and entry 99's own finding ("only
        the header byte 3 varies") was wrong -- its check only compared
        same-bank groups. Corrected here.
      - Fix: `hashProgramRecordForComparison(record, size, ignoreName)`
        (ProgramDecoder) skips those bytes (and the name on request);
        `findDuplicateProgramsAcrossFiles()` and `findProgramDifferencesAcrossFiles()`
        use it (computed on demand; `ProgramInfo::contentHash` and all
        in-file features are unchanged). Replaces the short-lived
        `hashProgramRecordIgnoringName`. Real-file result, K1 vs INIT:
        duplicates 0 -> 1,201 groups; differences now 466 only-in-A, 255
        only-in-B, 4 renamed, 458 modified twins, 1,257 moved. K1 vs K2
        still 0 (no false positives). `pcg_file_test` passes incl. new hash
        unit cases (header/Drum Track ref/name/body).
      - UI (`cross-dataset-duplicates-panel.js`, restructured): title "Cross
        Dataset analysis"; ONE screen -- mode toggle, two dataset dropdowns
        (A/B, with a "(select a dataset)" placeholder), a Find button
        (disabled unless two DIFFERENT datasets are picked), result inline
        below (no results view / "new search" button); bank filter removed
        from every mode. Duplicates now takes the A/B pair (the N-dataset
        checkbox list and its bank buttons are gone). The dropdowns
        subscribe to datasets.js's `onDatasetsChanged()` so they follow
        files opening/closing live, results are dropped when a picked
        dataset closes/changes/goes dirty, and `run()` clears its busy flag
        in a `finally` so a bridge error can't leave "Searching..." stuck.
        "Compare two files" kept working on the shared layout, otherwise
        untouched (its own redesign is still to be discussed).
      - NOT verified in the running app: the panel was syntax-checked and
        the app rebuilt, but not clicked through. The "stuck when a third
        dataset is opened" and the empty "Find differences" result could not
        be reproduced without the UI -- the native side returned rows for
        the same files, so a stale-state cause in the old panel (dropdowns
        only refreshed at sidebar open; A/B defaults; the busy flag) is the
        working theory, addressed by the rewrite but unconfirmed.
      - Docs synced (in-app guide, cross-dataset guide page). Not committed.

  101. **CROSS DATASET ANALYSIS: "Compare COMBI" mode, mode buttons renamed
      (2026-09-26, per direct request).** Toggle is now `Duplicates |
      Differences | Compare PROG | Compare COMBI`; the old combined "Compare
      two files" is split (results keys `comparePrograms` / `compareCombis`,
      each running only its own bridge call). Triggered by a real test: K1_
      20260418.PCG vs K2_20260401.PCG Combis slot by slot -- 1,792 slots in
      both, 1,763 identical, 29 diverged (all bank 7 / USER-A..G), mostly a
      few Timbre volumes; #094 gained a Timbre 3; #100/#101 are different
      songs in the two files ("Tainted Love"/"Your Song", "Just a Girl"/"Blue
      on Black") with every IFX/MFX/TFX/EQ/Mixer block differing.
      - Combi rows gain a **Changes** column: a high-level summary built
        from `describeCombiDivergence()`'s strings -- Volume (master +
        per-Timbre, counted), Timbres, IFX with which slots, MFX, TFX, EQ,
        Mixer, Other. `summarizeCombiChanges()` (pure JS in the panel;
        checked in JavaScriptCore against cases taken from the real output,
        e.g. #014 -> "Volume x2, IFX 3, EQ").
      - **Different-song rule**: name differs AND >= 4 changes ("more than
        3") -> the row reads "Different song", the section header counts
        them, and the expanded view shows one note ("N changes, not listed")
        instead of every difference (no per-change resolve buttons for such
        a row). `DIFFERENT_SONG_MIN_CHANGES` in the panel. Detection is in
        the frontend only (display rule; the bridge still returns every
        change).
      - Category names kept as before (no new native decoding): the user's
        "IFX1 1..16" is IFX1-12 in this format. Not run in the real app;
        docs synced. Not committed.

  102. **CROSS DATASET ANALYSIS FIXES (2026-09-26, per direct report after
      trying entry 101).** Reported: "Differences" threw `... is not a
      function`; on Compare COMBI the opened row was not orange, the change
      rows did not stretch to the table width, "Changes" should just be a
      count / "Different song", all sidebar tables should share one CSS, and
      the sidebar should be 100px wider.
      - **"Differences" error = a stale binary, not a code bug.** The app
        was being run from `build/kronos_editor` (dated Sep 21); only
        `build-release/` had been rebuilt after entries 99-101, so the new
        bridge binding `findProgramDifferencesAcrossDatasets` did not exist
        in the running binary. `build/` rebuilt (binding confirmed present
        via `strings`). This very likely also explains the earlier "Find
        differences shows nothing" / stuck-sidebar reports in entry 100 --
        the call threw inside `run()` -- so those were probably never the
        stale-state theory written there. Lesson: rebuild BOTH `build/` and
        `build-release/` (or state which one is run) after any bridge change.
      - Orange when opened: `.cross-dataset-dup-row.is-open` now also
        colours its `<td>`s (Bulma's `.table td` colour beat the colour
        inherited from the row) and gives them the `--panel` background.
      - Rows not stretching: `.cross-dataset-dup-change-actions` was
        `display: flex` ON a `<td>`, which removes it from the table's column
        layout (its colspan is ignored). It is a normal table cell again,
        with the flex row in an inner `div`. The description now spans 3
        columns and the ←/→ buttons sit under "Changes" (4th column).
      - "Changes" column = the number of changes, or "Different song"
        (`changesLabel()`); the category summarizer added in entry 101
        (`summarizeCombiChanges()`) is removed. The breakdown lines still
        name Volume / Timbres / IFX1-12 / MFX / TFX / EQ / Mixer.
      - One table implementation for the whole sidebar: `buildResultTable()`
        (header via textContent -- file names were going through innerHTML),
        `buildResultRow()`, `onActivate()`, used by Duplicates, Differences
        and both Compare tables (shared class list `table is-fullwidth
        is-hoverable is-narrow cross-dataset-dup-table`).
      - Sidebar width 470 -> 570px (`#crossDatasetDuplicatesPanelRoot`).
      - Syntax-checked only; NOT clicked through in the running app. Docs
        synced. Not committed.

  103. **CROSS DATASET ANALYSIS: results-only scrolling + collapsible
      sections (2026-09-26, per direct request; "Differences works perfect"
      after the rebuild in entry 102).**
      - Only the result area scrolls: in `#crossDatasetDuplicatesPanelRoot`
        the panel body is a non-scrolling flex column (mode toggle,
        dropdowns, Find fixed) and the results live in a new
        `.cross-dataset-results` wrapper (flex 1, `overflow-y: auto`).
        Scoped to this sidebar; the shared `.sidebar-panel-body` (MIDI
        Settings, Duplicates resolve-picker) is unchanged.
      - Every result heading is collapsible (`buildCollapsibleHeading()`,
        click or Enter/Space, "▾"/"▸" prefix, `aria-expanded`): the summary
        lines ("1183 difference(s) + 1257 moved", "N duplicate group(s)",
        "N Program/Combi divergence(s)" -- collapsing one hides everything
        under it) and each Differences section ("Only in <file> (466)",
        Renamed, Modified twins, Moved). State lives in `sectionOverrides`
        (key -> collapsed), so it survives the re-renders; default is
        expanded except "Moved" (as before, now via the same mechanism,
        replacing the old Show/Hide button and `diffShowMoved`). Empty
        sections get a plain, non-interactive heading. The Compare tables'
        own redundant "Programs"/"Combis" heading was dropped (the summary
        heading covers it).
      - Syntax-checked, both build dirs rebuilt; NOT clicked through in the
        running app. Docs synced. Not committed.

  104. **HUGO DOCS SYNCED WITH THE CROSS DATASET ANALYSIS FEATURES
      (2026-09-26, per direct request).** `docs/content/guide/cross-dataset/
      index.md` rewritten from the piecemeal edits of entries 99-103 (it
      still said "three tools"/"both tools", "press Show", and called Moved
      "byte-identical"): now one coherent page -- the single-screen sidebar
      (toggle, A/B dropdowns, Find, inline collapsible results, results-only
      scrolling), Duplicates, Differences (only in A/B, renamed, modified
      twins, moved), Compare PROG / Compare COMBI (Changes count, "Different
      song" rule, expanded breakdown, resolve buttons). `guide/_index.md`
      (two bullets) matches. `docs/content/format/index.md` gains section
      5.7 (slot-dependent bytes inside a Program record: header bytes 0-3 and
      the Drum Track Program Number/Bank at 2692-2693, with the evidence and
      the not-yet-explained remaining differences) -- the format reference
      had none of the entry-100 findings. Verified with a one-shot Docker
      build (`hugomods/hugo:exts-non-root`, `hugo --minify --destination
      <tmp>`, same image as docs/docker-compose.yml): builds clean (21
      pages; the two WARNs are theme-level), the cross-dataset page renders
      its four sections, all internal links resolve, and the format-section
      deep link (`/format#57-slot-dependent-bytes-inside-a-program-record--
      confirmed-2026-09-26`) matches the id Hugo generated. Not committed.

  105. **CROSS DATASET ANALYSIS FROZEN + THREE LAST TWEAKS (2026-09-26,
      per direct request).** (1) "Differences": every section now starts
      collapsed (the summary heading stays open). (2) The "Only in <file>"
      sections show ONE column (that file's slot + name) via a `side`
      option on `buildDifferenceSection()`. (3) Compare COMBI hides slots
      whose name contains "Init Combi" (case-insensitive) on BOTH sides by
      default -- `isInitCombiRow()`; a "Hide "Init Combi" slots (N)" checkbox
      (`hideInitCombis`) brings them back, the section title counts only
      visible rows. Logic checked in JavaScriptCore (both Init -> hidden;
      Init vs. real name / two real names -> kept); UI not clicked through.
      Then "frozen": recorded in the STATE block status paragraph and in a
      new README section ("Cross Dataset analysis (frozen 2026-09-26)").
      Docs (Hugo page, in-app guide) updated for the tweaks.

  106. **TABULATOR IN THE CROSS DATASET SIDEBAR (2026-09-26, per direct
      request: "solely in the sidebar tables, all other panes supporting
      drag and drop are not touched").** All four modes' tables (Duplicates,
      the Differences sections, Compare PROG, Compare COMBI) are now Tabulator
      6.5.3 tables (MIT; vendored as `frontend/vendor/tabulator/
      tabulator.min.{js,css}` + LICENSE, note in `vendor/TABULATOR_VERSION.txt`;
      `<link>`/`<script>` in index.html; picked up by the release embed's
      `GLOB_RECURSE frontend/*` automatically). Nothing else in the app uses it.
      - New: click a header to sort (ID columns sort by bank/number, "Changes"
        by count with "Different song" last), a filter box under every header.
      - Behavior contract of the frozen sidebar kept: Duplicates keeps its
        "N copies" group headers (`groupBy`, not collapsible) and click /
        Shift+click jumps; Differences keeps one column for "Only in ..." and
        per-side jumps; Compare PROG click jumps both panes; Compare COMBI
        keeps click = expand the change list inside the row (row formatter),
        double-click = jump, ←/→ resolve buttons, the "Different song" note,
        the orange open row, the Init Combi checkbox. Rows stay keyboard
        activatable (Enter/Space). No fixed table height: every row renders
        and the RESULT AREA scrolls, as before (2,500 rows fine).
      - Plumbing: `createTable()` (one place), tables destroyed before every
        body rebuild (`destroyTables()`), per-table sort + filter state kept in
        `tableStates` and restored on rebuild, cleared when that mode's rows are
        replaced by a new Find (kept across a Combi resolve). `newTableHost()`
        wraps the host because Tabulator turns its host element INTO `.tabulator`.
        Old hand-built table helpers and their CSS removed.
      - **Verified in a real browser** (the first time this sidebar's UI was
        actually exercised): `frontend/cross-dataset-panel.test.html`, a harness
        with the bridge stubbed by fixtures and a scripted run of the REAL panel
        code, run in headless Chrome (command in its header) -- ~35 checks,
        all pass: title/dropdowns/Find enablement, filter + sort + filter
        restored after a rebuild, group headers, collapsed-by-default sections,
        one- vs two-column tables, jumps (right pane / both panes / own slots),
        Init Combi hide toggle, expand/collapse with detail stretching to the
        table width, resolve call, dblclick, 2,500-row render + filter. Screenshots
        reviewed. Bugs the harness/screenshots caught: `.tabulator-table` white
        background (rows were white), Tabulator pinning its body height at
        build time (expanding a row clipped the list by 109px -- fixed with
        `table.redraw()` after each toggle; the harness check fails without it,
        confirmed), and a wrong test expectation. NOT verified: the real app's
        WKWebView (macOS CHOC) -- only Chrome; and the harness's virtual-time
        timings are meaningless (no real-time performance number yet).
      - Docs: README, components page (new section incl. how to run the
        harness), guide page, in-app guide. Not committed.

  107. **CROSS DATASET SIDEBAR POLISH: DIFFERENCES DROPDOWN + FILL-HEIGHT
      TABLES (2026-09-26, per direct request: "UI needs polishing").**
      - "Differences": the five section headings ("Only in <file>", Renamed,
        Modified twins, Moved) are gone -- one dropdown above the table
        ("<title> (<count>)") picks the category; the pane below it is exactly
        ONE table. Starts on the first category that has rows (`diffCategory`,
        remembered across redraws); each category keeps its own sort/filter
        state. "Only in" categories are still one column.
      - Scrolling: the result pane is now a non-scrolling flex column (summary,
        folded hint, dropdown) and the table takes ALL remaining height
        (`.cross-dataset-table-fill`; Tabulator `height: "100%"` on an
        absolutely-sized host), so ITS ROWS scroll while its header -- titles,
        sort arrows, filter boxes -- stays fixed. Applied to every mode's
        table, not only Differences, for consistency (one table per mode).
        Tabulator's virtual rendering now applies (2,500 rows -> a few dozen
        DOM rows). Compare COMBI's expandable rows work inside it (`table.
        redraw()` after each toggle; the harness checks the expanded row is tall
        enough for its list and shrinks back).
      - The explanatory hints are folded into a "How to read this" `<details>`
        (closed by default) so the table gets the room. The other headings are
        just the collapsible summary line now (section headings only existed
        for Differences).
      - Harness (`frontend/cross-dataset-panel.test.html`) updated: 52 checks
        pass in headless Chrome, incl. dropdown categories/counts/columns,
        no section headings, pane not scrolling, table reaching the pane's
        bottom, rows scrolling while the header stays fixed, virtual rendering.
        Screenshots reviewed. Still NOT checked in the real app's web view.
        Docs synced (README, guide page, in-app guide). Not committed.

  108. **CROSS DATASET SIDEBAR: OUTER COLLAPSIBLE REMOVED (2026-09-26, per
      direct request: "PERFECT ... we do not need the outer collapsible any
      longer").** With one table filling the pane there is nothing to fold, so
      the result's summary line is plain text in every mode ("1201 Duplicate
      Group(s)", "N Difference(s) + M Moved", "N Program Divergence(s)", "N
      Combi Divergence(s) (K Different Songs)"): `addHeading()` replaces
      `buildCollapsibleHeading()`; the helper, its `sectionOverrides` state and
      the collapsible-heading CSS are deleted (entry 103's collapsible headings
      are superseded). Harness updated (52 checks pass in headless Chrome: the
      title is not interactive, and the filter-restore check now rebuilds by
      leaving/re-entering the mode). Docs synced. Not committed.

  109. **COMMAND-LINE FILES OPEN AS DATASETS AT STARTUP (2026-09-26, per
      direct request: "starting the app and selecting datasets is
      tedious").** `./build/kronos_editor a.PCG b.PCG ...` opens 1..n files.
      - `src/main.cpp`: `main(argc, argv)` collects the arguments (skipping
        anything starting with "-", e.g. macOS `-psn_...`), makes each
        absolute + `weakly_canonical` (so `./../x.PCG` and the same file named
        twice resolve to one path -> `openFileAtPath()`'s existing dedupe makes
        one dataset), and the main window gets a new bound function
        `getStartupFiles()` (in `webviewIsReady`, next to the bridge binds).
      - `frontend/app.js`: at load, if `window.getStartupFiles` exists (real app
        only; the plain-browser mock has none), each path goes through the
        existing `window.openFile(path)` and then the new shared
        `showOpenedDataset(result)` -- the same "first empty pane takes it
        (A before B), otherwise only the selectors know" logic the Open button
        uses, now factored out of that handler. A failing path shows an error
        toast and the rest still open; the topbar shows "Loading <file>...".
        Nothing on the native side opens files itself, so no threading/timing
        concerns -- the frontend pulls once it is ready.
      - Verified: built (both dirs), launched `build/kronos_editor INIT.PCG
        K1_20260418.PCG INIT.PCG` from the repo root with RELATIVE paths and
        screenshotted the real window: INIT.PCG open in the left pane, the
        second file loading (36MB, takes a few seconds), the status bar
        reporting the loaded dataset's duplicate counts. Only the first file
        was seen fully loaded in that capture (my launched instance was closed
        before a second capture showed the right pane); the second-file-in-the-
        right-pane and the "same file twice opens once" behavior rest on the
        existing `showOpenedDataset`/dedupe logic, not on a screenshot. The
        macOS `.app` bundle path (`open ... --args`) is untried. Docs: README +
        building page. Not committed.
  110. **BUILT (2026-09-28)**: a small sample (waveform) icon marks every
      Program that plays a user sample bank. First attempt put the icon on
      EVERY Program -- wrong (the owner's catch: not every Program is
      sample-based, let alone KSC-based), reverted. Rebuilt on real,
      verified format data (format doc §5.8): HD-1 Oscillator Mode (file
      2562; raw 0 Single, 1 Double, 2 Drums, 5 Double Drums -- 2/5 read off
      real kit Programs), OSC1/OSC2 zones at file 2778/3244 (8 x 22 bytes,
      identical shape: MS Type, 16-byte MS Bank UUID, little-endian MS
      Number), and Korg's own UUID classes from KRONOS_MIDI_SysEx.txt
      (invalid / legacy "KORG...MS" ROM, Old RAM, EXs1-126 / generated =
      EXs127+ or user bank). Rule (`hd1UsesGeneratedSampleBank()`): HD-1
      bank, OSC1 (+OSC2 only in Double -- Single Programs keep stale OSC2
      data), MS Type = Multisample, generated UUID. Verified: every
      generated UUID in "Narf Ultimate Covers K2.PCG" but one (a Wave
      Sequence zone, skipped) is exactly `SGC SAMPLES.KSC`'s own UUID; 11
      MS Numbers resolve by name in that KSC's manifest; 22 Programs
      flagged there, 14 in setlist_test_2.PCG, no EXi Program flagged;
      `testMultisampleBankReferences()` patches the real
      Init-Program-HD1.raw per rule. `ProgramInfo::usesGeneratedSampleBank`
      -> `programToValue()`; references look it up on the referenced
      Program itself, never re-derived: Set List slots via
      `PcgFile::findProgram()` in `EditorBridge::getEntries()` (request
      time, so never stale), Combi Timbres via the frontend's existing
      `findProgram()`. Icon in the Programs table, Duplicates tab, Set List
      bank button and Combi Timbre refs (`pane.js`'s `sampleIcon()`/
      `setLabelWithSampleIcon()`). Corrects the KSC entry in OPEN: IDEAS
      ("4 Programs" -> 22 in the Narf file). Open: the Cross Dataset tables
      (each result struct needs the flag, per side for Divergences/
      Differences -- sidebar is frozen, so only on request); Drums-mode
      Programs (Drum Kit sample UUIDs not decoded); EXi MOD-7 `[PCM]`
      multisamples not decoded. Placement changed 2026-09-30 per direct
      request: the icon sits right AFTER the engine type ("HD-1 <icon>" --
      the Programs table's Type cell; after "(HD-1)" in the other labels,
      before "(off)"/usage counts). Extended 2026-09-30 per direct request:
      two icon colors -- orange = user samples (generated UUID, now also
      the Sampling-mode "Smp: Old RAM"), light gray = Korg EXs1-126;
      `hd1UsesGeneratedSampleBank()` -> `hd1PlayedSampleSources()` ({user,
      exs}, one pass), `ProgramInfo::usesGeneratedSampleBank` ->
      `usesUserSamples`/`usesExsSamples`, frontend `programSampleKind()`.
      Real files: K1 44 user (14 generated + 30 Old RAM) / 121 EXs, INIT 0 /
      166, Narf 22 / 184. Prompted by the owner's question whether the Bank I
      pianos are really not sample-based: of 78 piano/grand Programs in
      I-A..I-F (K1), 35 are SGX-2, 19 HD-1 on ROM/EXs, 24 other EXi engines
      -- sample-based, but none on a user bank. Not committed.
  111. **REFACTORED (2026-09-29)**: duplication cleanup, per the owner's new
      standing rule ("avoid code duplication"), in four verified steps:
      (1) the frontend's hand-synced copy of `kConfirmedTimbreBanks`
      (`pane-combi-editor.js`, drifted twice -- #20/#21/#29) is gone:
      `combiToValue()` now sends each Timbre's `programBank` (-1 if
      unconfirmed) from `kronos::programBankForConfirmedTimbreCode()`, now
      public; `isConfirmedTimbreProgramBank()` reuses the same lookup.
      (2) `PcgFile`: one `BankLocation` struct (`ProgramBankLocation`
      extends it with `bankType`) and private `bankLocation()`/
      `recordOffset()`/`recordBytes()`/`putRecordBytes()`/
      `moveRecordWithinBank()` keyed by `isProgram` -- the single bounds
      check + offset math behind program/combiRecordBytes(),
      put*RecordBytes(), decodeProgram()/decodeCombi(), refresh*Info(),
      move*WithinBank(), resetCombi(), programBankTypeAt();
      `groupByContentHash()`/`upsertAtSlot()` templates;
      `decodeProgramInfo()`/`decodeCombiInfo()` are the one place each
      Info struct is built (fixes `decodeCombi()` never setting
      contentHash); `findProgram()` replaces 5 hand-written scans.
      (3) `EditorBridge`: `nameCollisionGroupsToValue()` behind both
      name-collision calls (Combi groups now carry `bankType: -1`, which the
      frontend already expected). (4) Duplicates labels: one
      `formatBankNumber()` call instead of an isProgram ternary x3.
      Verified: `ctest` green; old-vs-new (git worktree of eace6f0) on both
      real files -- identical Program/Combi lists, duplicate groups, move
      results and byte-identical saved files after real moves (43-67
      Timbre repoints); bridge JSON identical apart from the new fields;
      all 16,255 real Set List Program slots agree with a direct lookup;
      mock UI: unchanged labels/Timbre refs, no JS errors. Deliberately NOT
      merged (similar outline, different rules): swap/move-to-bank/reset/
      resolve/copy Program vs Combi, the two library panels, bridge
      move/divergence wrappers. Not committed.
  112. **BUILT (2026-10-01)**: Cross Dataset results as a pane filter,
      first iteration of the RFC in STATE.md's IDEAS section (design
      agreed with the owner the same day). Each Programs/Combis toolbar is
      now two equal Bulma columns: name filter + None/All/Invert | a
      "Cross Dataset" label + dropdown. After a Differences find whose pair
      includes the pane's file, the PGM dropdown offers that file's own
      categories (Only in this file / Renamed / Modified twins / Moved, with
      counts, 0-row ones disabled); the pick narrows the list (AND with the
      name filter and bank buttons; `filterBySlots()` next to
      `filterByName()`), the select turns orange while filtering. Hidden on
      Duplicates (the name filter then takes the full row), disabled with a
      tooltip otherwise; one pick per tab, reset on a new dataset; a jump to
      a slot the filter hides clears it.
      - **Store**: new `frontend/cross-dataset-results.js` (shaped like
        `datasets.js`) is now the ONLY home of the sidebar's results -- the
        sidebar writes/reads it instead of its private `results`, the panes
        read `getCrossDatasetFilterCategories(datasetId, kind)` (per dataset
        id, so A/B order never matters to a pane). One pair at a time; a
        different pick clears everything.
      - **Staleness gap found and fixed**: the sidebar dropped results only
        when a dataset closed or its `dirty` flag FLIPPED -- and in practice
        even that never ran after an edit, since `refreshDatasets()` is only
        called on open/close/sidebar-open. New `PcgFile::editCount()`
        (bumped in `writeIntoData()` -- confirmed the only write into
        `data_` -- and on every (re)load, never reset; a save keeps it),
        exposed as `editCount` by `listDatasets()`/open results, mirrored in
        mock_bridge.js (`markEdited()` replaced its 15 hand-written
        `dirty = true` lines). `revalidateCrossDatasetResults()` runs in
        `fetchPrograms()`/`fetchCombis()` -- every Program/Combi write path
        ends in one of those re-fetches (`resetProgram` only calls its own
        panel's refresh, not the coordinator's `load()`, which is why it
        isn't there). Conservative on purpose: ANY edit to either file
        drops ALL modes' results (a Setlist-only edit too, noticed at the
        next list re-fetch); after a Combi resolve the sidebar's re-run
        brings Compare COMBI back, the other modes need a new Find.
      - **Bulma gotcha**: `columns is-gapless` also matches Bulma 1.0's
        spacing helper `.is-gapless { gap: 0 !important }`, which silently
        kills a flex `gap` -- the toolbar zeroes `--bulma-column-gap`
        instead (same as `.panes.columns`). `.select-control-row` now wraps
        (Combis' Set List dropdown drops below None/All/Invert at narrow
        widths; it keeps its 8px left margin there -- cosmetic).
      Verified: `ctest` green (new `editCount()` checks: bumped by a copy,
      unchanged by rejected copies); `cross-dataset-results.test.js` 23/23
      (JavaScriptCore; per-dataset categories, swapped-pair symmetry,
      one-pair replacement, revalidation); new real-app harness
      `frontend/cross-dataset-filter.test.html` 32/32 at 1400px and at the
      800px minimum (index.html in an iframe on the mock bridge: find,
      both panes' categories, filtering, AND with name filter, tab
      switches, jumps, edit invalidation, pair change, dataset change);
      sidebar harness still 53/53 (it needs `--window-size=1400,1000` --
      its 3 layout checks fail at headless Chrome's default size, before
      and after this change). Not yet exercised by hand in the native app
      with real files. Next: Duplicates + Compare PROG categories, then the
      COMBI tab (Compare COMBI). Not committed.
      - **Second iteration (same day): all four modes.** PGM dropdown, in
        the sidebar's mode order: **Duplicates** (this file's own copies --
        every member of a group whose `datasetId` is this file), the
        Differences optgroup, **Compare PROG** (the diverged slot, same in
        both files). A one-category mode is a plain option named after the
        sidebar mode, a several-category mode an <optgroup> (store:
        `group: null`). Only modes actually searched are listed. COMBI tab
        (`createCombisPanel()` got `getSlotFilter` + `filterBySlots()` too):
        a **Compare COMBI** optgroup with **Diverged** / **Different songs**
        (follow-up, same day: **All diverged** / **Edited** / **Different songs**
        -- "Edited" = diverged minus different songs, the last two never
        overlap; key `compareCombis:diverged` kept for the renamed one), all
        leaving out "Init Combi"-on-both-sides slots ALWAYS (the
        sidebar's checkbox only governs its own table). The disabled-tooltip
        names the modes that would fill that tab. `isInitCombiRow()`/
        `isDifferentSong()` MOVED from the sidebar into cross-dataset-
        results.js (one copy, used by both). Verified: store test 32/32,
        filter harness 44/44 at 1400px and 800px (adds Duplicates/Compare
        PROG/Compare COMBI runs, both panes' Duplicates sides, Combis
        filtering, per-tab picks, a Combi jump clearing the Combis filter),
        sidebar harness 53/53, `ctest` green. Still not exercised by hand in
        the native app with real files.
  114. **BUILT (2026-10-02)**: ONE Cross Dataset Find runs all four searches,
      per the owner's RFC ("both dropdowns and one Find button change all
      cross data findings"). The sidebar's mode toggle now only switches
      which already-computed result is shown; the panes' filter dropdowns
      get every category at once (the "only searched modes are listed"
      state is gone; disabled hint: "Run Find in the Cross Dataset sidebar
      ..."). Measured first with a throwaway timing test against the real
      backups -- all four native searches back to back: K1 vs INIT 109 ms,
      K1 vs K2 82 ms, Narf K1X vs K1 115 ms (Differences is the slowest,
      52-75 ms; Compare PROG < 1 ms).
      - **Store** (`cross-dataset-results.js`): `setCrossDatasetResults(pair,
        modes)` replaces the whole set; `clearCrossDatasetResults()` drops
        all of it (per-mode set/clear gone); new `getCrossDatasetPair()`.
      - **Sidebar**: `run()` = `Promise.all` of the four bridge calls; any
        one failing fails the whole Find (one toast, nothing kept). A new
        Find forgets every table's saved sort/filter (`tableStates.clear()`),
        a resolve re-run keeps them.
      - **Resolve order fixed**: a Combi ←/→ resolve now re-runs the Find
        BEFORE refreshing the panes, so their revalidation sees the new edit
        counts and keeps the result -- and the panes' filter picks.
        Previously the refresh dropped the result (and every pick) first.
      Verified: store test 33/33; sidebar harness 61/61 (new: one click =
      each search once, every mode shown without another Find, switching
      modes searches nothing, one failing search keeps nothing in any mode);
      filter harness 47/47 at 1400px and 800px (one Find fills PGM and
      COMBI; new resolve check -- mutation-tested: with the old order both
      panes lose their picks and it fails); `ctest` green. Not exercised by
      hand in the native app.
      - **Follow-up (same day, per direct request)**: the sidebar now reads
        Dataset A, Dataset B, Find, THEN the mode toggle (right above the
        result it switches) -- the dropdowns and Find act on all modes.
        Sidebar harness 62/62 (new order check), filter harness 47/47.
