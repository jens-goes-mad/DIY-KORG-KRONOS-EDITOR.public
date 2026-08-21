// Generic Yes/No confirmation dialog -- replaces window.confirm(), which
// silently does nothing in this app's CHOC/WKWebView shell. Reported
// directly, 2026-08-15: Unload's "unsaved changes" warning never appeared
// for a dirty dataset, no dialog, no error -- the click just did nothing.
// Root cause: on macOS, WKWebView only shows a native JS confirm()/alert()
// dialog if its UIDelegate implements
// `runJavaScriptConfirmPanelWithMessage:initiatedByFrame:completionHandler:`
// -- CHOC's WebView (third_party/choc/choc/gui/choc_WebView.h) registers a
// UIDelegate for the open-file panel
// (`runOpenPanelWithParameters:initiatedByFrame:completionHandler:`) but
// never implements that method, so WKWebView just drops the call: confirm()
// resolves immediately to undefined (falsy), and pane.js's
// `if (!confirmed) return;` silently no-opped every single time, with
// nothing visibly wrong. Fixed by never relying on a native dialog anywhere
// in this app -- an in-page Bulma `.modal` (frontend/vendor/bulma.min.css
// already ships the CSS, no custom styling needed here) rendered with
// lit-html, same pilot pattern as combi-cross-dataset-panel.js.
//
// Mounted once at the app level (`#confirmDialogRoot` in index.html), same
// reasoning as toastContainer/combiCrossDatasetPanelRoot -- a confirm can be
// triggered from either pane, so it isn't owned by either pane's own DOM.
//
// Wrapped in an IIFE (2026-08-21, bug fix) -- see combi-cross-dataset-panel.js's
// own note on why: this file's `let lit`/`let litHtmlPromise` collided with
// that file's identically-named top-level ones (classic <script> tags share
// one global scope), and since this file loads AFTER it in index.html, THIS
// file was the one that threw a SyntaxError and never ran at all --
// window.showConfirmDialog has apparently never actually existed. Only
// showConfirmDialog() is genuinely public (called via `window.` from other
// files already); everything else here was always private in practice.
(function () {

const confirmDialogRoot = document.getElementById("confirmDialogRoot");

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

let dialogState = null; // { message, confirmLabel, cancelLabel, isDanger, resolve } while open, else null

function renderDialog() {
  if (!dialogState) {
    lit.render(lit.html``, confirmDialogRoot);
    return;
  }
  const { message, confirmLabel, cancelLabel, isDanger } = dialogState;
  lit.render(
    lit.html`
      <div class="modal is-active confirm-dialog-modal">
        <div class="modal-background" @click=${() => settle(false)}></div>
        <div class="modal-card">
          <header class="modal-card-head">
            <p class="modal-card-title">Confirm</p>
            <button class="delete" aria-label="close" @click=${() => settle(false)}></button>
          </header>
          <section class="modal-card-body">${message}</section>
          <footer class="modal-card-foot">
            <button
              class="button is-small ${isDanger ? "is-danger" : "is-link"}"
              type="button"
              @click=${() => settle(true)}
            >${confirmLabel}</button>
            <button class="button is-small" type="button" @click=${() => settle(false)}>${cancelLabel}</button>
          </footer>
        </div>
      </div>
    `,
    confirmDialogRoot
  );
}

function settle(result) {
  const resolve = dialogState ? dialogState.resolve : null;
  dialogState = null;
  renderDialog();
  if (resolve) resolve(result);
}

document.addEventListener("keydown", (ev) => {
  if (!dialogState) return;
  if (ev.key === "Escape") settle(false);
  else if (ev.key === "Enter") settle(true);
});

// Resolves true (confirmed) or false (cancelled/dismissed). `isDanger`
// switches the confirm button to Bulma's red "is-danger" color, for
// destructive confirmations (e.g. Unload with unsaved changes).
async function showConfirmDialog(message, { confirmLabel = "OK", cancelLabel = "Cancel", isDanger = false } = {}) {
  await loadLitHtml();
  return new Promise((resolve) => {
    dialogState = { message, confirmLabel, cancelLabel, isDanger, resolve };
    renderDialog();
  });
}

window.showConfirmDialog = showConfirmDialog;

})();
