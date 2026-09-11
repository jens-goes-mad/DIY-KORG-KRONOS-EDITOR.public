cmake_minimum_required(VERSION 3.21)

# Must be set before project() to take effect -- ignored on non-Apple
# platforms. Without this, the binary's minimum-OS load command
# (LC_BUILD_VERSION) just takes whatever the build machine's own SDK
# defaults to, not a deliberate compatibility floor. Confirmed directly
# this actually matters, not just in theory: the real v0.1.3 release
# binaries (built on GitHub's macos-15/macos-15-intel runners with no
# explicit deployment target) came out with `minos 15.0` baked in
# (`otool -l build/kronos_editor.app/Contents/MacOS/kronos_editor`),
# refusing to even launch on a real user's macOS 14.7.7 -- "You can't use
# this version of the application... with this version of macOS." 11.0
# (Big Sur, 2020) chosen as a broadly compatible floor; nothing in this
# codebase is known to need anything newer (the one genuinely
# version-gated call, WKWebView's isInspectable in choc_WebView.h, is
# already reached via a runtime respondsToSelector: check, not a
# compile-time availability attribute, so it doesn't push this floor up).
if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

# VERSION is just a fallback for the macOS .app bundle's CFBundleVersion/
# CFBundleShortVersionString (see EDITOR_BUNDLE_VERSION in the APPLE block
# below) -- used for a plain local build with no tag context. CI's release
# builds override it with the actual git tag being built
# (EDITOR_VERSION_OVERRIDE, native-build.yml's macOS Configure steps) so
# the two can never drift the way this hardcoded number already did twice
# in a row (v0.1.2/v0.1.3 both shipped still saying 0.1.1) -- bumping it by
# hand here is no longer required, kept current mostly for cosmetic reasons
# on a from-source build. Nothing else in the build reads this.
project(kronos_editor VERSION 0.1.3 LANGUAGES CXX)

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

# A build that embeds resources is a real shippable build -- lock it down:
# WebView debug mode forced off with a compile-time assert against regression
# (src/main.cpp), devtools-shortcut/context-menu suppression injected into
# every window, and a symbol-stripped Release binary (see the strip/
# visibility settings and the APPLE/Linux/MSVC blocks below). Defined here
# rather than derived from NDEBUG so a non-Release `-DEDITOR_EMBED_RESOURCES=ON`
# build is hardened too.
if (EDITOR_EMBED_RESOURCES)
    set(EDITOR_RELEASE_HARDENED ON)

    # Per-build obfuscation key for the embedded frontend (see
    # tools/gen_asset_key.py, tools/embed_resources.py --obfuscate,
    # src/kronos/AssetObfuscation.cpp). Generated ONCE per build tree: this
    # command has no DEPENDS, so it only runs when its outputs are missing (a
    # clean build dir / CI / fresh checkout). Both this repo's frontend embed
    # and the optional private module's own frontend embed consume
    # ${EDITOR_ASSET_KEY_BIN}, so the two share one key.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(EDITOR_ASSET_KEY_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/AssetKey.h")
    set(EDITOR_ASSET_KEY_BIN "${CMAKE_CURRENT_BINARY_DIR}/generated/asset_key.bin")
    add_custom_command(
        OUTPUT "${EDITOR_ASSET_KEY_HEADER}" "${EDITOR_ASSET_KEY_BIN}"
        COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_asset_key.py"
                "${EDITOR_ASSET_KEY_HEADER}"
                "${EDITOR_ASSET_KEY_BIN}"
        COMMENT "Generating per-build frontend-asset obfuscation key"
        VERBATIM
    )
    add_custom_target(editor_gen_asset_key DEPENDS "${EDITOR_ASSET_KEY_HEADER}" "${EDITOR_ASSET_KEY_BIN}")
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
                --obfuscate --key "${EDITOR_ASSET_KEY_BIN}"
        DEPENDS ${EDITOR_FRONTEND_ASSETS} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_resources.py"
                "${EDITOR_ASSET_KEY_BIN}"
        COMMENT "Embedding + obfuscating frontend/ assets into the binary (release build)"
        VERBATIM
    )

    # Shared obfuscation codec: AssetObfuscation.cpp is the pure transform
    # (deflate/inflate + keystream cipher, testable against a fixed key);
    # AssetKey.cpp reassembles the compiled-in per-build key from
    # generated/AssetKey.h; EmbeddedAssetRegistry.cpp is the fallback lookup
    # an optional private module registers its own embedded frontend through.
    target_sources(kronos_editor PRIVATE
        "${EDITOR_EMBED_SOURCE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/kronos/AssetObfuscation.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/kronos/AssetKey.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/bridge/EmbeddedAssetRegistry.cpp"
    )
    add_dependencies(kronos_editor editor_gen_asset_key)
    target_include_directories(kronos_editor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(kronos_editor PRIVATE EDITOR_EMBED_RESOURCES=1)
    if (EDITOR_RELEASE_HARDENED)
        target_compile_definitions(kronos_editor PRIVATE EDITOR_RELEASE_HARDENED=1)
    endif()

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
        # CI's release builds pass -DEDITOR_VERSION_OVERRIDE=<tag, v-stripped>
        # (native-build.yml) so the bundle's own version always matches the
        # git tag actually being built, with nothing to remember to bump by
        # hand. Falls back to PROJECT_VERSION above for a plain from-source
        # build with no tag context.
        set(EDITOR_BUNDLE_VERSION "${PROJECT_VERSION}")
        if (EDITOR_VERSION_OVERRIDE)
            set(EDITOR_BUNDLE_VERSION "${EDITOR_VERSION_OVERRIDE}")
        endif()
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

# Hardened (shippable) build: strip symbols and dead-strip unused code so the
# binary doesn't carry function names / unreferenced string literals that
# make the frontend obfuscation (above) pointless to have. kronos_editor
# only -- pcg_file_test and the dev-only tools below are untouched. A plain
# Debug build never sets EDITOR_RELEASE_HARDENED, so the normal dev loop is
# unaffected.
if (EDITOR_RELEASE_HARDENED)
    set_target_properties(kronos_editor PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_compile_options(kronos_editor PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections -fdata-sections>
    )
    if (APPLE)
        target_link_options(kronos_editor PRIVATE -Wl,-x -Wl,-dead_strip)
        add_custom_command(TARGET kronos_editor POST_BUILD
            COMMAND "${CMAKE_STRIP}" -x "$<TARGET_FILE:kronos_editor>"
            COMMENT "Stripping local symbols from kronos_editor")
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        target_link_options(kronos_editor PRIVATE -s -Wl,--gc-sections)
    elseif (MSVC)
        target_link_options(kronos_editor PRIVATE /OPT:REF /OPT:ICF)
    endif()
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

# Frontend-asset obfuscation round-trip: deobfuscateAssetWithKey() against a
# committed fixture produced by tools/embed_resources.py with a FIXED test
# key (tests/fixtures/asset_obfuscation.*). Pulls in third_party/choc for
# choc::zlib but nothing else -- kept a separate small target rather than
# folded into pcg_file_test, which deliberately has no choc dependency.
add_executable(asset_obfuscation_test
    tests/asset_obfuscation_test.cpp
    src/kronos/AssetObfuscation.cpp
)
target_include_directories(asset_obfuscation_test PRIVATE src third_party/choc)
target_compile_definitions(asset_obfuscation_test PRIVATE
    ASSET_OBFUSCATION_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)
add_test(NAME asset_obfuscation_test COMMAND asset_obfuscation_test)

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
