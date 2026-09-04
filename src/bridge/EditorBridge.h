#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "choc/containers/choc_Value.h"
#include "kronos/PcgFile.h"

// Bridge surface exposed to the web UI. Each public method here is bound 1:1
// to a whitelisted JS-callable function in main.cpp (via WebView::bind) --
// there is no generic eval/exec channel into native code from the UI.
//
// Holds any number of loaded .PCG files in memory at once, each a "dataset"
// identified by an id this class mints itself on open (never supplied by the
// caller) -- decoupled entirely from "which UI pane displays it". A file has
// 128 Set Lists; the frontend picks one per pane to view/act on and passes
// its index explicitly on every call (this class holds no "current
// selection" state of its own). Any two calls naming the same datasetId
// (including copyEntry's src/dst) act on the one shared PcgFile, so two panes
// pointed at the same dataset edit the same in-memory file -- this is
// deliberate, see docs/content/components/index.md's dataset section. Dataset
// count is unbounded and nothing evicts a dataset except an explicit
// closeDataset() call. Cross-Set-List copyEntry() only rearranges the
// in-memory Setlist struct (a real, known limitation -- see its own doc
// comment), but copyProgram(), putSongRecordBytes(), putNameRecordBytes(),
// and reorderSongEntry() write directly into the dataset's own retained raw
// bytes -- see README.md/STATE.md. saveFileAs() writes that same retained
// buffer straight to disk;
// there's no real Save UI wired up yet (no dialog, no dirty-tracking), just
// the bridge-level building block -- see saveFileAs()'s own doc comment.
class EditorBridge {
public:
    choc::value::Value openFile(const choc::value::ValueView& args);    // [path] -> {ok, datasetId, displayName, setlistCount}

    // No args -- shows a native "Open" dialog (src/platform/NativeFileDialog.h),
    // invoked directly rather than through CHOC's own WebView-triggered
    // picker (see NativeFileDialog.cpp for why that distinction matters).
    // This is the only UI-reachable way to open a file (drag-and-drop-to-
    // open was removed once this worked -- see STATE.md). Returns the same
    // shape as openFile(), or `{ok: true, cancelled: true}` if the user
    // cancelled (not an error -- see makeCancelled()), or
    // `{ok: true, ..., alreadyOpen: true}` if this exact path was already
    // open (see openFileAtPath() -- opening the same file twice reuses the
    // existing dataset rather than duplicating it in memory). Currently
    // macOS-only; returns an error on platforms NativeFileDialog.cpp
    // doesn't support yet.
    choc::value::Value openFileDialog(const choc::value::ValueView& args);

    // No args -- every currently open dataset, so any UI selector (a pane, or
    // Library) can populate/refresh its options regardless of which pane (if
    // any) originally opened it.
    choc::value::Value listDatasets(const choc::value::ValueView& args);   // [] -> [{datasetId, displayName, setlistCount}]

    // Frees a loaded dataset. A no-op (still returns ok) if datasetId is
    // already gone -- callers don't need to track whether they raced another
    // close. Nothing here checks whether a pane is currently showing it;
    // that's the frontend's job (reset to empty state on the next
    // listDatasets() refresh it was already subscribed to).
    choc::value::Value closeDataset(const choc::value::ValueView& args);   // [datasetId]

    // [datasetId] -> {ok, dirty} or {ok:false, error}. A direct point query
    // of PcgFile::isDirty() -- deliberately NOT routed through
    // listDatasets()'s cached/broadcast list (that exists to populate
    // dataset selectors, and every pane's own copy of it only refreshes on
    // a full open/close broadcast, not after every write elsewhere in the
    // app -- reported directly, 2026-08-15: the Unload button's own
    // confirmation went stale immediately after a Combi swap/move/copy for
    // exactly this reason). The dirty flag lives on the raw data itself
    // (PcgFile) and is always current the instant it's asked for -- this
    // is that ask, with no cache in between.
    choc::value::Value isDatasetDirty(const choc::value::ValueView& args);  // [datasetId]

    choc::value::Value listSetlists(const choc::value::ValueView& args);  // [datasetId]
    choc::value::Value getEntries(const choc::value::ValueView& args);    // [datasetId, setlistIndex]

