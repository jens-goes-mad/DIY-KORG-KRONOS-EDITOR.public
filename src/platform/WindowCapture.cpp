#include "WindowCapture.h"

#include "choc/platform/choc_Platform.h"

#if CHOC_APPLE

#include <ApplicationServices/ApplicationServices.h>
#include <ImageIO/ImageIO.h>
#include <spawn.h>
#include <sys/wait.h>

#include <vector>

#include "choc/platform/choc_ObjectiveCHelpers.h"

extern char** environ;

namespace kronos {

namespace {

// Runs a program directly (no shell, so paths with spaces/quotes need no
// escaping) and waits for it.
bool runProcess(const std::vector<std::string>& args, std::string& error) {
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = 0;
    if (const int rc = posix_spawn(&pid, argv[0], nullptr, nullptr, argv.data(), environ); rc != 0) {
        error = args[0] + ": cannot start (posix_spawn error " + std::to_string(rc) + ")";
        return false;
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = args[0] + " failed (exit status " + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1) + ")";
        return false;
    }
    return true;
}

CFURLRef fileUrl(const std::string& path) {
    return CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()),
                                                   static_cast<CFIndex>(path.size()), false);
}

CGImageRef loadImage(const std::string& path, std::string& error) {
    CFURLRef url = fileUrl(path);
    CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (source == nullptr) {
        error = "cannot read image " + path;
        return nullptr;
    }
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (image == nullptr) error = "cannot decode image " + path;
    return image;
}

}  // namespace

bool isWindowCaptureSupported() {
    return true;
}

bool captureWindowPng(void* nativeWindowHandle, const std::string& pngPath, std::string& error) {
    const long windowNumber = choc::objc::call<long>((id)nativeWindowHandle, "windowNumber");
    // -x: no sound, -o: no shadow, -l: this one window by its window-server id.
    if (!runProcess({"/usr/sbin/screencapture", "-x", "-o", "-l" + std::to_string(windowNumber), pngPath}, error))
        return false;
    // screencapture exits 0 without writing anything in some refusal cases.
    int width = 0, height = 0;
    if (!imagePixelSize(pngPath, width, height, error)) {
        error = "screencapture wrote no image -- Screen Recording permission missing? (" + error + ")";
        return false;
    }
    return true;
}

bool isWindowActive(void* nativeWindowHandle) {
    return choc::objc::call<BOOL>((id)nativeWindowHandle, "isKeyWindow");
}

bool imagePixelSize(const std::string& path, int& width, int& height, std::string& error) {
    CGImageRef image = loadImage(path, error);
    if (image == nullptr) return false;
    width = static_cast<int>(CGImageGetWidth(image));
    height = static_cast<int>(CGImageGetHeight(image));
    CGImageRelease(image);
    return true;
}

bool cropPng(const std::string& inPath, const std::string& outPath, int x, int y, int width, int height,
             std::string& error) {
    CGImageRef image = loadImage(inPath, error);
    if (image == nullptr) return false;
    CGImageRef cropped = CGImageCreateWithImageInRect(image, CGRectMake(x, y, width, height));
    CGImageRelease(image);
    if (cropped == nullptr) {
        error = "crop rectangle outside the image";
        return false;
    }
    CFURLRef url = fileUrl(outPath);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
    CFRelease(url);
    bool ok = false;
    if (dest != nullptr) {
        CGImageDestinationAddImage(dest, cropped, nullptr);
        ok = CGImageDestinationFinalize(dest);
        CFRelease(dest);
    }
    CGImageRelease(cropped);
    if (!ok) error = "cannot write " + outPath;
    return ok;
}

}  // namespace kronos

#else

namespace kronos {

bool isWindowCaptureSupported() {
    return false;
}

bool captureWindowPng(void*, const std::string&, std::string& error) {
    error = "window capture is only implemented on macOS";
    return false;
}

bool isWindowActive(void*) {
    return false;
}

bool imagePixelSize(const std::string&, int&, int&, std::string& error) {
    error = "window capture is only implemented on macOS";
    return false;
}

bool cropPng(const std::string&, const std::string&, int, int, int, int, std::string& error) {
    error = "window capture is only implemented on macOS";
    return false;
}

}  // namespace kronos

#endif
