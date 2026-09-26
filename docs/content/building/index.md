---
title: Building the App
links:
  - title: Compiling the DIY Kronos Editor on macOS, Linux, and Windows
    description: CMake-based build instructions, verified via CI on all three platforms
menu:
    main:
        weight: 4
        params:
            icon: cpu

toc: true
---
The editor is a single CMake project with no platform-specific source trees --
[CHOC](https://github.com/Tracktion/choc) provides one native-webview abstraction that
maps to WebKit (macOS), WebKit2GTK (Linux), or WebView2 (Windows) depending on what
`CMakeLists.txt` detects. As of this writing it's built and verified on all three via CI
(macOS arm64 + Intel, Linux x86_64, Windows x86_64) --
[`.github/workflows/native-build.yml`](https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR/blob/main/.github/workflows/native-build.yml)
is the living, tested reference these instructions are drawn from.

## Requirements

| | macOS | Linux | Windows |
|---|---|---|---|
| Compiler | Xcode Command Line Tools (AppleClang) | GCC or Clang | Visual Studio 2022 (MSVC) |
| CMake | 3.21+ | 3.21+ | 3.21+ |
| WebView backend | WebKit + Cocoa (system frameworks, nothing to install) | `libgtk-3-dev` + `libwebkit2gtk-4.1-dev` + `pkg-config` | WebView2 (Evergreen runtime, preinstalled on current Windows 10/11) |
| Python 3 | only for Release builds (`embed_resources.py`) | only for Release builds | only for Release builds |

Linux dependency install (Debian/Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y libgtk-3-dev libwebkit2gtk-4.1-dev pkg-config
```

## Build

```bash
git clone https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR.git
cd DIY-KORG-KRONOS-EDITOR
cmake -B build -S .
cmake --build build
```

That's a **Debug** build by default -- `frontend/` (the HTML/JS/CSS UI) is read live off
disk from the checked-out source tree at runtime, which is convenient while developing
(edit a file, reload the window, no rebuild needed) but means the binary isn't portable
on its own.

For a **Release** build -- `frontend/` gets embedded into the binary at compile time as
byte arrays (via `tools/embed_resources.py`, hence the Python 3 requirement above), so
the result is a single self-contained executable:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The `--config Release` flag matters on Windows specifically: MSVC is a multi-config
generator, so the actual build configuration is chosen at build time, not at configure
time, even though `-DCMAKE_BUILD_TYPE=Release` is also needed at configure time (it's
what this project's own `CMakeLists.txt` checks to decide whether to embed resources at
all).

#### Hardened Release build

A build that embeds resources (`CMAKE_BUILD_TYPE=Release`, or an explicit
`-DEDITOR_EMBED_RESOURCES=ON`) is treated as a shippable build and gets locked down
(`EDITOR_RELEASE_HARDENED`):

- **Embedded frontend is obfuscated** -- each file is deflate-compressed then run
  through a keystream cipher (`tools/gen_asset_key.py` mints a fresh per-build-tree key,
  `tools/embed_resources.py --obfuscate` applies the transform, `src/kronos/AssetObfuscation.cpp`
  reverses it at load time). `strings`/a text editor on the binary reveal nothing
  readable. **This is obfuscation, not encryption** -- the key is compiled into the
  binary because the app decodes its own assets with no user input, so a determined
  reverse-engineer can always recover it. The point is to make pulling the frontend back
  out real work rather than a two-second `strings` dump. The optional private module's
  own frontend is embedded + obfuscated the same way, with the same key.
- **WebView debug mode is forced off**, with a `static_assert` in `main.cpp` so it can
  never silently regress, plus per-window suppression of the devtools keyboard shortcuts
  and the native context menu.
- **The binary is symbol-stripped** and dead-code-stripped so function names and
  unreferenced string literals don't leak.

A plain Debug build is unaffected by all of this -- frontend read live off disk,
devtools on, symbols intact.

### Where the binary ends up

- macOS: `build/kronos_editor.app` -- a real bundle for Release builds specifically
  (`open build/kronos_editor.app`, or double-click it in Finder), not a bare executable.
  A Debug build stays a plain `build/kronos_editor`, unaffected -- Finder-launchability
  only matters for something you'd actually hand to someone else.
- Linux: `build/kronos_editor`
- Windows: `build/Release/kronos_editor.exe` (or `build/Debug/kronos_editor.exe` for a
  Debug build -- MSVC's multi-config generator puts each configuration in its own
  subfolder)

### Opening files at startup

Any `.PCG`/`.SNG` paths given as arguments are opened as datasets as soon as the app is up
(relative paths resolve against the directory you launch from; the same file named twice
opens once; arguments starting with `-` are ignored):

```sh
./build/kronos_editor ../KRONOS-SOUNDS/INIT.PCG ../KRONOS-SOUNDS/K1_20260418.PCG
```

The first file shows in the left pane, the second in the right; a third and later ones are
opened too and can be picked from a pane's dataset selector. A path that can't be read shows an
error toast and the remaining files still open. (Launching the macOS Release bundle with
`open build/kronos_editor.app --args /absolute/path/a.PCG ...` should work the same way, but
macOS starts a bundle with `/` as its working directory, so use absolute paths there; this
form has not been tried yet.)

## Debugging, especially the JavaScript side

The editor's UI is plain HTML/JS/CSS running inside CHOC's native webview, and there are
three different ways to debug it depending on what you're actually chasing.

### Headless, per-component tests

For the pure `decode`/`encode` codec modules under `frontend/components/kronos/*.js` (no
DOM at all), run the headless test directly in Node:

```bash
node frontend/components/kronos/setlist-editor-comment-and-font.test.js
```

### Plain-browser mode -- no native app at all

Open `frontend/index.html` directly in a real browser (or serve the `frontend/` folder,
e.g. `python3 -m http.server` from inside it). `frontend/mock_bridge.js` auto-detects that
there's no native bridge and fabricates Set Lists/Programs/Combis, so drag-and-drop,
filters, the Unload button, the cross-dataset copy panel, and so on can all be exercised
with completely unrestricted Chrome/Safari DevTools. This is the fastest loop for UI/logic
work that doesn't need real file data. The one thing it genuinely can't do is open a real
`.PCG`/`.SNG` file -- a plain browser page has no filesystem access, so "Open..." falls
back to a `window.prompt()` stub instead of a real file picker.

### Real DevTools attached to the running app

`main.cpp` sets `options.enableDebugMode = true` on the `choc::ui::WebView` for **Debug
builds** (a hardened Release build forces it off -- see "Hardened Release build" above),
which CHOC wires up per platform and allows remote debugging.<br>

This gives you breakpoints, a live console, and the DOM inspector against the *real*
native bridge (`window.copyProgram`, `window.listDatasets`, actual file bytes) -- not
fabricated data. One gotcha: `console.log` inside the webview only goes to that DevTools
console, never to the terminal `kronos_editor` was launched from.

Combined with a Debug build reading `frontend/` live off disk (see "Build" above), this
means the normal edit loop for JS/CSS changes is: edit the file, reload the window
(Cmd+R/Ctrl+R works inside the webview), no C++ rebuild at all -- only changes under
`src/` need `cmake --build build` again.

#### **macOS**

Select: `Safari -> Preferences -> Show features for Web Developers`

![Safari Preferences](Debug-Safari-Setup.png)

Now start Safari and select: `Develop -> <YOUR MAC> -> Kronos Editor, choc.choc`<br>
And here you go: Full browser dev tools support. Just press CTRL+R to refresh the UI (CSS/JS/HTML), no build required. 

![Safari Preferences](Debug-Safari-KE.png)

#### **Windows (WebView2)**

Same flag sets `AreDevToolsEnabled` -- **F12**, or `right-click
-> Inspect`, opens Chrome DevTools attached to the embedded WebView2.

#### **Linux (WebKitGTK)**

Do: `right-click -> Inspect Element` works the same way.

### Conclusion

Each of the per-component codec modules above also has a browser-based `.test.html`
harness (open it via a static file server) if you want to see the component actually
rendered. See [App architecture & components](/components)'s "Committed, headless test
suites" section for how the two fit together.

For most day-to-day JS bug hunting, DevTools attached to the real running app (above) is
the one to reach for: real data, real bridge, and a reload is all a JS edit needs.

## A note for contributors touching `main.cpp` on Linux

CHOC's `choc_DesktopWindow.h` uses GTK types (`GdkDragContext`, `GtkWidget`, ...) without
including `<gtk/gtk.h>` itself -- it relies on some other header having already pulled
that in. `choc_MessageLoop.h` and `choc_WebView.h` both do (under `#if CHOC_LINUX`), so
one of those **must** be included before `choc_DesktopWindow.h`, or the Linux build fails
with "has not been declared" errors for basic GTK types. `main.cpp` already orders these
correctly -- just worth knowing if you ever reorder those three includes.
