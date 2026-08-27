//
//    ██████ ██   ██  ██████   ██████
//   ██      ██   ██ ██    ██ ██            ** Classy Header-Only Classes **
//   ██      ███████ ██    ██ ██
//   ██      ██   ██ ██    ██ ██           https://github.com/Tracktion/choc
//    ██████ ██   ██  ██████   ██████
//
//   CHOC is (C)2022 Tracktion Corporation, and is offered under the terms of the ISC license:
//
//   Permission to use, copy, modify, and/or distribute this software for any purpose with or
//   without fee is hereby granted, provided that the above copyright notice and this permission
//   notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//   WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//   AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//   CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//   WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//   CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#ifndef CHOC_DESKTOPWINDOW_HEADER_INCLUDED
#define CHOC_DESKTOPWINDOW_HEADER_INCLUDED

#include <memory>
#include <string>
#include <functional>
#include "../platform/choc_Platform.h"
#include "../platform/choc_Assert.h"


//==============================================================================
namespace choc::ui
{

/// Represents the position and size of a DesktopWindow or other UI elements.
struct Bounds
{
    int x = 0, y = 0, width = 0, height = 0;
};

/**
    A very basic desktop window class.

    The main use-case for this is as a simple way to host other UI elements
    such as the choc::ui::WebView.

    Because this is a GUI, it needs a message loop to be running. If you're using
    it inside an app which already runs a message loop, it should just work,
    or you can use choc::messageloop::run() and choc::messageloop::stop() for an easy
    but basic loop.

    Note that on Linux this uses GTK, so to build it you'll need to:
       1. Install the libgtk-3-dev package.
       2. Link the gtk+3.0 library in your build.
          You might want to have a look inside choc/tests/CMakeLists.txt for
          an example of how to add this packages to your build without too
          much fuss.

    For an example of how to use this class, see `choc/tests/main.cpp` where
    there's a simple demo.
*/
struct DesktopWindow
{
    DesktopWindow (Bounds);
    ~DesktopWindow();

    /// Sets the title of the window that the browser is inside
    void setWindowTitle (const std::string& newTitle);

    /// Gives the window a child/content view to display.
    /// The pointer being passed in will be a platform-specific native handle,
    /// so a HWND on Windows, an NSView* on OSX, etc.
    void setContent (void* nativeView);

    /// Shows or hides the window. It's visible by default when created.
    void setVisible (bool visible);

    /// Changes the window's position
    void setBounds (Bounds);

    /// Returns the window's current position and size.
    Bounds getBounds();

    /// Enables/disables user resizing of the window
    void setResizable (bool);

    /// Enables/disables the window's close button (if applicable).
    void setClosable (bool);

    /// Gives the window a given size and positions it in the middle of the
    /// default monitor
    void centreWithSize (int width, int height);

    /// Sets a minimum size below which the user can't shrink the window
    void setMinimumSize (int minWidth, int minHeight);
    /// Sets a maximum size above which the user can't grow the window
    void setMaximumSize (int maxWidth, int maxHeight);

    /// Tries to bring this window to the front of the Z-order.
    void toFront();

    /// Returns the native OS handle, which may be a HWND on Windows, an
    /// NSWindow* on OSX or a GtkWidget* on linux.
    void* getWindowHandle() const;

    /// An optional callback that will be called when the parent window is resized
    std::function<void()> windowResized;
    /// An optional callback that will be called when the parent window is closed
    std::function<void()> windowClosed;

    /// DIY-KRONOS-EDITOR local addition (2026-08-26, see that project's own
    /// STATE.md -- "Guard against quitting with unsaved changes"): an
    /// optional callback invoked when the user attempts to close this window
    /// via any native gesture (title-bar close button, Cmd+Q, Alt+F4, a
    /// window-manager "Close", etc). Setting this SUPPRESSES the window's
    /// default native close behavior -- the window no longer closes on its
    /// own just because the user tried to; returning from this callback does
    /// nothing by itself. Call forceClose() (immediately, or later from an
    /// asynchronous decision such as a confirmation dialog resolving) to
    /// actually close the window for real. Leave unset (the default, and the
    /// only behavior every OTHER user of this vendored class still gets) for
    /// a close attempt to succeed immediately exactly as before --
    /// windowClosed still fires once the window is genuinely gone, in either
    /// case.
    std::function<void()> closeRequested;

    /// DIY-KRONOS-EDITOR local addition (2026-08-26) -- actually closes this
    /// window for real, bypassing closeRequested if it's set (so it's safe
    /// to call from inside that very callback, for a "yes, always allow"
    /// case, or later/asynchronously once some other decision resolves).
    /// Fires windowClosed() once done, exactly as an unintercepted close
    /// always did. A no-op if the window is already gone.
    void forceClose();

    /// Information about a file drop event - see setFileDropCallback()
    struct FileDropEvent
    {
        std::vector<std::string> filePaths;
        float x, y;
    };

    /// This callback should return true if the operation was handled, or false
    /// to allow it to be passed on to other handlers.
    using FileDropCallback = std::function<bool(const FileDropEvent&)>;

    /// Call this to enable file drag-and-drop support, and to set a callback
    /// that will be called when files are dropped onto the window. Pass an
    /// empty function to disable drag-and-drop.
    void setFileDropCallback (FileDropCallback);

private:
    struct Pimpl;
    std::unique_ptr<Pimpl> pimpl;
};

//==============================================================================
/// This Windows-only function turns on high-DPI awareness for the current
/// process. On other OSes where no equivalent call is needed, this function is
/// just a stub.
void setWindowsDPIAwareness();


} // namespace choc::ui


