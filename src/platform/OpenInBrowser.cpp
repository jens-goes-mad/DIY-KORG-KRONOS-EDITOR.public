#include "OpenInBrowser.h"

#include "choc/platform/choc_Platform.h"

#if CHOC_APPLE

#include "choc/platform/choc_ObjectiveCHelpers.h"

namespace kronos {

bool isOpenInBrowserSupported() {
    return true;
}

bool openInDefaultBrowser(const std::string& filePath) {
    using namespace choc::objc;
    bool opened = false;
    CHOC_AUTORELEASE_BEGIN
    id url = callClass<id>("NSURL", "fileURLWithPath:", getNSString(filePath));
    id workspace = callClass<id>("NSWorkspace", "sharedWorkspace");
    opened = call<BOOL>(workspace, "openURL:", url);
    CHOC_AUTORELEASE_END
    return opened;
}

}  // namespace kronos

#elif CHOC_WINDOWS || CHOC_LINUX

// Not implemented yet, for the same reason as NativeFileDialog.cpp's
// Windows/Linux branch: ShellExecuteW (Windows) / xdg-open (Linux) code
// can't be verified in this environment, so it isn't written blind. The
// Setlist print export reports "not supported on this platform" instead.
namespace kronos {

bool isOpenInBrowserSupported() {
    return false;
}

bool openInDefaultBrowser(const std::string&) {
    return false;
}

}  // namespace kronos

#endif
