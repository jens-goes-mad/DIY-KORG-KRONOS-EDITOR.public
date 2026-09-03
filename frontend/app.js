function setStatus(message) {
  document.getElementById("statusBar").textContent = message;
}

// A transient, auto-dismissing popup -- distinct from setStatus() above,
// which writes to the persistent bottom status bar (easy to miss, and
// overwritten by the next status message before a user necessarily reads
// it). Used for things that genuinely need to interrupt/get noticed in the
// moment, e.g. "another pane already has this open" -- see pane.js's
// toggleEditor(). `toastContainer` is one shared element (index.html) so
// toasts from either pane stack in the same place rather than each pane
// needing its own.
//
// Deliberately hand-rolled (~15 lines, no queueing/stacking-limit/action-
// button support) rather than pulling in a ready-made toast library --
// matches this project's no-extra-dependencies-until-actually-needed
// convention (see CLAUDE.md), and the one real use case so far doesn't
// need more than this. Worth revisiting if toast usage grows beyond simple
// single-line messages -- there are mature, small, generic JS toast libs
// (e.g. Notyf, Toastify) that would be a reasonable swap-in at that point
// rather than growing this by hand.
const toastContainer = document.getElementById("toastContainer");

// `isError` (2026-08-15) switches the toast to Bulma's semantic "danger"
// color instead of the default "warning" one (style.css's .toast-error),
// same "reuse Bulma's real color system" reasoning the default color
// already used -- reported directly that every toast, success or failure,
// looked identical, and that errors in general were easy to miss entirely
// when they only went to the persistent status bar (setStatus() above,
// whose own doc comment already admits this). No existing call site passed
// a second positional arg (grepped first to confirm), so this is a safe,
// non-breaking signature change from a bare `durationMs` number to an
// options object.
function showToast(message, { isError = false, durationMs = 3500 } = {}) {
  const toast = document.createElement("div");
  toast.className = isError ? "toast toast-error" : "toast";
  toast.textContent = message;
  toastContainer.appendChild(toast);

  // Added in a separate frame so the fade-in transition (style.css) actually
  // animates from the "not visible" state instead of starting already at
  // its end state (both classes present on the same frame == no visible
  // transition at all).
  requestAnimationFrame(() => toast.classList.add("toast-visible"));

  setTimeout(() => {
    toast.classList.remove("toast-visible");
    toast.addEventListener("transitionend", () => toast.remove(), { once: true });
  }, durationMs);
}

// The native app (choc_DesktopWindow.h) has no menu bar at all, so there's
// no OS-level Cmd+R/Ctrl+R reload the way a real browser tab gets for free
// -- without this, the only way to pick up a frontend/ change while testing
// (even a live-off-disk debug build, see main.cpp's loadFrontendResource())
// is fully quitting and relaunching the whole process, which is easy to
// forget and looks exactly like "my CSS/JS change isn't taking effect."
window.addEventListener("keydown", (ev) => {
  if ((ev.metaKey || ev.ctrlKey) && ev.key.toLowerCase() === "r") {
    ev.preventDefault();
    location.reload();
  }
});

const panes = {};