//==============================================================================
//        _        _           _  _
//     __| |  ___ | |_   __ _ (_)| | ___
//    / _` | / _ \| __| / _` || || |/ __|
//   | (_| ||  __/| |_ | (_| || || |\__ \ _  _  _
//    \__,_| \___| \__| \__,_||_||_||___/(_)(_)(_)
//
//   Code beyond this point is implementation detail...
//
//==============================================================================

#if CHOC_LINUX

struct choc::ui::DesktopWindow::Pimpl
{
    Pimpl (DesktopWindow& w, Bounds bounds)  : owner (w)
    {
        if (! gtk_init_check (nullptr, nullptr))
            return;

        window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        g_object_ref_sink (G_OBJECT (window));

        destroyHandlerID = g_signal_connect (G_OBJECT (window), "destroy",
                                             G_CALLBACK (+[](GtkWidget*, gpointer arg)
                                             {
                                                 static_cast<Pimpl*> (arg)->windowDestroyEvent();
                                             }),
                                             this);
        // DIY-KRONOS-EDITOR local addition (2026-08-26, UNVERIFIED -- no
        // Linux/GTK toolchain available where this was written, see that
        // project's own STATE.md): "delete-event" is GTK's own VETOABLE
        // close signal (fired when the window manager asks the window to
        // close -- the title-bar X, Alt+F4, etc); returning TRUE from its
        // handler stops the default action (destroying the widget) dead,
        // unlike "destroy" above, which only ever fires once destruction is
        // already underway -- too late to prevent anything. Needed as a
        // SEPARATE connection (closeRequested's own veto-then-confirm
        // model, see its own doc comment above) since gtk_widget_destroy()
        // (forceClose() below) does NOT re-emit "delete-event" -- only a
        // window-manager-initiated close does -- so there's no risk of this
        // handler re-vetoing a forceClose() call.
        deleteEventHandlerID = g_signal_connect (G_OBJECT (window), "delete-event",
                                                 G_CALLBACK (+[](GtkWidget*, GdkEvent*, gpointer arg) -> gboolean
                                                 {
                                                     return static_cast<Pimpl*> (arg)->windowDeleteEvent();
                                                 }),
                                                 this);
        setBounds (bounds);
        setVisible (true);
    }

    ~Pimpl()
    {
        if (window != nullptr)
        {
            if (destroyHandlerID != 0)
                g_signal_handler_disconnect (G_OBJECT (window), destroyHandlerID);
            if (deleteEventHandlerID != 0)
                g_signal_handler_disconnect (G_OBJECT (window), deleteEventHandlerID);
        }

        g_clear_object (&window);
    }

    void windowDestroyEvent()
    {
        g_clear_object (&window);

        if (owner.windowClosed != nullptr)
            owner.windowClosed();
    }

    // DIY-KRONOS-EDITOR local addition (2026-08-26, UNVERIFIED, see the
    // "delete-event" connection above for the full reasoning) -- returning
    // TRUE vetoes the close outright (owner.closeRequested is responsible
    // for calling forceClose() later if it decides the close should
    // actually happen); returning FALSE (the un-intercepted, default case)
    // lets GTK proceed exactly as it always did, which still ends up
    // emitting "destroy" -> windowDestroyEvent() -> windowClosed() above,
    // unchanged.
    gboolean windowDeleteEvent()
    {
        if (owner.closeRequested != nullptr)
        {
            owner.closeRequested();
            return TRUE;
        }

        return FALSE;
    }

    // DIY-KRONOS-EDITOR local addition (2026-08-26, UNVERIFIED) --
    // gtk_widget_destroy() is a direct, programmatic destroy: unlike a
    // window-manager-initiated close, it does NOT go through "delete-event"
    // at all, so this can never re-trigger windowDeleteEvent()'s own veto
    // above. Still emits "destroy" as normal, so windowClosed() above fires
    // exactly the same way a real close always did.
    void forceClose()
    {
        if (window != nullptr)
            gtk_widget_destroy (window);
    }

    void* getWindowHandle() const     { return (void*) window; }

    void setWindowTitle (const std::string& newTitle)
    {
        gtk_window_set_title (GTK_WINDOW (window), newTitle.c_str());
    }

    void setContent (void* view)
    {
        if (content != nullptr)
            gtk_container_remove (GTK_CONTAINER (window), content);

        content = GTK_WIDGET (view);
        gtk_container_add (GTK_CONTAINER (window), content);
        gtk_widget_grab_focus (content);
    }

    void setVisible (bool visible)
    {
        if (visible)
            gtk_widget_show_all (window);
        else
            gtk_widget_hide (window);
    }

    void setResizable (bool b) { gtk_window_set_resizable (GTK_WINDOW (window), b); }
    void setClosable (bool b)  { gtk_window_set_deletable (GTK_WINDOW (window), b); }

    void setMinimumSize (int w, int h)
    {
        GdkGeometry g;
        g.min_width = w;
        g.min_height = h;
        gtk_window_set_geometry_hints (GTK_WINDOW (window), nullptr, &g, GDK_HINT_MIN_SIZE);
    }

    void setMaximumSize (int w, int h)
    {
        GdkGeometry g;
        g.max_width = w;
        g.max_height = h;
        gtk_window_set_geometry_hints (GTK_WINDOW (window), nullptr, &g, GDK_HINT_MAX_SIZE);
    }

    void setBounds (Bounds b)
    {
        setSize (b.width, b.height);
        gtk_window_move (GTK_WINDOW (window), b.x, b.y);
    }

