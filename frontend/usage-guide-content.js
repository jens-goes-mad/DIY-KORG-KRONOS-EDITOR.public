// The Usage Guide's own content (2026-09-05), rendered by help.html via
// markdown-lite.js's renderMarkdownToHtml(). A deliberately short IN-APP
// quick reference, not a copy of the full docs site (docs/content/guide/*.md,
// published at /guide) -- this is meant to fit one modest window without
// scrolling forever, and is expected to grow incrementally as features are
// added (per direct request: "We will extend this over time"), not to
// duplicate the Hugo site's own much longer walkthrough page-by-page.
//
// A plain JS template-literal string, not a separate .md file fetched at
// runtime -- loaded exactly like every other frontend/*.js file (a plain
// <script> tag, no build step), rather than adding a new "fetch an arbitrary
// asset through CHOC's fetchResource" code path that nothing else in this
// app currently exercises.
const USAGE_GUIDE_MARKDOWN = `
# DIY Kronos Editor -- Usage Guide

A quick reference for how this app is laid out and how its drag-and-drop
editing actually behaves. This window is independent of the main one -- drag
it to a second screen and leave it open while you work.

## The two panes (Norton-Commander style)

The app is always showing two panes, A (left) and B (right). Each pane
independently picks:

- a **dataset** -- one of the open .PCG/.SNG files (Open... in the topbar
  loads a new one)
- a **category** -- Setlist, Combis, Programs, Duplicates, or Internals

Both panes can show the same dataset from two different angles (e.g. two
different Set Lists side by side), or two entirely different files for
comparison. The **⇄** button between the panes swaps which physical side
each pane is showing on. The **Left only / Both / Right only** buttons in the
topbar hide one pane to give the other the full window width -- handy on a
small screen.

## Structure inside a pane

- **Setlist**: 128 Set Lists, 128 slots each. A slot points at a Program or a
  Combi, plus Color/Volume/Hold Time/Font size/Transpose/Comment.
- **Programs** / **Combis**: every bank, filterable by bank and by name.
- **Duplicates**: byte-for-byte identical Programs, groupable and
  resolvable in one click.
- **Internals**: raw structural info, mostly for debugging the file itself.

The filter box does a live, display-only substring search -- it narrows what
you see, never what's actually in the file. Setlist's **A-Z / Z-A** buttons
are different: they **physically reorder every one of the 128 slots**, real
bytes, immediately, no undo.

## Drag-and-drop: copy, move, and swap

Every table (Setlist, Programs, Combis) uses the same drag gesture,
with a copy/move/swap decision made per-category:

- **Drop directly onto a row** to copy the dragged slot's content onto it.
- Setlist / Programs: this only works onto an **empty** target -- a row
  that's already in use never even lights up as a drop target, so there's
  nothing to undo by accident. Use **Reset entry** first (below) if you
  need to clear a slot to make it a valid target again.
- Combis: dropping onto an **empty** Combi copies, same as above. Dropping
  onto an **already-used** Combi instead **swaps** the two -- always safe,
  since nothing is destroyed either way.
- **Drop between two rows** (a line appears along the row's top/bottom edge)
  to **insert/move** it there instead, shifting the intervening slots down
  one. For Programs/Combis this works within the same bank, or across two
  different banks -- crossing a bank boundary overwrites whatever sits at
  the destination instead of shifting, since there's no "shift" concept
  spanning two independent 128-slot arrays. Setlist's insert only works
  **within the same Set List** -- a Setlist row can still be copied onto an
  empty slot of a *different* Set List (the onto-a-row gesture above), just
  not inserted/reordered into one.
- **Green** with a **"+"** cursor means the drop will copy. **Blue** means it
  will swap (Combis' onto-occupied gesture, or Programs' Shift+drop below).
  A line along an edge means insert. A "not allowed" cursor means exactly
  that -- nothing will happen if you let go here.

### The one modifier key: Shift, for Programs only

Hold **Shift before you start dragging** a Program (and keep it held) to
**swap** it with the target instead of copying onto it -- the only way to
exchange two occupied Program slots without losing either one's content.
Setlist and Combis don't use Shift at all; Combis get the same "exchange two
occupied slots" outcome for free by dropping onto an occupied Combi (no
modifier needed).

### Reset entry

Right-click any row, or click its **⋯** button, for a small menu with
**Reset entry** -- clears that one slot back to a blank, clearly-marked
placeholder ("- Init Setlist -", "- Init Combi -", or the bank's own factory
Init Program). Immediate, no undo, same as every other write in this app.

## Cross-links and navigating between panes

Anywhere a slot references another one -- a Setlist slot's Program/Combi, a
Combi Timbre's Program -- clicking it **jumps straight there in the same
pane**, keeping a per-pane Back/Forward history (the ← / → buttons next to
the category tabs) so you can always get back to exactly the row you came
from.

**Shift+click** the same reference to jump in the **opposite pane** instead
-- it switches that pane to the right dataset/category first if it isn't
already showing it. This is the fastest way to compare "what does this
Combi's Timbre 3 actually sound like" without losing your place in the pane
you started from.

## Cross-dataset tools (⧉)

The **⧉** button in the topbar (next to Left only / Both / Right only) opens a sidebar
with four tools that work on two open files at once, independent of either pane. Pick
the two files in the **A** and **B** dropdowns (they follow files being opened/closed),
press **Find**, and the result appears below (only that area scrolls; click any result
heading to collapse or expand it):

- **Duplicates** -- finds Programs with the same content in both files, at any slot
  (matched by content, ignoring the slot-dependent header bytes and Drum Track reference).
- **Compare PROG** / **Compare COMBI** -- finds every Program (or Combi) slot both files
  actually have at the same position that's **diverged** between them -- two backups of
  what's meant to be the same setup that have since drifted apart. A Combi row summarises
  what changed (Volume, Timbres, IFX, MFX, TFX, EQ, Mixer) and how many things differ; a
  Combi with a different name and more than 3 changes shows **Different song** instead.
- **Differences** -- matches **Programs by
  content, wherever they sit**, so a patch that just moved to another slot isn't
  reported as a difference. It lists what's only in A, only in B, **Renamed** (the same
  sound under a different name), **Modified twins** (same name, different content), and
  -- collapsed by default -- **Moved** (identical, different slot). Click a row to open
  A in the left pane and B in the right, each at its own slot. Programs only for now.

For Combis specifically, a diverged row tells you *why*, not just *that* it differs.
**Click** it to expand a breakdown of exactly what changed -- a specific value where
it's known (a Volume, a Timbre's Program reference), or a named section otherwise (an
Insert Effect slot, its EQ, its Mixer settings). Each line has its own **←** / **→**
button: **←** copies B's value into A, **→** copies A's value into B -- "A"/"B" here
always mean this sidebar's own left/right columns, not whichever pane currently shows
which side (Left only/Both/Right only and **⇄** don't change that). Resolving a change
removes it from the list right away, and refreshes either pane if it's currently
showing the file that just changed.

**Double-click** a diverged row instead to jump both panes at once -- A's copy of that
slot into the left pane, B's into the right -- for a live side-by-side look. (A Program
row does the same thing on a single click, since it has no further breakdown to expand
yet.)

## Saving

Every edit above writes straight into the loaded file's own in-memory bytes
immediately -- there's no separate "commit" step and no undo. Nothing
reaches disk, though, until you click a pane's own **Save As...** button and
pick a destination via the native Save dialog.

---
*This guide is a living document -- it'll grow as new features land.*
`;
