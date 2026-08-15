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
//
// PILOT: this is the first component in the app rendered with lit-html
// (frontend/vendor/lit-html.js) instead of manual createElement()/innerHTML
// wiring, tried here specifically because this file is legacy-free and
// nothing else touches its DOM (grepped, confirmed) -- see
// frontend/vendor/LIT_HTML_VERSION.txt and STATE.md for how it went before
// using it anywhere else. `?attr=`/`@event=` below are lit-html's own
// boolean-attribute/event-listener binding syntax, not real HTML.

const combiCrossDatasetPanelRoot = document.getElementById("combiCrossDatasetPanelRoot");

// looksLikeEmptyProgramName() is a shared global now (pane.js, 2026-08-15)
// -- used below for the per-bank slot dropdown, same as everywhere else in
// the app that needs to recognize a genuinely-empty Program slot.

// Lazily loads lit-html the same way pane-setlist-editor.js's
// loadSlotCodecs() loads the SBK1 codecs -- a dynamic import() works from
// this plain (non-module) script without converting index.html's scripts to
// type="module". Cached in one shared promise so re-opening the panel
// doesn't re-trigger a second import. Kicked off immediately below (not
// deferred to first drag) so the panel's closed-shell markup exists in the
// DOM from page load, same guarantee the old synchronous innerHTML template
// gave -- nothing else in the app reaches into this file's DOM (grepped,
// confirmed), so the brief async gap before that first render is safe.
let lit = null;
let litHtmlPromise = null;
function loadLitHtml() {
  if (!litHtmlPromise) {
    litHtmlPromise = import("./vendor/lit-html.js").then((mod) => {
      lit = mod;
      return mod;
    });
  }
  return litHtmlPromise;
}

// All panel state lives here, not in the DOM -- lit-html re-renders the
// same nodes from this object instead of the old imperative classList/
// disabled/innerHTML mutation. `mounted` drives the `hidden` attribute,
// `open` drives the `is-open`/`is-visible` transition classes; they're
// separate (not one flag) so opening/closing can still do the same two-step
// reveal the old code did (unhide on one frame, add the transition classes
// on the next; on close, remove the classes and only re-hide once the CSS
// transition actually finishes) -- collapsing them into a single flag would
// skip the slide animation.
let panelState = { mounted: false, open: false, session: null };

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

function renderRoot() {
  const { mounted, open, session } = panelState;

  if (!session) {
    lit.render(
      lit.html`
        <div class="cross-dataset-panel-backdrop" hidden></div>
        <div class="cross-dataset-panel" hidden></div>
      `,
      combiCrossDatasetPanelRoot
    );
    return;
  }

  try {
    renderSession(mounted, open, session);
  } catch (err) {
    // A render bug here must never just leave the panel silently blank --
    // that's exactly the failure mode this project's own "verify by
    // actually running it" norm exists to catch, and this codebase has no
    // browser devtools access during a live test session to spot a
    // swallowed exception otherwise. Surface it directly instead.
    console.error("[combi-cross-dataset-panel] render failed:", err);
    lit.render(
      lit.html`
        <div class="cross-dataset-panel-backdrop is-visible" ?hidden=${!mounted}></div>
        <div class="cross-dataset-panel slide-from-right is-open" ?hidden=${!mounted}>
          <div class="cross-dataset-panel-header">
            <h2 class="cross-dataset-panel-title">Something went wrong</h2>
          </div>
          <div class="cross-dataset-panel-body">
            <div class="cross-dataset-unresolved-empty">
              The panel failed to render: ${err && err.message ? err.message : String(err)}
            </div>
          </div>
          <div class="cross-dataset-panel-footer">
            <button class="button is-small cross-dataset-cancel" type="button" @click=${() => closeSession()}>Close</button>
          </div>
        </div>
      `,
      combiCrossDatasetPanelRoot
    );
  }
}

