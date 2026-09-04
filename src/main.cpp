#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// Include order matters on Linux: choc_DesktopWindow.h uses GTK types
// (GdkDragContext, GtkWidget, ...) without including <gtk/gtk.h> itself --
// it relies on a header included earlier having already pulled that in.
// choc_MessageLoop.h/choc_WebView.h both do (CHOC_LINUX branch), so either
// must come first, or this fails to compile on Linux with "has not been
// declared" errors for basic GTK types.
#include "choc/gui/choc_MessageLoop.h"
#include "choc/gui/choc_DesktopWindow.h"
#include "choc/gui/choc_WebView.h"

#include "bridge/EditorBridge.h"
#include "bridge/EditorExtension.h"

#ifdef EDITOR_EMBED_RESOURCES
#include "generated/EmbeddedAssets.h"
#endif

namespace {

std::string mimeTypeFor(const std::string& path) {
    if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".html") == 0) return "text/html";
    if (path.size() >= 3 && path.compare(path.size() - 3, 3, ".js") == 0) return "application/javascript";
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".css") == 0) return "text/css";
    return "application/octet-stream";
}

#ifdef EDITOR_EMBED_RESOURCES

// Release build: served straight out of the binary, no files on disk needed.
// KNOWN GAP, deliberately deferred (matching the resources/ dir's own "no
// Release build is packaged/shipped yet" note below): this only searches the
// PUBLIC repo's own embedded frontend/ table (tools/embed_resources.py globs
// only this repo's frontend/ directory, see CMakeLists.txt). An optional
// private module's own frontend/ (e.g. private/diy-korg-kronos-editor/
// frontend/) is never embedded, so a Release build with EDITOR_HAS_PRIVATE_MODULE
// defined would successfully open that module's window and then fail every
// resource request for it. Not solved this pass -- Debug builds (the only
// ones actually used/tested so far) read everything live off disk instead,
// unaffected by this gap.
std::optional<choc::ui::WebView::Options::Resource> loadFrontendResource(const std::string& relative,
                                                                          const std::string&) {
    for (const auto& file : editor_embedded::getEmbeddedFiles()) {
        if (relative == file.path) {
            choc::ui::WebView::Options::Resource resource;
            resource.data.assign(file.data, file.data + file.size);
            resource.mimeType = mimeTypeFor(relative);
            return resource;
        }
    }
    return std::nullopt;
}

#else

// Debug build: read live off disk so editing frontend/ doesn't require a rebuild.
std::optional<choc::ui::WebView::Options::Resource> loadFrontendResource(const std::string& relative,
                                                                          const std::string& resourceDir) {
    std::ifstream file(resourceDir + relative, std::ios::binary);
    if (!file) return std::nullopt;

    std::ostringstream ss;
    ss << file.rdbuf();
    const auto content = ss.str();

    choc::ui::WebView::Options::Resource resource;
    resource.data.assign(content.begin(), content.end());
    resource.mimeType = mimeTypeFor(relative);
    return resource;
}

#endif