    // [srcDatasetId, srcSetlistIndex, srcIndex, dstDatasetId, dstSetlistIndex, dstIndex].
    // Cross-Set-List only now (same-Set-List dragging uses the real byte-
    // level getSongRecordBytes()/getNameRecordBytes()/putSongRecordBytes()/
    // putNameRecordBytes() pair instead, see pane.js) -- still the older,
    // in-memory-only mechanism (see the class doc comment above), since
    // making cross-Set-List operations save-durable raises the same
    // fixed-128-slot-capacity question reorderSongEntry() above
    // deliberately doesn't attempt yet. `moveEntry()` (same-Set-List swap)
    // is gone -- fully superseded by the new byte-level copy-over/insert
    // gestures.
    choc::value::Value copyEntry(const choc::value::ValueView& args);

    // [datasetId, setlistIndex, songIndex] -> {ok, bytes:[0-255 x 542]} or
    // {ok:false, error}. The raw SBK1 record for one Set List slot -- see
    // PcgFile::songRecordBytes()'s doc comment. Decoded/edited entirely in
    // JS (frontend/components/kronos/setlist-editor-comment-and-font.js,
    // setlist-editor-color.js, and setlist-editor-volume.js), the same
    // two-tier data-flow idea decodeProgram()/decodeCombi() already use
    // for detail views.
    choc::value::Value getSongRecordBytes(const choc::value::ValueView& args);

    // [datasetId, setlistIndex, songIndex, bytes[0-255 x 542]] -> {ok} or
    // {ok:false, error}. Writes straight into the dataset's retained raw
    // bytes via PcgFile::putSongRecordBytes(), which also re-derives the
    // decoded Song fields (params/comment) from what was just written --
    // see that method's doc comment for why. This is the Setlist-side
    // counterpart to copyProgram(): the second bridge method that writes
    // directly into a dataset's retained raw bytes rather than only
    // mutating in-memory bookkeeping.
    choc::value::Value putSongRecordBytes(const choc::value::ValueView& args);

    // [datasetId, setlistIndex, songIndex] -> {ok, bytes:[0-255 x 28]} or
    // {ok:false, error}. The raw SDB1 name record for one Set List slot --
    // a completely separate chunk and byte stride from
    // getSongRecordBytes() above, see PcgFile::nameRecordBytes()'s own doc
    // comment for why a slot's name and params need independent
    // read/write paths (they aren't stored together).
    choc::value::Value getNameRecordBytes(const choc::value::ValueView& args);

    // [datasetId, setlistIndex, songIndex, bytes[0-255 x 28]] -> {ok} or
    // {ok:false, error}. Writes straight into the dataset's retained raw
    // bytes via PcgFile::putNameRecordBytes().
    choc::value::Value putNameRecordBytes(const choc::value::ValueView& args);

    // [datasetId, bank, number] -> {ok, bytes:[0-255 x <record size>]} or
    // {ok:false, error}. The raw MBK1/PBK1 record for one Program slot --
    // see PcgFile::programRecordBytes()'s own doc comment. Same two-tier
    // "detail view requests exactly the bytes it needs, decodes/encodes
    // entirely in JS (or a private companion module)" shape as
    // getSongRecordBytes() above -- generic on purpose: this bridge has no
    // idea what a caller does with the bytes, which is what lets an
    // optional private module (see EditorExtension.h) build a real editor
    // for a specific EXi engine without this repo needing to know
    // anything about it.
    choc::value::Value getProgramRecordBytes(const choc::value::ValueView& args);

    // [datasetId, bank, number, bytes[0-255 x <record size>]] -> {ok} or
    // {ok:false, error}. Writes straight into the dataset's retained raw
    // bytes via PcgFile::putProgramRecordBytes(), which also re-derives
    // the decoded ProgramInfo fields (name, exiAlgorithmType, ...) from
    // what was just written -- see that method's own doc comment.
    choc::value::Value putProgramRecordBytes(const choc::value::ValueView& args);

