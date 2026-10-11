// SetlistPrint: turns one Set List (slot names + comments) into a
// self-contained, printable HTML leadsheet -- A4 portrait sheets, each
// holding two A4-landscape pages scaled to A5 and stacked on top of each
// other (cut line in the middle), the layout the project owner previously
// built by hand in LibreOffice Calc and then re-exported "2 pages per
// sheet". Printing/saving as PDF is left to the browser's own print
// dialog, so this file needs no PDF library at all.
//
// Same split as the setlist-editor-* family: pure functions (no DOM) that
// the headless setlist-print.test.js covers, plus buildPrintDocument(),
// which only builds a string. The one step that genuinely needs a browser
// -- measuring each row's real rendered height, because comments wrap and
// row heights vary a lot -- runs INSIDE the generated document on load
// (packRows() below, embedded via its own source), so the rows are packed
// with the fonts of the browser that actually prints them.
//
// Marker slots (setlist-markers.js): "---- SET BREAK ----" ends a set
// (printed as a grey bar, next set on a new page); "---- GIG 2025-10-28 ----"
// puts the gig date into every page header (also recognized in the Set
// List's own name).

import { parseGigDate, isSetBreakName } from "./setlist-markers.js";

export const PAGES_PER_SHEET = 2;

// setlist: { name, slots: [{ index, name, comment }] } -- slots in Kronos
// order. isEmpty(name): the caller's own "unused slot" test (the app passes
// pane.js's looksLikeEmptySetlistName(), so blank and "- Init Setlist -"
// slots are recognized in exactly one place).
// Returns { title, gigDate, rows }, rows being
//   { type: "song",  slot, name, comment, set }   or
//   { type: "break", slot, set }                  (set = the set it ends)
// The GIG marker slot itself is not a row; its date goes to gigDate.
export function buildPrintModel(setlist, { isEmpty }) {
  let gigDate = parseGigDate(setlist.name);
  const rows = [];
  let set = 1;
  for (const slot of setlist.slots) {
    if (isEmpty(slot.name)) continue;
    const slotGig = parseGigDate(slot.name);
    if (slotGig) {
      gigDate = slotGig;
      continue;
    }
    if (isSetBreakName(slot.name)) {
      rows.push({ type: "break", slot: slot.index, set });
      set++;
      continue;
    }
    rows.push({ type: "song", slot: slot.index, name: slot.name, comment: slot.comment || "", set });
  }
  return { title: setlist.name, gigDate, rows };
}

// Greedy packing: heights[i] is row i's rendered height, `available` one
// page's room below its header. Returns pages as arrays of row indexes, in
// order, never splitting a row. A row taller than a whole page still gets
// a page of its own (clipped there by .page's overflow:hidden) rather than
// being dropped. pageBreakAfter[i] true ends the page after row i -- used
// after every SET BREAK row, so each set starts on a fresh page and every
// page header names the right set.
// Must stay self-contained: buildPrintDocument() embeds its source text.
export function packRows(heights, available, pageBreakAfter = []) {
  const pages = [];
  let page = [];
  let used = 0;
  for (let i = 0; i < heights.length; i++) {
    if (page.length > 0 && used + heights[i] > available) {
      pages.push(page);
      page = [];
      used = 0;
    }
    page.push(i);
    used += heights[i];
    if (pageBreakAfter[i] && i < heights.length - 1) {
      pages.push(page);
      page = [];
      used = 0;
    }
  }
  if (page.length > 0) pages.push(page);
  return pages;
}

function escapeHtml(text) {
  return String(text).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
}

// JSON that's safe inside <script>: "</script>" or "<!--" in a comment
// can't end the element early.
function scriptJson(value) {
  return JSON.stringify(value).replace(/</g, "\\u003c");
}

const STYLE = `
  @page { size: A4 portrait; margin: 0; }
  * { box-sizing: border-box; }
  html, body { margin: 0; background: #888; font-family: Arial, Helvetica, sans-serif; color: #000; }
  .toolbar { position: sticky; top: 0; padding: 8px 16px; background: #333; color: #fff; font-size: 14px; z-index: 1; }
  .toolbar button { font-size: 14px; margin-right: 12px; }
  .sheet { width: 210mm; height: 297mm; margin: 8mm auto; background: #fff; overflow: hidden; }
  .half { position: relative; width: 210mm; height: 148.5mm; overflow: hidden; }
  .half + .half { border-top: 0.2mm dashed #999; }
  /* One A4-landscape page, scaled by 1/sqrt(2) into its A5 half -- so font
     sizes below read like the Calc original at 100%. */
  .page { position: absolute; top: 0; left: 0; width: 297mm; height: 210mm; padding: 10mm 12mm;
          transform: scale(0.70711); transform-origin: top left; overflow: hidden; }
  .measure { position: absolute; visibility: hidden; left: -10000px; top: 0; transform: none; height: auto; }
  .row { display: grid; grid-template-columns: 72mm 1fr 20mm; border-bottom: 0.3mm solid #999; break-inside: avoid; }
  .row > div { padding: 1.2mm 2mm; }
  .row > div:last-child { text-align: right; }
  .head { background: #ccc; font-size: 9pt; font-weight: bold; border-bottom: 0.3mm solid #666; }
  .slot { font-size: 18pt; font-weight: bold; }
  .name { font-size: 18pt; font-weight: bold; line-height: 1.1; }
  .comment { font-size: 15pt; white-space: pre-wrap; overflow-wrap: anywhere; }
  .break { background: #bbb; font-size: 9pt; font-weight: bold; }
  .break > div { padding: 0.6mm 2mm; }
  @media print {
    html, body { background: #fff; }
    .toolbar { display: none; }
    .sheet { margin: 0; break-after: page; }
  }
`;