// Dropping within the same Set List (same dataset) swaps the two slots'
// contents (a reorder). Dropping onto a different Set List in the SAME
// dataset copies the source slot's content onto the destination slot,
// leaving the destination's slot positions untouched -- the Norton-
// Commander-style "copy between lists" interaction. Comparing dataset
// identity (not pane identity) is what makes two panes pointed at the same
// dataset behave like one shared document: dragging between them still
// resolves to the one underlying PcgFile. Neither writes anything back to
// disk yet (see STATE.md).
//
// Copying ACROSS two different datasets is deliberately blocked: a Song
// slot's bank/number is a physical-location reference into its OWN
// dataset's Program/Combi tables, not portable content -- copying it
// as-is into a different dataset could silently point at the wrong
// Program/Combi there (or nothing at all), since bank/number positions
// aren't guaranteed to mean the same thing across two different backups.
// This is the CMB/PRG physical-bank-position problem from the
// explore/sqlite-patch-datastore branch (see STATE.md's "EXPLORATION"
// section) -- revisit once that's solved, not before.
// `target.zone` (pane.js's dropZoneForEvent()) is one of "on" (dropped
// mid-row -- copy over), "before"/"after" (dropped near a row's top/bottom
// edge -- insert, shifting the intervening range). Copy-over is a direct
// 1:1 slot overwrite -- safe by construction whether source and target are
// the same Set List or two different ones in the same dataset, so it's
// always a real byte-level write. Insert is Same-Set-List only: relocating
// an entry there is a pure rearrangement of the same 128 slots, safe by
// construction (nothing added or removed, see STATE.md's RFC). Cross-Set-
// List insert stays on the older copyEntry() path -- inserting into a
// *different*, already-full 128-slot Set List would have to evict
// something at its far end to make room, a real data-loss question
// deliberately not tackled yet.
async function onDropEntry(source, target) {
  if (source.datasetId !== target.datasetId) {
    showToast(
      "Can't copy Set List slots between different datasets yet -- they reference physical Program/Combi " +
        "bank positions that aren't portable across datasets. See STATE.md's EXPLORATION section.",
      { isError: true }
    );
    return;
  }

  const sameList = source.setlistIndex === target.setlistIndex;
  let result;

  if (target.zone === "on") {
    // Copy over: target becomes an exact copy of source (name + params);
    // source stays unchanged. Real byte-level writes (getSongRecordBytes/
    // getNameRecordBytes/putSongRecordBytes/putNameRecordBytes) -- both
    // halves of a slot's data, since they live in separate SBK1/SDB1
    // records, see PcgFile.h's own doc comment on nameRecordBytes().
    if (sameList && source.index === target.index) return;
    const [srcParams, srcName] = await Promise.all([
      window.getSongRecordBytes(source.datasetId, source.setlistIndex, source.index),
      window.getNameRecordBytes(source.datasetId, source.setlistIndex, source.index),
    ]);
    if (!srcParams.ok || !srcName.ok) {
      showToast(`Copy failed: ${srcParams.ok ? srcName.error : srcParams.error}`, { isError: true });
      return;
    }
    const [putParams, putName] = await Promise.all([
      window.putSongRecordBytes(target.datasetId, target.setlistIndex, target.index, srcParams.bytes),
      window.putNameRecordBytes(target.datasetId, target.setlistIndex, target.index, srcName.bytes),
    ]);
    if (!putParams.ok || !putName.ok) {
      showToast(`Copy failed: ${putParams.ok ? putName.error : putParams.error}`, { isError: true });
      return;
    }
    setStatus(`Copied slot ${kronosNumber(source.index)} -> slot ${kronosNumber(target.index)}.`);
  } else if (sameList) {
    // Insert: relocate source to just before/after target, shifting the
    // intervening range -- one native call (EditorBridge::reorderSongEntry),
    // not one bridge round-trip per shifted slot.
    let toIndex = target.zone === "before" ? target.index : target.index + 1;
    toIndex = Math.min(toIndex, SETLIST_SONG_COUNT - 1);
    if (toIndex === source.index) return;  // no real move
    result = await window.reorderSongEntry(target.datasetId, target.setlistIndex, source.index, toIndex);
    if (!result.ok) {
      showToast(`Move failed: ${result.error}`, { isError: true });
      return;
    }
    setStatus(`Moved slot ${kronosNumber(source.index)} to position ${kronosNumber(toIndex)}.`);
  } else {
    result = await window.copyEntry(
      source.datasetId, source.setlistIndex, source.index,
      target.datasetId, target.setlistIndex, target.index
    );
    if (!result.ok) {
      showToast(`Copy failed: ${result.error}`, { isError: true });
      return;
    }
    setStatus(`Copied slot ${kronosNumber(source.index)} -> slot ${kronosNumber(target.index)}.`);
  }

  // Refresh every pane currently showing either affected dataset -- could be
  // 0, 1, or both panes (e.g. both pointed at the same dataset, exactly the
  // "shared gig Set List" case this refactor exists for).
  const affected = new Set([source.datasetId, target.datasetId]);
  for (const pane of Object.values(panes)) {
    if (affected.has(pane.getCurrentDatasetId())) await pane.refreshEntries();
  }
}