// Every EditorBridge method the frontend can call, bound 1:1 by name --
// shared verbatim between every window this app opens (main.cpp's
// createEditorWindow() below), all against the SAME EditorBridge instance,
// so any window can see/edit any dataset any other window opened. Pulled
// out of main() (2026-08-16, multi-window work -- see STATE.md) so a
// second window doesn't need its own copy of this list; every window gets
// the FULL bridge surface regardless of what its own frontend page actually
// uses, same "simplest thing that works" reasoning as giving every pane the
// same category tabs rather than a per-window whitelist.
void bindEditorBridgeFunctions(choc::ui::WebView& view, EditorBridge& bridge) {
    view.bind("openFile", [&bridge](const choc::value::ValueView& args) { return bridge.openFile(args); });
    view.bind("openFileDialog",
               [&bridge](const choc::value::ValueView& args) { return bridge.openFileDialog(args); });
    view.bind("listDatasets",
               [&bridge](const choc::value::ValueView& args) { return bridge.listDatasets(args); });
    view.bind("closeDataset",
               [&bridge](const choc::value::ValueView& args) { return bridge.closeDataset(args); });
    view.bind("isDatasetDirty",
               [&bridge](const choc::value::ValueView& args) { return bridge.isDatasetDirty(args); });
    view.bind("listSetlists",
               [&bridge](const choc::value::ValueView& args) { return bridge.listSetlists(args); });
    view.bind("getEntries", [&bridge](const choc::value::ValueView& args) { return bridge.getEntries(args); });
    view.bind("copyEntry", [&bridge](const choc::value::ValueView& args) { return bridge.copyEntry(args); });
    view.bind("getSongRecordBytes",
               [&bridge](const choc::value::ValueView& args) { return bridge.getSongRecordBytes(args); });
    view.bind("putSongRecordBytes",
               [&bridge](const choc::value::ValueView& args) { return bridge.putSongRecordBytes(args); });
    view.bind("getNameRecordBytes",
               [&bridge](const choc::value::ValueView& args) { return bridge.getNameRecordBytes(args); });
    view.bind("putNameRecordBytes",
               [&bridge](const choc::value::ValueView& args) { return bridge.putNameRecordBytes(args); });
    view.bind("getProgramRecordBytes",
               [&bridge](const choc::value::ValueView& args) { return bridge.getProgramRecordBytes(args); });
    view.bind("putProgramRecordBytes",
               [&bridge](const choc::value::ValueView& args) { return bridge.putProgramRecordBytes(args); });
    view.bind("reorderSongEntry",
               [&bridge](const choc::value::ValueView& args) { return bridge.reorderSongEntry(args); });
    view.bind("copySetlistEntries",
               [&bridge](const choc::value::ValueView& args) { return bridge.copySetlistEntries(args); });
    view.bind("sortSetlistEntries",
               [&bridge](const choc::value::ValueView& args) { return bridge.sortSetlistEntries(args); });
    view.bind("saveFileAs", [&bridge](const choc::value::ValueView& args) { return bridge.saveFileAs(args); });
    view.bind("saveFileDialog",
               [&bridge](const choc::value::ValueView& args) { return bridge.saveFileDialog(args); });
    view.bind("listPrograms",
               [&bridge](const choc::value::ValueView& args) { return bridge.listPrograms(args); });
    view.bind("listCombis", [&bridge](const choc::value::ValueView& args) { return bridge.listCombis(args); });
    view.bind("getProgramUsage",
               [&bridge](const choc::value::ValueView& args) { return bridge.getProgramUsage(args); });
    view.bind("findDuplicatePrograms",
               [&bridge](const choc::value::ValueView& args) { return bridge.findDuplicatePrograms(args); });
    view.bind("findProgramNameCollisions",
               [&bridge](const choc::value::ValueView& args) { return bridge.findProgramNameCollisions(args); });
    view.bind("findCombiNameCollisions",
               [&bridge](const choc::value::ValueView& args) { return bridge.findCombiNameCollisions(args); });
    view.bind("findDuplicateCombis",
               [&bridge](const choc::value::ValueView& args) { return bridge.findDuplicateCombis(args); });
    view.bind("getProgramBankTypes",
               [&bridge](const choc::value::ValueView& args) { return bridge.getProgramBankTypes(args); });
    view.bind("copyProgram", [&bridge](const choc::value::ValueView& args) { return bridge.copyProgram(args); });
    view.bind("swapProgram", [&bridge](const choc::value::ValueView& args) { return bridge.swapProgram(args); });
    view.bind("moveProgramWithinBank",
               [&bridge](const choc::value::ValueView& args) { return bridge.moveProgramWithinBank(args); });
    view.bind("moveProgramToBank",
               [&bridge](const choc::value::ValueView& args) { return bridge.moveProgramToBank(args); });
    view.bind("resolveDuplicateProgram", [&bridge](const choc::value::ValueView& args) {
        return bridge.resolveDuplicateProgram(args);
    });
    view.bind("resetProgram", [&bridge](const choc::value::ValueView& args) { return bridge.resetProgram(args); });
    view.bind("resolveDuplicateCombis", [&bridge](const choc::value::ValueView& args) {
        return bridge.resolveDuplicateCombis(args);
    });
    view.bind("swapCombis", [&bridge](const choc::value::ValueView& args) { return bridge.swapCombis(args); });
    view.bind("resetCombi", [&bridge](const choc::value::ValueView& args) { return bridge.resetCombi(args); });
    view.bind("moveCombiWithinBank",
               [&bridge](const choc::value::ValueView& args) { return bridge.moveCombiWithinBank(args); });
    view.bind("moveCombiToBank",
               [&bridge](const choc::value::ValueView& args) { return bridge.moveCombiToBank(args); });
    view.bind("copyCombi", [&bridge](const choc::value::ValueView& args) { return bridge.copyCombi(args); });
    view.bind("analyzeCombiCrossDatasetCopy", [&bridge](const choc::value::ValueView& args) {
        return bridge.analyzeCombiCrossDatasetCopy(args);
    });
    view.bind("applyCombiCrossDatasetCopy", [&bridge](const choc::value::ValueView& args) {
        return bridge.applyCombiCrossDatasetCopy(args);
    });
    view.bind("getDatasetInternals",
               [&bridge](const choc::value::ValueView& args) { return bridge.getDatasetInternals(args); });
}