    void setSize (int w, int h)
    {
        if (gtk_window_get_resizable (GTK_WINDOW (window)))
            gtk_window_resize (GTK_WINDOW (window), w, h);
        else
            gtk_widget_set_size_request (window, w, h);
    }

    void centreWithSize (int w, int h)
    {
        setSize (w, h);
        gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);
    }

    void toFront()
    {
        gtk_window_activate_default (GTK_WINDOW (window));
    }

    Bounds getBounds()
    {
        int x = 0, y = 0, w = 0, h = 0;
        gtk_window_get_position (GTK_WINDOW (window), &x, &y);
        gtk_window_get_size (GTK_WINDOW (window), &w, &h);
        return { x, y, w, h };
    }

    void setFileDropCallback (FileDropCallback handler)
    {
        if (handler)
        {
            fileDropCallback = std::move (handler);

            gtk_drag_dest_set (window, GTK_DEST_DEFAULT_ALL, nullptr, 0, GDK_ACTION_COPY);
            gtk_drag_dest_add_uri_targets (window);

            g_signal_connect (window, "drag-data-received",
                              G_CALLBACK (+[](GtkWidget*, GdkDragContext* context,
                                              gint x, gint y, GtkSelectionData* selectionData,
                                              guint /*info*/, guint time, gpointer userData)
                                          {
                                              static_cast<Pimpl*> (userData)
                                                ->onDragDataReceived (context, x, y, time, selectionData);
                                          }), this);
        }
        else
        {
            fileDropCallback = {};
            gtk_drag_dest_unset (window);
        }
    }

    void onDragDataReceived (GdkDragContext* context, gint x, gint y,
                             guint time, GtkSelectionData* selectionData)
    {
        FileDropEvent e;
        e.x = static_cast<float> (x);
        e.y = static_cast<float> (y);

        if (const auto uris = gtk_selection_data_get_uris (selectionData))
        {
            for (auto uri = uris; *uri != nullptr; ++uri)
            {
                if (auto filename = g_filename_from_uri (*uri, nullptr, nullptr))
                {
                    e.filePaths.push_back (filename);
                    g_free (filename);
                }
            }

            g_strfreev (uris);
        }

        if (fileDropCallback)
            fileDropCallback (e);

        gtk_drag_finish (context, TRUE, FALSE, time);
    }

    DesktopWindow& owner;
    GtkWidget* window = {};
    GtkWidget* content = {};
    unsigned long destroyHandlerID = 0;
    unsigned long deleteEventHandlerID = 0;
    FileDropCallback fileDropCallback;
};

inline void choc::ui::setWindowsDPIAwareness() {}

//==============================================================================
#elif CHOC_APPLE

#include "choc_MessageLoop.h"

namespace choc::ui
{

inline void setWindowsDPIAwareness() {}

struct DesktopWindow::Pimpl
{
    Pimpl (DesktopWindow& w, Bounds bounds)  : owner (w)
    {
        using namespace choc::objc;
        CHOC_AUTORELEASE_BEGIN
        call<void> (getSharedNSApplication(), "setActivationPolicy:", NSApplicationActivationPolicyRegular);

        window = call<id> (callClass<id> ("NSWindow", "alloc"),
                           "initWithContentRect:styleMask:backing:defer:",
                           createCGRect (bounds),
                           NSWindowStyleMaskTitled, NSBackingStoreBuffered, (int) 0);

        delegate = createDelegate();
        setStyleBit (NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable, true);
        objc_setAssociatedObject (delegate, "choc_window", (CHOC_OBJC_CAST_BRIDGED id) this, OBJC_ASSOCIATION_ASSIGN);

        intermediateView = objc::call<id> (objc::callClass<id> ("NSView", "alloc"), "init");
        objc::call<void> (intermediateView, "setAutoresizingMask:", 18); // NSViewWidthSizable | NSViewHeightSizable
        objc::call<void> (window, "setContentView:", intermediateView);

        call<void> (window, "setDelegate:", delegate);
        CHOC_AUTORELEASE_END
    }

    ~Pimpl()
    {
        CHOC_AUTORELEASE_BEGIN
        objc::call<void> (window, "setDelegate:", nullptr);
        objc::call<void> (window, "close");
        objc::call<void> (delegate, "release");
        objc::call<void> (intermediateView, "release");
        CHOC_AUTORELEASE_END
    }

    void* getWindowHandle() const     { return (CHOC_OBJC_CAST_BRIDGED void*) window; }

    void setWindowTitle (const std::string& newTitle)
    {
        CHOC_AUTORELEASE_BEGIN
        objc::call<void> (window, "setTitle:", objc::getNSString (newTitle));
        CHOC_AUTORELEASE_END
    }

    void setContent (void* view)
    {
        CHOC_AUTORELEASE_BEGIN

        auto subviews = objc::call<id> (intermediateView, "subviews");
        auto count = objc::call<int> (subviews, "count");

        for (int i = 0; i < count; ++i)
            objc::call<void> (objc::call<id> (subviews, "objectAtIndex:", 0), "removeFromSuperview");

        auto newView = (CHOC_OBJC_CAST_BRIDGED id) view;
        objc::call<void> (newView, "setAutoresizingMask:", 18); // NSViewWidthSizable | NSViewHeightSizable
        objc::call<void> (newView, "setFrame:", objc::call<objc::CGRect> (intermediateView, "bounds"));
        objc::call<void> (intermediateView, "addSubview:", newView);
        CHOC_AUTORELEASE_END
    }

