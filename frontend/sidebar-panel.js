// Generic sliding side panel (2026-08-28) -- knows NOTHING about what it
// contains. Extracted after the third near-identical hand-rolled sidebar
// shell showed up in this app (combi-cross-dataset-panel.js first, then the
// Duplicates panel's own resolve-picker, then the MIDI Settings sidebar),
// per direct request ("make the sidebar generic and usable for all kinds of
// content... avoid tightly coupling between the different components...
// we will add new functionality soon") -- this is the shared base those
// features (the ones migrated onto it, see below) now build on, instead of
// each hand-rolling its own backdrop/open-close-state/two-step-reveal
// again.
//
// Deliberately NOT migrated onto this: combi-cross-dataset-panel.js, this
// app's first sidebar (lit-html PILOT, not plain DOM like this file and
// every one of its callers) -- it DOES use the same CSS shell this file
// renders into (`.sidebar-panel*`, style.css, renamed from
// `.cross-dataset-panel*` in the same pass that extracted this file), just
// still builds its own DOM via its own lit-html render() rather than
// calling createSidebarPanel() below. Converting it too is real, separate
// future work (a lit-html-vs-plain-DOM decision, not just "use the shared
// shell"), flagged here rather than done silently or left unmentioned.
//
// Usage: `const panel = createSidebarPanel(rootEl, { edge: "right" });`
// then `panel.open({ title, edge, build(bodyEl, footerEl) {...} })` to show
// it (plays the slide-in transition), `panel.update({ title })` to
// re-invoke the same `build` against fresh caller-owned state WITHOUT
// re-triggering that transition (the normal "something changed, redraw"
// path), and `panel.close()`. `rootEl` is an already-in-the-page empty
// container this function owns completely from then on (e.g.
// `#midiSettingsPanelRoot`, index.html) -- one call per container; two
// panels sharing one rootEl would fight over its contents.
//
// Mouse-drag resize (2026-09-20, direct request), built here rather than
// per-caller since every current user of this shell (MIDI Settings, the
// Duplicates resolve-picker, the cross-dataset tools panel) benefits
// identically -- a thin handle on the panel's INNER edge (opposite
// whichever side `edge` pins it to: left-edge handle for a right-docked
// panel, right-edge handle for a left-docked one), drag to resize. Sets
// `panelEl.style.width` directly, which -- being an inline style --
// overrides ANY class-based width (including a caller's own scoped
// default, e.g. index.html's `#crossDatasetDuplicatesPanelRoot
// .sidebar-panel` override) regardless of specificity, so this works the
// same way for every sidebar without each needing its own resize logic.
// Session-only, per direct scope (not asked for): the width resets to
// each panel's own CSS default the next time the app launches, not
// persisted anywhere.
//
// Wrapped in an IIFE, same reason combi-cross-dataset-panel.js/
// confirm-dialog.js already are (STATE.md entry 60) -- classic <script>
// tags on one page share ONE global lexical scope for let/const, so an
// unwrapped top-level declaration here could collide with some other
// file's own identically-named one in the future the same way
// litHtmlPromise once did.
(function () {

function createSidebarPanel(rootEl, { edge: defaultEdge = "right" } = {}) {
  let isOpenState = false;
  let currentBuild = null;
  let currentTitle = "";
  let currentEdge = defaultEdge;
  let els = null;

  function ensureEls() {
    if (els) return els;
    rootEl.innerHTML = `
      <div class="sidebar-panel-backdrop" hidden></div>
      <div class="sidebar-panel" hidden></div>
    `;
    const backdrop = rootEl.querySelector(".sidebar-panel-backdrop");
    const panelEl = rootEl.querySelector(".sidebar-panel");

    const header = document.createElement("div");
    header.className = "sidebar-panel-header";
    const titleEl = document.createElement("h2");
    titleEl.className = "sidebar-panel-title";
    const closeBtn = document.createElement("button");
    closeBtn.type = "button";
    closeBtn.className = "sidebar-panel-close";
    closeBtn.title = "Close";
    closeBtn.textContent = "✕";
    closeBtn.addEventListener("click", close);
    header.append(titleEl, closeBtn);

    const bodyEl = document.createElement("div");
    bodyEl.className = "sidebar-panel-body";
    const footerEl = document.createElement("div");
    footerEl.className = "sidebar-panel-footer";

    const resizeHandle = document.createElement("div");
    resizeHandle.className = "sidebar-panel-resize-handle";
    resizeHandle.addEventListener("mousedown", onResizeStart);

    panelEl.append(header, bodyEl, footerEl, resizeHandle);
    backdrop.addEventListener("click", close);

    els = { backdrop, panelEl, titleEl, bodyEl, footerEl, resizeHandle };
    return els;
  }

  // Drag-resize -- see this file's own top comment for why this lives here
  // (shared by every sidebar) rather than per-caller. `currentEdge` decides
  // which side grows the panel: dragging the handle LEFT widens a
  // right-docked panel (its own right edge stays pinned at `right: 0`);
  // dragging RIGHT widens a left-docked one.
  let resizeStartX = 0;
  let resizeStartWidth = 0;
  const MIN_WIDTH = 280;
  const MAX_WIDTH = 900;

  function onResizeStart(evt) {
    evt.preventDefault();
    resizeStartX = evt.clientX;
    resizeStartWidth = els.panelEl.getBoundingClientRect().width;
    document.addEventListener("mousemove", onResizeMove);
    document.addEventListener("mouseup", onResizeEnd);
  }

  function onResizeMove(evt) {
    const deltaX = evt.clientX - resizeStartX;
    const signedDelta = currentEdge === "right" ? -deltaX : deltaX;
    const maxAllowed = Math.min(MAX_WIDTH, window.innerWidth * 0.9);
    const newWidth = Math.max(MIN_WIDTH, Math.min(maxAllowed, resizeStartWidth + signedDelta));
    els.panelEl.style.width = `${newWidth}px`;
  }

  function onResizeEnd() {
    document.removeEventListener("mousemove", onResizeMove);
    document.removeEventListener("mouseup", onResizeEnd);
  }

  function renderContent() {
    const { titleEl, bodyEl, footerEl } = ensureEls();
    titleEl.textContent = currentTitle;
    bodyEl.innerHTML = "";
    footerEl.innerHTML = "";
    if (currentBuild) currentBuild(bodyEl, footerEl);
  }

  function open({ title = "", edge, build }) {
    currentTitle = title;
    currentEdge = edge || defaultEdge;
    currentBuild = build;
    isOpenState = true;

    const { backdrop, panelEl, resizeHandle } = ensureEls();
    backdrop.hidden = false;
    panelEl.hidden = false;
    panelEl.classList.remove("slide-from-left", "slide-from-right");
    panelEl.classList.add(`slide-from-${currentEdge}`);
    // The resize handle always sits on the panel's INNER edge -- opposite
    // whichever side `slide-from-*` just pinned it to above.
    resizeHandle.classList.remove("sidebar-panel-resize-handle-left", "sidebar-panel-resize-handle-right");
    resizeHandle.classList.add(currentEdge === "right" ? "sidebar-panel-resize-handle-left" : "sidebar-panel-resize-handle-right");
    renderContent();

    // Two-step reveal (unhide this frame, add the transition classes next
    // frame) -- without this the panel just snaps straight to its open
    // position instead of actually sliding, same trick this shell's own
    // predecessors already used.
    requestAnimationFrame(() => {
      backdrop.classList.add("is-visible");
      panelEl.classList.add("is-open");
    });
  }

  // Re-invokes the SAME build() against whatever the caller's own state
  // looks like now -- the normal "something changed, redraw" path, distinct
  // from open() (which also plays the slide-in transition and would look
  // wrong replayed on every keystroke/click inside an already-open panel).
  function update({ title } = {}) {
    if (!isOpenState) return;
    if (title !== undefined) currentTitle = title;
    renderContent();
  }

  function close() {
    if (!els) return;
    isOpenState = false;
    els.backdrop.classList.remove("is-visible");
    els.panelEl.classList.remove("is-open");
    els.backdrop.hidden = true;
    els.panelEl.hidden = true;
  }

  return { open, update, close, isOpen: () => isOpenState };
}

window.createSidebarPanel = createSidebarPanel;

})();
