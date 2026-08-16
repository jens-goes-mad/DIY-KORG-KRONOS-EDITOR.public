# CLAUDE.md

Durable, project-level guidance for AI-assisted work on this repo. For current status,
what's built, and open questions, read `STATE.md` first -- it's kept up to date and is
more detailed than this file. For the file format itself, `docs/content/format/index.md`
(`docs/README.md` is now just a short pointer to it, see below).

## The core method: no guessing, ever

This entire project is a from-scratch reverse-engineering of an undocumented binary
format. The one rule that has made it work: **never present a byte offset, formula, or
field meaning as fact unless it's been checked against real ground truth** -- either data
we're given directly (a known song/Program/Combi name, a purpose-built test
file with stated values), or independent cross-verification against an external source
(see `docs/references/`). Several early findings in this project turned out to be
misreadings of *other* fields' data (Font size was originally misattributed to the
Color-sweep test's own edge cases) -- caught and corrected because every claim got
checked, not assumed. When a hypothesis can't be verified yet, say so explicitly rather
than picking the most plausible-looking guess.

When you derive a new offset/formula, verify it by actually running the parser (a
standalone `clang++`-compiled smoke test against `PcgFile.cpp`, or a Python script
against the raw file) against real bytes -- not just by reasoning about hex dumps.

## Collaboration norms established over this project

- **Only commit/push when explicitly asked.** Building and verifying freely; committing
  is a separate, explicit step every time.
- **Small iterations are preferred** over big-bang changes, especially for new
  architecture (see the componentization/decoder work below) -- ship one small proven
  piece, confirm it in tests and real UI, then extend.
- **Don't build for hypothetical future needs.** An encoder/write-path only gets built
  once there's a concrete feature that needs it (see the decoder/encoder architecture
  below) -- e.g. `setlist-editor-comment-and-font.js` has an encoder because Comment/
  Font-size editing is a real feature; a hypothetical Program-renaming encoder doesn't
  exist yet because nothing uses it yet.
- **Verify claims by actually running code**, not just by writing it and reading it back.
  This project's standard pattern: a throwaway smoke-test binary/script, run against real
  sample files, output inspected directly -- for both C++ and the frontend's pure codec
  functions (Node can run ES modules directly, or a quick CommonJS-adapted copy works
  around this environment's older Node version).
- **Keep the docs in sync by hand.** `docs/content/format/index.md` is the single
  canonical file-format reference (2026-08-09 -- `docs/README.md` used to be a second,
  hand-maintained full copy; it drifted in practice, so it's now just a short pointer to
  this file instead, not something to update in parallel). The Hugo Overview page
  (`docs/content/overview/index.md`) is still its own separate summary, though -- keep
  its "what's confirmed" section current after refactors or new findings, not just
  STATE.md.
- **Real Kronos data makes tests worth trusting.** Test fixtures (in JS component test
  harnesses, in C++ smoke tests) should be real bytes extracted from an actual backup
  file where possible, not invented data -- see `frontend/components/kronos/
  setlist-editor-comment-and-font.test.html`'s fixture for the pattern.
- **Flag duplicate code for discussion, don't silently refactor OR silently leave it.**
  Whenever a building block is finished and it turns out to duplicate/closely parallel
  something already in the codebase, raise it explicitly rather than either unilaterally
  extracting a shared abstraction or just moving on. Bring a concrete recommendation
  (what's duplicated, a rough shape for a shared version, the cost/benefit), but let the
  project owner decide -- matches "don't build for hypothetical future needs" above: two
  similar call sites don't automatically justify an abstraction, but they're always worth
  a deliberate look. See STATE.md's "Drag-and-drop code reuse assessed, not (yet)
  refactored" entry (2026-08-08) for the pattern this follows: `pane.js`'s Setlist drag-
  and-drop and `library.js`'s Programs drag-and-drop were found to be independently
  hand-written near-duplicates, discussed, and deliberately left unmerged for now with
  the reasoning written down.

## Current architecture direction (as of 2026-08-01)

Both frontend and backend are moving toward small, focused, independently-testable
decoder/encoder units instead of one big eager parse. Full rationale in
`docs/content/components/index.md` and the decision record in `STATE.md`'s
"ARCHITECTURE: DECODER/ENCODER REFACTOR" section (read that section for the full
reasoning -- this is a compressed pointer, not a replacement). Short version:

- Frontend: `frontend/components/{kronos,generic}/*.js` -- each a pure codec
  (`decode`/`encode`, no DOM) plus a component (DOM only, operates on decoded state)
  plus a standalone `.test.html` harness (no native build, no CHOC, just a static file
  server). `setlist-editor-comment-and-font.js` (originally `setlist-comment.js`, renamed
  2026-08-16 alongside splitting Color/Volume into their own files -- see the Setlist
  editor's `frontend/components/kronos/setlist-editor-*.js` family) is the first one built
  this way.
- Backend: `PcgFile` retains raw file bytes (`data_`) instead of discarding them after
  parsing. `src/kronos/ProgramDecoder.{h,cpp}` (built, verified zero-regression) is the
  first per-record decoder; Combi and Set List slot are next, same pattern.
- **Two-tier data flow, not one architecture for everything**: bulk/list views (tables,
  dedup) stay served by native decoders walking the whole retained buffer -- real
  efficiency win for scanning/hashing thousands of records. Detail/edit views request
  the *specific raw byte chunk* they need via the bridge and decode/encode it entirely
  in JS, same as `setlist-editor-comment-and-font.js` already does -- preserves "test
  without building the native app" specifically where it matters (interactive UI).
- **Writes are immediate, not a deferred overlay**: `encode()` writes straight into
  `data_` via `putRecordBytes()` rather than tracking pending edits separately keyed by
  position -- a position-keyed overlay breaks the moment something reorders records
  (the overlay would apply to whatever now occupies that position). Safe because this
  app is single-threaded with one user editing at a time. `putRecordBytes()` must also
  re-derive any cached structured fields (e.g. `Song.comment`) from the freshly-written
  bytes, not leave them stale.
- Encoders only get built once a real write feature needs one, not speculatively --
  by now that covers the whole `setlist-editor-*.js` family (Comment/Font size, Color,
  Volume, Name all have real write features), but e.g. a Program-renaming encoder still
  doesn't exist because nothing uses it yet.
- **Current top priority, agreed as a team: testing outranks
  new features right now.** Before Combi or any further component wiring: a real,
  committed C++ test target (scoped to just the format-parsing code, not the full app/
  CHOC, so it stays fast) via CMake/`ctest`, plus a headless `node`-runnable `.test.js`
  per frontend component alongside its browser harness. Not built yet.

This shape is explicitly not committed to being final -- revisit it as each piece
(Program decoder now done; chunk-based component wiring next) proves itself against
real tests and the real UI.