    // [datasetId, setlistIndex, fromIndex, toIndex] -> {ok} or {ok:false,
    // error}. Relocates one Set List slot (its name AND its params
    // together) within the same Set List, shifting the intervening range
    // -- see PcgFile::reorderSong()'s own doc comment. The Setlist drag-
    // and-drop "insert between two entries" gesture is built on this: one
    // native call so a wide shift (up to ~127 slots) is a single bridge
    // round-trip, not one per shifted slot. Same-Set-List only, per direct
    // agreement -- relocating *between* Set Lists raises a real data-loss
    // question (a fixed-128-slot destination has to evict something to
    // make room for an insert) that needs its own careful design, not
    // bundled into this pass. copyEntry() below still handles cross-
    // Set-List copying, unchanged, at its existing (in-memory-only, not
    // yet save-durable) fidelity.
    choc::value::Value reorderSongEntry(const choc::value::ValueView& args);

    // [datasetId, srcSetlistIndex, dstSetlistIndex] -> {ok} or {ok:false,
    // error}. Overwrites every song slot in dstSetlistIndex with
    // srcSetlistIndex's content (both Set Lists live in the SAME dataset --
    // this is a same-file operation, not a cross-dataset one, see
    // PcgFile::copySetlist()'s own doc comment) -- the "copy all to
    // opposite" pane-header button (frontend/pane.js/app.js), for starting a
    // gig's Set List from an existing one and then tweaking a few entries.
    // Leaves dstSetlistIndex's own Setlist::name untouched -- only the 128
    // song slots move. The frontend is responsible for only offering this
    // when both panes already share one dataset with two different Set
    // Lists selected (see app.js's copyAllToOpposite()) -- this method
    // itself doesn't know or care which pane called it.
    choc::value::Value copySetlistEntries(const choc::value::ValueView& args);

    // [datasetId, setlistIndex, ascending] -> {ok} or {ok:false, error}.
    // Physically reorders every one of a Set List's 128 slots into
    // alphabetical order -- see PcgFile::sortSetlist()'s own doc comment
    // for why this is a real, immediate, whole-Set-List rewrite rather
    // than a display-only convenience: a Kronos has no notion of "sorted"
    // independent of a slot's actual record position, so the app's own
    // A-Z/Z-A buttons (frontend/pane.js) commit straight to the file's raw
    // bytes, exactly like drag-and-drop -- there is no separate "undo the
    // sort" once clicked, same as every other immediate-write edit in this
    // app.
    choc::value::Value sortSetlistEntries(const choc::value::ValueView& args);

    // [datasetId, path] -> {ok} or {ok:false, error}. Writes the dataset's
    // retained raw bytes straight to `path` via PcgFile::save() -- see that
    // method's own doc comment for why this needs no serialization step of
    // its own (every edit already lands directly in the retained buffer).
    // Deliberately minimal: takes a plain path, no dialog -- callers
    // (currently a devtools console script) supply an absolute path
    // themselves. saveFileDialog() below is the real, UI-reachable front
    // door onto the same write; this stays as the lower-level building
    // block it wraps.
    choc::value::Value saveFileAs(const choc::value::ValueView& args);

    // [datasetId] -> {ok, path} or {ok:true, cancelled:true} (user closed
    // the dialog, see makeCancelled()) or {ok:false, error}. Shows a native
    // "Save As" dialog (src/platform/NativeFileDialog.h's
    // showSaveFileDialog(), invoked directly for the same reason
    // openFileDialog() above doesn't go through CHOC's own WebView-
    // triggered picker -- see NativeFileDialog.cpp), pre-filled with the
    // dataset's own display name, then writes to wherever the user picked
    // via the same PcgFile::save() saveFileAs() above uses. This is the
    // per-pane "Save As..." button's bridge method (frontend/pane.js) --
    // the first real, UI-reachable way to write an edited dataset back to
    // disk; saveFileAs() above stays reachable directly (path supplied by
    // the caller) for scripted/devtools use. Currently macOS-only, like
    // openFileDialog(); returns an error on platforms
    // NativeFileDialog.cpp doesn't support yet.
    choc::value::Value saveFileDialog(const choc::value::ValueView& args);

