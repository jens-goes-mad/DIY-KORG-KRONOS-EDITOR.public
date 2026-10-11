// wincap -- capture a single macOS window as an image or a video, by name.
//
//   wincap list [match]                         list on-screen windows (id, app, title, size)
//   wincap shot  <match|id> [-o out.jpg] [--shadow] [--rect T,L,B,R]
//   wincap video <match|id> [-o out.mov] [-d seconds] [--fps n] [--no-cursor] [--rect T,L,B,R]
//
// <match> is a case-insensitive substring of "App — Window title"; when several
// windows match, the frontmost one wins. A purely numeric argument is a window id
// (as printed by `wincap list`). Video recording stops after -d seconds or on Ctrl-C.
// --rect captures only part of the window: top, left, bottom, right edges in percent
// of the window size (default 0,0,100,100; upper half 0,0,50,100; right half 0,50,100,100).
//
// Needs Screen Recording permission for the terminal/app that runs it
// (System Settings > Privacy & Security > Screen Recording).
//
// Build: tools/wincap/build.sh [install-dir]

import AVFoundation
import AppKit
import CoreGraphics
import Foundation
import ImageIO
import ScreenCaptureKit
import UniformTypeIdentifiers

// Part of a window as fractions 0...1 of its size, top-left origin.
struct RelRect {
    var top = 0.0, left = 0.0, bottom = 1.0, right = 1.0
    var isFull: Bool { top == 0 && left == 0 && bottom == 1 && right == 1 }

    // "T,L,B,R" in percent.
    init() {}
    init(parsing s: String) {
        let v = s.split(separator: ",").map { Double($0.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: "%", with: "")) }
        guard v.count == 4, let t = v[0], let l = v[1], let b = v[2], let r = v[3],
              [t, l, b, r].allSatisfy({ (0...100).contains($0) }), t < b, l < r else {
            fail("--rect wants \"T,L,B,R\" percentages 0..100 with T<B and L<R, got \"\(s)\"")
        }
        (top, left, bottom, right) = (t / 100, l / 100, b / 100, r / 100)
    }

    // The matching sub-rectangle of a w x h area (top-left origin).
    func apply(width w: Double, height h: Double) -> CGRect {
        CGRect(x: left * w, y: top * h, width: (right - left) * w, height: (bottom - top) * h)
    }
}

struct WindowInfo {
    let id: CGWindowID
    let app: String
    let title: String
    let bounds: CGRect
    var label: String { title.isEmpty ? app : "\(app) — \(title)" }
}

func fail(_ msg: String) -> Never {
    FileHandle.standardError.write(("wincap: " + msg + "\n").data(using: .utf8)!)
    exit(1)
}

// Front-to-back list of normal (layer 0), non-trivial on-screen windows.
func onScreenWindows() -> [WindowInfo] {
    let opts: CGWindowListOption = [.optionOnScreenOnly, .excludeDesktopElements]
    guard let raw = CGWindowListCopyWindowInfo(opts, kCGNullWindowID) as? [[String: Any]] else { return [] }
    return raw.compactMap { d in
        guard (d[kCGWindowLayer as String] as? Int) == 0,
              let id = d[kCGWindowNumber as String] as? CGWindowID,
              let bd = d[kCGWindowBounds as String] as? NSDictionary,
              let rect = CGRect(dictionaryRepresentation: bd),
              rect.width > 40, rect.height > 40 else { return nil }
        return WindowInfo(id: id,
                          app: d[kCGWindowOwnerName as String] as? String ?? "?",
                          title: d[kCGWindowName as String] as? String ?? "",
                          bounds: rect)
    }
}

func resolveWindow(_ query: String) -> WindowInfo {
    let all = onScreenWindows()
    if let id = UInt32(query) {
        guard let w = all.first(where: { $0.id == id }) else { fail("no on-screen window with id \(id)") }
        return w
    }
    let q = query.lowercased()
    let hits = all.filter { $0.label.lowercased().contains(q) }
    guard let w = hits.first else { fail("no window matches \"\(query)\" (try `wincap list`)") }
    if hits.count > 1 {
        FileHandle.standardError.write("wincap: \(hits.count) windows match, using frontmost: [\(w.id)] \(w.label)\n".data(using: .utf8)!)
    }
    return w
}

func timestampedName(_ ext: String) -> String {
    let f = DateFormatter()
    f.dateFormat = "yyyy-MM-dd_HH.mm.ss"
    return "wincap_\(f.string(from: Date())).\(ext)"
}

// MARK: - commands

func cmdList(_ filter: String?) {
    for w in onScreenWindows() where filter == nil || w.label.lowercased().contains(filter!.lowercased()) {
        print(String(format: "%6u  %4.0fx%-4.0f  %@", w.id, w.bounds.width, w.bounds.height, w.label))
    }
}

