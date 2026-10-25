#include <algorithm>
#include <filesystem>
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
#include "platform/WindowCapture.h"
#include "scenes/SceneScript.h"

#ifdef EDITOR_EMBED_RESOURCES
#include "bridge/EmbeddedAssetRegistry.h"
#include "generated/EmbeddedAssets.h"
#include "kronos/AssetObfuscation.h"
#endif

namespace {

std::string mimeTypeFor(const std::string& path) {
    auto endsWith = [&](const char* ext) {
        const std::size_t n = std::string(ext).size();
        return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
    };
    if (endsWith(".html")) return "text/html";
    if (endsWith(".js")) return "application/javascript";
    if (endsWith(".css")) return "text/css";
    if (endsWith(".json")) return "application/json";
    if (endsWith(".svg")) return "image/svg+xml";
    return "application/octet-stream";
}

#ifdef EDITOR_EMBED_RESOURCES

// Release build: served straight out of the binary, no files on disk needed.
// The embedded bytes are OBFUSCATED (deflate + keystream cipher, see
// tools/embed_resources.py --obfuscate) so `strings`/a text editor on the
// shipped binary reveal nothing readable -- kronos::deobfuscateAsset()
// reverses it here. This is obfuscation, not encryption: the key is compiled
// in (it has to be), it just makes pulling the frontend back out real work.
//
// Two tables are consulted: this repo's own frontend/ (editor_embedded), and
// -- when an optional private companion module is linked in -- that module's
// own separately-embedded frontend/, via the EmbeddedAssetRegistry resolver
// it registers. Path namespaces are disjoint (/index.html, /pane.js,
// /components/* here vs /sgx-2/*, /common/* there), so order is irrelevant;
// a build without the private submodule never registers a resolver.
std::optional<choc::ui::WebView::Options::Resource> loadFrontendResource(const std::string& relative,
                                                                          const std::string&) {
    for (const auto& file : editor_embedded::getEmbeddedFiles()) {
        if (relative == file.path) {
            choc::ui::WebView::Options::Resource resource;
            const auto plain = kronos::deobfuscateAsset(file.data, file.size);
            resource.data.assign(plain.begin(), plain.end());
            resource.mimeType = mimeTypeFor(relative);
            return resource;
        }
    }
    return kronos::resolveExtraEmbeddedAsset(relative);
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
    view.bind("compactSetlistEntries",
               [&bridge](const choc::value::ValueView& args) { return bridge.compactSetlistEntries(args); });
    view.bind("resetSetlistEntries",
               [&bridge](const choc::value::ValueView& args) { return bridge.resetSetlistEntries(args); });
    view.bind("sortSetlistEntries",
               [&bridge](const choc::value::ValueView& args) { return bridge.sortSetlistEntries(args); });
    view.bind("saveFileAs", [&bridge](const choc::value::ValueView& args) { return bridge.saveFileAs(args); });
    view.bind("saveFileDialog",
               [&bridge](const choc::value::ValueView& args) { return bridge.saveFileDialog(args); });
    view.bind("openHtmlInBrowser",
               [&bridge](const choc::value::ValueView& args) { return bridge.openHtmlInBrowser(args); });
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
    view.bind("findDuplicateProgramsAcrossDatasets", [&bridge](const choc::value::ValueView& args) {
        return bridge.findDuplicateProgramsAcrossDatasets(args);
    });
    view.bind("findDivergentProgramsAcrossDatasets", [&bridge](const choc::value::ValueView& args) {
        return bridge.findDivergentProgramsAcrossDatasets(args);
    });
    view.bind("findProgramDifferencesAcrossDatasets", [&bridge](const choc::value::ValueView& args) {
        return bridge.findProgramDifferencesAcrossDatasets(args);
    });
    view.bind("findDivergentCombisAcrossDatasets", [&bridge](const choc::value::ValueView& args) {
        return bridge.findDivergentCombisAcrossDatasets(args);
    });
    view.bind("resolveCombiDivergenceChange", [&bridge](const choc::value::ValueView& args) {
        return bridge.resolveCombiDivergenceChange(args);
    });
    view.bind("getProgramBankTypes",
               [&bridge](const choc::value::ValueView& args) { return bridge.getProgramBankTypes(args); });
    view.bind("copyProgram", [&bridge](const choc::value::ValueView& args) { return bridge.copyProgram(args); });
    view.bind("swapProgram", [&bridge](const choc::value::ValueView& args) { return bridge.swapProgram(args); });
    view.bind("moveProgramWithinBank",
               [&bridge](const choc::value::ValueView& args) { return bridge.moveProgramWithinBank(args); });
    view.bind("compactProgramBank",
               [&bridge](const choc::value::ValueView& args) { return bridge.compactProgramBank(args); });
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

// One `--scene` run (docs/screenshots/README.md, src/scenes/SceneScript.h):
// the scenes to capture and how far along the run is. The frontend asks for
// the current scene after every page load (getPendingScene), applies it
// (frontend/scene-runner.js) and calls captureScene once the UI has settled;
// native code writes the PNG, closes every dataset (numbering restarts at
// #1) and the page reloads for the next scene -- so every scene starts like
// a fresh launch, with no leftover datasets, sidebar, scroll or selection
// state to reset by hand. The first failure ends the whole run with exit
// code 1.
struct SceneRun {
    static constexpr uint32_t kSceneTimeoutMs = 30000;

    scenes::SceneScript script;
    size_t index = 0;
    int exitCode = 0;
    bool finished = false;
    std::string lastStep;  // the frontend's last sceneProgress() report, for the timeout message
    choc::ui::DesktopWindow* window = nullptr;  // the main window, set once it exists
    int windowWidth = 0, windowHeight = 0;      // its current size
    // Catches a page that never reports back at all (a JS exception before
    // scene-runner.js runs, a hung bridge call) -- the frontend's own errors
    // arrive via sceneFailed instead.
    choc::messageloop::Timer watchdog;

    const scenes::Scene* current() const { return finished || index >= script.scenes.size() ? nullptr : &script.scenes[index]; }

    void finish(int code) {
        finished = true;
        exitCode = code;
        choc::messageloop::stop();
    }

    void fail(const std::string& id, const std::string& error) {
        std::cerr << "scene " << id << ": " << error << "\n";
        finish(1);
    }
};

// Captures the main window for `scene` and writes scene.outPath, cropped to
// its rectangle. Goes through a temp file next to the target, renamed only
// once everything worked, so a failed scene never leaves a broken or half-
// written PNG behind.
bool captureScene(const SceneRun& run, const scenes::Scene& scene, std::string& error) {
    namespace fs = std::filesystem;
    const fs::path raw = scene.outPath + ".capture";
    const std::string partial = scene.outPath + ".partial";
    struct Cleanup {
        fs::path a, b;
        ~Cleanup() {
            std::error_code ec;
            fs::remove(a, ec);
            fs::remove(b, ec);
        }
    } cleanup{raw, partial};

    if (!kronos::isWindowActive(run.window->getWindowHandle())) {
        error = "the window lost focus during the scene (another app came to the front) -- "
                "an inactive title bar looks different; leave the Mac alone while scenes run";
        return false;
    }
    if (!kronos::captureWindowPng(run.window->getWindowHandle(), raw.string(), error)) return false;
    int width = 0, height = 0;
    if (!kronos::imagePixelSize(raw.string(), width, height, error)) return false;
    const auto r = scenes::toPixelRect(scene.rectangle, width, height);
    if (!kronos::cropPng(raw.string(), partial, r.x, r.y, r.width, r.height, error)) return false;
    std::error_code ec;
    fs::rename(partial, scene.outPath, ec);
    if (ec) {
        error = "cannot write " + scene.outPath + ": " + ec.message();
        return false;
    }
    std::cout << "scene " << scene.id << ": " << scene.outPath << " (" << r.width << "x" << r.height << " px)\n";
    return true;
}

void bindSceneFunctions(choc::ui::WebView& view, EditorBridge& bridge, SceneRun* run) {
    // Bound in every mode so app.js can always ask; null means "no scene
    // pending, start normally".
    view.bind("getPendingScene", [run](const choc::value::ValueView&) -> choc::value::Value {
        const scenes::Scene* scene = run ? run->current() : nullptr;
        if (scene == nullptr) return {};
        // Frontmost and focused for every scene (an inactive window's title
        // bar is drawn differently); captureScene checks it still is.
        run->window->toFront();
        // A scene with its own "window" size: resized before the page applies
        // the scene, so the runner's settle step covers the relayout too.
        if (scene->windowWidth != run->windowWidth || scene->windowHeight != run->windowHeight) {
            run->window->setBounds({100, 100, scene->windowWidth, scene->windowHeight});
            run->windowWidth = scene->windowWidth;
            run->windowHeight = scene->windowHeight;
        }
        auto datasets = choc::value::createObject("Datasets");
        for (const auto& [alias, path] : run->script.datasets) datasets.setMember(alias, path);
        auto result = choc::value::createObject("PendingScene");
        result.setMember("scene", scene->json);
        result.setMember("datasets", datasets);
        result.setMember("windowWidth", scene->windowWidth);
        result.setMember("windowHeight", scene->windowHeight);
        result.setMember("index", static_cast<int>(run->index));
        result.setMember("total", static_cast<int>(run->script.scenes.size()));

        const std::string id = scene->id;
        run->lastStep = "page loaded";
        run->watchdog = choc::messageloop::Timer(SceneRun::kSceneTimeoutMs, [run, id] {
            if (!run->finished)
                run->fail(id, "timed out after " + std::to_string(SceneRun::kSceneTimeoutMs / 1000) +
                                  " s, last step: " + run->lastStep);
            return false;
        });
        return result;
    });
    if (run == nullptr) return;

    view.bind("captureScene", [run, &bridge](const choc::value::ValueView& args) -> choc::value::Value {
        auto result = choc::value::createObject("CaptureResult");
        const scenes::Scene* scene = run->current();
        const std::string id = args.isArray() && args.size() > 0 ? std::string(args[0].getWithDefault<std::string>({})) : "";
        if (scene == nullptr || scene->id != id) {
            result.setMember("ok", false);
            return result;
        }
        run->watchdog.clear();
        std::string error;
        if (bridge.anyDatasetDirty()) {
            run->fail(id, "the scene modified a dataset -- scenes must be read-only");
        } else if (!captureScene(*run, *scene, error)) {
            run->fail(id, error);
        } else if (++run->index == run->script.scenes.size()) {
            std::cout << run->script.scenes.size() << " scene(s) captured\n";
            run->finish(0);
        } else {
            bridge.closeAllDatasets();  // the next scene starts like a fresh launch
        }
        result.setMember("ok", !run->finished || run->exitCode == 0);
        result.setMember("done", run->finished);
        return result;
    });

    view.bind("sceneProgress", [run](const choc::value::ValueView& args) -> choc::value::Value {
        if (args.isArray() && args.size() > 0) run->lastStep = std::string(args[0].getWithDefault<std::string>({}));
        return {};
    });

    view.bind("sceneFailed", [run](const choc::value::ValueView& args) -> choc::value::Value {
        const scenes::Scene* scene = run->current();
        if (scene == nullptr) return {};
        run->watchdog.clear();
        const std::string error = args.isArray() && args.size() > 1 ? std::string(args[1].getWithDefault<std::string>({})) : "";
        run->fail(scene->id, error.empty() ? "failed in the frontend" : error);
        return {};
    });
}

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

int main(int argc, char** argv) {
    const std::string frontendDir = EDITOR_FRONTEND_DIR;

    // Files named on the command line, e.g.
    //     ./build/kronos_editor ../KRONOS-SOUNDS/INIT.PCG ../KRONOS-SOUNDS/K1_20260418.PCG
    // The main window's frontend fetches this list at startup (getStartupFiles(), bound
    // below) and opens each one as a dataset -- the first two land in the left and right
    // pane, any more are available from the panes' dataset selectors. Paths are made
    // absolute here (relative to the launch directory) so "./../x.PCG" and the same file
    // named twice resolve to one dataset. Anything starting with "-" is ignored (macOS can
    // pass launch flags such as -psn_...).
    //
    // Except the screenshot options, which take a value and are handled first:
    //     --scene <file>        capture every scene in <file>, then quit (docs/screenshots/README.md)
    //     --scene-data <dir>    where the scene file's datasets live (default: next to <file>)
    //     --scene-only <a,b>    just these scene ids
    // Positional files are ignored in scene mode -- the scene file alone decides what's open.
    //
    // An optional private module sees the arguments first and takes out its
    // own options (consumePrivateEditorArguments(), bridge/EditorExtension.h).
    std::vector<std::string> args(argv + (argc > 0 ? 1 : 0), argv + argc);
#ifdef EDITOR_HAS_PRIVATE_MODULE
    if (int exitCode = 0; !consumePrivateEditorArguments(args, exitCode)) return exitCode;
#endif
    std::vector<std::string> startupFiles;
    std::string scenePath, sceneDataDir;
    std::vector<std::string> sceneOnly;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--scene" || arg == "--scene-data" || arg == "--scene-only") {
            if (i + 1 >= args.size()) {
                std::cerr << arg << " needs a value\n";
                return 1;
            }
            const std::string value = args[++i];
            if (arg == "--scene") {
                scenePath = value;
            } else if (arg == "--scene-data") {
                sceneDataDir = value;
            } else {
                std::stringstream ids(value);
                for (std::string id; std::getline(ids, id, ',');)
                    if (!id.empty()) sceneOnly.push_back(id);
            }
            continue;
        }
        if (arg.empty() || arg[0] == '-') continue;
        std::error_code ec;
        std::filesystem::path path = std::filesystem::absolute(arg, ec);
        if (ec) path = arg;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        startupFiles.push_back((ec ? path.lexically_normal() : canonical).string());
    }