    // Library browser (read-only) -- see docs/content/format/index.md and STATE.md's
    // Program/Combi Library Editor plan for scope/roadmap.
    choc::value::Value listPrograms(const choc::value::ValueView& args);          // [datasetId]
    choc::value::Value listCombis(const choc::value::ValueView& args);            // [datasetId]
    choc::value::Value getProgramUsage(const choc::value::ValueView& args);       // [datasetId, bank, number]
    choc::value::Value findDuplicatePrograms(const choc::value::ValueView& args); // [datasetId]

    // Same idea, for Combis -- added later than the Program version, for
    // symmetry with the Combi tab's own name-collision check below.
    choc::value::Value findDuplicateCombis(const choc::value::ValueView& args); // [datasetId]

    // [datasetId] -> [{name, variants: [{members: [ProgramInfo/CombiInfo...]}]}].
    // The inverse question from findDuplicatePrograms() above: entries
    // sharing a NAME but NOT byte-identical -- see PcgFile::
    // NameCollisionGroup's own doc comment for exactly what counts (2+
    // variants under one name; a name where every entry is ALSO
    // byte-identical is a plain duplicate, not returned here). `members` are
    // full ProgramInfo/CombiInfo objects (via programToValue()/
    // combiToValue()), not bare bank/number pairs, so the frontend can
    // render them the same way findDuplicatePrograms()'s groups already are.
    choc::value::Value findProgramNameCollisions(const choc::value::ValueView& args); // [datasetId]
    choc::value::Value findCombiNameCollisions(const choc::value::ValueView& args);   // [datasetId]

    // [datasetId] -> [{bank, bankType}] for every Program bank in this file --
    // lighter than listPrograms() for UI that labels a *bank* rather than a
    // specific Program row (bank-filter buttons, a Set List slot's Bank-jump
    // button). See PcgFile::programBankTypes()'s doc comment.
    choc::value::Value getProgramBankTypes(const choc::value::ValueView& args);

    // [srcDatasetId, srcBank, srcNumber, dstDatasetId, dstBank, dstNumber] ->
    // {ok} or {ok:false, error}. Copies a Program's raw bytes from the source
    // slot into the destination slot -- see PcgFile::copyProgramFrom()'s own
    // doc comment for the exact validation guards (bank type must match,
    // target slot must be empty -- a byte-identical duplicate elsewhere in
    // the destination dataset is fine, no restriction on that). Same-dataset
    // or cross-dataset both work (srcDatasetId may equal dstDatasetId). This
    // is the first bridge method that writes directly into a dataset's
    // retained raw bytes rather than only mutating in-memory bookkeeping --
    // see STATE.md.
    choc::value::Value copyProgram(const choc::value::ValueView& args);

    // [datasetId, bankA, numberA, bankB, numberB] -> {ok, setlistRefsRepointed,
    // combiRefsRepointed, combiRefsSkipped} or {ok:false, error}. Swaps two
    // Programs' entire content (same dataset only -- see
    // PcgFile::swapPrograms()'s own doc comment for why, unlike copyProgram()
    // above, this doesn't go cross-dataset) and repoints every Set List slot
    // AND Combi Timbre reference pointing at either one to follow its
    // content -- see PcgFile::swapPrograms()'s own doc comment for why this
    // stays the one non-destructive way to exchange two occupied slots
    // holding genuinely different content, even now that copyProgram()
    // above no longer refuses a byte-identical match elsewhere in the file.
    choc::value::Value swapProgram(const choc::value::ValueView& args);

    // [datasetId, bank, fromNumber, toNumber] -> {ok, setlistRefsRepointed,
    // combiRefsRepointed, combiRefsSkipped} or {ok:false, error}. Moves a
    // Program to a new position within its own bank, shifting the
    // intervening range to make room -- see
    // PcgFile::moveProgramWithinBank()'s own doc comment.
    choc::value::Value moveProgramWithinBank(const choc::value::ValueView& args);

