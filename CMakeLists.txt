cmake_minimum_required(VERSION 3.21)
# VERSION is just the macOS .app bundle's CFBundleVersion/
# CFBundleShortVersionString (see the APPLE block below) -- bump by hand
# alongside a new git tag (.github/workflows/native-build.yml's release
# job), not auto-derived from git describe or the tag itself. Nothing else
# in the build reads this.
project(kronos_editor VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Debug: frontend/ is read live from disk (fast edit-reload loop, but the
# built binary depends on the source tree being present at that path).
# Release: frontend/ is embedded into the binary at compile time as byte
# arrays (tools/embed_resources.py), so the packaged app ships as a single
# file with no loose HTML/JS/CSS to install alongside it.
option(EDITOR_EMBED_RESOURCES "Embed frontend/ into the binary instead of reading it from disk" OFF)
if (NOT EDITOR_EMBED_RESOURCES AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set(EDITOR_EMBED_RESOURCES ON)
endif()

add_executable(kronos_editor
    src/main.cpp
    src/bridge/EditorBridge.cpp
    src/platform/NativeFileDialog.cpp
    src/kronos/PcgFile.cpp
    src/kronos/ProgramDecoder.cpp
    src/kronos/CombiDecoder.cpp
)

target_include_directories(kronos_editor PRIVATE
    src
    third_party/choc
)

target_compile_definitions(kronos_editor PRIVATE
    EDITOR_FRONTEND_DIR="${CMAKE_CURRENT_SOURCE_DIR}/frontend"
    # Read live off disk, same as EDITOR_FRONTEND_DIR above -- resources/ is
    # small (a couple of raw Program-record templates, see resources/
    # Init-Program-HD1.raw/Init-Program-EXi.raw) and not yet wired into
    # EDITOR_EMBED_RESOURCES' release-build embedding below. That's a known,
    # deliberately deferred gap (no Release build is packaged/shipped yet),
    # not an oversight -- see STATE.md.
    EDITOR_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

# Optional private companion submodule (private/diy-korg-kronos-editor) --
# the SGX-2/EXi Program parameter editors and MIDI SysEx transport layer,
# kept in a separate PRIVATE repo rather than this public one (too large/
# open-ended a maintenance surface -- see STATE.md's multi-window entry for
# the full reasoning). Guarded on the submodule's own CMakeLists.txt
# existing, not just the directory, since `git submodule add` always
# creates the directory even before `git submodule update --init` has
# actually populated it -- so a clone without access to the private repo
# (every public contributor) still configures and builds kronos_editor
# fine, just without whatever this adds.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/private/diy-korg-kronos-editor/CMakeLists.txt")
    add_subdirectory(private/diy-korg-kronos-editor)
    # Lets main.cpp's call to registerPrivateEditorExtensions() (see
    # src/bridge/EditorExtension.h -- the generic extension point; this repo
    # has no idea what it actually does) compile in or out based on the
    # exact same condition -- so a build without the private submodule
    # never links against or calls into it at all. The frontend
    # (pane-program-editor.js's openSgx2Editor()) checks whether
    # window.openSgx2EditorWindow exists at all and shows a toast instead
    # of calling it when it's missing -- that function is bound (or not)
    # entirely by the private module's own code, never by this repo.
    target_compile_definitions(kronos_editor PRIVATE EDITOR_HAS_PRIVATE_MODULE=1)
endif()

if (EDITOR_EMBED_RESOURCES)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    file(GLOB_RECURSE EDITOR_FRONTEND_ASSETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/frontend/*")

    set(EDITOR_EMBED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedAssets.h")
    set(EDITOR_EMBED_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedAssets.cpp")

    add_custom_command(
        OUTPUT "${EDITOR_EMBED_HEADER}" "${EDITOR_EMBED_SOURCE}"
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_resources.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/frontend"
                "${EDITOR_EMBED_HEADER}"
                "${EDITOR_EMBED_SOURCE}"
                "editor_embedded"
        DEPENDS ${EDITOR_FRONTEND_ASSETS} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_resources.py"
        COMMENT "Embedding frontend/ assets into the binary (release build)"
        VERBATIM
    )

    target_sources(kronos_editor PRIVATE "${EDITOR_EMBED_SOURCE}")
    target_include_directories(kronos_editor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(kronos_editor PRIVATE EDITOR_EMBED_RESOURCES=1)

    # Same mechanism, second independent table (namespace
    # editor_embedded_resources, not editor_embedded -- two separate lookups,
    # see embed_resources.py's own doc comment) for resources/'s Init Program
    # templates. Closes the gap this file used to flag directly: those
    # templates (EditorBridge::readResourceFile(), used by "Reset entry" and
    # Duplicates resolution) were still read from EDITOR_RESOURCES_DIR, a
    # compile-time absolute path into wherever the source checkout was built
    # -- harmless for local dev (path still valid), but a real Release
    # binary handed to someone else would silently fail to find them, since
    # this project started actually shipping Release binaries via GitHub
    # Releases (see .github/workflows/native-build.yml's release job).
    file(GLOB EDITOR_RESOURCES_ASSETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/*")

    set(EDITOR_EMBED_RESOURCES_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedResources.h")
    set(EDITOR_EMBED_RESOURCES_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedResources.cpp")

    add_custom_command(
        OUTPUT "${EDITOR_EMBED_RESOURCES_HEADER}" "${EDITOR_EMBED_RESOURCES_SOURCE}"
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_resources.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/resources"
                "${EDITOR_EMBED_RESOURCES_HEADER}"
                "${EDITOR_EMBED_RESOURCES_SOURCE}"
                "editor_embedded_resources"
        DEPENDS ${EDITOR_RESOURCES_ASSETS} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_resources.py"
        COMMENT "Embedding resources/ (Init Program templates) into the binary (release build)"
        VERBATIM
    )

    target_sources(kronos_editor PRIVATE "${EDITOR_EMBED_RESOURCES_SOURCE}")
endif()

if (APPLE)
    target_link_libraries(kronos_editor PRIVATE "-framework WebKit" "-framework Cocoa")

    # Real .app bundle, Release builds only (EDITOR_EMBED_RESOURCES is the
    # existing "this is a real packaged/shippable build" marker, already
    # auto-on for Release -- see the option() above) -- a Debug build stays
    # a plain `build/kronos_editor` binary, unaffected, so the documented
    # local dev loop (`./build/kronos_editor` straight after `cmake --build
    # build`) keeps working exactly as before. Without this, Finder has no
    # way to know a bare Unix executable is a windowed GUI app -- double-
    # clicking one launches it INSIDE Terminal.app instead (confirmed
    # directly: the released v0.1.1 binaries did exactly this), which is
    # what a real .app bundle (an Info.plist + CFBundlePackageType=APPL)
    # exists to fix.
    if (EDITOR_EMBED_RESOURCES)
        set(EDITOR_BUNDLE_VERSION "${PROJECT_VERSION}")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/Info.plist.in"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/Info.plist"
            @ONLY
        )
        set_target_properties(kronos_editor PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/generated/Info.plist"
        )
        # No CFBundleIconFile/AppIcon.icns yet -- no icon asset exists in
        # this repo at all, not just unwired. Ships with the generic system
        # app icon until one gets designed; a real fix, not a placeholder
        # left half-done, just a smaller scope than this pass covers.
    endif()
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_check_modules(WEBKIT2 REQUIRED webkit2gtk-4.1)

    target_include_directories(kronos_editor PRIVATE ${GTK3_INCLUDE_DIRS} ${WEBKIT2_INCLUDE_DIRS})
    target_link_libraries(kronos_editor PRIVATE ${GTK3_LIBRARIES} ${WEBKIT2_LIBRARIES})
    target_compile_options(kronos_editor PRIVATE ${GTK3_CFLAGS_OTHER} ${WEBKIT2_CFLAGS_OTHER})
elseif (WIN32)
    # choc's WebView on Windows uses WebView2 via embedded COM interop headers
    # and the OS-provided Evergreen runtime -- no extra link libraries needed.
endif()

# Scoped test target: only PcgFile.cpp/ProgramDecoder.cpp/CombiDecoder.cpp,
# no main.cpp, no EditorBridge.cpp, no CHOC/WebView -- so `ctest` (or
# building this target alone) stays fast and never needs a platform WebView
# toolchain. See tests/pcg_file_test.cpp and STATE.md's "ARCHITECTURE:
# DECODER/ENCODER REFACTOR" section for why this is kept separate from
# kronos_editor.
enable_testing()

add_executable(pcg_file_test
    tests/pcg_file_test.cpp
    src/kronos/PcgFile.cpp
    src/kronos/ProgramDecoder.cpp
    src/kronos/CombiDecoder.cpp
)

target_include_directories(pcg_file_test PRIVATE src)

# Same define kronos_editor itself gets (see EDITOR_RESOURCES_DIR above) --
# lets tests read the real, byte-extracted resources/Init-Program-*.raw
# templates directly (testExiAlgorithmTypeRealTemplates()) instead of only
# ever testing against synthetic fixtures, same "real Kronos data makes
# tests worth trusting" convention as everywhere else in this project.
target_compile_definitions(pcg_file_test PRIVATE
    EDITOR_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

add_test(NAME pcg_file_test COMMAND pcg_file_test)

# Standalone hardware-validation CLI tool, same minimal dependency set as
# pcg_file_test above (no main.cpp/EditorBridge.cpp/CHOC) -- see
# tools/generate_setlist_test_matrix.cpp's own header comment.
add_executable(generate_setlist_test_matrix
    tools/generate_setlist_test_matrix.cpp
    src/kronos/PcgFile.cpp
    src/kronos/ProgramDecoder.cpp
    src/kronos/CombiDecoder.cpp
)

target_include_directories(generate_setlist_test_matrix PRIVATE src)