function renderSession(mounted, open, session) {
  const { analysis, selections, dstPrograms, edge, sourceLabel, targetLabel } = session;

  const applyDisabled = analysis.unresolved.some((u) => {
    if (u.candidateBanks.length === 0) return true; // nothing to select -- can never be resolved
    return !selections.has(`${u.srcBank}-${u.srcNumber}`);
  });

  // One row per Timbre in analysis.dependencies -- found ones grayed out/
  // disabled (informational: nothing to decide, this Program's already in
  // the destination), not-found ones just noted as "needs a destination
  // below" (the actual bank-picker lives in the unresolved section, grouped
  // by unique Program so several Timbres sharing one Program share one
  // decision).
  // Timbres whose raw bank code isn't a confirmed Program bank AND isn't one
  // of the confirmed permanently-indexless hardware codes either (GM/G(n)/
  // g(n) -- see PcgFile.h's UnmappableTimbre doc comment) -- this project
  // genuinely doesn't know what these reference yet. Apply still copies
  // their raw bytes through unchanged (same as GM), so this is a warning,
  // not a blocker: `applyDisabled` below deliberately does NOT look at this
  // list. Reported directly, 2026-08-15: these were being silently carried
  // over with no indication anything was uncertain.
  const unmappableRows = (analysis.unmappableTimbres || []).map(
    (u) => lit.html`
      <div class="cross-dataset-unmappable-row">
        Timbre ${u.timbreIndex + 1}: reference not recognized (raw code ${u.rawBankCode}) -- will be copied as-is;
        may not point at a valid Program in the destination.
      </div>
    `
  );

  const depRows = analysis.dependencies.map(
    (dep) => lit.html`
      <div class="cross-dataset-dep-row ${dep.found ? "is-found" : ""}">
        <span class="cross-dataset-dep-label">Timbre ${dep.timbreIndex + 1}: ${dep.name || "(unnamed)"}</span>
        <span class="cross-dataset-dep-status">${
          dep.found
            ? `already in destination (${formatBankNumber({ isProgram: true, bank: dep.foundBank, number: dep.foundNumber })})`
            : "needs a destination bank below"
        }</span>
      </div>
    `
  );

  // One two-column row-editor per unique unresolved Program -- one table
  // row per eligible destination bank (matching engine type, already
  // filtered server-side into `candidateBanks`): column 1 is the bank ID,
  // column 2 is a dropdown of that bank's own empty ("Init Program") slots,
  // built from `dstPrograms` (a plain window.listPrograms(dstDatasetId)
  // snapshot fetched once when the panel opened -- candidateBanks only ever
  // told us WHICH banks have room, not the actual free slot numbers).
  // Picking a slot in any one bank's dropdown is this Program's selection
  // (stored as `{bank, number}` in the session's own selections Map, keyed
  // by source bank/number) -- true single-select ACROSS the whole group of
  // per-bank dropdowns, like a radio-button group spanning several
  // controls, not independent selects. Two real bugs fixed here 2026-08-15,
  // reported directly:
  //  1. Resetting the OTHER banks' dropdowns back to their placeholder used
  //     `?selected` on each <option> -- a lit-html boolean-ATTRIBUTE
  //     binding, which only affects an <option>'s initial parse state, not
  //     a live <select>'s actual displayed value once the browser has
  //     already rendered it (attribute vs. the real `.value` PROPERTY are
  //     different things for a <select> that already exists in the DOM).
  //     So picking a slot in bank B never visually cleared bank A's own
  //     already-rendered dropdown, even though `selections` itself was
  //     already correctly single-valued underneath -- looked like "two
  //     banks both have a value chosen" when only one really did. Fixed by
  //     binding the <select>'s own `.value` PROPERTY directly (a lit-html
  //     property binding, the leading `.`), which DOES force the browser's
  //     actual displayed selection to match on every render, regardless of
  //     prior user interaction.
  //  2. Per direct request: once one bank has a selection, every OTHER
  //     bank's dropdown for this SAME Program is now also disabled (not
  //     just visually reset) until the selection is cleared -- true radio-
  //     group behavior, not just several independent selects that happen
  //     to reset each other.
  // If a genuinely empty bank turns out to have zero actual free slots in
  // `dstPrograms` (stale between analyze() and now -- e.g. a write from the
  // opposite pane), its dropdown is simply empty (disabled with a
  // placeholder) rather than pretending a slot exists; applyCombiCrossDatasetCopy()
  // re-validates the chosen slot fresh either way, so this is a UX nicety,
  // not the only safety net.
  const unresolvedRows = analysis.unresolved.map((program) => {
    const key = `${program.srcBank}-${program.srcNumber}`;
    const selected = selections.get(key);
    return lit.html`
      <div class="cross-dataset-unresolved-row">
        <div class="cross-dataset-unresolved-label">
          ${program.name || "(unnamed)"} (${formatBankNumber(
      { isProgram: true, bank: program.srcBank, number: program.srcNumber },
      program.bankType
    )})
        </div>
        ${
          program.candidateBanks.length === 0
            ? lit.html`<div class="cross-dataset-unresolved-empty">No free bank available in the destination -- cancel and free up a slot first.</div>`
            : lit.html`<div class="cross-dataset-slot-picker">
                ${program.candidateBanks.map((bank) => {
                  const freeSlots = dstPrograms
                    .filter((p) => p.bank === bank && looksLikeEmptyProgramName(p.name))
                    .sort((a, b) => a.number - b.number);
                  const isSelectedBank = selected != null && selected.bank === bank;
                  // Disabled if this bank genuinely has nothing to offer, OR
                  // a DIFFERENT bank already holds this Program's one
                  // allowed selection (the radio-group behavior).
                  const isDisabled = freeSlots.length === 0 || (selected != null && !isSelectedBank);
                  const options = [lit.html`<option value="">${freeSlots.length === 0 ? "-- no free slot --" : "-- choose a slot --"}</option>`];
                  for (const p of freeSlots) {
                    options.push(lit.html`<option value=${p.number}>${kronosNumber(p.number)} (${p.name || "empty"})</option>`);
                  }
                  return lit.html`
                    <div class="cross-dataset-slot-picker-row">
                      <span class="cross-dataset-slot-picker-bank">${PROGRAM_BANK_NAMES[bank] || String(bank)}</span>
                      <select
                        class="cross-dataset-slot-picker-select"
                        ?disabled=${isDisabled}
                        .value=${isSelectedBank ? String(selected.number) : ""}
                        @change=${(ev) => {
                          if (ev.target.value === "") selections.delete(key);
                          else selections.set(key, { bank, number: parseInt(ev.target.value, 10) });
                          renderRoot();
                        }}
                      >
                        ${options}
                      </select>
                    </div>
                  `;
                })}
              </div>`
        }
      </div>
    `;
  });

  const doClose = () => closeSession();
  const doApply = async () => {
    const placements = analysis.unresolved
      .filter((u) => selections.has(`${u.srcBank}-${u.srcNumber}`))
      .map((u) => {
        const chosen = selections.get(`${u.srcBank}-${u.srcNumber}`);
        return { srcBank: u.srcBank, srcNumber: u.srcNumber, dstBank: chosen.bank, dstNumber: chosen.number };
      });
    const applied = await session.onApply(placements);
    if (applied) doClose();
  };

  lit.render(
    lit.html`
      <div
        class="cross-dataset-panel-backdrop ${open ? "is-visible" : ""}"
        ?hidden=${!mounted}
        @click=${doClose}
      ></div>
      <div class="cross-dataset-panel slide-from-${edge} ${open ? "is-open" : ""}" ?hidden=${!mounted}>
        <div class="cross-dataset-panel-header">
          <h2 class="cross-dataset-panel-title">Copy ${sourceLabel} &rarr; ${targetLabel}</h2>
          <button class="cross-dataset-panel-close" type="button" title="Cancel" @click=${doClose}>&#10005;</button>
        </div>
        <div class="cross-dataset-panel-body">
          ${
            analysis.dependencies.length > 0
              ? lit.html`<h3 class="cross-dataset-section-heading">Timbres</h3>
                ${depRows}`
              : lit.nothing
          }
          ${
            unmappableRows.length > 0
              ? lit.html`<h3 class="cross-dataset-section-heading">Unrecognized references</h3>
                ${unmappableRows}`
              : lit.nothing
          }
          <h3 class="cross-dataset-section-heading">Choose a destination bank</h3>
          ${unresolvedRows}
        </div>
        <div class="cross-dataset-panel-footer">
          <button class="button is-small cross-dataset-cancel" type="button" @click=${doClose}>Cancel</button>
          <button class="button is-small is-link cross-dataset-apply" type="button" ?disabled=${applyDisabled} @click=${doApply}>
            Apply
          </button>
        </div>
      </div>
    `,
    combiCrossDatasetPanelRoot
  );
}