// Dragging a Program row onto another Program row COPIES its raw bytes into
// that slot -- same dataset or across two panes' different datasets both
// work (unlike Setlist's onDropEntry above, cross-dataset is fine here: a
// Program's bank/number isn't referenced by anything OUTSIDE its own
// dataset the way a Setlist slot or Combi Timbre is, so there's no dangling-
// reference risk to solve first -- see STATE.md's EXPLORATION section for
// why that distinction matters). The source is never touched -- only the
// destination's bridge call (EditorBridge::copyProgram(), which enforces
// matching engine type, an empty target slot, and no existing byte-
// identical duplicate in the destination dataset -- see its own doc
// comment) can reject the whole thing.
async function onDropProgram(source, target) {
  if (source.datasetId === target.datasetId && source.bank === target.bank && source.number === target.number) return;

  const result = await window.copyProgram(
    source.datasetId, source.bank, source.number,
    target.datasetId, target.bank, target.number
  );
  if (!result.ok) {
    showToast(`Copy failed: ${result.error}`, { isError: true });
    return;
  }
  setStatus(
    `Copied ${formatBankNumber({ isProgram: true, bank: source.bank, number: source.number })} -> ` +
      `${formatBankNumber({ isProgram: true, bank: target.bank, number: target.number })}.`
  );

  // Only the destination dataset's Programs table(s) can have changed -- a
  // copy never touches the source. Could be 0, 1, or both panes (e.g. both
  // pointed at the same destination dataset).
  for (const pane of Object.values(panes)) {
    if (pane.getCurrentDatasetId() === target.datasetId) await pane.refreshLibrary();
  }
}

// Shift+drag a Program row onto another SWAPS their entire content instead
// of copying (pane-program-editor.js's own dragover/drop handlers pick
// this vs. onDropProgram above based on ev.shiftKey at drop time, with a
// "move" vs. "copy" cursor hint during the drag itself) -- per direct
// request: copyProgram()'s own DuplicateExists guard makes a plain copy
// meaningless between two slots that are BOTH genuinely empty ("Init
// Program" -- every one is byte-identical to every other one, so copying
// one onto another always trips it, even though nothing is actually
// wrong), and a swap sidesteps that scenario entirely since it never
// creates a new copy of anything -- see EditorBridge::swapProgram()'s own
// doc comment. Same-dataset only (unlike onDropProgram's copy, which
// works across datasets too) -- PcgFile::swapPrograms() itself refuses a
// cross-dataset call, checked here first for a clearer message than
// letting the bridge round-trip just to reject it.
async function onSwapProgram(source, target) {
  if (source.datasetId !== target.datasetId) {
    showToast("Can't swap Programs between different datasets -- drag without Shift to copy instead.", { isError: true });
    return;
  }
  if (source.bank === target.bank && source.number === target.number) return;

  const result = await window.swapProgram(source.datasetId, source.bank, source.number, target.bank, target.number);
  if (!result.ok) {
    showToast(`Swap failed: ${result.error}`, { isError: true });
    return;
  }
  showToast(
    `Swapped ${formatBankNumber({ isProgram: true, bank: source.bank, number: source.number })} <-> ` +
      `${formatBankNumber({ isProgram: true, bank: target.bank, number: target.number })} -- ` +
      `repointed ${result.setlistRefsRepointed} Set List slot(s), ${result.combiRefsRepointed} Combi Timbre(s)` +
      (result.combiRefsSkipped ? `, skipped ${result.combiRefsSkipped} Combi Timbre(s) (unconfirmed bank)` : "")
  );

  // Both positions are in the SAME dataset (checked above) -- refresh every
  // pane currently showing it, same "which panes need refreshing" pattern
  // onDropProgram/onCopySetlist already use. A swap can repoint Set List
  // slots too (unlike a copy), so the Setlist tab needs refreshing as well
  // when that happened, not just the Library tables.
  for (const pane of Object.values(panes)) {
    if (pane.getCurrentDatasetId() === target.datasetId) {
      await pane.refreshLibrary();
      if (result.setlistRefsRepointed > 0) await pane.refreshEntries();
    }
  }
}

// "Copy all to opposite" (the Setlist panel's setlist-info row, pane.js) --
// overwrites every one of the opposite pane's current Set List's 128 slots
// with this pane's current Set List's content, in one native call
// (EditorBridge::copySetlistEntries -> PcgFile::copySetlist()). Only
// reachable when both panes already show the same dataset with two
// different Set Lists selected (pane.js's updateCopyButtonState() gates the
// button itself), so unlike onDropEntry above there's no cross-dataset case
// to reject here -- source.datasetId === target.datasetId always holds by
// construction.
async function onCopySetlist(source, target) {
  const result = await window.copySetlistEntries(source.datasetId, source.setlistIndex, target.setlistIndex);
  if (!result.ok) {
    showToast(`Copy Set List failed: ${result.error}`, { isError: true });
    return;
  }
  setStatus(`Copied Set List ${kronosNumber(source.setlistIndex)} onto Set List ${kronosNumber(target.setlistIndex)}.`);

  // Only the destination Set List's content changed -- refresh every pane
  // currently showing it (could be both, if a third pane existed; today
  // that's just "the opposite pane", but this mirrors onDropEntry's own
  // affected-panes refresh rather than assuming exactly one other pane).
  for (const pane of Object.values(panes)) {
    if (pane.getCurrentDatasetId() === target.datasetId) await pane.refreshEntries();
  }
}

