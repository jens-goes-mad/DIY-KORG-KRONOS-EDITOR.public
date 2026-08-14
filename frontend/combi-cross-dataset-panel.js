// Cross-dataset Combi copy: the sliding side panel that appears when the
// analysis (PcgFile::analyzeCombiCrossDatasetCopy(), via
// window.analyzeCombiCrossDatasetCopy()) finds at least one of the dragged
// Combi's real Program dependencies not already present in the destination
// dataset -- lets the user pick a destination bank per unresolved Program
// (a radio-button-bar per Program, same `.is-link`-active look
// pane.js's renderBankFilterRow()/createSelectControlRow() already use
// elsewhere, just single-select instead of a toggle filter) before writing
// anything. If every dependency is ALREADY present, this file's own
// startCombiCrossDatasetCopy() applies immediately and this panel never
// appears at all -- same "write immediately, no confirmation" convention
// every other edit in this app follows; the panel is only for the case that
// genuinely needs a decision.
//
// Mounted once at the app level (`#combiCrossDatasetPanelRoot` in
// index.html, alongside `toastContainer`) -- this spans both panes (the
// Combi being copied lives in one pane's dataset, the destination in the
// other), so it isn't owned by either pane's own closure, same reasoning as
// app.js's onDropProgram()/onCopySetlist(). pane-combi-editor.js's drop
// handler calls startCombiCrossDatasetCopy() below as a plain shared
// top-level function (same cross-file access pattern this app already uses
// for dropZoneForEvent()/formatBankNumber()/etc.) -- pane-combi-editor.js
// loads before this file in index.html, but that only matters at CALL time,
// long after every script has finished loading, so the load order is fine.

const combiCrossDatasetPanelRoot = document.getElementById("combiCrossDatasetPanelRoot");

combiCrossDatasetPanelRoot.innerHTML = `
  <div class="cross-dataset-panel-backdrop" hidden></div>
  <div class="cross-dataset-panel" hidden>
    <div class="cross-dataset-panel-header">
      <h2 class="cross-dataset-panel-title"></h2>
      <button class="cross-dataset-panel-close" type="button" title="Cancel">&#10005;</button>
    </div>
    <div class="cross-dataset-panel-body"></div>
    <div class="cross-dataset-panel-footer">
      <button class="button is-small cross-dataset-cancel" type="button">Cancel</button>
      <button class="button is-small is-link cross-dataset-apply" type="button" disabled>Apply</button>
    </div>
  </div>
`;

const crossDatasetBackdrop = combiCrossDatasetPanelRoot.querySelector(".cross-dataset-panel-backdrop");
const crossDatasetPanel = combiCrossDatasetPanelRoot.querySelector(".cross-dataset-panel");
const crossDatasetTitle = combiCrossDatasetPanelRoot.querySelector(".cross-dataset-panel-title");
const crossDatasetBody = combiCrossDatasetPanelRoot.querySelector(".cross-dataset-panel-body");
const crossDatasetCloseButton = combiCrossDatasetPanelRoot.querySelector(".cross-dataset-panel-close");
const crossDatasetCancelButton = combiCrossDatasetPanelRoot.querySelector(".cross-dataset-cancel");
const crossDatasetApplyButton = combiCrossDatasetPanelRoot.querySelector(".cross-dataset-apply");

// Which edge (left/right) the panel is currently anchored to -- set fresh
// each time it opens (slideDirectionFor() below), since which pane is the
// destination can differ drag to drag.
function setSlideEdge(edge) {
  crossDatasetPanel.classList.toggle("slide-from-left", edge === "left");
  crossDatasetPanel.classList.toggle("slide-from-right", edge === "right");
}

// The panel slides in from the screen edge nearest wherever the Combi was
// actually dropped -- read from the destination pane's own DOM position
// (`.pane:first-of-type` vs `:last-of-type`), NOT from paneId ("A"/"B"):
// swapPanes() already means paneId and visual side aren't the same thing
// (see style.css's own comment on this), so this must ask the DOM, not
// trust which shell created the pane.
function slideDirectionFor(dstPaneEl) {
  if (!dstPaneEl) return "right";
  return dstPaneEl.matches(".pane:last-of-type") ? "right" : "left";
}

