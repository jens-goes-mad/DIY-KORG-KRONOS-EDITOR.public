#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "choc/containers/choc_Value.h"

// Screenshot scene files (`kronos_editor --scene <file>`, see src/main.cpp and
// docs/screenshots/README.md): one JSON file describing every documentation
// screenshot -- which datasets are open, what each pane shows, which part of
// the window ends up in which PNG. This half is pure parsing/validation and
// the percent-to-pixel math, no window or WebView code, so it's unit-tested
// on its own (tests/scene_script_test.cpp).
//
// Only the fields the NATIVE side needs are validated here (top level, id,
// out, rectangle, window). Everything describing UI state (left/right/...) is passed
// through untouched as `Scene::json` and validated by the frontend's
// scene-runner.js, the only code that knows what those fields mean -- one
// place per field, not a second copy of the pane vocabulary in C++.
namespace scenes {

// Capture area in percent of the captured window image (title bar included):
// two corners, [left, top, right, bottom]. Omitted = the whole window.
struct Rectangle {
    double left = 0, top = 0, right = 100, bottom = 100;
};

struct PixelRect {
    int x = 0, y = 0, width = 0, height = 0;
};

struct Scene {
    std::string id;
    std::string outPath;  // absolute
    Rectangle rectangle;
    int windowWidth = 0, windowHeight = 0;  // the scene's own "window", else the file's
    choc::value::Value json;  // the scene object as written, handed to the frontend
};

struct SceneScript {
    int windowWidth = 1100;
    int windowHeight = 650;
    std::vector<std::pair<std::string, std::string>> datasets;  // alias -> absolute path, file order
    std::vector<Scene> scenes;
};

// Parses and validates a scene file's text. Relative `out` paths resolve
// against `sceneDir` (where the scene file lives), relative dataset paths
// against `dataDir` (--scene-data, or the scene file's own directory). A
// non-empty `only` keeps just those scene ids, in file order -- an id that
// doesn't exist is an error, not silently nothing. Dataset files must exist.
// Returns false and sets `error` on the first problem.
bool parseSceneScript(std::string_view jsonText, const std::filesystem::path& sceneDir,
                      const std::filesystem::path& dataDir, const std::vector<std::string>& only, SceneScript& out,
                      std::string& error);

// Reads `scenePath` and calls parseSceneScript(); an empty `dataDir` means
// the scene file's own directory.
bool loadSceneScript(const std::string& scenePath, const std::string& dataDir, const std::vector<std::string>& only,
                     SceneScript& out, std::string& error);

// The scene's rectangle in image pixels for an image of the given size.
// Edges are rounded independently, so adjacent rectangles ([0,0,50,100] and
// [50,0,100,100]) share their boundary exactly; never smaller than 1x1.
PixelRect toPixelRect(const Rectangle& rectangle, int imageWidth, int imageHeight);

}  // namespace scenes