document.querySelectorAll(".pane").forEach((root) => {
  const paneId = root.dataset.pane;
  panes[paneId] = createPane(paneId, root, {
    onDropEntry,
    onDropProgram,
    onSwapProgram,
    onCopySetlist,
    // Lazy on purpose -- at THIS point in the forEach loop, the opposite
    // pane may not exist in `panes` yet (both panes are created in the same
    // loop). Evaluated only when actually called (pane.js's
    // updateCopyButtonState()/copySetlistButton click), by which point both
    // panes are always present.
    getOpposite: () => panes[paneId === "A" ? "B" : "A"],
    log: setStatus,
    showToast,
  });
});

// "Swap panes" (topbar) -- purely a DOM reorder: physically swaps the two
// <section class="pane"> elements' positions in .panes, which is all Bulma's
// flex-based .columns grid needs to visually flip which side shows what.
// Deliberately NOT a data/state swap -- each pane's `paneId` ("A"/"B") stays
// attached to the same element and is only ever used as a label (status log
// prefixes, the cross-pane slot-edit lock in pane.js), never to decide
// left/right, so moving the element is enough and there's no hidden state
// anywhere else that would need to follow along.
function swapPanes() {
  const panesEl = document.querySelector(".panes");
  const sections = panesEl.querySelectorAll(".pane");
  if (sections.length !== 2) return;
  panesEl.insertBefore(sections[1], sections[0]);
}

document.querySelector(".swap-panes-button").addEventListener("click", swapPanes);

// Left-only/Both/Right-only pane visibility (topbar-adjacent toolbar,
// index.html) -- purely a display toggle via a class on .panes (see
// style.css's structural :first-of-type/:last-of-type rules, which key off
// visual position, not paneId, so this stays correct across swapPanes()
// either order). "both" is the default/no-class state. Single-select, same
// is-link-active-button convention as the bank-filter buttons
// (pane.js's renderBankFilterRow()) -- just three fixed buttons here,
// not data-driven, so no need for that helper's full generality.
const panesEl = document.querySelector(".panes");
const visibilityButtons = document.querySelectorAll(".pane-visibility-button");
function setPaneVisibility(visibility) {
  panesEl.classList.toggle("show-only-left", visibility === "left");
  panesEl.classList.toggle("show-only-right", visibility === "right");
  visibilityButtons.forEach((btn) => btn.classList.toggle("is-link", btn.dataset.visibility === visibility));
}
visibilityButtons.forEach((btn) => btn.addEventListener("click", () => setPaneVisibility(btn.dataset.visibility)));

refreshDatasets();  // so every pane's selectors have data as soon as the bridge is ready

// A single, global Open button (topbar) rather than one per pane -- opening
// a file isn't inherently "for" any particular pane (a dataset is decoupled
// from panes, see EditorBridge.h), so one control is enough. Once opened, it
// lands in the first empty pane (A checked before B) purely for convenience;
// if both panes already show something, the dataset still becomes available
// in either pane's selector via refreshDatasets() below, just not auto-shown.
const openFileButton = document.querySelector(".open-file-button");
const topbarLoading = document.querySelector(".topbar-loading");
const topbarLoadingText = document.querySelector(".topbar-loading-text");

openFileButton.addEventListener("click", async () => {
  // Genuinely blocking today -- showOpenFileDialog()'s runModal() is
  // native-modal (expected, normal dialog behavior), and once a path comes
  // back the read itself is still the same synchronous PcgFile::load() as
  // openFile() -- no chunking/progress reporting yet (see STATE.md's
  // EXPLORATION section's Phase 2 for that). So this spinner is mostly
  // cosmetic for now: the bridge call blocks the whole native side,
  // including the JS engine, so there's no guarantee the browser gets to
  // paint it before that block starts -- a real fix needs Phase 2's
  // backgrounded read.
  topbarLoadingText.textContent = "Loading...";
  topbarLoading.hidden = false;
  try {
    const result = await window.openFileDialog();
    if (result.cancelled) return;  // user closed the dialog -- not an error, nothing to log
    if (!result.ok) {
      showToast(result.error, { isError: true });
      return;
    }
    if (result.alreadyOpen) setStatus(`${result.displayName} is already open -- showing the existing dataset.`);
    await refreshDatasets();  // every pane's selector learns about the (possibly new) dataset first
    const targetPane = Object.values(panes).find((pane) => pane.isEmpty());
    if (targetPane) {
      await targetPane.loadDataset(result.datasetId, result.displayName);
    } else if (!result.alreadyOpen) {
      setStatus(`Opened ${result.displayName} -- pick it from a pane's dataset selector to view it (both panes already show something).`);
    }
  } catch (err) {
    showToast(String(err), { isError: true });
  } finally {
    topbarLoading.hidden = true;
  }
});

