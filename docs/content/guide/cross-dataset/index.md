---
title: Cross-Dataset Tools
toc: true
---
Three global tools, reached from one topbar icon, that work across *every open file at
once* -- independent of either pane: finding byte-exact duplicate Programs across any
number of open backups, comparing exactly two backups to find what's drifted apart
between them, and finding which Programs differ between two backups regardless of slot.

<!--more-->

Part of the [User Guide](/guide) -- see there first for opening a file and the
dual-pane layout. Both tools below live behind the **⧉** button in the topbar, next to
**Left only / Both / Right only**, and open one sidebar ("Cross Dataset analysis") with
a mode toggle at the top, two dataset dropdowns (**A** and **B**) and a **Find** button.
The result appears right below them, in its own scrolling area (the controls above stay
in place), and every result heading -- the summary line and each section -- collapses
and expands when you click it. **Find** stays disabled until two *different*
datasets are picked, and the dropdowns follow files being opened or closed while the
sidebar is open.
Screenshots to follow.

## Duplicates

Pick datasets **A** and **B**, then **Find**. This finds every group of Programs with
the **same content in both files**, at whatever slot -- the same sound sitting in more
than one backup, not just more than one slot of the same file (that's the per-dataset
[Duplicates](/guide/prog#duplicates) check instead). Matching is by content, not slot,
and ignores the bytes that depend on *where* a Program sits or which instrument saved
it (the record's 4-byte header and the Drum Track's Program reference), so the same
patch in another instrument's backup still matches. Empty/Init slots are skipped. Each
group lists every copy with its own dataset (by filename) and slot; click one to jump
straight to it in the right pane, or Shift+click for the left pane. This tool is
read-only -- it doesn't resolve or write anything, since a Program's bank/number has no
portable meaning between two different files' own bank layouts.

## Compare PROG and Compare COMBI

Pick the two datasets -- **A** and **B** -- then **Find**. **Compare PROG** finds every
Program slot, and **Compare COMBI** every Combi slot, that both files actually have at
the exact same (bank, number) position and whose content has **diverged** between them:
two backups of what's meant to be the same setup that have since drifted apart.

**"A" and "B" always mean this sidebar's own left/right columns -- never whichever
Norton pane currently shows which side.** The **Left only / Both / Right only** buttons
and the **⇄** swap button change which physical pane shows which dataset; they never
change what this tool means by A and B.

**Double-click** any diverged row to see it for yourself: A's copy of that slot opens in
the *left* pane, B's in the *right*, both jumped straight to it, for a live side-by-side
look.

### Reading and resolving a diverged Combi

A diverged Program row only tells you *that* it differs (Programs have no confirmed
internal parameter layout in this project yet). A diverged **Combi** row tells you
*why*. Its **Changes** column shows how many things differ. A Combi whose **name
differs** and that has **more than 3 changes** shows **Different song** instead --
listing every differing block of two unrelated songs would just be noise. The
breakdown itself names the high-level areas (Volume, Timbres, IFX1-12, MFX, TFX, EQ,
Mixer). **Click** a row (a single click, not a double-click) to expand a
breakdown of exactly what changed:

- A specific value, where it's already decoded -- e.g. `Master Volume 127 -> 124` or a
  Timbre's own `Volume 100 -> 90`, or which Program a Timbre now points at.
- A named section otherwise -- e.g. `IFX3 differs`, `EQ differs`, `Mixer differs` --
  when the *general area* that changed is known but the exact value inside it isn't
  decoded yet.
- `Other section differs` as a last resort, if the two records differ somewhere this
  tool doesn't have a name for at all yet.

Each line has its own **←** / **→** button: **←** copies B's value into A, **→** copies
A's value into B -- copying only the exact bytes that one specific change covers,
nothing else in the slot. This applies immediately, no confirmation step, same as every
other write in this app. A resolved change disappears from the list right away (and the
whole row, once nothing about that Combi diverges any more); if either pane is
currently showing the dataset that was just written to, it refreshes automatically.

## Differences

For two backups of *mostly the same rig* whose Programs have moved around -- say two
instruments sharing ~90% of the same patches at different slots. Pick datasets **A** and **B**
(the same two dropdowns as the other tools), then **Find**. Programs are matched **by
content, not by slot**, so a patch that only moved isn't a difference. The same
location-independent matching as **Duplicates** applies. Every Program that isn't identical at the same slot falls
into one of these groups (strongest match first):

- **Only in A / Only in B** -- nothing in the other file matches it, by content or name.
- **Renamed** -- identical to a Program in the other file *except its name*: the same
  sound under a different name.
- **Modified twins** -- the other file has a Program with the *same name*, but different
  content: the same patch, edited.
- **Moved** -- byte-identical to a Program at a *different* slot in the other file.
  Collapsed by default (press **Show**) since it can be most of a large rig.

Empty/Init slots are ignored, and matching is by presence: two identical copies in A and
one in B is not reported as a difference. **Click** a row to open A
in the left pane and B in the right, each jumped to *its own* slot (the two usually
differ). Programs only for now -- Combis reference Programs by slot number, so comparing
them regardless of slot is a separate problem. This tool is read-only.

A "Modified twin" can be a very small change -- there's no field-level breakdown for
Programs yet (only that they differ).

## See also

- [Programs](/guide/prog#duplicates) -- the per-dataset version of duplicate detection,
  for finding byte-exact copies *within* one file.
- [Combi](/guide/combi) -- what a Combi's own Timbre references are, browsed the normal
  way.