    std::unique_ptr<SceneRun> sceneRun;
    if (!scenePath.empty()) {
        if (!kronos::isWindowCaptureSupported()) {
            std::cerr << "--scene: window capture is only implemented on macOS\n";
            return 1;
        }
        sceneRun = std::make_unique<SceneRun>();
        std::string error;
        if (!scenes::loadSceneScript(scenePath, sceneDataDir, sceneOnly, sceneRun->script, error)) {
            std::cerr << scenePath << ": " << error << "\n";
            return 1;
        }
        startupFiles.clear();
    } else if (!sceneDataDir.empty() || !sceneOnly.empty()) {
        std::cerr << "--scene-data/--scene-only need --scene\n";
        return 1;
    }

    choc::ui::setWindowsDPIAwareness();
    choc::messageloop::initialise();

    EditorBridge bridge;

    // Every currently open top-level window. Not just the main window --
    // an optional private module can open more via the extension point
    // below, each closing independently; the whole app only quits once the
    // LAST one closes (see windowClosed below), not on the first.
    std::vector<std::unique_ptr<EditorWindowInstance>> openWindows;

    // The one Usage Guide window (2026-09-05, "i" button next to the pane-
    // visibility toggle) -- a singleton, not a new window per click:
    // openUsageGuideWindow below (bound per-window, see webviewIsReady's own
    // capture list further down) brings this one to front if it's already
    // open rather than stacking duplicates, same reasoning the private
    // module's own SGX-2 editor window uses for a given Program. Reset to
    // nullptr via its own onClosed callback once the user closes it, so a
    // later click knows to open a fresh one rather than dereferencing a
    // dangling pointer.
    EditorWindowInstance* usageGuideWindow = nullptr;

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
        // rather than a dev build. EDITOR_RELEASE_HARDENED is set by
        // CMakeLists.txt for a Release / embedded-resources build (it implies
        // EDITOR_EMBED_RESOURCES -- checked below); a Debug build keeps debug
        // mode on exactly as before.
#if defined(EDITOR_RELEASE_HARDENED)
    #if !defined(EDITOR_EMBED_RESOURCES)
        #error "EDITOR_RELEASE_HARDENED must imply EDITOR_EMBED_RESOURCES"
    #endif
        constexpr bool kEnableWebViewDebug = false;
#else
        constexpr bool kEnableWebViewDebug = true;
#endif
        // Can never silently regress: a hardened build that somehow set this
        // true would fail to compile here rather than ship with devtools.
#if defined(EDITOR_RELEASE_HARDENED)
        static_assert(!kEnableWebViewDebug, "hardened build must not enable WebView debug mode");
#endif
        options.enableDebugMode = kEnableWebViewDebug;