    void setVisible (bool visible)
    {
        CHOC_AUTORELEASE_BEGIN
        objc::call<void> (window, "setIsVisible:", (BOOL) visible);
        CHOC_AUTORELEASE_END
    }

    // DIY-KRONOS-EDITOR local addition (2026-08-26) -- see closeRequested's
    // own doc comment in the public class declaration, and the
    // windowShouldClose: handler above for the forcingClose escape hatch
    // this sets. "close" re-enters that same delegate method (Cocoa always
    // consults windowShouldClose: for a delegate that implements it, even
    // for a programmatic close, not just a user-initiated one) -- forcing
    // it to return TRUE this one time is what actually lets the close
    // through for real.
    void forceClose()
    {
        if (window == id{})
            return;

        CHOC_AUTORELEASE_BEGIN
        forcingClose = true;
        objc::call<void> (window, "close");
        forcingClose = false;
        CHOC_AUTORELEASE_END
    }

    void setStyleBit (long bit, bool shouldEnable)
    {
        CHOC_AUTORELEASE_BEGIN
        auto style = objc::call<unsigned long> (window, "styleMask");
        style = shouldEnable ? (style | (unsigned long) bit) : (style & ~(unsigned long) bit);
        objc::call<void> (window, "setStyleMask:", style);
        CHOC_AUTORELEASE_END
    }

    void setResizable (bool b) { setStyleBit (NSWindowStyleMaskResizable, b); }
    void setClosable (bool b)  { setStyleBit (NSWindowStyleMaskClosable, b); }

    void setMinimumSize (int w, int h) { CHOC_AUTORELEASE_BEGIN objc::call<void> (window, "setContentMinSize:", createCGSize (w, h)); CHOC_AUTORELEASE_END }
    void setMaximumSize (int w, int h) { CHOC_AUTORELEASE_BEGIN objc::call<void> (window, "setContentMaxSize:", createCGSize (w, h)); CHOC_AUTORELEASE_END }

    objc::CGRect getFrameRectForContent (Bounds b)
    {
        return objc::call<objc::CGRect> (window, "frameRectForContentRect:", createCGRect (b));
    }

    void centreWithSize (int w, int h)
    {
        CHOC_AUTORELEASE_BEGIN
        objc::call<void> (window, "setFrame:display:animate:", getFrameRectForContent ({ 0, 0, w, h }), (BOOL) 1, (BOOL) 0);
        objc::call<void> (window, "center");
        CHOC_AUTORELEASE_END
    }

    void setBounds (Bounds b)
    {
        CHOC_AUTORELEASE_BEGIN
        objc::call<void> (window, "setFrame:display:animate:", getFrameRectForContent (b), (BOOL) 1, (BOOL) 0);
        CHOC_AUTORELEASE_END
    }

    void toFront()
    {
        CHOC_AUTORELEASE_BEGIN
        objc::call<void> (objc::getSharedNSApplication(), "activateIgnoringOtherApps:", (BOOL) 1);
        objc::call<void> (window, "makeKeyAndOrderFront:", (id) nullptr);
        CHOC_AUTORELEASE_END
    }

    Bounds getBounds()
    {
        auto frame = objc::call<objc::CGRect> (window, "frame");
        auto contentRect = objc::call<objc::CGRect> (window, "contentRectForFrameRect:", frame);

        return Bounds { (int) contentRect.origin.x,
                        (int) contentRect.origin.y,
                        (int) contentRect.size.width,
                        (int) contentRect.size.height };
    }

    void setFileDropCallback (FileDropCallback handler)
    {
        CHOC_AUTORELEASE_BEGIN

        if (handler)
        {
            fileDropCallback = std::move (handler);
            auto types = objc::callClass<id> ("NSArray", "arrayWithObject:", objc::getNSString ("NSFilenamesPboardType"));
            objc::call<void> (window, "registerForDraggedTypes:", types);
        }
        else
        {
            fileDropCallback = {};
            objc::call<void> (window, "unregisterDraggedTypes");
        }

        CHOC_AUTORELEASE_END
    }

    long draggingEntered()
    {
        return fileDropCallback ? NSDragOperationCopy : NSDragOperationNone;
    }

    BOOL performDragOperation (id sender)
    {
        if (fileDropCallback == nullptr)
            return NO;

        FileDropEvent e;

        {
            CHOC_AUTORELEASE_BEGIN
            auto pboard = objc::call<id> (sender, "draggingPasteboard");
            auto files = objc::call<id> (pboard, "propertyListForType:", objc::getNSString ("NSFilenamesPboardType"));

            auto count = objc::call<long> (files, "count");

            for (long i = 0; i < count; ++i)
            {
                auto nsPath = objc::call<id>(files, "objectAtIndex:", i);
                auto utf8Path = objc::call<const char*>(nsPath, "UTF8String");
                e.filePaths.push_back (utf8Path);
            }

            struct NSPoint { double x = 0, y = 0; };
            auto point = objc::call<NSPoint> (sender, "draggingLocation");
            e.x = static_cast<float> (point.x);
            e.y = static_cast<float> (point.y);

            CHOC_AUTORELEASE_END
        }

        return fileDropCallback (e) ? YES : NO;
    }

    static Pimpl& getPimplFromContext (id self)
    {
       #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wcast-align"
       #endif
        auto view = (CHOC_OBJC_CAST_BRIDGED Pimpl*) objc_getAssociatedObject (self, "choc_window");
       #if defined(__clang__)
        #pragma clang diagnostic pop
       #endif
        CHOC_ASSERT (view != nullptr);
        return *view;
    }