// Runs inside the generated document: renders every row once in a hidden,
// unscaled page to measure it, packs the rows into pages (packRows()), and
// builds the sheets. All text goes through textContent -- never innerHTML.
function layoutScript() {
  const model = JSON.parse(document.getElementById("model").textContent);
  const PAGES_PER_SHEET = Number(document.body.dataset.pagesPerSheet);

  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }
  function slotNumber(n) {
    return String(n).padStart(3, "0");
  }
  function buildRow(row) {
    if (row.type === "break") {
      const r = el("div", "row break");
      r.append(el("div", null, `End of set ${row.set}`), el("div"), el("div", null, slotNumber(row.slot)));
      return r;
    }
    const r = el("div", "row");
    r.append(el("div", "name", row.name), el("div", "comment", row.comment.replace(/\r\n?/g, "\n").replace(/\n+$/, "")), el("div", "slot", slotNumber(row.slot)));
    return r;
  }
  // Columns: song name | comment | Kronos slot number (last, like the
  // "Kronos" column of the owner's Calc original).
  function buildHead(set) {
    const r = el("div", "row head");
    r.append(el("div", null, `Set ${set}${model.gigDate ? ` · ${model.gigDate}` : ""}`), el("div", null, model.title), el("div", null, "Kronos"));
    return r;
  }

  // Measured unscaled (transform:none, height:auto) at the real page width;
  // the room per page is the real page height minus padding and header.
  const measure = el("div", "page measure");
  document.body.append(measure);
  const head = measure.appendChild(buildHead(1));
  const rowEls = model.rows.map((row) => measure.appendChild(buildRow(row)));
  const style = getComputedStyle(measure);
  const pageHeight = measure.ownerDocument.querySelector(".probe").clientHeight;
  const available = pageHeight - parseFloat(style.paddingTop) - parseFloat(style.paddingBottom) - head.offsetHeight;
  const heights = rowEls.map((e) => e.getBoundingClientRect().height);
  measure.remove();
  document.querySelector(".probe").remove();

  const pages = packRows(heights, available, model.rows.map((row) => row.type === "break"));
  let sheet = null;
  pages.forEach((indexes, p) => {
    if (p % PAGES_PER_SHEET === 0) {
      sheet = el("div", "sheet");
      document.body.append(sheet);
    }
    const half = el("div", "half");
    const page = el("div", "page");
    const first = model.rows[indexes[0]];
    page.append(buildHead(first.set));
    for (const i of indexes) page.append(buildRow(model.rows[i]));
    half.append(page);
    sheet.append(half);
  });
  document.getElementById("status").textContent =
    `${model.rows.filter((r) => r.type === "song").length} songs, ${pages.length} pages on ${Math.ceil(pages.length / PAGES_PER_SHEET)} A4 sheet(s)`;
}

// model: buildPrintModel()'s result. Returns a complete HTML document as a
// string -- open it in any browser and print (A4, margins "None", scale
// 100%, no headers/footers), or "Save as PDF".
export function buildPrintDocument(model) {
  const title = model.gigDate ? `${model.title} – ${model.gigDate}` : model.title;
  return `<!doctype html>
<html><head><meta charset="utf-8"><title>${escapeHtml(title)}</title>
<style>${STYLE}</style></head>
<body data-pages-per-sheet="${PAGES_PER_SHEET}">
<div class="toolbar"><button onclick="window.print()">Print…</button><span id="status"></span>
  &nbsp;– print on A4 with margins “None”, scale 100%, no headers/footers.</div>
<div class="page probe" style="visibility:hidden;left:-10000px;transform:none"></div>
<script type="application/json" id="model">${scriptJson(model)}</script>
<script>
${packRows.toString()}
(${layoutScript.toString()})();
</script>
</body></html>
`;
}
