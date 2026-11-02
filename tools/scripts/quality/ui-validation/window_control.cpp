#include "x_window_lookup.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace
{
using xlookup::findWindowForPid;

bool sendWindowState(Display* display, Window window, long action)
{
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = action;
    event.xclient.data.l[1] = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    event.xclient.data.l[2] = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    event.xclient.data.l[3] = 2;
    return XSendEvent(
               display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event) != 0;
}

// Whether an EWMH window manager is running: one sets _NET_SUPPORTING_WM_CHECK
// on the root window. A private Xvfb screen -- which the testing suite uses when
// it has no desktop display -- has none, and a _NET_WM_STATE request there is
// a message nobody reads.
bool windowManagerRunning(Display* display)
{
    const Atom check = XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", True);
    if (check == None)
    {
        return false;
    }

    Atom type = None;
    int format = 0;
    unsigned long items = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    const auto status = XGetWindowProperty(
        display, DefaultRootWindow(display), check, 0, 1, False, XA_WINDOW, &type, &format, &items,
        &remaining, &data);
    const auto running = status == Success && type == XA_WINDOW && items == 1;
    if (data != nullptr)
    {
        XFree(data);
    }
    return running;
}

// Where a window was before this tool maximised it without a window manager,
// so "restore" can put it back. Kept on the window itself, because each call is
// a separate process.
Atom restoreGeometryAtom(Display* display)
{
    return XInternAtom(display, "_PRACTICE_TAKES_RESTORE_GEOMETRY", False);
}

// What a window manager would do for _NET_WM_STATE_MAXIMIZED, done directly:
// remember the geometry, then fill the screen.
bool maximizeWithoutWindowManager(Display* display, Window window)
{
    XWindowAttributes attributes{};
    if (XGetWindowAttributes(display, window, &attributes) == 0)
    {
        return false;
    }

    const long saved[4] = {attributes.x, attributes.y, attributes.width, attributes.height};
    XChangeProperty(
        display, window, restoreGeometryAtom(display), XA_CARDINAL, 32, PropModeReplace,
        reinterpret_cast<const unsigned char*>(saved), 4);

    const auto screen = DefaultScreen(display);
    return XMoveResizeWindow(
               display, window, 0, 0, static_cast<unsigned int>(DisplayWidth(display, screen)),
               static_cast<unsigned int>(DisplayHeight(display, screen))) != 0;
}

bool restoreWithoutWindowManager(Display* display, Window window)
{
    Atom type = None;
    int format = 0;
    unsigned long items = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    const auto status = XGetWindowProperty(
        display, window, restoreGeometryAtom(display), 0, 4, True, XA_CARDINAL, &type, &format,
        &items, &remaining, &data);
    if (status != Success || data == nullptr || items != 4)
    {
        if (data != nullptr)
        {
            XFree(data);
        }
        // Never maximised by this tool: nothing to restore.
        return false;
    }

    const auto* saved = reinterpret_cast<const long*>(data);
    const auto moved =
        XMoveResizeWindow(
            display, window, static_cast<int>(saved[0]), static_cast<int>(saved[1]),
            static_cast<unsigned int>(saved[2]), static_cast<unsigned int>(saved[3])) != 0;
    XFree(data);
    return moved;
}

bool activateWindow(Display* display, Window window)
{
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = 2;
    event.xclient.data.l[1] = CurrentTime;
    return XSendEvent(
               display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event) != 0;
}
} // namespace

int main(int argc, char** argv)
{
    const std::string_view action(argc > 2 ? argv[2] : "");
    const auto resize = action == "resize";
    const auto requiredArguments = resize ? 5 : 3;
    if (argc != requiredArguments && argc != requiredArguments + 2)
    {
        std::cerr << "usage: window_control PID activate|maximize|restore|resize|geometry "
                     "[WIDTH HEIGHT] [--title WINDOW_TITLE]\n";
        return 2;
    }
    std::optional<std::string> title;
    if (argc == requiredArguments + 2)
    {
        if (std::string_view{argv[requiredArguments]} != "--title")
        {
            std::cerr << "expected --title before WINDOW_TITLE\n";
            return 2;
        }
        title = argv[requiredArguments + 1];
    }

    auto* display = XOpenDisplay(nullptr);
    if (display == nullptr)
    {
        return 1;
    }

    const auto window = findWindowForPid(display, std::stoul(argv[1]), title);
    if (!window)
    {
        XCloseDisplay(display);
        return 1;
    }

    // Reporting the size is not a window action, so it answers and returns
    // before the action dispatch below. The testing suite polls this after
    // asking the application to change geometry: a window that has not yet
    // adopted the requested size would otherwise be captured at the old one,
    // and nothing downstream could tell that from a real layout result.
    if (action == "geometry")
    {
        XWindowAttributes attributes{};
        const auto read = XGetWindowAttributes(display, *window, &attributes) != 0;
        XCloseDisplay(display);
        if (!read)
        {
            return 1;
        }
        std::cout << attributes.width << ' ' << attributes.height << '\n';
        return 0;
    }

    auto sent = false;
    if (action == "activate")
    {
        sent = activateWindow(display, *window);
    }
    else if (action == "maximize")
    {
        sent = windowManagerRunning(display)
                   ? activateWindow(display, *window) && sendWindowState(display, *window, 1)
                   : maximizeWithoutWindowManager(display, *window);
    }
    else if (action == "restore")
    {
        sent = windowManagerRunning(display) ? sendWindowState(display, *window, 0)
                                             : restoreWithoutWindowManager(display, *window);
    }
    else if (action == "resize")
    {
        sent = activateWindow(display, *window) &&
               XResizeWindow(
                   display, *window, static_cast<unsigned int>(std::stoul(argv[3])),
                   static_cast<unsigned int>(std::stoul(argv[4]))) != 0;
    }
    else
    {
        XCloseDisplay(display);
        return 2;
    }

    XFlush(display);
    XCloseDisplay(display);
    return sent ? 0 : 1;
}