    // [datasetId, srcBank, srcNumber, dstBank, dstNumber] -> {ok,
    // setlistRefsRepointed, combiRefsRepointed, combiRefsSkipped} or
    // {ok:false, error}. Moves a Program into a specific slot in a
    // DIFFERENT bank of the same engine type, overwriting whatever was
    // there -- see PcgFile::moveProgramToBank()'s own doc comment for the
    // destination-referenced/engine-type refusals and how the vacated
    // source slot gets refilled. Reads resources/Init-Program-HD1.raw/
    // Init-Program-EXi.raw fresh off disk (same as resetProgram() above).
    choc::value::Value moveProgramToBank(const choc::value::ValueView& args);

    // [datasetId, bank, number, targets, requireByteExactMatch] -> {ok,
    // clearedPrograms, setlistRefsRepointed, combiRefsRepointed,
    // combiRefsSkipped} or {ok:false, error}. `targets` is a JS array of
    // {bank, number} objects -- exactly which duplicates to fold in, chosen
    // by the user in the Duplicates panel's resolve-picker sidebar
    // (2026-08-25 -- replaces an earlier "resolve the WHOLE group" version;
    // see PcgFile::resolveDuplicates()'s own doc comment for why an explicit
    // list). `requireByteExactMatch` (defaults true if omitted) picks which
    // of the panel's two checks this is servicing -- true ("Same content,
    // different location") clears every target to its own bank's factory
    // Init Program template; false ("Same name, different content") never
    // clears anything, targets are expected to genuinely differ (2026-08-25,
    // per direct decision: destroying real, non-recoverable content isn't
    // acceptable there). Either way every Set List/Combi reference to a
    // target gets repointed to (bank, number). See PcgFile::
    // resolveDuplicates()'s own doc comment for the full behavior, including
    // the Combi-repointing caveat (combiRefsSkipped). Reads resources/
    // Init-Program-HD1.raw/Init-Program-EXi.raw fresh off disk on every
    // byte-exact call (small files, no caching needed) via
    // EDITOR_RESOURCES_DIR -- see CMakeLists.txt; never read at all when
    // requireByteExactMatch is false.
    choc::value::Value resolveDuplicateProgram(const choc::value::ValueView& args);

    // [datasetId, bank, number, targets, requireByteExactMatch] -> {ok,
    // setlistRefsRepointed} or {ok:false, error}. Same `targets`/
    // `requireByteExactMatch` shapes/defaults as resolveDuplicateProgram()
    // above. Deliberately NOT symmetric with resolveDuplicateProgram() --
    // see PcgFile::resolveDuplicateCombis()'s own doc comment for why: no
    // real Init Combi byte capture exists yet, so a target's own content is
    // NEVER cleared in either mode, only its Set List references move --
    // requireByteExactMatch only changes whether the contentHash match is
    // verified first.
    choc::value::Value resolveDuplicateCombis(const choc::value::ValueView& args);

    // [datasetId, bank, number] -> {ok} or {ok:false, error}. Writes the
    // matching Init Program template (HD-1 or EXi, by this bank's own type)
    // straight into this one Program slot -- "reset entry", nothing else in
    // the file is touched, unlike resolveDuplicateProgram() above (no
    // repointing: anything that already referenced this slot keeps pointing
    // at it, now showing the Init Program). See PcgFile::resetProgram()'s
    // own doc comment. Reads the same template files the same way
    // resolveDuplicateProgram() does.
    choc::value::Value resetProgram(const choc::value::ValueView& args);

    // [datasetId, bankA, numberA, bankB, numberB] -> {ok, setlistRefsRepointed}
    // or {ok:false, error}. Swaps two Combis' entire content (same or
    // different bank) and repoints every Set List slot referencing either
    // one to follow it to its new position. See PcgFile::swapCombis()'s own
    // doc comment.
    choc::value::Value swapCombis(const choc::value::ValueView& args);

    // [datasetId, bank, fromNumber, toNumber] -> {ok, setlistRefsRepointed}
    // or {ok:false, error}. Moves a Combi to a new position within its own
    // bank, shifting the intervening range to make room -- see
    // PcgFile::moveCombiWithinBank()'s own doc comment.
    choc::value::Value moveCombiWithinBank(const choc::value::ValueView& args);

