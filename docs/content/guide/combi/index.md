---
title: Combi
toc: true
---
Browsing Combis and their Timbre references, drag-and-drop rearranging, and copying a
Combi to a different backup.

<!--more-->

Part of the [User Guide](/guide) -- see there first for opening a file, the dual-pane
layout, [jumping between panes](/guide#jumping-to-a-program-combi-or-set-list-slot), and
[browsing basics](/guide#browsing-programs-and-combis) (filter by bank, expanding a row).
This page covers what's specific to the Combi tab.

![Combi References](DIY-CombiOverview.png)

Expanding a Combi row shows all 16 Timbre slots, each with its own referenced [Program](/guide/prog)
(when assigned) as a jump button, and its on/off/engine-type status.

![Combi References](DIY-CombiTimbres.png)

## Rearranging Combis by drag-and-drop

Combi rows are draggable, same as [Setlist](/guide/setlist) slots -- but a Combi is only
ever referenced by Set List slots (never by anything else, unlike a Program, which a
Combi's own Timbres can also reference), so every one of these writes immediately,
repoints every affected Set List reference, and never touches anything else in the file:

- **Drop it directly onto an empty slot** (one still named "Init Combi," Korg's own default
  -- shown in the Name column) to **copy** it there. The source is left completely
  untouched, including its own Set List references -- this is how you keep two variations
  of the same Combi, e.g. one tuned for a band with a brass section and one without, without
  ever editing the original.
- **Drop it directly onto any other (already-used) Combi** to **swap** the two -- both
  keep their content, just at each other's position, and every Set List slot referencing
  either one follows it to its new spot. Works across banks too, since nothing is destroyed.
- **Drop it between two rows in the same bank** (or before the first / after the last) to
  **move** it there, shifting the intervening Combis down one to make room -- same insert
  behavior as a Setlist slot.
- **Drop it between two rows in a different bank** to move it there, **overwriting**
  whatever Combi currently occupies that exact slot. This is refused if the slot being
  overwritten is still referenced by any Set List -- move or copy it elsewhere first. The
  vacated source slot is refilled with a real "Init Combi" record from its own bank (renamed
  `- Init Combi -` so a cleared slot is unmistakable, same visibility idea as
  [Duplicates](/guide/prog#duplicates)' `- Init Program (HD1) -`), so this only works if the
  source's own bank has at least one spare "Init Combi" slot to draw from.

Every one of these shows a toast reporting how many Set List slots got repointed. All four
gestures above are same-dataset only -- for copying across two different files, see below.

## Copying a Combi to a different dataset

Dragging a Combi onto an empty slot works across datasets too, not just within one file --
drop it onto an "Init Combi" slot in the *other* pane's dataset to copy it there. Unlike a
same-dataset copy, this has real work to do first: each of the Combi's Timbres references a
specific Program, and that Program has to actually exist in the destination file for the
copy to mean anything.

- If every Program the Combi's Timbres depend on **already exists byte-for-byte identical**
  in the destination, the copy applies immediately -- no extra step, same as any other
  drag-and-drop write in this app.
- If **any Program doesn't exist yet**, a panel slides in from whichever side of the screen
  the drop landed on, listing every Timbre (the ones already resolved grayed out) and, for
  each Program that's missing, a small table -- one row per destination bank of the matching
  engine type that has room, each with its own dropdown of that bank's actual free ("empty")
  Program slots to choose from. Pick the exact slot you want it copied into. Apply copies each
  chosen Program into its chosen slot and repoints the new Combi's Timbres to match, all in
  one step; Cancel abandons the whole drop.

The source Combi (and its dataset) is never touched by any of this -- exactly like a
same-dataset copy, only the destination changes.

**Cross-dataset swap and cross-dataset move-to-a-different-bank (overwrite) aren't
supported yet** -- only the copy-onto-an-empty-slot gesture above works across two
different files; the other three rearrange gestures stay same-dataset-only for now.

## See also

- [Programs](/guide/prog) -- what a Timbre reference actually points at, and how Program
  copies/resets/duplicates work.
- [Setlist](/guide/setlist) -- which slots reference a given Combi.
