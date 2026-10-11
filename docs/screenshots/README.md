# Documentation screenshots

`scenes.json` lists every app screenshot in the Hugo docs and the root
README, and the app regenerates them by itself: which datasets are open, what
each pane shows, and which part of the window goes into which PNG. Hugo
ignores this directory; the PNGs are written straight into `docs/content/...`
(and the repo root) under their existing names, so no Markdown changes when a
screenshot is refreshed. Only the two Safari screenshots on the Building page
are not covered -- they show another app.

```
KRONOS_SCENE_DATA=~/path/to/KRONOS-SOUNDS tools/update-screenshots.sh                # all scenes
KRONOS_SCENE_DATA=~/path/to/KRONOS-SOUNDS tools/update-screenshots.sh open,duplicates # just these ids
```

or directly:

```
build/kronos_editor --scene docs/screenshots/scenes.json --scene-data <dir> [--scene-only id,id]
```

The app opens one non-resizable window, works through the scenes, writes each
PNG and quits (17 scenes take about a minute). Exit code 0 only if every scene
was captured; the **first failure stops the run** with exit code 1 and the
reason on stderr (`scene <id>: ...`). A failed scene never leaves a broken PNG
behind. macOS only (uses `screencapture`); the app's launcher (Terminal, VS
Code) needs the Screen Recording permission once.

**Leave the Mac alone while it runs.** The window must stay in front and
focused -- an inactive window's title bar looks different, so a capture
fails if another app comes to the front. (The mouse pointer is ignored:
screenshot mode turns off hover effects and clicks on the page.)

The same scene file on the same data produces byte-identical PNGs run after
run (checked: three consecutive full runs, 17/17 identical), so `git status`
shows exactly the screenshots whose UI actually changed.

The PCG files are private and not in this repo: `datasets` names files
relative to `--scene-data` (default: this directory).

## Screenshot mode

While a scene runs, the page differs from a normal session only in what would
make screenshots vary or leak private data:

- the window title is just "DIY Kronos Editor" (no build timestamp)
- every dataset's full path is shown as its file name only
- the status bar says "Ready.", toasts, text cursor and scrollbars are hidden
- every scene starts like a fresh launch: no datasets open, numbering from #1

## File format

```json
{
  "version": 1,
  "window":   { "width": 1100, "height": 815 },
  "datasets": { "test": "setlist_test_2.PCG", "halen": "HALEN-SPLIT.PCG" },
  "scenes": [
    {
      "id": "setlist-editor",
      "out": "../content/guide/setlist/DIY-SetlistEditor.png",
      "window": { "width": 1100, "height": 1000 },
      "right": { "dataset": "test", "category": "setlist", "setlist": 1,
                 "open": { "song": 20, "sections": ["general", "comment"] } },
      "visibility": "right",
      "rectangle": [0, 13, 100, 100]
    }
  ]
}
```

Top level:

| Field | Meaning |
|---|---|
| `version` | must be `1` |
| `window` | default window size in points, `{width, height}` (optional, default 1100 x 650) |
| `datasets` | alias -> PCG file, relative to `--scene-data` |
| `scenes` | the scenes, captured in this order |

A scene:

| Field | Meaning |
|---|---|
| `id` | unique name, used by `--scene-only` and in messages |
| `out` | PNG path, relative to this file; the directory must exist |
| `window` | this scene's own window size (optional) |
| `rectangle` | capture area in percent of the captured image, **title bar included**: `[left, top, right, bottom]` (two corners). Omitted = `[0, 0, 100, 100]`. `[0, 0, 50, 50]` = upper-left quarter, `[50, 50, 100, 100]` = centre to bottom right |
| `openDatasets` | aliases opened first, in this order (fixes their #numbers; also the way to have a dataset open that no pane shows) |
| `left` / `right` | what that pane shows (below); a pane that isn't mentioned stays empty |
| `swapped` | `true` = the panes are swapped (⇄); `left`/`right` still mean what's visually there |
| `visibility` | `both` (default), `left` or `right` -- the Left only / Both / Right only buttons |
| `tooltip` | CSS selector of an element whose tooltip (`title`) is drawn below it |
| `dropdown` | CSS selector of a `<select>` drawn as an open menu |

A pane (`left` / `right`):

| Field | Meaning |
|---|---|
| `dataset` | alias (required) |
| `category` | `setlist` `combis` `programs` `duplicates` `internals` |
| `setlist` | Set List number shown in the Setlist tab |
| `banks` | only these banks selected in the bank filter, e.g. `["I-A"]` (programs/combis) |
| `setlistFilter` | the Combis tab's Set List filter, a Set List number |
| `filter` | text in the tab's filter box |
| `open` | setlist: `{ "song": 20, "sections": ["general", "comment"] }` (editor open with these sections expanded); programs/combis: a slot like `"U-A 019"` (row expanded, as after a jump); duplicates: a group name |

Tooltips and open `<select>` menus are drawn by macOS outside the page, so a
capture can't show the real ones; `tooltip`/`dropdown` draw a close stand-in
from the element's own text.

Unknown fields are an error, not ignored -- a typo must not produce a
screenshot of the wrong state. So are an empty category, a bank that doesn't
exist, a slot that isn't there, or a `tooltip`/`dropdown` element outside the
window.

## How it works

Native code (`src/main.cpp`'s `SceneRun`) hands the page one scene per page
load (`getPendingScene()`, after resizing and focusing the window);
`frontend/scene-runner.js` applies it, waits until the layout and all
scrolling have stopped, then calls `captureScene()`. Native code captures the
window, crops it, writes the PNG, closes every dataset and the page reloads
for the next scene. A scene that modifies a dataset fails the run, as does
a page that doesn't report back within 30 s (the message names the last step
it reached).

Scene fields go through the same functions the UI's own controls call
(`loadDataset()`, `switchCategory()`, `selectSetlist()`, `jumpToInstrument()`,
`setPaneVisibility()`, `swapPanes()`, ...). Three states have no such
function -- the bank filter, the Combis tab's Set List filter, expanding a
Duplicates group -- so the runner operates the pane's own control there;
those element lookups live only in `scene-runner.js` and fail with a clear
message when the markup changes.