    // [datasetId, srcBank, srcNumber, dstBank, dstNumber] -> {ok,
    // setlistRefsRepointed} or {ok:false, error}. Moves a Combi into a
    // specific slot in a different bank, overwriting whatever was there --
    // see PcgFile::moveCombiToBank()'s own doc comment for the destination-
    // referenced refusal and how the vacated source slot gets filled.
    choc::value::Value moveCombiToBank(const choc::value::ValueView& args);

    // [datasetId, srcBank, srcNumber, dstBank, dstNumber] -> {ok,
    // setlistRefsRepointed} or {ok:false, error}. Copies a Combi's raw
    // content into a DIFFERENT slot, leaving the source untouched --
    // `setlistRefsRepointed` is always 0. Refuses unless the destination is
    // currently an empty ("Init Combi") slot -- see PcgFile::copyCombi()'s
    // own doc comment.
    choc::value::Value copyCombi(const choc::value::ValueView& args);

    // [datasetId, bank, number] -> {ok} or {ok:false, error}. Clears one
    // Combi slot back to a blank "- Init Combi -" record -- the Combi
    // equivalent of resetProgram(), nothing else in the file repointed. See
    // PcgFile::resetCombi()'s own doc comment (including why it can refuse
    // for a fully-populated bank).
    choc::value::Value resetCombi(const choc::value::ValueView& args);

    // [srcDatasetId, srcBank, srcNumber, dstDatasetId, dstBank, dstNumber] -> {ok,
    // dependencies:[{timbreIndex, srcBank, srcNumber, name, bankType, found,
    // foundBank, foundNumber}...], unresolved:[{srcBank, srcNumber, name,
    // bankType, candidateBanks:[...]}...]} or {ok:false, error}. Read-only
    // first step of a cross-dataset Combi copy -- see
    // PcgFile::analyzeCombiCrossDatasetCopy()'s own doc comment.
    choc::value::Value analyzeCombiCrossDatasetCopy(const choc::value::ValueView& args);

    // [srcDatasetId, srcBank, srcNumber, dstDatasetId, dstBank, dstNumber,
    // placements:[{srcBank, srcNumber, dstBank}...]] -> {ok, setlistRefsRepointed}
    // or {ok:false, error}. Applies a cross-dataset Combi copy using the
    // caller's chosen destination bank per unresolved Program from a prior
    // analyzeCombiCrossDatasetCopy() call (`placements` may be empty/omitted
    // if every dependency was already found) -- see
    // PcgFile::applyCombiCrossDatasetCopy()'s own doc comment.
    choc::value::Value applyCombiCrossDatasetCopy(const choc::value::ValueView& args);

    // [datasetId] -> {ok, topLevelChunks:[...], programBanks:[{index,
    // bankType, numRecords, bytesPerRecord}...], combiBanks:[{index,
    // numRecords, bytesPerRecord}...]} or {ok:false, error}. Backs the
    // "Internals" pane -- see PcgFile::topLevelChunkTags()/
    // programBankInfo()/combiBankInfo()'s own doc comments, especially the
    // caveat that `index` is file-order position, not a confirmed-stable
    // bank identity.
    choc::value::Value getDatasetInternals(const choc::value::ValueView& args);

    // NOT bound to JS -- this is a native-side-only hook, called by main.cpp
    // once per open window, so every window's own frontend can learn when
    // the shared SET of open datasets changes (a dataset opened/closed --
    // not fired for every edit) even though it happened via a DIFFERENT
    // window's WebView. This class is otherwise unaware of "windows" at
    // all (window/webview lifecycle stays entirely in main.cpp, see
    // STATE.md's multi-window entry); it just knows to call back whenever
    // m_datasets gains or loses an entry. Each registered listener is
    // expected to push a refresh into its own WebView (e.g.
    // `view.evaluateJavascript("window.refreshDatasets()")`) -- this class
    // has no WebView reference of its own, deliberately: it stays testable
    // by tests/pcg_file_test.cpp-style code with zero CHOC dependency,
    // same reasoning PcgFile itself is kept CHOC-free.
    using DatasetsChangedListener = std::function<void()>;
    void addDatasetsChangedListener(DatasetsChangedListener listener);