    id createDelegate()
    {
        static DelegateClass dc;
        return objc::call<id> ((id) dc.delegateClass, "new");
    }

    DesktopWindow& owner;
    id window = {}, delegate = {}, intermediateView = {};
    FileDropCallback fileDropCallback;
    bool forcingClose = false;  // DIY-KRONOS-EDITOR local addition (2026-08-26) -- see forceClose() above

    struct DelegateClass
    {
        DelegateClass()
        {
            delegateClass = choc::objc::createDelegateClass ("NSResponder", "CHOCDesktopWindowDelegate_");

            if (auto* p = objc_getProtocol ("NSWindowDelegate"))
                class_addProtocol (delegateClass, p);

            if (auto* p = objc_getProtocol ("NSDraggingDestination"))
                class_addProtocol (delegateClass, p);

            class_addMethod (delegateClass, sel_registerName ("windowShouldClose:"),
                             (IMP) (+[](id self, SEL, id) -> BOOL
                             {
                                 // Declared OUTSIDE the CHOC_AUTORELEASE_BEGIN/
                                 // END pair (a plain `{`/`}` brace pair around
                                 // an @autoreleasepool -- see
                                 // choc_ObjectiveCHelpers.h) so it's still in
                                 // scope for the `return` below, after that
                                 // scope closes. `p` itself (from
                                 // getPimplFromContext()) can't be read out
                                 // here the same way -- it's declared INSIDE
                                 // that scope -- hence deciding the actual
                                 // BOOL via this flag instead of `p` directly.
                                 bool shouldVeto = false;

                                 CHOC_AUTORELEASE_BEGIN
                                 auto& p = getPimplFromContext (self);

                                 // DIY-KRONOS-EDITOR local addition
                                 // (2026-08-26, see closeRequested's own doc
                                 // comment in the public class declaration
                                 // above): closeRequested being set means
                                 // "this window closes ONLY via an explicit
                                 // forceClose() call" -- veto every close
                                 // attempt unconditionally (shouldVeto=true,
                                 // leaving `window` untouched) while it's
                                 // set, notify the callback instead of
                                 // windowClosed, and let IT decide whether/
                                 // when to call forceClose(). forcingClose
                                 // is forceClose()'s own escape hatch --
                                 // without it, forceClose()'s own "close"
                                 // call below would just re-enter this exact
                                 // handler and veto itself right back.
                                 if (p.owner.closeRequested != nullptr && ! p.forcingClose)
                                 {
                                     shouldVeto = true;

                                     if (auto callback = p.owner.closeRequested)
                                         choc::messageloop::postMessage ([callback] { callback(); });
                                 }
                                 else
                                 {
                                     p.window = {};

                                     if (auto callback = p.owner.windowClosed)
                                         choc::messageloop::postMessage ([callback] { callback(); });
                                 }

                                 CHOC_AUTORELEASE_END
                                 return shouldVeto ? FALSE : TRUE;
                             }),
                             "c@:@");

            class_addMethod (delegateClass, sel_registerName ("windowDidResize:"),
                             (IMP) (+[](id self, SEL, id)
                             {
                                 CHOC_AUTORELEASE_BEGIN

                                 if (auto callback = getPimplFromContext (self).owner.windowResized)
                                     callback();

                                 CHOC_AUTORELEASE_END
                             }),
                             "v@:@");

            class_addMethod (delegateClass, sel_registerName ("applicationShouldTerminateAfterLastWindowClosed:"),
                             (IMP) (+[](id, SEL, id) -> BOOL { return 0; }),
                             "c@:@");

            class_addMethod (delegateClass, sel_registerName ("draggingEntered:"),
                             (IMP) (+[](id self, SEL, id) -> long
                             {
                                 return getPimplFromContext (self).draggingEntered();
                             }),
                             "l@:@");

            class_addMethod (delegateClass, sel_registerName ("performDragOperation:"),
                             (IMP) (+[](id self, SEL, id sender) -> BOOL
                             {
                                 return getPimplFromContext (self).performDragOperation (sender);
                             }),
                             "B@:@");

            objc_registerClassPair (delegateClass);
        }

        ~DelegateClass()
        {
            objc_disposeClassPair (delegateClass);
        }

        Class delegateClass = {};
    };

    static constexpr long NSWindowStyleMaskTitled = 1;
    static constexpr long NSWindowStyleMaskMiniaturizable = 4;
    static constexpr long NSWindowStyleMaskResizable = 8;
    static constexpr long NSWindowStyleMaskClosable = 2;
    static constexpr long NSBackingStoreBuffered = 2;
    static constexpr long NSApplicationActivationPolicyRegular = 0;
    static constexpr long NSDragOperationNone = 0;
    static constexpr long NSDragOperationCopy = 1;

    static objc::CGSize createCGSize (double w, double h)  { return { (objc::CGFloat) w, (objc::CGFloat) h }; }
    static objc::CGRect createCGRect (choc::ui::Bounds b)  { return { { (objc::CGFloat) b.x, (objc::CGFloat) b.y }, { (objc::CGFloat) b.width, (objc::CGFloat) b.height } }; }
};

} // namespace choc::ui

//==============================================================================
#elif CHOC_WINDOWS

#undef  WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef  NOMINMAX
#define NOMINMAX
#define Rectangle Rectangle_renamed_to_avoid_name_collisions
#include <windows.h>
#undef Rectangle

#include "../platform/choc_DynamicLibrary.h"