func screencapture(_ id: CGWindowID, format: String, shadow: Bool, to path: String) {
    var args = ["-x", "-l\(id)", "-t", format]
    if !shadow { args.append("-o") }
    args.append(path)
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/sbin/screencapture")
    p.arguments = args
    do { try p.run() } catch { fail("cannot run screencapture: \(error)") }
    p.waitUntilExit()
    guard p.terminationStatus == 0, FileManager.default.fileExists(atPath: path) else {
        fail("screencapture failed (status \(p.terminationStatus)) -- Screen Recording permission granted?")
    }
}

func cmdShot(_ w: WindowInfo, out: String?, shadow: Bool, rect: RelRect) {
    let path = out ?? timestampedName("jpg")
    let ext = (path as NSString).pathExtension.lowercased()
    let format = ext.isEmpty ? "jpg" : (ext == "jpeg" ? "jpg" : ext)
    if rect.isFull {
        screencapture(w.id, format: format, shadow: shadow, to: path)
        print(path)
        return
    }

    // Partial: grab the whole window (never with shadow -- it would skew the
    // percentages), crop, and re-encode in the requested format.
    guard let type = UTType(filenameExtension: format), format != "pdf" else {
        fail("--rect cannot write .\(format) (use jpg, png, tiff or heic)")
    }
    let tmp = NSTemporaryDirectory() + "wincap-\(getpid()).png"
    defer { try? FileManager.default.removeItem(atPath: tmp) }
    screencapture(w.id, format: "png", shadow: false, to: tmp)
    guard let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: tmp) as CFURL, nil),
          let full = CGImageSourceCreateImageAtIndex(src, 0, nil) else { fail("cannot read capture") }
    let crop = rect.apply(width: Double(full.width), height: Double(full.height)).integral
    guard let part = full.cropping(to: crop),
          let dst = CGImageDestinationCreateWithURL(URL(fileURLWithPath: path) as CFURL, type.identifier as CFString, 1, nil) else {
        fail("cannot write \(path)")
    }
    // Keep the source's DPI metadata so Retina images still show at point size.
    CGImageDestinationAddImage(dst, part, CGImageSourceCopyPropertiesAtIndex(src, 0, nil))
    guard CGImageDestinationFinalize(dst) else { fail("cannot write \(path)") }
    print(path)
}

// All writer access happens on `queue` (frames arrive there, finish() hops onto it).
final class Recorder: NSObject, SCStreamOutput, SCStreamDelegate, @unchecked Sendable {
    let writer: AVAssetWriter
    let input: AVAssetWriterInput
    var started = false
    var frames = 0
    let queue = DispatchQueue(label: "wincap.frames")

    init(url: URL, width: Int, height: Int, fileType: AVFileType) throws {
        writer = try AVAssetWriter(outputURL: url, fileType: fileType)
        input = AVAssetWriterInput(mediaType: .video, outputSettings: [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: width,
            AVVideoHeightKey: height,
        ])
        input.expectsMediaDataInRealTime = true
        writer.add(input)
        writer.startWriting()
    }

