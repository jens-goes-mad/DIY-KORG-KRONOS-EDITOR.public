---
title: Cross-Dataset Tools
toc: true
---
One sidebar, reached from a single topbar icon, that compares **two open files** at once --
independent of either pane. Four searches: **Duplicates** and **Differences** match Programs
*by content*, wherever they sit; **Compare PROG** and **Compare COMBI** compare the two files
*slot by slot*.

<!--more-->

Part of the [User Guide](/guide) -- see there first for opening a file and the dual-pane
layout. Screenshots to follow.

## The sidebar

The **⧉** button in the topbar, next to **Left only / Both / Right only**, opens one sidebar,
"Cross Dataset analysis". It is a single screen:

1. A **mode toggle**: **Duplicates | Differences | Compare PROG | Compare COMBI**.
2. Two **dataset dropdowns**, **A** (opens in the left pane) and **B** (opens in the right
   pane). They follow files being opened or closed while the sidebar is open.
3. A **Find** button. It stays disabled until two *different* datasets are picked.
4. The **result**, right below. Only the result area scrolls -- the toggle, dropdowns and Find
   button stay in place -- and every result heading (the summary line and each section)
   collapses and expands when you click it.

**"A" and "B" always mean this sidebar's own dropdowns -- never whichever Norton pane
currently shows which side.** The **Left only / Both / Right only** buttons and the **⇄**
swap button change which physical pane shows which dataset; they never change what the tools
mean by A and B. Results are dropped as soon as either picked dataset changes (closed,
edited, or a different pick).

Matching by content (Duplicates and Differences) ignores the bytes of a Program record that
depend on *where* the Program sits or which instrument saved it -- the record's 4-byte header
and the Drum Track's Program reference (see
[the format reference](/format#57-slot-dependent-bytes-inside-a-program-record--confirmed-2026-09-26)).
Without that, the same sound in another backup or slot layout would never match. Empty and
"Init" slots are always skipped.

## Duplicates

Finds every group of Programs with the **same content in both files**, at whatever slot -- the
same sound sitting in more than one backup, not just more than one slot of the same file
(that's the per-dataset [Duplicates](/guide/prog#duplicates) check instead). The name counts:
the copies must be identical *including* their name. Each group lists every copy with its own
dataset (by filename) and slot; click one to jump to it in the right pane, or Shift+click for
the left pane. Read-only -- a Program's bank/number has no portable meaning between two
different files' own bank layouts, so there is nothing to resolve.

## Differences

For two backups of *mostly the same rig* whose Programs have moved around -- say two instruments
sharing ~90% of the same patches at different slots. Programs are matched **by content, not by
slot**, so a patch that only moved isn't a difference. Every Program that isn't identical at the
same slot falls into one of these sections (strongest match first):

- **Only in A / Only in B** -- nothing in the other file matches it, by content or by name.
- **Renamed** -- identical to a Program in the other file *except its name*: the same sound
  under a different name.
- **Modified twins** -- the other file has a Program with the *same name* but different
  content: the same patch, edited.
- **Moved** -- identical content at a *different* slot in the other file. Collapsed by default
  (click its heading) since it can be most of a large rig.

Matching is by presence: two identical copies in A and one in B is not reported as a
difference. **Click** a row to open A in the left pane and B in the right, each jumped to *its
own* slot (the two usually differ; a side with no partner isn't touched). Programs only for now
-- Combis reference Programs by slot number, so comparing them regardless of slot is a separate
problem. Read-only. A "Modified twin" can be a very small change; there's no field-level
breakdown for Programs yet (only that they differ).

## Compare PROG and Compare COMBI

**Compare PROG** finds every Program slot, and **Compare COMBI** every Combi slot, that both
files actually have at the exact same (bank, number) position and whose content has
**diverged** between them: two backups of what's meant to be the same setup that have since
drifted apart.

**Click** a Program row to open A in the left pane and B in the right, both jumped to that
slot. A Combi row is different (below): a single click expands it, a **double-click** opens
both panes.

### Reading and resolving a diverged Combi

A diverged Program row only tells you *that* it differs (Programs have no confirmed internal
parameter layout in this project yet). A diverged **Combi** row tells you *why*. Its **Changes**
column shows how many things differ -- or **Different song**: a Combi whose **name differs**
and that has **more than 3 changes** is treated as an unrelated song rather than an edited
one, since listing every differing block of two different songs would just be noise. The
section heading counts them too, e.g. "29 Combi divergence(s) (2 different songs)".

**Click** a row (a single click, not a double-click) to expand a breakdown of exactly what
changed, in high-level areas:

- A specific value, where it's already decoded -- e.g. `Master Volume 127 -> 124`, a Timbre's
  own `Volume 100 -> 90`, or which Program a Timbre now points at.
- A named section otherwise -- `IFX1` to `IFX12`, `MFX`, `TFX`, `EQ` or `Mixer` `differs` --
  when the *general area* that changed is known but the exact value inside it isn't decoded
  yet.
- `Other section differs` as a last resort, if the two records differ somewhere this tool
  doesn't have a name for at all yet.

For a "Different song" row, the expanded view is a single note instead of the list.

Each listed change has its own **←** / **→** button: **←** copies B's value into A, **→**
copies A's value into B -- copying only the exact bytes that one specific change covers,
nothing else in the slot. This applies immediately, no confirmation step, same as every other
write in this app. A resolved change disappears from the list right away (and the whole row,
once nothing about that Combi diverges any more); if either pane is currently showing the
dataset that was just written to, it refreshes automatically.

## See also

- [Programs](/guide/prog#duplicates) -- the per-dataset version of duplicate detection, for
  finding byte-exact copies *within* one file.
- [Combi](/guide/combi) -- what a Combi's own Timbre references are, browsed the normal way.