namespace choc::ui
{

static RECT boundsToRect (Bounds b)
{
    RECT r;
    r.left = b.x;
    r.top = b.y;
    r.right = b.x + b.width;
    r.bottom = b.y + b.height;
    return r;
}

template <typename FunctionType>
FunctionType getUser32Function (const char* name)
{
    if (auto user32 = choc::file::DynamicLibrary ("user32.dll"))
        return reinterpret_cast<FunctionType> (user32.findFunction (name));

    return {};
}

struct HWNDHolder
{
    HWNDHolder() = default;
    HWNDHolder (HWND h) : hwnd (h) {}
    HWNDHolder (const HWNDHolder&) = delete;
    HWNDHolder& operator= (const HWNDHolder&) = delete;
    HWNDHolder (HWNDHolder&& other) : hwnd (other.hwnd) { other.hwnd = {}; }
    HWNDHolder& operator= (HWNDHolder&& other)  { reset(); hwnd = other.hwnd; other.hwnd = {}; return *this; }
    ~HWNDHolder() { reset(); }

    operator HWND() const  { return hwnd; }
    operator void*() const  { return (void*) hwnd; }

    void reset() { if (IsWindow (hwnd)) DestroyWindow (hwnd); hwnd = {}; }

    HWND hwnd = {};
};

struct WindowClass
{
    WindowClass (std::wstring name, WNDPROC wndProc)
    {
        name += std::to_wstring (static_cast<uint32_t> (GetTickCount()));

        moduleHandle = GetModuleHandle (nullptr);
        auto icon = (HICON) LoadImage (moduleHandle, IDI_APPLICATION, IMAGE_ICON,
                                       GetSystemMetrics (SM_CXSMICON),
                                       GetSystemMetrics (SM_CYSMICON),
                                       LR_DEFAULTCOLOR);

        WNDCLASSEXW wc;
        ZeroMemory (&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC;
        wc.hInstance = moduleHandle;
        wc.lpszClassName = name.c_str();
        wc.hIcon = icon;
        wc.hIconSm = icon;
        wc.lpfnWndProc = wndProc;

        classAtom = (LPCWSTR) (uintptr_t) RegisterClassExW (&wc);
        CHOC_ASSERT (classAtom != 0);
    }

    ~WindowClass()
    {
        UnregisterClassW (classAtom, moduleHandle);
    }

    HWNDHolder createWindow (DWORD style, int w, int h, void* userData)
    {
        if (auto hwnd = CreateWindowW (classAtom, L"", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                       w, h, nullptr, nullptr, moduleHandle, nullptr))
        {
            SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR) userData);
            return hwnd;
        }

        return {};
    }

    auto getClassName() const    { return classAtom; }

    HINSTANCE moduleHandle = {};
    LPCWSTR classAtom = {};
};

static std::string createUTF8FromUTF16 (const std::wstring& utf16)
{
    if (! utf16.empty())
    {
        auto numWideChars = static_cast<int> (utf16.size());
        auto resultSize = WideCharToMultiByte (CP_UTF8, WC_ERR_INVALID_CHARS, utf16.data(), numWideChars, nullptr, 0, nullptr, nullptr);

        if (resultSize > 0)
        {
            std::string result;
            result.resize (static_cast<size_t> (resultSize));

            if (WideCharToMultiByte (CP_UTF8, WC_ERR_INVALID_CHARS, utf16.data(), numWideChars, result.data(), resultSize, nullptr, nullptr) > 0)
                return result;
        }
    }

    return {};
}

static std::wstring createUTF16StringFromUTF8 (std::string_view utf8)
{
    if (! utf8.empty())
    {
        auto numUTF8Bytes = static_cast<int> (utf8.size());
        auto resultSize = MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), numUTF8Bytes, nullptr, 0);

        if (resultSize > 0)
        {
            std::wstring result;
            result.resize (static_cast<size_t> (resultSize));

            if (MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), numUTF8Bytes, result.data(), resultSize) > 0)
                return result;
        }
    }

    return {};
}

inline void setWindowsDPIAwareness()
{
    if (auto setProcessDPIAwarenessContext = getUser32Function<int(__stdcall *)(void*)> ("SetProcessDpiAwarenessContext"))
        setProcessDPIAwarenessContext (/*DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2*/ (void*) -4);
}

//==============================================================================
struct DesktopWindow::Pimpl
{
    Pimpl (DesktopWindow& w, Bounds b)  : owner (w)
    {
        hwnd = windowClass.createWindow (WS_OVERLAPPEDWINDOW, 640, 480, this);

        if (hwnd.hwnd == nullptr)
            return;

        setBounds (b);
        ShowWindow (hwnd, SW_SHOW);
        UpdateWindow (hwnd);
        SetFocus (hwnd);
    }

    ~Pimpl()
    {
        hwnd.reset();
    }

    void* getWindowHandle() const     { return hwnd; }

    void setWindowTitle (const std::string& newTitle)
    {
        SetWindowTextW (hwnd, createUTF16StringFromUTF8 (newTitle).c_str());
    }

    void setContent (void* childHandle)
    {
        if (auto child = getFirstChildWindow())
        {
            ShowWindow (child, SW_HIDE);
            SetParent (child, nullptr);
        }

        auto child = (HWND) childHandle;
        auto flags = GetWindowLongPtr (child, -16);
        flags = (flags & ~(decltype (flags)) WS_POPUP) | (decltype (flags)) WS_CHILD;
        SetWindowLongPtr (child, -16, flags);

        SetParent (child, hwnd);
        resizeContentToFit();
        ShowWindow (child, IsWindowVisible (hwnd) ? SW_SHOW : SW_HIDE);
    }