// Called from native code (src/main.cpp's own closeRequested handler, via
// evaluateJavascript()) the instant the user tries to close THIS window
// specifically -- the title-bar button, or Cmd+W -- AND at least one open
// dataset has unsaved changes -- native code already checked that part
// (EditorBridge::anyDatasetDirty(), across every dataset in every pane of
// every window, not just this one) before ever calling this, so this
// function's only job is showing the dialog and acting on the answer.
// window.confirmQuitAndClose() (bound per-window in main.cpp) is what
// actually lets the close through for real -- native code otherwise
// vetoes EVERY close attempt outright while closeRequested is set, so
// doing nothing here (the Cancel/dismiss path) correctly leaves the
// window exactly as it was, nothing lost.
//
// Deliberately NOT what fires for Cmd+Q / the app's own Quit menu item /
// an AppleScript `quit` Apple Event -- confirmed the hard way (2026-08-26,
// see STATE.md) that those go through a COMPLETELY SEPARATE native gate
// (-[NSApplication terminate:], macOS-only), which calls
// window.confirmAppQuitRequested() below instead.
//
// window.showConfirmDialog() (confirm-dialog.js), NOT window.confirm() --
// same reason pane.js's own Unload button uses it: WKWebView silently
// drops native JS confirm() dialogs under CHOC's WebView here.
window.confirmQuitRequested = async () => {
  const confirmed = await window.showConfirmDialog("You have unsaved changes. Quit without saving?", {
    confirmLabel: "Quit Without Saving",
    isDanger: true,
  });
  if (!confirmed) return;
  await window.confirmQuitAndClose();
};

// The app-level counterpart of confirmQuitRequested() above -- called from
// native code's own applicationShouldTerminate: delegate (main.cpp,
// macOS-only) for Cmd+Q / the Quit menu item / Dock "Quit" / an AppleScript
// `quit` Apple Event, once it's already established something is unsaved.
// Same dialog, same wording -- from the user's perspective this is the
// exact same question confirmQuitRequested() asks, just reached from a
// different native gesture. The two native-side replies are NOT
// symmetric, though: confirmAppQuitAndTerminate() ends the whole app (all
// windows, not just one), and the Cancel/dismiss path here MUST explicitly
// reply "don't terminate" (cancelAppQuitReply()) -- unlike
// confirmQuitRequested() above, where doing nothing is enough (nothing
// native is left waiting on an answer over there), a rejected
// applicationShouldTerminate: leaves Cocoa BLOCKED expecting exactly one
// reply, and never sending one would leave the app stuck looking like
// it's still quitting.
window.confirmAppQuitRequested = async () => {
  const confirmed = await window.showConfirmDialog("You have unsaved changes. Quit without saving?", {
    confirmLabel: "Quit Without Saving",
    isDanger: true,
  });
  if (confirmed) {
    await window.confirmAppQuitAndTerminate();
  } else {
    await window.cancelAppQuitReply();
  }
};

// The Settings (gear) button (index.html's topbar) -- only ever shown when
// the optional private companion module is actually linked into this
// build. `window.hasPrivateModule` is bound per-window by the private
// module's own registerPrivateEditorExtensions() (Sgx2Editor.cpp) --
// existing at all (not its return value) IS the "is this a private build"
// signal, same "does this binding exist" pattern pane-program-editor.js's
// openSgx2Editor() already uses for window.openSgx2EditorWindow, just
// gating a whole button's visibility here instead of one click's outcome.
// A public build never gets this binding, so the button stays hidden
// (index.html's own default) and nothing here ever runs. Toggles the MIDI
// Settings sidebar (midi-settings-panel.js) -- a plain, synchronous open/
// close, not a window.openXyz()-style async native call, since (2026-08-28,
// per direct feedback) this no longer opens a second native window at all.
if (typeof window.hasPrivateModule === "function") {
  const settingsButton = document.querySelector(".settings-button");
  settingsButton.hidden = false;
  settingsButton.addEventListener("click", () => window.toggleMidiSettingsPanel());
}
