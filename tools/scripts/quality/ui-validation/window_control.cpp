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
        sent = activateWindow(display, *window) && sendWindowState(display, *window, 1);
    }
    else if (action == "restore")
    {
        sent = sendWindowState(display, *window, 0);
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