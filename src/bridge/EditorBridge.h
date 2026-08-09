#pragma once

#include <map>
#include <string>

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

    // [datasetId, setlistIndex, songIndex, newComment] -- in-memory only,
    // like move/copy; nothing is written back to disk (see README.md/STATE.md).
    // Superseded by getSongRecordBytes()/putSongRecordBytes() for the real
    // Setlist row editors (frontend/pane.js), which read-modify-write the
    // Comment field through the same raw-byte path Color/Volume use --
    // this older struct-only setter is unused by them, kept only in case
    // something else still calls it directly.
    choc::value::Value setComment(const choc::value::ValueView& args);

    // [datasetId, setlistIndex, songIndex] -> {ok, bytes:[0-255 x 542]} or
    // {ok:false, error}. The raw SBK1 record for one Set List slot -- see
    // PcgFile::songRecordBytes()'s doc comment. Decoded/edited entirely in
    // JS (frontend/components/kronos/setlist-comment.js and
    // setlist-slot-params.js), the same two-tier data-flow idea
    // decodeProgram()/decodeCombi() already use for detail views.
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

    // Library browser (read-only) -- see docs/README.md and STATE.md's
    // Program/Combi Library Editor plan for scope/roadmap.
    choc::value::Value listPrograms(const choc::value::ValueView& args);          // [datasetId]
    choc::value::Value listCombis(const choc::value::ValueView& args);            // [datasetId]
    choc::value::Value getProgramUsage(const choc::value::ValueView& args);       // [datasetId, bank, number]
    choc::value::Value findDuplicatePrograms(const choc::value::ValueView& args); // [datasetId]

    // [datasetId] -> [{bank, bankType}] for every Program bank in this file --
    // lighter than listPrograms() for UI that labels a *bank* rather than a
    // specific Program row (bank-filter buttons, a Set List slot's Bank-jump
    // button). See PcgFile::programBankTypes()'s doc comment.
    choc::value::Value getProgramBankTypes(const choc::value::ValueView& args);

    // [srcDatasetId, srcBank, srcNumber, dstDatasetId, dstBank, dstNumber] ->
    // {ok} or {ok:false, error}. Copies a Program's raw bytes from the source
    // slot into the destination slot -- see PcgFile::copyProgramFrom()'s own
    // doc comment for the exact validation guards (bank type must match,
    // target slot must be empty, no byte-identical duplicate may already
    // exist in the destination dataset). Same-dataset or cross-dataset both
    // work (srcDatasetId may equal dstDatasetId). This is the first bridge
    // method that writes directly into a dataset's retained raw bytes rather
    // than only mutating in-memory bookkeeping -- see STATE.md.
    choc::value::Value copyProgram(const choc::value::ValueView& args);

    // [datasetId] -> {ok, topLevelChunks:[...], programBanks:[{index,
    // bankType, numRecords, bytesPerRecord}...], combiBanks:[{index,
    // numRecords, bytesPerRecord}...]} or {ok:false, error}. Backs the
    // "Internals" pane -- see PcgFile::topLevelChunkTags()/
    // programBankInfo()/combiBankInfo()'s own doc comments, especially the
    // caveat that `index` is file-order position, not a confirmed-stable
    // bank identity.
    choc::value::Value getDatasetInternals(const choc::value::ValueView& args);

private:
    struct Dataset {
        kronos::PcgFile file;
        std::string displayName;  // the path it was opened from (both openFile() and openFileDialog() set this to the real path)
    };

    std::map<int, Dataset> m_datasets;
    int m_nextDatasetId = 1;

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
