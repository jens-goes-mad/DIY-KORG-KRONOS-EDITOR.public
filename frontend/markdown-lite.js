// A tiny, hand-rolled Markdown-to-DOM renderer -- NOT a general-purpose
// Markdown implementation, just the subset the in-app Usage Guide
// (usage-guide-content.js, rendered by help.html) actually uses: headings
// (#/##/###), paragraphs, **bold**, *italic*, `inline code`, fenced ``` code
// blocks, unordered (-/*) and ordered (1.) lists (one level, no nesting),
// links, and a horizontal rule (---).
//
// Built rather than vendoring a real Markdown library (2026-09-05, see
// STATE.md) -- this app has no CDN/internet-access assumption (every asset
// is served from local disk via CHOC's fetchResource, same as third_party/
// choc/ and vendor/bulma.min.css), so "vendor" here would mean checking in
// someone else's whole parser (plus a version-tracking file, per this
// project's own vendor/BULMA_VERSION.txt convention) for a handful of
// features over content THIS project fully authors and controls. If a
// second in-app Markdown consumer shows up wanting real Markdown (tables,
// nested lists, footnotes, ...), that's the point to reconsider vendoring
// something real rather than growing this file feature-by-feature -- flagged
// here rather than guessed at now, per this project's "don't build for
// hypothetical future needs" convention.
//
// Plain content -> HTML text is escaped (escapeHtml() below) before any
// inline-formatting markup is layered on top, so the renderer is safe to
// point at anything, not just content this project wrote -- even though
// today's one caller (help.html) only ever renders its own bundled
// usage-guide-content.js.
//
// No DOM access at load time (pure functions only, mirrors this project's
// other pure-codec files, e.g. frontend/components/kronos/*.js), so this is
// parse-checkable with `jsc` the same way.

function escapeHtml(text) {
  return text
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

// Inline formatting within one already-HTML-escaped line: `code` first (so
// its own contents are never re-touched by **/* below), then bold, then
// italic, then [text](url) links. Order matters -- bold's ** would otherwise
// swallow a lone * meant as italic, and code spans must never have their
// contents reinterpreted as markup.
function renderInline(escapedText) {
  let html = escapedText;
  html = html.replace(/`([^`]+)`/g, '<code>$1</code>');
  html = html.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
  html = html.replace(/(?<!\*)\*([^*]+)\*(?!\*)/g, '<em>$1</em>');
  html = html.replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank" rel="noopener">$1</a>');
  return html;
}

// Renders `markdown` (a plain string) to an HTML string suitable for
// `container.innerHTML =`. Line-oriented, single pass, no lookahead beyond
// "is this line still part of the block I'm already in."
function renderMarkdownToHtml(markdown) {
  const lines = markdown.replace(/\r\n/g, "\n").split("\n");
  const out = [];
  let paragraph = [];
  let list = null;  // { tag: "ul"|"ol", items: [...] } while inside a list
  let inCodeBlock = false;
  const codeLines = [];

  function flushParagraph() {
    if (paragraph.length > 0) {
      out.push(`<p>${renderInline(paragraph.join(" "))}</p>`);
      paragraph = [];
    }
  }

  function flushList() {
    if (list) {
      const items = list.items.map((item) => `<li>${renderInline(item)}</li>`).join("");
      out.push(`<${list.tag}>${items}</${list.tag}>`);
      list = null;
    }
  }

  for (const rawLine of lines) {
    const line = rawLine;

    if (inCodeBlock) {
      if (line.trim() === "```") {
        out.push(`<pre><code>${codeLines.join("\n")}</code></pre>`);
        codeLines.length = 0;
        inCodeBlock = false;
      } else {
        codeLines.push(escapeHtml(line));
      }
      continue;
    }

    if (line.trim() === "```") {
      flushParagraph();
      flushList();
      inCodeBlock = true;
      continue;
    }

    const heading = line.match(/^(#{1,3})\s+(.*)$/);
    if (heading) {
      flushParagraph();
      flushList();
      const level = heading[1].length;
      out.push(`<h${level}>${renderInline(escapeHtml(heading[2]))}</h${level}>`);
      continue;
    }

    if (/^-{3,}\s*$/.test(line)) {
      flushParagraph();
      flushList();
      out.push("<hr>");
      continue;
    }

    const bullet = line.match(/^\s*[-*]\s+(.*)$/);
    if (bullet) {
      flushParagraph();
      if (!list || list.tag !== "ul") { flushList(); list = { tag: "ul", items: [] }; }
      list.items.push(escapeHtml(bullet[1]));
      continue;
    }

    const numbered = line.match(/^\s*\d+\.\s+(.*)$/);
    if (numbered) {
      flushParagraph();
      if (!list || list.tag !== "ol") { flushList(); list = { tag: "ol", items: [] }; }
      list.items.push(escapeHtml(numbered[1]));
      continue;
    }

    if (line.trim() === "") {
      flushParagraph();
      flushList();
      continue;
    }

    // A plain line right after a list item, still indented, with no bullet/
    // number of its own -- a wrapped continuation of that item's own text
    // (this is how usage-guide-content.js's source itself wraps a long
    // bullet across lines for readability), not a new paragraph. Joined
    // with a space onto the item's already-escaped text.
    if (list && /^\s+\S/.test(rawLine)) {
      const items = list.items;
      items[items.length - 1] = `${items[items.length - 1]} ${escapeHtml(line.trim())}`;
      continue;
    }

    flushList();
    paragraph.push(escapeHtml(line.trim()));
  }

  flushParagraph();
  flushList();
  if (inCodeBlock && codeLines.length > 0) out.push(`<pre><code>${codeLines.join("\n")}</code></pre>`);

  return out.join("\n");
}