function openSession(session) {
  panelState = { mounted: true, open: false, session };
  renderRoot();
  // Same next-frame trick the old code used for its fade-in -- both classes
  // present on the same frame means no visible transition at all.
  requestAnimationFrame(() => {
    panelState = { ...panelState, open: true };
    renderRoot();
  });
}

function closeSession() {
  panelState = { ...panelState, open: false };
  renderRoot();
  const panelEl = combiCrossDatasetPanelRoot.querySelector(".cross-dataset-panel");
  panelEl.addEventListener(
    "transitionend",
    () => {
      panelState = { mounted: false, open: false, session: null };
      renderRoot();
    },
    { once: true }
  );
}

// The one entry point pane-combi-editor.js's drop handler calls for a
// cross-dataset empty-slot drop. Always runs the read-only analysis first;
// applies immediately with no panel at all if nothing needs a decision
// (every dependency already present in the destination), otherwise opens
// the panel above.
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
    showToast(analysis.error, { isError: true });
    return;
  }

  if (analysis.unresolved.length === 0) {
    const result = await window.applyCombiCrossDatasetCopy(srcDatasetId, srcBank, srcNumber, dstDatasetId, dstBank, dstNumber, []);
    if (!result.ok) {
      showToast(result.error, { isError: true });
      return;
    }
    showToast(`Copied ${sourceLabel} -> ${targetLabel}.`);
    await onApplied();
    return;
  }

  // The per-bank slot dropdowns need each candidate bank's ACTUAL free slot
  // numbers, not just "this bank has room" (candidateBanks) -- a plain
  // listPrograms() snapshot of the destination, same call the Programs
  // table itself uses. Fetched once per panel-open, not per Program/bank
  // row, and not re-fetched on every re-render (selecting a slot doesn't
  // change what's free elsewhere) -- applyCombiCrossDatasetCopy() is the
  // real authority at Apply time regardless, this is just what populates
  // the dropdowns.
  const dstPrograms = await window.listPrograms(dstDatasetId);

  await loadLitHtml();
  openSession({
    analysis,
    dstPrograms,
    selections: new Map(), // srcBank-srcNumber -> chosen {bank, number}
    edge: slideDirectionFor(dstPaneEl),
    sourceLabel,
    targetLabel,
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
        showToast(result.error, { isError: true });
        return false;
      }
      showToast(`Copied ${sourceLabel} -> ${targetLabel} -- placed ${placements.length} Program(s).`);
      await onApplied();
      return true;
    },
  });
}

loadLitHtml().then(renderRoot);
