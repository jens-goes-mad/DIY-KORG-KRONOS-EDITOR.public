#pragma once

#include <string>

namespace kronos {

// False on any platform OpenInBrowser.cpp doesn't implement yet (see its own
// comments) -- checked first, so callers can report "not supported here"
// instead of a generic failure.
bool isOpenInBrowserSupported();

// Opens a local file (an absolute path) with the system's default
// application for its type -- for an .html file, the default browser. Used
// by the Setlist's "Print Setlist…" export: printing and "Save as PDF" are
// left to the browser's own print dialog. Returns false if the system
// refused to open it.
bool openInDefaultBrowser(const std::string& filePath);

}  // namespace kronos