// One top-level native window + its WebView -- this app can now have more
// than one open at once (2026-08-16, see STATE.md's multi-window entry),
// all sharing the one EditorBridge instance in main() below, so an optional
// private module's own editor window can read/write the exact same
// in-memory dataset the main window has open, with no copying/serialization
// at all.
struct EditorWindowInstance {
    std::unique_ptr<choc::ui::DesktopWindow> window;
    std::unique_ptr<choc::ui::WebView> webView;
};

#if CHOC_APPLE
// Guard against quitting with unsaved changes (2026-08-26, see STATE.md) --
// macOS-only half of that feature. Every DesktopWindow already gets a
// closeRequested veto (choc_DesktopWindow.h, a DIY-KRONOS-EDITOR-local
// patch to that otherwise-vendored file) covering a PER-WINDOW close
// gesture -- the title-bar button, or Cmd+W. Cmd+Q, the app's own "Quit"
// menu item, Dock "Quit", and an AppleScript `quit` Apple Event are a
// COMPLETELY SEPARATE gate: all of them go straight through
// -[NSApplication terminate:], which (confirmed the hard way -- a live
// `quit` Apple Event sent to a real .app bundle build closed the app
// instantly even with every open window's own closeRequested forcing a
// veto) never consults any window's delegate AT ALL when, as here before
// this addition, no NSApplicationDelegate implements
// applicationShouldTerminate: -- Cocoa's documented default with no
// delegate opinion is to just terminate immediately.
//
// NSTerminateLater (2) + a later explicit replyToApplicationShouldTerminate:
// is the standard Cocoa pattern for an asynchronous decision here --
// applicationShouldTerminate: itself must return synchronously, but
// confirming via this app's own JS dialog (showConfirmDialog(), the only
// one that actually shows anything in this WebView -- see confirm-dialog.js's
// own doc comment) can't be. Raw ObjC runtime, mirroring EXACTLY the
// pattern choc_DesktopWindow.h's own per-window delegate already
// establishes (createDelegateClass, class_addMethod with a stateless
// captureless `+[]` IMP, objc_setAssociatedObject to reach real context
// from inside it) -- kept in this file rather than folded into that
// vendored one, since applicationShouldTerminate: is a genuinely
// app-level, not per-window, concern.
struct AppTerminateContext {
    EditorBridge* bridge = nullptr;
    // Runs the confirm-then-terminate round trip against whichever
    // window's WebView is still available (any one -- every window shares
    // the exact same in-memory datasets, see EditorWindowInstance's own
    // doc comment above) once applicationShouldTerminate: has already
    // decided there's something worth asking about (bridge->anyDatasetDirty()).
    // Always eventually calls choc::messageloop::stop() (confirmed) or
    // replyToApplicationShouldTerminate:NO (cancelled) -- Cocoa is
    // BLOCKED waiting on exactly one reply once NSTerminateLater is
    // returned below, and never receiving one would leave the app stuck
    // in a "still quitting" limbo state indefinitely.
    std::function<void()> confirmThenTerminate;
};

void installAppTerminateDelegate(AppTerminateContext* context) {
    using namespace choc::objc;

    static Class delegateClass = [] {
        auto c = createDelegateClass("NSObject", "CHOCAppTerminateDelegate_");
        class_addMethod(c, sel_registerName("applicationShouldTerminate:"),
                         (IMP)(+[](id self, SEL, id) -> unsigned long {
                             auto* ctx = (AppTerminateContext*)objc_getAssociatedObject(self, "ctx");
                             if (ctx == nullptr || ctx->bridge == nullptr || !ctx->bridge->anyDatasetDirty())
                                 return 1;  // NSTerminateNow

                             if (ctx->confirmThenTerminate) ctx->confirmThenTerminate();
                             return 2;  // NSTerminateLater -- see confirmThenTerminate's own doc comment above
                         }),
                         "L@:@");
        objc_registerClassPair(c);
        return c;
    }();

    auto delegate = call<id>((id)delegateClass, "new");
    objc_setAssociatedObject(delegate, "ctx", (CHOC_OBJC_CAST_BRIDGED id)context, OBJC_ASSOCIATION_ASSIGN);
    call<void>(getSharedNSApplication(), "setDelegate:", delegate);
}
#endif

}  // namespace

