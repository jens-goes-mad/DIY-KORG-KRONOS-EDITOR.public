// Tests for the screenshot scene-file parser (src/scenes/SceneScript.cpp,
// `kronos_editor --scene`): validation errors, path resolution, --scene-only
// filtering, and the percent-to-pixel rectangle math.
//
// Hand-rolled assertions, no framework -- matching pcg_file_test.cpp.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "scenes/SceneScript.h"

namespace {

int g_failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                      \
        if (!(expr)) {                                                        \
            ++g_failures;                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        }                                                                     \
    } while (0)

namespace fs = std::filesystem;

// A scratch directory with one dataset file and a docs/ output directory --
// the parser checks both exist.
struct Fixture {
    fs::path dir;
    Fixture() {
        dir = fs::temp_directory_path() / "scene_script_test";
        fs::remove_all(dir);
        fs::create_directories(dir / "data");
        fs::create_directories(dir / "docs");
        std::ofstream(dir / "data" / "DEMO.PCG") << "x";
    }
    ~Fixture() { fs::remove_all(dir); }

    bool parse(const std::string& json, scenes::SceneScript& out, std::string& error,
               const std::vector<std::string>& only = {}) const {
        error.clear();
        return scenes::parseSceneScript(json, dir, dir / "data", only, out, error);
    }
};

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

const char* kValid = R"({
  "version": 1,
  "window": { "width": 1600, "height": 1000 },
  "datasets": { "main": "DEMO.PCG" },
  "scenes": [
    { "id": "a", "out": "docs/a.png", "left": { "dataset": "main", "category": "setlist" } },
    { "id": "b", "out": "docs/b.png", "rectangle": [50, 50, 100, 100] },
    { "id": "c", "out": "docs/c.png", "rectangle": [0, 0, 33.5, 100], "window": { "width": 900, "height": 500 } }
  ]
})";

void testValidFile(const Fixture& fx) {
    scenes::SceneScript s;
    std::string error;
    CHECK(fx.parse(kValid, s, error));
    CHECK(error.empty());
    CHECK(s.windowWidth == 1600 && s.windowHeight == 1000);
    CHECK(s.datasets.size() == 1 && s.datasets[0].first == "main");
    CHECK(fs::path(s.datasets[0].second).is_absolute());
    CHECK(fs::equivalent(s.datasets[0].second, fx.dir / "data" / "DEMO.PCG"));
    CHECK(s.scenes.size() == 3);
    CHECK(s.scenes[0].id == "a");
    CHECK(fs::path(s.scenes[0].outPath) == fs::weakly_canonical(fx.dir / "docs" / "a.png"));
    // Omitted rectangle = the whole window.
    CHECK(s.scenes[0].rectangle.left == 0 && s.scenes[0].rectangle.top == 0);
    CHECK(s.scenes[0].rectangle.right == 100 && s.scenes[0].rectangle.bottom == 100);
    CHECK(s.scenes[1].rectangle.left == 50 && s.scenes[1].rectangle.bottom == 100);
    CHECK(s.scenes[2].rectangle.right == 33.5);
    // A scene's own window overrides the file's; others inherit it.
    CHECK(s.scenes[0].windowWidth == 1600 && s.scenes[0].windowHeight == 1000);
    CHECK(s.scenes[2].windowWidth == 900 && s.scenes[2].windowHeight == 500);
    // UI fields pass through untouched for the frontend.
    CHECK(s.scenes[0].json["left"]["category"].getString() == "setlist");
}

void testDefaults(const Fixture& fx) {
    scenes::SceneScript s;
    std::string error;
    CHECK(fx.parse(R"({"version":1,"scenes":[{"id":"a","out":"docs/a.png"}]})", s, error));
    CHECK(s.windowWidth == 1100 && s.windowHeight == 650);  // main.cpp's normal main-window size
    CHECK(s.datasets.empty());
}

void testOnly(const Fixture& fx) {
    scenes::SceneScript s;
    std::string error;
    CHECK(fx.parse(kValid, s, error, {"c", "a"}));
    CHECK(s.scenes.size() == 2 && s.scenes[0].id == "a" && s.scenes[1].id == "c");  // file order, not --scene-only order
    CHECK(!fx.parse(kValid, s, error, {"nope"}));
    CHECK(contains(error, "nope"));
}