        options.fetchResource = [resourceDir, entryHtml](const std::string& path)
            -> std::optional<choc::ui::WebView::Options::Resource> {
#ifdef EDITOR_HAS_PRIVATE_MODULE
            if (auto resource = fetchPrivateEditorResource(path)) return resource;
#endif
            std::string relative = (path.empty() || path == "/") ? entryHtml : path;
            return loadFrontendResource(relative, resourceDir);
        };

        options.webviewIsReady = [&bridge, &ctx, extraBindings, rawInstance, &createEditorWindow, frontendDir,
                                   &usageGuideWindow, &startupFiles, &sceneRun](choc::ui::WebView& view) {
            bindEditorBridgeFunctions(view, bridge);
            bindSceneFunctions(view, bridge, sceneRun.get());
            view.bind("getStartupFiles", [&startupFiles](const choc::value::ValueView&) {
                auto files = choc::value::createEmptyArray();
                for (const auto& file : startupFiles) files.addArrayElement(choc::value::Value(file));
                return files;
            });
#ifdef EDITOR_RELEASE_HARDENED
            // Defence-in-depth on top of enableDebugMode = false: swallow the
            // devtools keyboard shortcuts and the native context menu on
            // every window (main, SGX-2, Usage Guide -- this handler runs per
            // window). The app's own right-click row menus are unaffected --
            // showRowContextMenu() in frontend/pane.js builds its own DOM
            // dropdown and calls preventDefault() itself; this only kills the
            // browser-native menu. addInitScript covers reloads; the inline
            // evaluate covers the current (already-loading) document.
            static const std::string kLockdownJs = R"JS(
              (function () {
                if (window.__editorLockdownInstalled) return;
                window.__editorLockdownInstalled = true;
                window.addEventListener('keydown', function (e) {
                  var k = (e.key || '').toLowerCase();
                  if (k === 'f12'
                      || ((e.ctrlKey || e.metaKey) && e.shiftKey && (k === 'i' || k === 'j' || k === 'c'))
                      || (e.metaKey && e.altKey && (k === 'i' || k === 'j' || k === 'c' || k === 'u'))) {
                    e.preventDefault();
                    e.stopPropagation();
                  }
                }, true);
                window.addEventListener('contextmenu', function (e) { e.preventDefault(); }, true);
              })();
            )JS";
            view.addInitScript(kLockdownJs);
            view.evaluateJavascript(kLockdownJs);
#endif
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
            // The "i" button next to the pane-visibility toggle (2026-09-05,
            // reported directly) -- opens frontend/help.html (this repo's own
            // frontendDir, never a private module's, since the Usage Guide is
            // public-project content) in a separate top-level window, so it
            // can be dragged to a second screen and left open while working.
            // Bound on EVERY window (main or otherwise), same "full bridge
            // surface everywhere" reasoning as bindEditorBridgeFunctions()
            // itself -- there's no reason a secondary window couldn't also
            // offer it. Re-focuses the existing window instead of opening a
            // second one if it's already open (usageGuideWindow reset to
            // nullptr by its own onClosed below once the user closes it).
            view.bind("openUsageGuideWindow", [&createEditorWindow, frontendDir,
                                                &usageGuideWindow](const choc::value::ValueView&) -> choc::value::Value {
                if (usageGuideWindow != nullptr) {
                    usageGuideWindow->window->toFront();
                    return choc::value::Value();
                }
                usageGuideWindow = createEditorWindow("/help.html", "DIY Kronos Editor -- Usage Guide", 720, 640, 480,
                                                       400, nullptr, [&usageGuideWindow] { usageGuideWindow = nullptr; },
                                                       frontendDir);
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
        // `viewPtr` itself doubles as this listener's own removal key below
        // -- unique for exactly as long as the listener capturing it stays
        // registered, which is exactly the lifetime windowClosed's own
        // removeDatasetsChangedListener() call needs to bound.
        bridge.addDatasetsChangedListener(viewPtr, [viewPtr] {
            viewPtr->evaluateJavascript("if (window.refreshDatasets) window.refreshDatasets();");
        });

        instance->window->windowClosed = [&openWindows, rawInstance, onClosed, &bridge, viewPtr] {
            // FIXED (2026-09-11, reported directly -- a real crash): without
            // this, closing ANY secondary window (Usage Guide, an optional
            // private module's own window) left its listener above
            // registered forever, capturing `viewPtr` by raw pointer -- the
            // NEXT dataset change anywhere (e.g. opening another file)
            // called into that now-destroyed WebView and crashed
            // (EXC_BAD_ACCESS inside evaluateJavascript). Must run before
            // `instance`/its WebView are actually destroyed below, which
            // this does (openWindows.erase() is what triggers that).
            bridge.removeDatasetsChangedListener(viewPtr);
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

    // Scene mode: the first scene's size, not resizable, so every run
    // captures identical geometry (a rectangle is a percentage of it); a
    // plain title, since the build timestamp would change every screenshot.
    const scenes::Scene* firstScene = sceneRun ? sceneRun->current() : nullptr;
    const int mainWidth = firstScene ? firstScene->windowWidth : 1100;
    const int mainHeight = firstScene ? firstScene->windowHeight : 650;
    const std::string mainTitle =
        sceneRun ? "DIY Kronos Editor" : "DIY Kronos Editor (jens-goes-mad with claude) -- built " __DATE__ " " __TIME__;
    EditorWindowInstance* mainWindow = createEditorWindow("/index.html", mainTitle, mainWidth, mainHeight,
                                                          std::min(800, mainWidth), std::min(500, mainHeight), nullptr,
                                                          nullptr, frontendDir);
    if (mainWindow == nullptr) return 1;
    if (sceneRun) {
        mainWindow->window->setResizable(false);
        mainWindow->window->setMinimumSize(200, 200);  // a later scene may be smaller than the first
        sceneRun->window = mainWindow->window.get();
        sceneRun->windowWidth = mainWidth;
        sceneRun->windowHeight = mainHeight;
    }
#ifdef EDITOR_HAS_PRIVATE_MODULE
    privateEditorMainWindowCreated(*mainWindow->window, *mainWindow->webView);
#endif

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
    const int exitCode = sceneRun ? sceneRun->exitCode : 0;
#ifdef EDITOR_HAS_PRIVATE_MODULE
    return privateEditorExitCode(exitCode);
#else
    return exitCode;
#endif
}
