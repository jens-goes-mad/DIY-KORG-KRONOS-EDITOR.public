---
title: Setlist
toc: true
---
Browsing, filtering, sorting, drag-and-drop reordering and copying, and editing a
Setlist slot's Name/Color/Volume/Comment.

<!--more-->

Part of the [User Guide](/guide) -- see there first for opening a file, the dual-pane
layout, [jumping between panes](/guide#jumping-to-a-program-combi-or-set-list-slot), and
saving. This page covers the Setlist tab itself.

Browse any one of the file's 128 Set Lists, 128 slots each, via the dropdown at the top.

![Filter and Sort](DIY-SetlistOnePaneRight.png)

## Filter

The filter box does a live substring search against song names. This is purely a display
convenience -- it only changes which rows are currently *shown*, nothing about the
underlying file.

## Sort (A→Z / Z→A)

These two buttons **physically reorder every one of the Set List's 128 slots** by name --
writing real bytes immediately, exactly like drag-and-drop. This is not a display
convenience: a Korg Kronos has no concept of "sorted" independent of where a slot's data
actually sits in the file (confirmed directly against Korg's own documentation, see
[The file format](/format)) -- it plays back Set List slots strictly in their physical
record position, so the *only* way to make a real unit display things in a different order
is to actually move that data. There's no undo once clicked, same as every other immediate
write in this app.

Sorting always acts on the **whole 128-slot Set List**, regardless of whether the filter
box above is currently narrowing what's shown -- it's not limited to whatever rows happen
to be visible at the time. Empty slots always end up at the bottom regardless of direction,
so they don't get interleaved with the songs you're actually organizing.

## Reordering and copying slots by drag-and-drop

Drag any Setlist row within its own Set List, or onto the same Set List shown in the
*opposite* pane:

- **Drop it directly onto an empty slot** to copy that slot's whole content (name, Program/
  Combi reference, Color, Volume, Comment -- everything) onto the target. The source slot
  is left completely unchanged. This works between two *different* Set Lists too (same
  dataset), not just within one. **Dropping onto a slot that's already in use refuses** --
  same reasoning the [Programs](/guide/prog) and [Combi](/guide/combi) tables already
  enforce for their own onto-occupied copy: a copy-over is silent and total, so landing on
  a used slot by accident would destroy it with no undo anywhere in this app. [Reset the
  slot](#resetting-a-slot) first to make it a valid target again, or drop onto a genuinely
  empty one.
- **Drop it between two rows** (or before the first / after the last) to *insert* it there
  instead -- every slot in between shifts down one to make room. A line appears along the
  top or bottom edge of the row you're hovering to show exactly where the insert will
  land. Unlike copy-over above, insert only works **within the same Set
  List** -- inserting into a *different*, already-full 128-slot Set List would have to
  evict something at its far end to make room, a real data-loss question not tackled yet,
  so that specific case still uses an older, in-memory-only path that isn't part of what
  Save As writes out.

While you drag, the feedback tells you what a drop right now would do: a **green** row
with a **"+"** cursor means it will *copy* (the only thing an onto-a-row drop ever does
here), the edge line means *insert*, and hovering a slot that's already in use shows
neither -- the cursor itself goes to "not allowed." Same green-for-copy/edge-line-for-
insert visual language the Programs and Combis tables use, plus a **blue** row there for
their own *swap* gesture, which Setlist doesn't have.

Both of these write real bytes into the loaded file's own in-memory data immediately --
there's no separate "commit" step, for every case except the cross-Set-List insert noted
above. Dragging a slot to a *different loaded file* (a different dataset entirely) is
intentionally blocked, since a slot's Program/Combi reference is a raw bank/number pointer
that's only meaningful inside its own file.

## Resetting a slot

Click the **⋯** button in a slot's own last column (or right-click the row) for a small
local menu: **Reset entry** (the same menu the [Programs](/guide/prog#resetting-a-slot)
and [Combi](/guide/combi#resetting-a-slot) tables use), plus **Reset to SET BREAK** and,
on slot 000 only, **Reset to GIG** -- the same blank slot, named as one of the
[printout's marker slots](#printing-a-set-list) instead. Reset to GIG asks for the gig date
(today by default). Confirming Reset entry clears the slot back
to a blank **"- Init Setlist -"** -- no Program/Combi reference, and Color/Volume/Font
size/Transpose/Comment all back to their defaults. Unlike a Program or Combi reset, this
needs no factory template or live donor: a genuinely untouched Set List slot's own bytes
are confirmed blank on real hardware, so writing blank bytes back already matches that
exactly -- the visible "- Init Setlist -" name is purely this app's own marker (same
convention as Combi's "- Init Combi -"), so a slot you deliberately cleared reads
differently from one that was simply never touched.

This applies immediately once confirmed -- no undo, same as every other write in this app.

## Multi-select

Ctrl/Cmd-click a row to mark it (a green left-edge stripe). This is groundwork for a future
bulk action -- right now it's purely visual bookkeeping and doesn't do anything on its own.

## Set List menu: copy, reset, compact, print

The **⋯** button in the table's header row (the same button every row has) opens the Set
List-wide actions. Each one that changes the Set List asks for confirmation first and applies
immediately -- no undo. **Print Setlist** changes nothing (see [Printing a Set List](#printing-a-set-list)).

- **Copy entire Setlist to opposite** -- overwrites all 128 slots of the Set List the
  *opposite* pane is showing with this one's, slot for slot -- handy for starting a gig's
  list from an existing prepared one and then only tweaking the slots that need to change.
  The destination Set List keeps its own name; only its song slots are replaced.
- **Copy entries from Setlist to opposite** -- the same, but only for the rows this table
  is showing right now (after the [filter](#filter)), each to the **same slot number** in
  the opposite Set List, overwriting whatever is there.
- **Reset entries** -- [Reset entry](#resetting-a-slot) for every row shown right now (after
  the filter): "You are resetting *n* entries".
- **Compact Setlist** -- moves every song up, keeping their order, so all empty slots (blank
  or `- Init Setlist -`) end up at the end of the list -- the same idea as
  [Compact bank](/guide/prog#compacting-a-bank) for Programs, minus the references (nothing
  else points at a Set List slot).

The two copy items are only available when the opposite pane shows a **different Set List
of the same dataset** -- a slot's Program/Combi reference is just a bank/number, which means
something else in another file (the same reason [drag-and-drop](#reordering-and-copying-slots-by-drag-and-drop)
won't copy slots across datasets either). Hover a greyed-out item to see why it's off.

## Printing a Set List

**Print Setlist** in the header menu turns the whole Set List (not just the filtered rows)
into a leadsheet and opens it in your default browser; print it from there, or choose "Save
as PDF" in the print dialog. Print on A4 with margins set to "None", scale 100%, and the
browser's own headers/footers switched off.

- Each A4 portrait sheet holds two A4-landscape pages, scaled to A5 and stacked, with a
  dashed cut line in between -- the classic "2 pages per sheet" printout, in one step.
- Every page starts with a grey header row (set number, gig date, Set List name); every
  song is one row: song name, the slot's comment exactly as stored (line breaks and the
  spaces used for alignment on the Kronos screen are kept), and the Kronos slot number in
  the last column. A song is never split across two pages.
- Empty slots (blank or `- Init Setlist -`) are left out.

Two slot names have a special meaning for the printout. They are ordinary slot names, so
they're stored on the Kronos and survive every save and load:

- `---- SET BREAK ----` ends a set: printed as a grey "End of set *n*" bar, and the next set
  starts on a new page with its own set number in the header.
- `---- GIG 2025-10-28 ----` (any date, `YYYY-MM-DD`) puts the gig date into every page
  header and isn't printed itself. `GIG 2025-10-28` in the Set List's own name works too.

Create them with the row menu's **Reset to SET BREAK** / **Reset to GIG** (see
[Resetting a slot](#resetting-a-slot)), or type the name into the
[General](#editing-a-slot-general-and-comment) editor's Name field. They count as used
slots: drag-and-drop won't copy over them. Opening the browser is macOS-only for now; on
Windows/Linux the menu item reports that it isn't supported yet.

## Editing a slot: General and Comment

Click a slot's **#** or **Vol** cell for **General** (Name, Color, Volume), or its Song/Type
cell for **Comment** (and Font size). Each opens its own collapsible section below the row --
both can be open on the same slot at once.

- **Name, Color, and Volume all apply immediately** -- no Apply button, no confirmation.
  Color applies the moment you click a swatch, Volume the moment you release the slider,
  and Name the moment you leave the field (click elsewhere, Tab away, or press Enter).
  The name field caps at 24 characters (the format's own real limit), enforced as you type.
- **Comment and Font size use an Apply button** -- type/pick, then click Apply to commit.
  The Comment box scales its own on-screen font to match whichever Font size is selected,
  as a live approximation of how the text will actually wrap on a real Kronos screen.
  Comments cap at 512 characters, matching the hardware's own limit.

![Setlist edit](DIY-SetlistEditor.png)

If both panes are showing the same slot of the same Set List, only one of them can have an
editor open on it at a time -- the second attempt is blocked with a popup explaining why,
rather than risking one pane's edit silently overwriting the other's.

## See also

- A slot's **Bank** cell jumps straight to the [Program](/guide/prog) or [Combi](/guide/combi) it
  references -- see the [User Guide](/guide)'s Jumping section for the full set of
  cross-link gestures (Shift+click, Shift+Cmd+click, Back/Forward).
- [Saving your changes](/guide#saving-your-changes) covers what's and isn't written to
  disk immediately.
