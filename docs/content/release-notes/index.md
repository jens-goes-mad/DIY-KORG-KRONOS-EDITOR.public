---
title: Release Notes
links:
  - title: What's changed, release by release
    description: a running, user-facing summary of what each tagged version added or fixed -- see STATE.md in the repo for the full day-by-day history
menu:
    main:
        weight: 7
        params:
            icon: history

toc: true
---
A short, user-facing summary of what changed in each tagged release -- not a full commit
log (see [`STATE.md`](https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR/blob/main/STATE.md)
in the repo for that level of detail).

## 0.1.9 (latest)

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