    void setVisible (bool visible)
    {
        ShowWindow (hwnd, visible ? SW_SHOW : SW_HIDE);

        if (visible)
            InvalidateRect (hwnd, nullptr, 0);
    }

    void setResizable (bool b)
    {
        auto style = GetWindowLong (hwnd, GWL_STYLE);

        if (b)
            style |= (WS_THICKFRAME | WS_MAXIMIZEBOX);
        else
            style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

        SetWindowLong (hwnd, GWL_STYLE, style);
    }

    void setClosable (bool closable)
    {
        EnableMenuItem (GetSystemMenu (hwnd, FALSE), SC_CLOSE,
                        closable ? (MF_BYCOMMAND | MF_ENABLED)
                                 : (MF_BYCOMMAND | MF_DISABLED | MF_GRAYED));
    }

    void setMinimumSize (int w, int h)
    {
        minimumSize.x = w;
        minimumSize.y = h;
    }

    void setMaximumSize (int w, int h)
    {
        maximumSize.x = w;
        maximumSize.y = h;
    }

    void getMinMaxInfo (MINMAXINFO& m) const
    {
        if (maximumSize.x > 0 && maximumSize.y > 0)
        {
            m.ptMaxSize = maximumSize;
            m.ptMaxTrackSize = maximumSize;
        }

        if (minimumSize.x > 0 && minimumSize.y > 0)
            m.ptMinTrackSize = minimumSize;
    }