int main() {
    const std::string frontendDir = EDITOR_FRONTEND_DIR;

    choc::ui::setWindowsDPIAwareness();
    choc::messageloop::initialise();

    EditorBridge bridge;

    // Every currently open top-level window. Not just the main window --
    // an optional private module can open more via the extension point
    // below, each closing independently; the whole app only quits once the
    // LAST one closes (see windowClosed below), not on the first.
    std::vector<std::unique_ptr<EditorWindowInstance>> openWindows;

    // The entire extension-point surface this repo exposes to an optional
    // private companion module (see bridge/EditorExtension.h). Declared
    // (default-constructed, `createWindow` left empty) BEFORE
    // createEditorWindow below so createEditorWindow's own lambda can
    // capture `ctx` by reference -- same reasoning as the createEditorWindow
    // self-capture right below it: only the DECLARATION needs to exist at
    // the point a lambda is written, not the full initialization, as long
    // as everything is assigned before anything actually INVOKES it
    // (nothing does until webviewIsReady fires asynchronously). `ctx.bridge`
    // is set immediately since `bridge` already exists; `ctx.createWindow`
    // is assigned further down, once createEditorWindow itself exists to
    // wrap.
    EditorExtensionContext ctx{bridge, {}};

    // Declared before assignment so the lambda body (which calls itself, to
    // let a window's own JS open ANOTHER window) can capture
    // `createEditorWindow` by reference -- standard recursive-lambda-via-
    // std::function idiom. Safe here because nothing actually INVOKES it
    // until webviewIsReady fires asynchronously, well after this whole
    // assignment has completed.
    //
    // `resourceDir` lets a caller (e.g. an optional private module, via the
    // EditorExtensionContext wrapper below) serve its OWN frontend/
    // directory instead of this repo's -- NOT the same as `frontendDir`
    // above, which is only this app's own default for the main window.
    // `extraBindings` (may be null) lets a caller add window-TYPE-specific
    // JS bindings beyond the common bindEditorBridgeFunctions() set.
    // `onClosed` (may be null) lets a caller hook additional cleanup when
    // THIS window closes, beyond createEditorWindow's own generic
    // openWindows bookkeeping.
    //
    // Returns the new window (already shown and tracked in openWindows), or
    // nullptr (and logs why) if the WebView itself failed to load -- the
    // caller decides whether that's fatal (the main window) or just a
    // failed attempt (a secondary window opened later).
    std::function<EditorWindowInstance*(const std::string&, const std::string&, int, int, int, int,
                                         std::function<void(choc::ui::WebView&)>, std::function<void()>,
                                         const std::string&)>
        createEditorWindow;
    createEditorWindow = [&](const std::string& entryHtml, const std::string& title, int width, int height,
                              int minWidth, int minHeight, std::function<void(choc::ui::WebView&)> extraBindings,
                              std::function<void()> onClosed, const std::string& resourceDir) -> EditorWindowInstance* {
        auto instance = std::make_unique<EditorWindowInstance>();
        // Computed here (not after openWindows.push_back(std::move(instance))
        // below, where it USED to first appear) so options.webviewIsReady's
        // own closure below can bind confirmQuitAndClose to THIS exact
        // window instance -- `instance` itself can't be captured by value
        // (unique_ptr) or safely by reference (about to be moved into
        // openWindows), but the raw pointer stays valid identically to how
        // windowClosed's own closure already relies on further down.
        EditorWindowInstance* rawInstance = instance.get();
        instance->window = std::make_unique<choc::ui::DesktopWindow>(choc::ui::Bounds{100, 100, width, height});
        instance->window->setWindowTitle(title);
        instance->window->setResizable(true);
        instance->window->setMinimumSize(minWidth, minHeight);

        choc::ui::WebView::Options options;
        // Release builds ship without remote debugging -- Safari's Develop
        // menu, right-click "Inspect Element", and the legacy
        // developerExtrasEnabled/isInspectable machinery (see CHOC's own
        // choc_WebView.h) all stay off, matching a real shipped product
        // rather than a dev build. EDITOR_EMBED_RESOURCES is already this
        // project's "is this a real packaged/Release build" marker
        // (CMakeLists.txt), reused here rather than a second flag -- a
        // Debug build keeps debug mode on exactly as before.
#ifdef EDITOR_EMBED_RESOURCES
        options.enableDebugMode = false;
#else
        options.enableDebugMode = true;
#endif

        options.fetchResource = [resourceDir, entryHtml](const std::string& path)
            -> std::optional<choc::ui::WebView::Options::Resource> {
            std::string relative = (path.empty() || path == "/") ? entryHtml : path;
            return loadFrontendResource(relative, resourceDir);
        };

        options.webviewIsReady = [&bridge, &ctx, extraBindings, rawInstance](choc::ui::WebView& view) {
            bindEditorBridgeFunctions(view, bridge);
#ifdef EDITOR_HAS_PRIVATE_MODULE
            registerPrivateEditorExtensions(view, ctx);
#endif
            // Per-WINDOW-instance binding, not part of bindEditorBridgeFunctions()'s
            // shared set (2026-08-26, see STATE.md's "Guard against quitting
            // with unsaved changes") -- the JS-side confirm dialog this
            // window's own closeRequested triggers (below) calls this once
            // the user actually confirms "quit without saving", to close
            // THIS specific window for real. Bound directly to rawInstance
            // rather than reaching through some shared registry, since each
            // window already owns its own WebView/bindings pair.
            view.bind("confirmQuitAndClose", [rawInstance](const choc::value::ValueView&) -> choc::value::Value {
                rawInstance->window->forceClose();
                return choc::value::Value();
            });
#if CHOC_APPLE
            // Bound on every window (not just whichever one happens to run
            // the confirm dialog -- installAppTerminateDelegate's own
            // confirmThenTerminate picks any available window's WebView, see
            // its doc comment above) so the binding is always there
            // regardless of which one that ends up being. See app.js's
            // confirmAppQuitRequested()'s own doc comment for why this pair
            // isn't just confirmQuitAndClose() reused: this ends the WHOLE
            // app (every window), and Cocoa is left BLOCKED waiting on
            // exactly one reply to applicationShouldTerminate: once
            // NSTerminateLater was returned, so the cancel path must
            // explicitly say so too, unlike a plain per-window close veto.
            view.bind("confirmAppQuitAndTerminate", [](const choc::value::ValueView&) -> choc::value::Value {
                choc::objc::call<void>(choc::objc::getSharedNSApplication(), "replyToApplicationShouldTerminate:",
                                        (BOOL)YES);
                choc::messageloop::stop();
                return choc::value::Value();
            });
            view.bind("cancelAppQuitReply", [](const choc::value::ValueView&) -> choc::value::Value {
                choc::objc::call<void>(choc::objc::getSharedNSApplication(), "replyToApplicationShouldTerminate:",
                                        (BOOL)NO);
                return choc::value::Value();
            });
#endif
            if (extraBindings) extraBindings(view);
        };

        instance->webView = std::make_unique<choc::ui::WebView>(options);
        if (!instance->webView->loadedOK()) {
            std::cerr << "Failed to create WebView for \"" << title << "\" (no suitable OS web engine found)\n";
            return nullptr;
        }

        instance->window->setContent(instance->webView->getViewHandle());
        instance->window->setVisible(true);
        instance->window->toFront();

        choc::ui::WebView* viewPtr = instance->webView.get();
        bridge.addDatasetsChangedListener([viewPtr] {
            viewPtr->evaluateJavascript("if (window.refreshDatasets) window.refreshDatasets();");
        });

        instance->window->windowClosed = [&openWindows, rawInstance, onClosed] {
            if (onClosed) onClosed();
            openWindows.erase(std::remove_if(openWindows.begin(), openWindows.end(),
                                              [rawInstance](const std::unique_ptr<EditorWindowInstance>& w) {
                                                  return w.get() == rawInstance;
                                              }),
                               openWindows.end());
            if (openWindows.empty()) choc::messageloop::stop();
        };

        // Guard against quitting with unsaved changes (2026-08-26, reported
        // directly: "all unsaved changes are lost without warning the
        // user"). Setting closeRequested at all suppresses this window's
        // OWN native close (any gesture: title-bar button, Cmd+Q, Alt+F4,
        // window-manager Close -- see choc_DesktopWindow.h's own doc
        // comment on closeRequested, a DIY-KRONOS-EDITOR-local addition to
        // that otherwise-vendored third_party/choc header) until
        // forceClose() is actually called -- either immediately below (no
        // unsaved changes anywhere -- the common case, no dialog needed at
        // all) or later, once the JS-side confirm dialog resolves and calls
        // back into confirmQuitAndClose (bound above).
        //
        // Deliberately asks bridge.anyDatasetDirty() -- every dataset
        // currently open in ANY pane of ANY window, not just this one --
        // since this app has one shared EditorBridge instance (see this
        // file's own EditorWindowInstance doc comment): a second window
        // (e.g. a private module's own SGX-2 editor) can hold the very same
        // in-memory data this window does, and losing it is losing it
        // regardless of which window's close gesture the user happened to
        // use. Real native confirm() is silently swallowed by this WebView
        // (see confirm-dialog.js's own doc comment, and pane.js's Unload
        // button, the first feature to hit this) -- window.showConfirmDialog()
        // is what actually shows something, hence the JS round-trip instead
        // of trying to block here synchronously and show a native dialog
        // directly from C++.
        instance->window->closeRequested = [&bridge, viewPtr, rawInstance] {
            if (!bridge.anyDatasetDirty()) {
                rawInstance->window->forceClose();
                return;
            }
            viewPtr->evaluateJavascript("if (window.confirmQuitRequested) window.confirmQuitRequested();");
        };

        openWindows.push_back(std::move(instance));
        return rawInstance;
    };

    // Now that createEditorWindow exists, wire up ctx.createWindow -- adapts
    // createEditorWindow()'s raw EditorWindowInstance* (a type private to
    // this file, never exposed outside it) into the generic
    // EditorWindowHandle the extension header defines.
    ctx.createWindow = [&createEditorWindow](const std::string& entryHtml, const std::string& title, int width,
                                              int height, int minWidth, int minHeight,
                                              std::function<void(choc::ui::WebView&)> extraBindings,
                                              std::function<void()> onClosed,
                                              const std::string& resourceDir) -> EditorWindowHandle {
        EditorWindowInstance* instance = createEditorWindow(entryHtml, title, width, height, minWidth, minHeight,
                                                              std::move(extraBindings), std::move(onClosed), resourceDir);
        if (instance == nullptr) return {};
        return {true, [instance] { instance->window->toFront(); }};
    };

    EditorWindowInstance* mainWindow =
        createEditorWindow("/index.html", "DIY Kronos Editor (jens-goes-mad with claude) -- built " __DATE__ " " __TIME__,
                            1100, 650, 800, 500, nullptr, nullptr, frontendDir);
    if (mainWindow == nullptr) return 1;

#if CHOC_APPLE
    // Guard against quitting with unsaved changes (2026-08-26, see
    // STATE.md) -- see installAppTerminateDelegate's own doc comment above
    // for why this is a separate mechanism from every window's own
    // closeRequested. appTerminateContext must outlive choc::messageloop::run()
    // below (the delegate holds a raw, non-owning pointer to it via
    // objc_setAssociatedObject/OBJC_ASSOCIATION_ASSIGN, mirroring
    // choc_DesktopWindow.h's own getPimplFromContext() pattern) -- declared
    // here, a local in main() itself, rather than heap-allocated and
    // deliberately leaked, since main() only returns once the app is
    // already quitting for real.
    AppTerminateContext appTerminateContext;
    appTerminateContext.bridge = &bridge;
    appTerminateContext.confirmThenTerminate = [&openWindows] {
        // Any open window's WebView works -- they all share the exact same
        // in-memory datasets (EditorWindowInstance's own doc comment), so
        // it doesn't matter which one actually shows the dialog.
        for (auto& w : openWindows) {
            if (w->webView) {
                w->webView->evaluateJavascript("if (window.confirmAppQuitRequested) window.confirmAppQuitRequested();");
                return;
            }
        }
    };
    installAppTerminateDelegate(&appTerminateContext);
#endif

    choc::messageloop::run();
    return 0;
}