    // NOT bound to JS -- like addDatasetsChangedListener() above, these are
    // native-side-only, for an optional private companion module's own C++
    // extension code (see EditorExtension.h) to call directly, since it
    // already holds a live EditorBridge& and has no need to round-trip
    // through JS. Marks a specific Program record as "claimed" by an
    // external editor (e.g. a per-engine parameter editor window) for as
    // long as that editor keeps it open -- copyProgram()/swapProgram()
    // below refuse to move/copy/overwrite a locked record, so an open
    // editor's own in-memory bytes can never be silently invalidated (or
    // silently overwritten) by a drag-and-drop elsewhere in the app. This
    // class has no idea WHY a record is locked or by what -- purely a
    // generic "hands off" marker, same "stay agnostic of what a caller
    // does with it" reasoning getProgramRecordBytes()/
    // putProgramRecordBytes() above already follow.
    void lockProgramRecord(int datasetId, int bank, int number);
    void unlockProgramRecord(int datasetId, int bank, int number);
    bool isProgramRecordLocked(int datasetId, int bank, int number) const;

    // NOT bound to JS -- plain native-typed counterparts of
    // getProgramRecordBytes()/putProgramRecordBytes() above, for the same
    // kind of direct C++ caller lockProgramRecord() above is for. Avoids
    // making a native caller hand-construct a choc::value::Value array just
    // to call the JS-shaped versions of these two -- same underlying
    // PcgFile calls, nullopt/false on failure (out of range, or wrong byte
    // count on the put side) rather than a JS-facing error string, since a
    // native caller doesn't need one forwarded through a bridge boundary
    // that was never actually crossed.
    std::optional<std::vector<uint8_t>> getProgramRecordBytesRaw(int datasetId, int bank, int number);
    bool putProgramRecordBytesRaw(int datasetId, int bank, int number, const std::vector<uint8_t>& bytes);

    // Same "direct C++ caller" reasoning as the Raw pair above -- main.cpp's
    // own quit-confirmation guard (2026-08-26, see STATE.md) needs to ask
    // "is there ANYTHING unsaved, across every open dataset, in any pane of
    // any window" before letting the app actually close, and that's a plain
    // native question with no JS-facing shape to bridge at all. True the
    // instant ANY open dataset's own PcgFile::isDirty() is true -- same
    // dirty flag isDatasetDirty() already exposes per-dataset to JS (see its
    // own doc comment), just checked across all of them at once here.
    bool anyDatasetDirty() const;

private:
    struct Dataset {
        kronos::PcgFile file;
        std::string displayName;  // the path it was opened from (both openFile() and openFileDialog() set this to the real path)
    };

    std::map<int, Dataset> m_datasets;
    int m_nextDatasetId = 1;
    std::vector<DatasetsChangedListener> m_datasetsChangedListeners;
    void notifyDatasetsChanged();
    std::set<std::tuple<int, int, int>> m_lockedProgramRecords;  // (datasetId, bank, number) -- see lockProgramRecord()'s own doc comment

    kronos::Setlist* setlistOf(int datasetId, int setlistIndex);
    kronos::PcgFile* fileOf(int datasetId);
    choc::value::Value finishOpen(Dataset dataset);
    static choc::value::Value datasetResultValue(int datasetId, const Dataset& dataset);

    // Shared by openFile() (JS supplies the path -- not currently reachable
    // from any UI control) and openFileDialog() (path comes from the native
    // dialog instead). If `path` is already open (an exact displayName
    // match against an existing dataset), returns that dataset's info
    // (with `alreadyOpen: true`) instead of loading a second copy.
    choc::value::Value openFileAtPath(const std::string& path);

    static choc::value::Value makeOk();
    static choc::value::Value makeCancelled();  // user closed the dialog without choosing a file -- not an error
    static choc::value::Value makeError(const std::string& error);
    static choc::value::Value songToValue(const kronos::Song& song);
    static choc::value::Value programToValue(const kronos::ProgramInfo& program);
    static choc::value::Value combiToValue(const kronos::CombiInfo& combi);
};