    void centreWithSize (int w, int h)
    {
        auto dpi = static_cast<int> (getWindowDPI());
        auto screenW = (GetSystemMetrics(SM_CXSCREEN) * 96) / dpi;
        auto screenH = (GetSystemMetrics(SM_CYSCREEN) * 96) / dpi;
        auto x = (screenW - w) / 2;
        auto y = (screenH - h) / 2;
        setBounds ({ x, y, w, h }, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    void setBounds (Bounds b)
    {
        setBounds (b, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    void setBounds (Bounds b, DWORD flags)
    {
        auto r = boundsToRect (scaleBounds (b, getWindowDPI() / 96.0));
        AdjustWindowRect (&r, WS_OVERLAPPEDWINDOW, 0);
        SetWindowPos (hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, flags);
        resizeContentToFit();
    }

    void toFront()
    {
        BringWindowToTop (hwnd);
    }

    Bounds getBounds()
    {
        RECT r;
        GetWindowRect (hwnd, &r);
        auto scale = 96.0 / getWindowDPI();
        return scaleBounds ({ r.left, r.top, r.right - r.left, r.bottom - r.top }, scale);
    }

    void setFileDropCallback (FileDropCallback handler)
    {
        if (auto shell32 = choc::file::DynamicLibrary ("shell32.dll"))
        {
            typedef UINT (WINAPI *DragAcceptFilesFunc)(HWND, BOOL);
            auto dragAcceptFiles = reinterpret_cast<DragAcceptFilesFunc> (shell32.findFunction ("DragAcceptFiles"));
            dragAcceptFiles (hwnd, handler ? TRUE : FALSE);
            fileDropCallback = std::move (handler);
        }
    }

private:
    DesktopWindow& owner;
    HWNDHolder hwnd;
    POINT minimumSize = {}, maximumSize = {};
    WindowClass windowClass { L"CHOCWindow", (WNDPROC) wndProc };
    FileDropCallback fileDropCallback;

    Bounds scaleBounds (Bounds b, double scale)
    {
        b.x      = static_cast<decltype(b.x)> (b.x * scale);
        b.y      = static_cast<decltype(b.y)> (b.y * scale);
        b.width  = static_cast<decltype(b.width)> (b.width * scale);
        b.height = static_cast<decltype(b.height)> (b.height * scale);

        return b;
    }

    HWND getFirstChildWindow()
    {
        HWND result = {};

        if (IsWindow (hwnd))
            EnumChildWindows (hwnd, findFirstWindowCallback, (LPARAM) &result);

        return result;
    }

    static BOOL WINAPI findFirstWindowCallback (HWND w, LPARAM context)
    {
        *reinterpret_cast<HWND*> (context) = w;
        return FALSE;
    }

    void resizeContentToFit()
    {
        if (auto child = getFirstChildWindow())
        {
            RECT r;
            GetClientRect (hwnd, &r);
            SetWindowPos (child, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE | SWP_FRAMECHANGED);
        }
    }

    void handleClose()
    {
        // DIY-KRONOS-EDITOR local addition (2026-08-26, UNVERIFIED -- no
        // Windows toolchain available where this was written, see that
        // project's own STATE.md): WM_CLOSE never destroyed the window
        // itself even before this change -- DestroyWindow() only ever
        // happened elsewhere (this Pimpl's own destructor, via
        // HWNDHolder::reset(), or forceClose() below) -- so WM_CLOSE was
        // ALREADY a natural veto point on this platform; the only real
        // change is notifying closeRequested (whose owner decides whether/
        // when to call forceClose()) instead of windowClosed when it's set,
        // rather than treating every WM_CLOSE as an immediate real close.
        if (owner.closeRequested != nullptr)
        {
            owner.closeRequested();
            return;
        }

        if (owner.windowClosed != nullptr)
            owner.windowClosed();
    }

    // DIY-KRONOS-EDITOR local addition (2026-08-26, UNVERIFIED) --
    // HWNDHolder::reset() sends WM_DESTROY (via DestroyWindow), not
    // WM_CLOSE, so this can never re-enter handleClose()'s own veto above.
    // wndProc doesn't handle WM_DESTROY at all (this Pimpl otherwise relies
    // on its owning DesktopWindow simply going out of scope to clean up),
    // so windowClosed is fired explicitly here rather than from a
    // WM_DESTROY handler, to preserve the existing contract (main.cpp's own
    // bookkeeping needs this to fire once the window is genuinely gone,
    // exactly as before this whole change).
    void forceClose()
    {
        hwnd.reset();

        if (owner.windowClosed != nullptr)
            owner.windowClosed();
    }

    void handleSizeChange()
    {
        resizeContentToFit();

        if (owner.windowResized != nullptr)
            owner.windowResized();
    }

    bool handleFileDrop (HANDLE hdrop)
    {
        typedef UINT (WINAPI *DragQueryFileWFunc)(HANDLE, UINT, LPWSTR, UINT);
        typedef BOOL (WINAPI *DragQueryPointFunc)(HANDLE, LPPOINT);
        typedef BOOL (WINAPI *DragFinishFunc)(HANDLE);

        if (fileDropCallback)
        {
            if (auto shell32 = choc::file::DynamicLibrary ("shell32.dll"))
            {
                auto dragQueryFileW = reinterpret_cast<DragQueryFileWFunc> (shell32.findFunction ("DragQueryFileW"));
                auto dragQueryPoint = reinterpret_cast<DragQueryPointFunc> (shell32.findFunction ("DragQueryPoint"));
                auto dragFinish = reinterpret_cast<DragFinishFunc> (shell32.findFunction ("DragFinish"));

                POINT pt;
                dragQueryPoint (hdrop, &pt);
                FileDropEvent e;
                e.x = static_cast<float> (pt.x);
                e.y = static_cast<float> (pt.y);

                auto numFiles = dragQueryFileW (hdrop, 0xffffffff, nullptr, 0);

                for (UINT i = 0; i < numFiles; ++i)
                {
                    auto size = dragQueryFileW (hdrop, i, nullptr, 0);
                    std::wstring path;
                    path.resize (size + 2);
                    dragQueryFileW (hdrop, i, path.data(), size + 1);
                    e.filePaths.push_back (createUTF8FromUTF16 (path));
                }

                dragFinish (hdrop);
                return fileDropCallback (e);
            }
        }

        return false;
    }

    static void enableNonClientDPIScaling (HWND h)
    {
        if (auto fn = getUser32Function<BOOL(__stdcall*)(HWND)> ("EnableNonClientDpiScaling"))
            fn (h);
    }

    uint32_t getWindowDPI() const
    {
        if (auto getDpiForWindow = getUser32Function<UINT(__stdcall*)(HWND)> ("GetDpiForWindow"))
            return getDpiForWindow (hwnd);

        return 96;
    }

    static Pimpl* getPimpl (HWND h)     { return (Pimpl*) GetWindowLongPtr (h, GWLP_USERDATA); }

    static LRESULT wndProc (HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
            case WM_NCCREATE:        enableNonClientDPIScaling (h); break;
            case WM_SIZE:            if (auto w = getPimpl (h)) w->handleSizeChange(); break;
            case WM_CLOSE:           if (auto w = getPimpl (h)) w->handleClose(); return 0;
            case WM_GETMINMAXINFO:   if (auto w = getPimpl (h)) w->getMinMaxInfo (*(LPMINMAXINFO) lp); return 0;
            case WM_DROPFILES:       if (auto w = getPimpl (h)) if (w->handleFileDrop ((HANDLE) wp)) return 0; break;
            default:                 break;
        }

        return DefWindowProcW (h, msg, wp, lp);
    }
};

} // namespace choc::ui

#else
 #error "choc DesktopWindow only supports OSX, Windows or Linux!"
#endif

namespace choc::ui
{

//==============================================================================
inline DesktopWindow::DesktopWindow (Bounds b) { pimpl = std::make_unique<Pimpl> (*this, b); }
inline DesktopWindow::~DesktopWindow()  {}

inline void* DesktopWindow::getWindowHandle() const                        { return pimpl->getWindowHandle(); }
inline void DesktopWindow::setContent (void* view)                         { pimpl->setContent (view); }
inline void DesktopWindow::setVisible (bool visible)                       { pimpl->setVisible (visible); }
inline void DesktopWindow::setWindowTitle (const std::string& title)       { pimpl->setWindowTitle (title); }
inline void DesktopWindow::setMinimumSize (int w, int h)                   { pimpl->setMinimumSize (w, h); }
inline void DesktopWindow::setMaximumSize (int w, int h)                   { pimpl->setMaximumSize (w, h); }
inline void DesktopWindow::setResizable (bool b)                           { pimpl->setResizable (b); }
inline void DesktopWindow::setClosable (bool b)                            { pimpl->setClosable (b); }
inline void DesktopWindow::setBounds (Bounds b)                            { pimpl->setBounds (b); }
inline Bounds DesktopWindow::getBounds()                                   { return pimpl->getBounds(); }
inline void DesktopWindow::centreWithSize (int w, int h)                   { pimpl->centreWithSize (w, h); }
inline void DesktopWindow::toFront()                                       { pimpl->toFront(); }
inline void DesktopWindow::setFileDropCallback (FileDropCallback h)         { pimpl->setFileDropCallback (std::move (h)); }
inline void DesktopWindow::forceClose()                                    { pimpl->forceClose(); }  // DIY-KRONOS-EDITOR local addition (2026-08-26)

} // namespace choc::ui

#endif // CHOC_DESKTOPWINDOW_HEADER_INCLUDED
