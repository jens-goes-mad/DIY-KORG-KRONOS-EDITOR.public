#pragma once

#include <string>

namespace kronos {

// False on any platform WindowCapture.cpp doesn't implement (only macOS so
// far) -- `--scene` refuses to run there rather than failing per scene.
bool isWindowCaptureSupported();

// Writes a PNG of one native window (choc's DesktopWindow::getWindowHandle(),
// an NSWindow* on macOS) -- title bar included, no drop shadow -- via
// /usr/sbin/screencapture. The calling process (or whatever launched it,
// e.g. Terminal/VS Code) needs the Screen Recording permission once.
bool captureWindowPng(void* nativeWindowHandle, const std::string& pngPath, std::string& error);

// True while the window is the key (focused) window -- macOS draws an
// inactive window's title bar differently, so a capture that should look the
// same every run needs this.
bool isWindowActive(void* nativeWindowHandle);

// Pixel size of a PNG/any ImageIO-readable image.
bool imagePixelSize(const std::string& path, int& width, int& height, std::string& error);

// Writes the given pixel rectangle (origin top-left) of `inPath` to
// `outPath` as PNG. Done with ImageIO/CoreGraphics rather than `sips
// --cropOffset`, which ignores its offset on macOS 14.7 (verified
// 2026-10-02: every offset returned the bottom-right corner).
bool cropPng(const std::string& inPath, const std::string& outPath, int x, int y, int width, int height,
             std::string& error);

}  // namespace kronos