function openPanel(edge) {
  setSlideEdge(edge);
  crossDatasetBackdrop.hidden = false;
  crossDatasetPanel.hidden = false;
  // Same next-frame trick as showToast()'s own fade-in -- both classes
  // present on the same frame means no visible transition at all.
  requestAnimationFrame(() => {
    crossDatasetBackdrop.classList.add("is-visible");
    crossDatasetPanel.classList.add("is-open");
  });
}

function closePanel() {
  crossDatasetBackdrop.classList.remove("is-visible");
  crossDatasetPanel.classList.remove("is-open");
  const onEnd = () => {
    crossDatasetBackdrop.hidden = true;
    crossDatasetPanel.hidden = true;
  };
  crossDatasetPanel.addEventListener("transitionend", onEnd, { once: true });
}

// One row per Timbre in analysis.dependencies -- found ones grayed out/
// disabled (informational: nothing to decide, this Program's already in the
// destination), not-found ones just noted as "needs a destination below"
// (the actual bank-picker lives in the unresolved section, grouped by
// unique Program so several Timbres sharing one Program share one decision).
function buildDependencyRow(dep) {
  const row = document.createElement("div");
  row.className = "cross-dataset-dep-row" + (dep.found ? " is-found" : "");
  const label = document.createElement("span");
  label.className = "cross-dataset-dep-label";
  label.textContent = `Timbre ${dep.timbreIndex + 1}: ${dep.name || "(unnamed)"}`;
  const status = document.createElement("span");
  status.className = "cross-dataset-dep-status";
  status.textContent = dep.found
    ? `already in destination (${formatBankNumber({ isProgram: true, bank: dep.foundBank, number: dep.foundNumber })})`
    : "needs a destination bank below";
  row.append(label, status);
  return row;
}

// One radio-button-bar per unique unresolved Program -- clicking a bank
// button selects it (Bulma's `.is-link`, deselecting any other button in
// the SAME row, unlike renderBankFilterRow()'s own independent multi-toggle
// semantics) and reports the choice via `onSelect`, used to gate the Apply
// button and build the final `placements` array.
function buildUnresolvedRow(program, onSelect) {
  const row = document.createElement("div");
  row.className = "cross-dataset-unresolved-row";

  const label = document.createElement("div");
  label.className = "cross-dataset-unresolved-label";
  label.textContent = `${program.name || "(unnamed)"} (${formatBankNumber({ isProgram: true, bank: program.srcBank, number: program.srcNumber }, program.bankType)})`;
  row.appendChild(label);

  if (program.candidateBanks.length === 0) {
    const none = document.createElement("div");
    none.className = "cross-dataset-unresolved-empty";
    none.textContent = "No free bank available in the destination -- cancel and free up a slot first.";
    row.appendChild(none);
    return row;
  }

  const bar = document.createElement("div");
  bar.className = "bank-filter-row";
  const buttons = [];
  for (const bank of program.candidateBanks) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "button is-small bank-filter-button";
    btn.textContent = PROGRAM_BANK_NAMES[bank] || String(bank);
    btn.addEventListener("click", () => {
      for (const b of buttons) b.classList.remove("is-link");
      btn.classList.add("is-link");
      onSelect(bank);
    });
    buttons.push(btn);
    bar.appendChild(btn);
  }
  row.appendChild(bar);
  return row;
}

