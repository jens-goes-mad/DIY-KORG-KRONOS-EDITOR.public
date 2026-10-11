#include "SceneScript.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include "choc/text/choc_JSON.h"

namespace scenes {

namespace {

bool isNumber(const choc::value::ValueView& v) {
    return v.isInt32() || v.isInt64() || v.isFloat32() || v.isFloat64();
}

double numberOf(const choc::value::ValueView& v) {
    if (v.isInt32()) return v.getInt32();
    if (v.isInt64()) return static_cast<double>(v.getInt64());
    if (v.isFloat32()) return v.getFloat32();
    return v.getFloat64();
}

// Same normalization as main.cpp's command-line files, so a dataset named
// here and the same file named on the command line (or reopened after a
// scene's page reload) resolve to one path -- EditorBridge::openFileAtPath()
// dedupes by exact path string.
std::string absolutePath(const std::filesystem::path& base, const std::string& path) {
    std::filesystem::path p(path);
    if (p.is_relative()) p = base / p;
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(p, ec);
    return (ec ? p.lexically_normal() : canonical).string();
}

bool checkOnlyKeys(const choc::value::ValueView& object, const std::set<std::string>& allowed, const std::string& where,
                   std::string& error) {
    for (uint32_t i = 0; i < object.size(); ++i) {
        const std::string name = object.getObjectMemberAt(i).name;
        if (!allowed.count(name)) {
            error = where + ": unknown field \"" + name + "\"";
            return false;
        }
    }
    return true;
}

// `{width, height}` in points, both positive integers.
bool parseWindow(const choc::value::ValueView& v, int& width, int& height, const std::string& where, std::string& error) {
    if (!v.isObject()) {
        error = where + ": \"window\" must be an object {width, height}";
        return false;
    }
    if (!checkOnlyKeys(v, {"width", "height"}, where + ": window", error)) return false;
    for (const char* key : {"width", "height"}) {
        if (!v.hasObjectMember(key) || !v[key].isInt() || v[key].getWithDefault<int64_t>(0) <= 0) {
            error = where + ": window \"" + key + "\" must be a positive integer";
            return false;
        }
    }
    width = static_cast<int>(v["width"].getWithDefault<int64_t>(0));
    height = static_cast<int>(v["height"].getWithDefault<int64_t>(0));
    return true;
}

bool parseRectangle(const choc::value::ValueView& v, Rectangle& out, const std::string& where, std::string& error) {
    if (!v.isArray() || v.size() != 4) {
        error = where + ": \"rectangle\" must be [left, top, right, bottom] in percent";
        return false;
    }
    double n[4];
    for (uint32_t i = 0; i < 4; ++i) {
        if (!isNumber(v[i])) {
            error = where + ": \"rectangle\" values must be numbers";
            return false;
        }
        n[i] = numberOf(v[i]);
    }
    const bool ok = n[0] >= 0 && n[0] < n[2] && n[2] <= 100 && n[1] >= 0 && n[1] < n[3] && n[3] <= 100;
    if (!ok) {
        error = where + ": \"rectangle\" needs 0 <= left < right <= 100 and 0 <= top < bottom <= 100";
        return false;
    }
    out = {n[0], n[1], n[2], n[3]};
    return true;
}

}  // namespace

bool parseSceneScript(std::string_view jsonText, const std::filesystem::path& sceneDir,
                      const std::filesystem::path& dataDir, const std::vector<std::string>& only, SceneScript& out,
                      std::string& error) {
    choc::value::Value root;
    try {
        root = choc::json::parse(jsonText);
    } catch (const choc::json::ParseError& e) {
        error = std::string("invalid JSON: ") + e.what() + " (line " + std::to_string(e.lineAndColumn.line) + ")";
        return false;
    }

    if (!root.isObject()) {
        error = "the top level must be an object with a \"scenes\" array";
        return false;
    }
    if (!checkOnlyKeys(root, {"version", "window", "datasets", "scenes"}, "top level", error)) return false;

    if (!root.hasObjectMember("version") || !root["version"].isInt() || root["version"].getWithDefault<int64_t>(0) != 1) {
        error = "\"version\" must be 1";
        return false;
    }

    SceneScript script;

    if (root.hasObjectMember("window") &&
        !parseWindow(root["window"], script.windowWidth, script.windowHeight, "top level", error))
        return false;

    if (root.hasObjectMember("datasets")) {
        const auto datasets = root["datasets"];
        if (!datasets.isObject()) {
            error = "\"datasets\" must be an object {alias: file}";
            return false;
        }
        for (uint32_t i = 0; i < datasets.size(); ++i) {
            const auto member = datasets.getObjectMemberAt(i);
            if (!member.value.isString() || member.value.getString().empty()) {
                error = std::string("datasets: \"") + member.name + "\" must be a file name";
                return false;
            }
            const std::string path = absolutePath(dataDir, std::string(member.value.getString()));
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec)) {
                error = std::string("datasets: \"") + member.name + "\": file not found: " + path;
                return false;
            }
            script.datasets.emplace_back(member.name, path);
        }
    }

    if (!root.hasObjectMember("scenes") || !root["scenes"].isArray() || root["scenes"].size() == 0) {
        error = "\"scenes\" must be a non-empty array";
        return false;
    }

    std::set<std::string> seenIds;
    const auto scenes = root["scenes"];
    for (uint32_t i = 0; i < scenes.size(); ++i) {
        const auto v = scenes[i];
        std::string where = "scenes[" + std::to_string(i) + "]";
        if (!v.isObject()) {
            error = where + ": must be an object";
            return false;
        }
        if (!v.hasObjectMember("id") || !v["id"].isString() || v["id"].getString().empty()) {
            error = where + ": \"id\" must be a non-empty string";
            return false;
        }
        Scene scene;
        scene.id = std::string(v["id"].getString());
        where = "scene \"" + scene.id + "\"";
        if (!seenIds.insert(scene.id).second) {
            error = where + ": duplicate id";
            return false;
        }
        if (!v.hasObjectMember("out") || !v["out"].isString()) {
            error = where + ": \"out\" must be a .png path";
            return false;
        }
        const std::string outRaw(v["out"].getString());
        if (outRaw.size() < 5 || outRaw.compare(outRaw.size() - 4, 4, ".png") != 0) {
            error = where + ": \"out\" must be a .png path";
            return false;
        }
        scene.outPath = absolutePath(sceneDir, outRaw);
        std::error_code ec;
        if (!std::filesystem::is_directory(std::filesystem::path(scene.outPath).parent_path(), ec)) {
            error = where + ": output directory doesn't exist: " +
                    std::filesystem::path(scene.outPath).parent_path().string();
            return false;
        }
        if (v.hasObjectMember("rectangle") && !parseRectangle(v["rectangle"], scene.rectangle, where, error))
            return false;
        scene.windowWidth = script.windowWidth;
        scene.windowHeight = script.windowHeight;
        if (v.hasObjectMember("window") && !parseWindow(v["window"], scene.windowWidth, scene.windowHeight, where, error))
            return false;
        scene.json = choc::value::Value(v);
        script.scenes.push_back(std::move(scene));
    }

    if (!only.empty()) {
        for (const auto& id : only) {
            if (!seenIds.count(id)) {
                error = "--scene-only: no scene with id \"" + id + "\"";
                return false;
            }
        }
        script.scenes.erase(std::remove_if(script.scenes.begin(), script.scenes.end(),
                                           [&only](const Scene& s) {
                                               return std::find(only.begin(), only.end(), s.id) == only.end();
                                           }),
                            script.scenes.end());
    }

    out = std::move(script);
    return true;
}

bool loadSceneScript(const std::string& scenePath, const std::string& dataDir, const std::vector<std::string>& only,
                     SceneScript& out, std::string& error) {
    std::ifstream file(scenePath, std::ios::binary);
    if (!file) {
        error = "cannot read scene file " + scenePath;
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    const std::filesystem::path sceneDir = std::filesystem::absolute(scenePath).parent_path();
    const std::filesystem::path data = dataDir.empty() ? sceneDir : std::filesystem::absolute(dataDir);
    return parseSceneScript(ss.str(), sceneDir, data, only, out, error);
}

PixelRect toPixelRect(const Rectangle& r, int imageWidth, int imageHeight) {
    auto edge = [](double percent, int size) { return static_cast<int>(std::lround(percent * size / 100.0)); };
    PixelRect p;
    p.x = std::min(edge(r.left, imageWidth), imageWidth - 1);
    p.y = std::min(edge(r.top, imageHeight), imageHeight - 1);
    p.width = std::max(1, edge(r.right, imageWidth) - p.x);
    p.height = std::max(1, edge(r.bottom, imageHeight) - p.y);
    return p;
}

}  // namespace scenes
