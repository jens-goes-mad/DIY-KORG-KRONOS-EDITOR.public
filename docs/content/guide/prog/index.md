---
title: Programs
toc: true
---
Part of the [User Guide](/guide) -- see there first for opening a file, the dual-pane
layout, [jumping between panes](/guide#jumping-to-a-program-combi-or-set-list-slot), and
[browsing basics](/guide#browsing-programs-and-combis) (filter by bank, expanding a row).
This page covers what's specific to the Programs tab, including Duplicates.

Expanding a Program row shows two lists: every **Set List slot** that references it, and
(where confirmed -- see [The file format](/format) for which banks that covers) every
**Combi** that references it through one of its Timbres. Each entry is its own jump button
(see the [User Guide](/guide)'s Jumping section), so you can go straight from a Program to
everywhere it's actually used.

## Copying a Program by drag-and-drop

Drag one Program row onto another (same pane or a different pane's dataset) to copy its
raw bytes into that slot -- unlike a [Setlist](/guide/setlist) slot, this works *across*
datasets too, since a Program's own bank/number isn't referenced by anything outside its
own file the way a Setlist slot is.

**Copying a Program this way copies its entire raw record verbatim, including its KARMA
settings** -- and whether every part of those settings is safe to carry over between two
different banks or two different files hasn't been investigated yet (see
[The file format](/format) §8 #15). If a copied Program's KARMA behavior (Switch/Fader
assignments, Generated Effect module) looks wrong after a copy, this is why -- please
report it with the specific Program/Combi involved if you hit this, it would be real,
valuable ground truth. This applies to copying a Program directly, or as part of a
[Combi](/guide/combi) copy, and to a same-dataset copy just as much as a cross-dataset one.

### Swapping Programs by Shift+drag

Dropping a Program directly onto another (already-used) Program normally refuses, since the
two would otherwise collide -- **hold Shift while dropping** to swap the two instead: both
keep their content, just at each other's position, and every Set List slot and every Combi
Timbre that referenced either one follows it to its new spot. Same dataset only (a swap has
no meaning across two different files). This is the same gesture Combis already support
(see [Combi](/guide/combi)), and it's the only way to reorder two Programs that are
otherwise byte-identical (e.g. two untouched "Init Program" slots), since a plain copy
would be rejected as a duplicate.

## Resetting a slot

Right-click a Program row for a small local menu with one action: **Reset entry**.
Confirming it writes that bank's factory Init Program (HD-1 or EXi, whichever matches the
slot) straight over it -- the same content a brand-new, never-touched slot has. This is a
single-slot reset: unlike resolving a [duplicate](#duplicates) below, nothing else in the
file is repointed -- any Set List slot or Combi Timbre that already referenced this exact
slot keeps pointing at it, and will now show the reset content instead.

This applies immediately once confirmed -- no undo, same as every other write in this app.

## Duplicates

Groups Programs that are byte-for-byte identical (a real hash of the raw record, not just a
matching name), one row per group. Expand a group to see every copy as its own button.

Clicking a copy's button makes it **the only version**: every *other* copy in that group is
cleared back to a blank slot -- its bank's own factory-default template, HD-1 or EXi
depending on that copy's engine type -- and every Set List slot or Combi Timbre that
referenced any of the cleared copies is repointed to the one you clicked instead. A cleared
slot's name reads `- Init Program (HD1) -` or `- Init Program (EXi) -` (deliberately more
visible than Korg's own plain `Init Program`/`Init EXi Program`, so a cleared slot is
unmistakable at a glance rather than looking like any other blank one).

This applies immediately -- no confirmation step, no undo, same as every other write in
this app -- and shows a toast reporting exactly what changed (how many duplicates were
cleared, how many Set List slots and Combi Timbres were repointed).

Resolving a duplicate can only repoint a Combi Timbre reference to/from a Program bank
whose raw Timbre bank code is independently confirmed (see [The file format](/format)) --
in practice this covers every real bank on a real backup, but if a reference can't be
safely repointed the toast says so explicitly rather than silently skipping it.

## See also

- [Combi](/guide/combi) -- Timbres reference a Program by exactly this bank/number.
- [Setlist](/guide/setlist) -- a slot's **Bank** cell jumps straight to a Program.
