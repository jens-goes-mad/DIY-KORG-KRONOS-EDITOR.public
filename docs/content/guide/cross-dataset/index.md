---
title: Cross-Dataset Tools
toc: true
---
Two global tools, reached from one topbar icon, that work across *every open file at
once* -- independent of either pane: finding byte-exact duplicate Programs across any
number of open backups, and comparing exactly two backups to find what's drifted apart
between them.

<!--more-->

Part of the [User Guide](/guide) -- see there first for opening a file and the
dual-pane layout. Both tools below live behind the **⧉** button in the topbar, next to
**Left only / Both / Right only**, and open a sidebar with a mode toggle at the top.
Screenshots to follow.

## Find duplicates

Pick any number of currently open datasets and a bank filter, then **Find**. This finds
every group of Programs that are byte-for-byte identical *across those files* -- the
same content sitting in more than one backup, not just more than one slot of the same
file (that's the per-dataset [Duplicates](/guide/prog#duplicates) check instead). Each
group lists every copy with its own dataset (by filename) and slot; click one to jump
straight to it in the right pane, or Shift+click for the left pane. This tool is
read-only -- it doesn't resolve or write anything, since a Program's bank/number has no
portable meaning between two different files' own bank layouts.

## Compare two files

Pick exactly two open datasets -- **A** and **B** -- plus a bank filter, then
**Compare**. This finds every Program *and* Combi slot both files actually have at the
exact same (bank, number) position, whose content has **diverged** between them: two
backups of what's meant to be the same setup that have since drifted apart.

**"A" and "B" always mean this sidebar's own left/right columns -- never whichever
Norton pane currently shows which side.** The **Left only / Both / Right only** buttons
and the **⇄** swap button change which physical pane shows which dataset; they never
change what this tool means by A and B.

**Double-click** any diverged row to see it for yourself: A's copy of that slot opens in
the *left* pane, B's in the *right*, both jumped straight to it, for a live side-by-side
look.

### Resolving a diverged Combi

A diverged Program row only tells you *that* it differs (Programs have no confirmed
internal parameter layout in this project yet). A diverged **Combi** row tells you
*why* -- **click** it (a single click, not a double-click) to expand a breakdown of
exactly what changed:

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

## See also

- [Programs](/guide/prog#duplicates) -- the per-dataset version of duplicate detection,
  for finding byte-exact copies *within* one file.
- [Combi](/guide/combi) -- what a Combi's own Timbre references are, browsed the normal
  way.
