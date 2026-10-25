#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

// WebView first (main.cpp: on Linux it pulls in GTK, which
// choc_DesktopWindow.h needs).
#include "choc/gui/choc_WebView.h"
#include "choc/gui/choc_DesktopWindow.h"
#include "EditorBridge.h"

// The ENTIRE extension-point surface this repo exposes to an optional
// private companion module (see STATE.md's repo-split entry) -- deliberately
// generic. Nothing in this file, or anywhere else in this repo, knows the
// word "SGX-2" or anything about any specific EXi engine; that all lives in
// the private module's own source (private/diy-korg-kronos-editor/src),
// which is the whole point of the split -- this repo only ever exposes
// reusable primitives any future private-module editor could use the same
// way (a generic per-Program raw-bytes get/put pair on EditorBridge, a
// generic lock-by-record marker, and the window-creation hook below).

// What a private module gets back from EditorExtensionContext::createWindow()
// -- deliberately NOT a pointer to main.cpp's own (file-local, unexported)
// window/webview bookkeeping type. `bringToFront` is the only thing a caller
// ever needs to do with an existing window handle (e.g. a dedup cache hit);
// `ok` is false if window/webview creation itself failed.
struct EditorWindowHandle {
    bool ok = false;
    std::function<void()> bringToFront;  // only valid when ok == true
};

// Everything an optional private module needs FROM this app to build its own
// window(s) and read/write the exact raw bytes it needs, without needing (or
// being given) anything more than that.
struct EditorExtensionContext {
    EditorBridge& bridge;

    // Mirrors main.cpp's own createEditorWindow() -- see its doc comment for
    // what each parameter does. `resourceDir` lets a private module serve
    // its OWN frontend/ directory instead of this repo's.
    std::function<EditorWindowHandle(const std::string& entryHtml, const std::string& title, int width, int height,
                                      int minWidth, int minHeight,
                                      std::function<void(choc::ui::WebView&)> extraBindings,
                                      std::function<void()> onClosed, const std::string& resourceDir)>
        createWindow;
};

#ifdef EDITOR_HAS_PRIVATE_MODULE
// Called once per window this app creates (main.cpp), right after that
// window's own bindEditorBridgeFunctions() call, so a private module can add
// whatever additional per-window JS bindings it needs (e.g. a way to open
// one of its own windows) to every window uniformly. Declared here, but
// DEFINED ONLY in private/diy-korg-kronos-editor/src -- this repo never
// implements it, and EDITOR_HAS_PRIVATE_MODULE (set in CMakeLists.txt only when
// that submodule's own CMakeLists.txt is actually present) is what keeps a
// build without it from ever referencing this at all.
void registerPrivateEditorExtensions(choc::ui::WebView& view, EditorExtensionContext& ctx);

// The app's own lifecycle, for a module that adds command-line modes of its
// own. Same rule as above: declared here, defined only in the private
// module, never referenced without EDITOR_HAS_PRIVATE_MODULE.
//
// First thing main() does, before it reads its own arguments (`args` =
// argv[1..]): the module removes every argument it handles, so main() never
// mistakes one for a file to open. Returning false makes main() exit at
// once with `exitCode`.
bool consumePrivateEditorArguments(std::vector<std::string>& args, int& exitCode);

// Asked first by every window's resource loader, in every build type: a
// file the module serves itself, or nullopt for the app's own lookup.
std::optional<choc::ui::WebView::Options::Resource> fetchPrivateEditorResource(const std::string& path);

// Once, for the main window, right after main() created it.
void privateEditorMainWindowCreated(choc::ui::DesktopWindow& window, choc::ui::WebView& view);

// What main() finally returns: `appExitCode`, or the module's own.
int privateEditorExitCode(int appExitCode);
#endif