// Renders the panel's full body for one analysis result, wires up the
// Apply/Cancel buttons, and returns nothing -- state (selections, resolved
// once every unresolved Program has a bank) lives in this closure only for
// as long as the panel is open for THIS one drag; a fresh call rebuilds it
// from scratch for the next one.
function renderPanel({ analysis, sourceLabel, targetLabel, dstPaneEl, onApply }) {
  crossDatasetTitle.textContent = `Copy ${sourceLabel} -> ${targetLabel}`;
  crossDatasetBody.innerHTML = "";

  if (analysis.dependencies.length > 0) {
    const timbresHeading = document.createElement("h3");
    timbresHeading.className = "cross-dataset-section-heading";
    timbresHeading.textContent = "Timbres";
    crossDatasetBody.appendChild(timbresHeading);
    for (const dep of analysis.dependencies) crossDatasetBody.appendChild(buildDependencyRow(dep));
  }

  const unresolvedHeading = document.createElement("h3");
  unresolvedHeading.className = "cross-dataset-section-heading";
  unresolvedHeading.textContent = "Choose a destination bank";
  crossDatasetBody.appendChild(unresolvedHeading);

  // srcBank-srcNumber -> chosen dstBank -- every unresolved Program must
  // have an entry (a real number, not just "present") before Apply enables.
  const selections = new Map();
  const updateApplyState = () => {
    crossDatasetApplyButton.disabled = analysis.unresolved.some((u) => {
      if (u.candidateBanks.length === 0) return true;  // nothing to select -- can never be resolved
      return !selections.has(`${u.srcBank}-${u.srcNumber}`);
    });
  };
  for (const program of analysis.unresolved) {
    crossDatasetBody.appendChild(
      buildUnresolvedRow(program, (dstBank) => {
        selections.set(`${program.srcBank}-${program.srcNumber}`, dstBank);
        updateApplyState();
      })
    );
  }
  updateApplyState();

  const cleanupAndClose = () => {
    crossDatasetCloseButton.onclick = null;
    crossDatasetCancelButton.onclick = null;
    crossDatasetBackdrop.onclick = null;
    crossDatasetApplyButton.onclick = null;
    closePanel();
  };
  crossDatasetCloseButton.onclick = cleanupAndClose;
  crossDatasetCancelButton.onclick = cleanupAndClose;
  crossDatasetBackdrop.onclick = cleanupAndClose;
  crossDatasetApplyButton.onclick = async () => {
    const placements = analysis.unresolved
      .filter((u) => selections.has(`${u.srcBank}-${u.srcNumber}`))
      .map((u) => ({ srcBank: u.srcBank, srcNumber: u.srcNumber, dstBank: selections.get(`${u.srcBank}-${u.srcNumber}`) }));
    const applied = await onApply(placements);
    if (applied) cleanupAndClose();
  };

  openPanel(slideDirectionFor(dstPaneEl));
}

// The one entry point pane-combi-editor.js's drop handler calls for a
// cross-dataset empty-slot drop. Always runs the read-only analysis first;
// applies immediately with no panel at all if nothing needs a decision
// (every dependency already present in the destination), otherwise renders
// and opens the panel above.
async function startCombiCrossDatasetCopy({
  srcDatasetId,
  srcBank,
  srcNumber,
  dstDatasetId,
  dstBank,
  dstNumber,
  dstPaneEl,
  sourceLabel,
  targetLabel,
  onApplied,
}) {
  const analysis = await window.analyzeCombiCrossDatasetCopy(srcDatasetId, srcBank, srcNumber, dstDatasetId, dstBank, dstNumber);
  if (!analysis.ok) {
    showToast(analysis.error);
    return;
  }

  if (analysis.unresolved.length === 0) {
    const result = await window.applyCombiCrossDatasetCopy(srcDatasetId, srcBank, srcNumber, dstDatasetId, dstBank, dstNumber, []);
    if (!result.ok) {
      showToast(result.error);
      return;
    }
    showToast(`Copied ${sourceLabel} -> ${targetLabel}.`);
    await onApplied();
    return;
  }

  renderPanel({
    analysis,
    sourceLabel,
    targetLabel,
    dstPaneEl,
    onApply: async (placements) => {
      const result = await window.applyCombiCrossDatasetCopy(
        srcDatasetId,
        srcBank,
        srcNumber,
        dstDatasetId,
        dstBank,
        dstNumber,
        placements
      );
      if (!result.ok) {
        showToast(result.error);
        return false;
      }
      showToast(`Copied ${sourceLabel} -> ${targetLabel} -- placed ${placements.length} Program(s).`);
      await onApplied();
      return true;
    },
  });
}