void testErrors(const Fixture& fx) {
    struct Case {
        const char* json;
        const char* expectedError;
    };
    const Case cases[] = {
        {"{", "invalid JSON"},
        {"[]", "top level must be an object"},
        {R"({"version":2,"scenes":[{"id":"a","out":"docs/a.png"}]})", "\"version\" must be 1"},
        {R"({"version":1,"scenes":[{"id":"a","out":"docs/a.png"}],"extra":1})", "unknown field \"extra\""},
        {R"({"version":1,"scenes":[]})", "non-empty array"},
        {R"({"version":1,"window":{"width":0,"height":10},"scenes":[{"id":"a","out":"docs/a.png"}]})", "width"},
        {R"({"version":1,"window":[1,2],"scenes":[{"id":"a","out":"docs/a.png"}]})", "{width, height}"},
        {R"({"version":1,"scenes":[{"id":"a","out":"docs/a.png","window":{"width":10}}]})", "scene \"a\": window \"height\""},
        {R"({"version":1,"window":{"width":10,"height":10,"x":1},"scenes":[{"id":"a","out":"docs/a.png"}]})",
         "unknown field \"x\""},
        {R"({"version":1,"datasets":{"m":"MISSING.PCG"},"scenes":[{"id":"a","out":"docs/a.png"}]})", "file not found"},
        {R"({"version":1,"scenes":[{"out":"docs/a.png"}]})", "\"id\""},
        {R"({"version":1,"scenes":[{"id":"a","out":"docs/a.png"},{"id":"a","out":"docs/b.png"}]})", "duplicate id"},
        {R"({"version":1,"scenes":[{"id":"a","out":"docs/a.jpg"}]})", ".png"},
        {R"({"version":1,"scenes":[{"id":"a","out":"nodir/a.png"}]})", "output directory doesn't exist"},
        {R"({"version":1,"scenes":[{"id":"a","out":"docs/a.png","rectangle":[0,0,50]}]})", "[left, top, right, bottom]"},
        {R"({"version":1,"scenes":[{"id":"a","out":"docs/a.png","rectangle":[50,0,50,100]}]})", "left < right"},
        {R"({"version":1,"scenes":[{"id":"a","out":"docs/a.png","rectangle":[0,60,100,40]}]})", "top < bottom"},
        {R"({"version":1,"scenes":[{"id":"a","out":"docs/a.png","rectangle":[0,0,100,101]}]})", "<= 100"},
        {R"({"version":1,"scenes":[{"id":"a","out":"docs/a.png","rectangle":[-1,0,100,100]}]})", "0 <= left"},
        {R"({"version":1,"scenes":[{"id":"a","out":"docs/a.png","rectangle":[0,0,"50",100]}]})", "numbers"},
    };
    for (const auto& c : cases) {
        scenes::SceneScript s;
        std::string error;
        const bool ok = fx.parse(c.json, s, error);
        CHECK(!ok);
        if (ok || !contains(error, c.expectedError)) {
            std::fprintf(stderr, "  case %s\n  expected error containing: %s\n  got: %s\n", c.json, c.expectedError,
                         error.c_str());
        }
    }
}

void testPixelRect() {
    // Retina capture of a 1600x1000-point window incl. a 28 pt title bar.
    const int w = 3200, h = 2056;
    auto r = scenes::toPixelRect({0, 0, 100, 100}, w, h);
    CHECK(r.x == 0 && r.y == 0 && r.width == w && r.height == h);
    r = scenes::toPixelRect({0, 0, 50, 50}, w, h);  // upper-left quarter
    CHECK(r.x == 0 && r.y == 0 && r.width == 1600 && r.height == 1028);
    r = scenes::toPixelRect({50, 50, 100, 100}, w, h);  // centre to bottom right
    CHECK(r.x == 1600 && r.y == 1028 && r.width == 1600 && r.height == 1028);

    // Adjacent rectangles share their edge exactly, even with odd sizes.
    const auto left = scenes::toPixelRect({0, 0, 33.3, 100}, 1001, 7);
    const auto right = scenes::toPixelRect({33.3, 0, 100, 100}, 1001, 7);
    CHECK(left.x + left.width == right.x);
    CHECK(right.x + right.width == 1001);

    // A sliver that rounds to nothing still yields 1 px, inside the image.
    r = scenes::toPixelRect({99.99, 99.99, 100, 100}, 100, 100);
    CHECK(r.width >= 1 && r.height >= 1 && r.x + r.width <= 100 && r.y + r.height <= 100);
}

}  // namespace

int main() {
    {
        Fixture fx;
        testValidFile(fx);
        testDefaults(fx);
        testOnly(fx);
        testErrors(fx);
    }
    testPixelRect();

    if (g_failures == 0) std::printf("scene_script_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
