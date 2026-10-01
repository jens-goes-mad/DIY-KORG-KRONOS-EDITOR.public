---
title: Release Notes
links:
  - title: What's changed, release by release
    description: a running, user-facing summary of what each tagged version added or fixed -- see STATE.md in the repo for the full day-by-day history
menu:
    main:
        weight: 7
        params:
            icon: calendar-stats

toc: true
---
A short, user-facing summary of what changed in each tagged release -- not a full commit
log (see [`STATE.md`](https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR/blob/main/STATE.md)
in the repo for that level of detail).

## 0.1.11 (latest)

- **Filter a pane by a Cross Dataset result**: the Programs and Combis lists now have a
  **Cross Dataset** dropdown next to the name filter. After a search, it narrows the list to
  one result category, seen from that pane's own file -- e.g. only the Programs that exist
  only in this file, the renamed ones, or the Combis that were edited versus replaced by a
  different song. It works together with the name filter and the bank buttons, and turns
  orange while filtering.
- New **Differences** search: matches Programs **by content, wherever they sit**, so a patch
  that only moved isn't reported. A dropdown picks the category: only in A, only in B,
  renamed, modified twins (same name, different content) or moved.
- **Duplicates** across two files now ignores the slot-dependent bytes of a Program (its
  record header and the Drum Track's Program reference), so the same sound in another
  backup or slot layout is actually found.
- Every Cross Dataset result table can be **sorted** by any column and **filtered** per
  column; only its rows scroll. **Compare COMBI** shows how many things changed, marks a
  renamed Combi with more than 3 changes as a **Different song**, and hides untouched "Init
  Combi" slots by default.
- Cross Dataset results are now reliably dropped as soon as either file is edited -- before,
  a result could go stale after more than one edit.
- A small **waveform icon** after a Program's engine type shows where its samples come from:
  orange = user samples (a .KSC user bank or Sampling mode), gray = a Korg EXs library.
- A Combi Timbre that points into a bank missing from a partial backup now says "(not in
  this backup)" instead of offering a dead jump.
- Files named on the command line open at startup (`kronos_editor a.PCG b.PCG`), the first two
  in the left and right pane.

## 0.1.10

- New **Cross Dataset analysis** sidebar (topbar button) that compares two open files:
  find the same Program in both, and **compare two files slot by slot** -- every Program or
  Combi that sits at the same position in both but has diverged.
- A diverged Combi shows **what** changed (Master/Timbre Volume, a Timbre's Program, or the
  IFX/MFX/TFX/EQ/Mixer section), and each change can be **copied from one file to the
  other** with a ← / → button.
- The sidebar can be resized.
- Fixed a crash when a dataset changed after a secondary window had been closed.
- New public User Guide page for the cross-dataset tools.
- The experimental Program parameter editor window grew a lot (the SGX-2 editor, all 186
  effects, Insert Effect routing) -- visible only in builds that include the optional private
  companion module, not in the public build.

## 0.1.9

- Added an **"i" info button** next to the pane-visibility toggle that opens a Usage Guide
  in its own separate window, so it can be kept open on a second screen while you work.
- Reordered the category tabs so **Combis** sits between Setlist and Programs.
- Added an experimental **MIDI Settings panel** for managing MIDI SysEx device
  communication -- visible only in builds that include the optional private companion
  module, not in the public build.
- Published a new public reference page on the Kronos's MIDI SysEx protocol.

## 0.1.8

- **Programs and Combis now share the same drag-and-drop gestures**: swap two slots, move
  a slot within or between banks, or copy it onto an empty one -- with every affected Set
  List reference repointed automatically.
- Added a **"Reset entry" action** to quickly clear a Set List, Program, or Combi slot back
  to a blank, clearly-marked placeholder.
- Removed a restriction that used to block copying a Program onto a slot when identical
  content already existed elsewhere in the file.
- Dragging a Set List slot onto one that's already in use now refuses the drop instead of
  silently overwriting it.
- Fixed Set List A-Z/Z-A sorting so a freshly reset slot lands with the other empty ones
  instead of sorting alphabetically ahead of real content.
- Reworked the Duplicates panel: Combi support, a clearer resolve picker, and a new
  "consolidate" mode.
- Fixed a bug where quitting the app could sometimes leave it running in the background.
- Fixed macOS release builds ("app is damaged" on first launch, a missing execute
  permission, and shipping a real `.app` bundle) and the Windows build.

## Initial (0.1.0)

- Browse Programs, Combis, and all 128 Set Lists in a dual-pane, side-by-side browser.
- Reorder and copy Set List slots by drag-and-drop; edit a slot's Name, Color, Volume,
  Comment, and Font size.
- Detect byte-for-byte duplicate Programs and resolve a group in one click.
- Cross-links between Set List slots, Combis, and the Programs they reference, with
  per-pane jump history.
- Save an edited file via a native Save dialog.
- One shared codebase, with native builds for macOS, Windows, and Linux.
