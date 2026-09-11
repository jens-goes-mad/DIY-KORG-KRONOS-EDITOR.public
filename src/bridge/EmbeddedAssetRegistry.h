#pragma once

#include <functional>
#include <optional>
#include <string>

#include "choc/gui/choc_WebView.h"

// A process-wide fallback for embedded-asset lookups in a Release build.
//
// The public repo's own frontend/ is embedded as the `editor_embedded`
// table (tools/embed_resources.py, see src/main.cpp's loadFrontendResource()).
// An optional private companion module has its OWN frontend/ (e.g. the SGX-2
// editor UI) which the public repo can't see at build time -- it embeds that
// into its own separate table and registers a resolver here, which
// loadFrontendResource() consults when its own table misses.
//
// Path namespaces are disjoint (public: /index.html, /pane.js, /components/*;
// private: /sgx-2/*, /common/*), so lookup order doesn't matter, and a build
// without the private submodule simply never registers anything.

namespace kronos {

using EmbeddedAssetResolver =
    std::function<std::optional<choc::ui::WebView::Options::Resource>(const std::string& path)>;

// Called once (guarded by the caller). A second call replaces the first.
void registerEmbeddedAssetResolver(EmbeddedAssetResolver resolver);

// Returns std::nullopt if no resolver is registered or it doesn't have the
// asset.
std::optional<choc::ui::WebView::Options::Resource> resolveExtraEmbeddedAsset(const std::string& path);

}  // namespace kronos
