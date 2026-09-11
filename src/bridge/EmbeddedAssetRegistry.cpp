#include "bridge/EmbeddedAssetRegistry.h"

namespace kronos {

namespace {
EmbeddedAssetResolver& resolverSlot() {
    static EmbeddedAssetResolver resolver;
    return resolver;
}
}  // namespace

void registerEmbeddedAssetResolver(EmbeddedAssetResolver resolver) { resolverSlot() = std::move(resolver); }

std::optional<choc::ui::WebView::Options::Resource> resolveExtraEmbeddedAsset(const std::string& path) {
    auto& resolver = resolverSlot();
    if (!resolver) return std::nullopt;
    return resolver(path);
}

}  // namespace kronos