    func stream(_ stream: SCStream, didOutputSampleBuffer sb: CMSampleBuffer, of type: SCStreamOutputType) {
        guard type == .screen, sb.isValid,
              let atts = CMSampleBufferGetSampleAttachmentsArray(sb, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
              let raw = atts.first?[.status] as? Int,
              SCFrameStatus(rawValue: raw) == .complete else { return }
        if !started {
            writer.startSession(atSourceTime: sb.presentationTimeStamp)
            started = true
        }
        if input.isReadyForMoreMediaData, input.append(sb) { frames += 1 }
    }

    func stream(_ stream: SCStream, didStopWithError error: Error) {
        fail("capture stopped: \(error.localizedDescription)")
    }

    func finish() async {
        await withCheckedContinuation { (c: CheckedContinuation<Void, Never>) in
            queue.async {
                self.input.markAsFinished()
                self.writer.finishWriting { c.resume() }
            }
        }
    }
}

func cmdVideo(_ w: WindowInfo, out: String?, seconds: Double?, fps: Int, cursor: Bool, rect: RelRect) async {
    let path = out ?? timestampedName("mov")
    let url = URL(fileURLWithPath: path)
    try? FileManager.default.removeItem(at: url)
    let fileType: AVFileType = url.pathExtension.lowercased() == "mp4" ? .mp4 : .mov

    let content: SCShareableContent
    do {
        content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
    } catch {
        fail("cannot list windows (\(error.localizedDescription)) -- Screen Recording permission granted?")
    }
    guard let scw = content.windows.first(where: { $0.windowID == w.id }) else { fail("window \(w.id) vanished") }

    let filter = SCContentFilter(desktopIndependentWindow: scw)
    let scale = CGFloat(filter.pointPixelScale)
    // sourceRect is in points, relative to the window's top-left corner.
    let source = rect.apply(width: scw.frame.width, height: scw.frame.height)
    // H.264 wants even dimensions.
    let width = Int(source.width * scale) & ~1
    let height = Int(source.height * scale) & ~1
    guard width >= 2, height >= 2 else { fail("--rect selects an empty area") }

    let cfg = SCStreamConfiguration()
    if !rect.isFull { cfg.sourceRect = source }
    cfg.width = width
    cfg.height = height
    cfg.minimumFrameInterval = CMTime(value: 1, timescale: CMTimeScale(fps))
    cfg.pixelFormat = kCVPixelFormatType_32BGRA
    cfg.showsCursor = cursor
    cfg.queueDepth = 6

    let rec: Recorder
    do { rec = try Recorder(url: url, width: width, height: height, fileType: fileType) } catch {
        fail("cannot create \(path): \(error.localizedDescription)")
    }
    let stream = SCStream(filter: filter, configuration: cfg, delegate: rec)
    do {
        try stream.addStreamOutput(rec, type: .screen, sampleHandlerQueue: rec.queue)
        try await stream.startCapture()
    } catch {
        fail("cannot start capture: \(error.localizedDescription)")
    }

    FileHandle.standardError.write(("wincap: recording [\(w.id)] \(w.label) at \(width)x\(height) -- "
        + (seconds.map { "\($0)s" } ?? "Ctrl-C to stop") + "\n").data(using: .utf8)!)

    // Wait for the duration or Ctrl-C, whichever comes first.
    signal(SIGINT, SIG_IGN)
    let sigSrc = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
    await withCheckedContinuation { (c: CheckedContinuation<Void, Never>) in
        var done = false
        let resume = { if !done { done = true; c.resume() } }
        sigSrc.setEventHandler { resume() }
        sigSrc.resume()
        if let s = seconds { DispatchQueue.main.asyncAfter(deadline: .now() + s) { resume() } }
    }
    sigSrc.cancel()

    try? await stream.stopCapture()
    await rec.finish()
    if rec.writer.status != .completed {
        fail("writing failed: \(rec.writer.error?.localizedDescription ?? "no frames captured")")
    }
    FileHandle.standardError.write("wincap: \(rec.frames) frames\n".data(using: .utf8)!)
    print(path)
}

// MARK: - argument parsing

func usage() -> Never {
    print("""
    usage:
      wincap list [match]
      wincap shot  <match|id> [-o out.jpg] [--shadow] [--rect T,L,B,R]
      wincap video <match|id> [-o out.mov|.mp4] [-d seconds] [--fps n] [--no-cursor] [--rect T,L,B,R]

    <match>: case-insensitive substring of "App — Window title" (frontmost match wins),
    or a numeric window id from `wincap list`. Default output: ./wincap_<timestamp>.jpg/.mov
    Image format follows the -o extension (jpg, png, pdf, tiff, heic).
    --rect: part of the window, edges in percent: top,left,bottom,right (default 0,0,100,100).
            Upper half: 0,0,50,100   right half: 0,50,100,100
    """)
    exit(2)
}

var args = Array(CommandLine.arguments.dropFirst())
guard let cmd = args.first else { usage() }
args.removeFirst()

var positional: [String] = []
var out: String?
var seconds: Double?
var fps = 30
var shadow = false
var cursor = true
var rect = RelRect()
var i = 0
func nextValue(_ flag: String) -> String {
    i += 1
    guard i < args.count else { fail("\(flag) needs a value") }
    return args[i]
}
while i < args.count {
    switch args[i] {
    case "-o", "--output": out = nextValue("-o")
    case "-d", "--duration":
        guard let v = Double(nextValue("-d")), v > 0 else { fail("-d needs a positive number of seconds") }
        seconds = v
    case "--fps":
        guard let v = Int(nextValue("--fps")), (1...120).contains(v) else { fail("--fps needs 1..120") }
        fps = v
    case "--rect": rect = RelRect(parsing: nextValue("--rect"))
    case "--shadow": shadow = true
    case "--no-cursor": cursor = false
    case "-h", "--help": usage()
    default: positional.append(args[i])
    }
    i += 1
}

switch cmd {
case "list":
    cmdList(positional.first)
case "shot":
    guard positional.count == 1 else { usage() }
    cmdShot(resolveWindow(positional[0]), out: out, shadow: shadow, rect: rect)
case "video":
    guard positional.count == 1 else { usage() }
    await cmdVideo(resolveWindow(positional[0]), out: out, seconds: seconds, fps: fps, cursor: cursor, rect: rect)
default:
    usage()
}
