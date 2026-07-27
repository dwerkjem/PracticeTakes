#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <iostream>
#include <optional>
#include <string_view>

namespace
{
std::optional<unsigned long> windowPid(Display* display, Window window, Atom pidAtom)
{
    Atom actualType{};
    int actualFormat{};
    unsigned long itemCount{};
    unsigned long bytesAfter{};
    unsigned char* data{};
    if (XGetWindowProperty(
            display, window, pidAtom, 0, 1, False, XA_CARDINAL, &actualType, &actualFormat,
            &itemCount, &bytesAfter, &data) != Success ||
        data == nullptr)
    {
        return std::nullopt;
    }

    const auto pid = *reinterpret_cast<unsigned long*>(data);
    XFree(data);
    return pid;
}

std::optional<Window> findWindowForPid(Display* display, unsigned long pid)
{
    const auto root = DefaultRootWindow(display);
    const auto clientsAtom = XInternAtom(display, "_NET_CLIENT_LIST", True);
    const auto pidAtom = XInternAtom(display, "_NET_WM_PID", True);
    if (clientsAtom == None || pidAtom == None)
    {
        return std::nullopt;
    }

    Atom actualType{};
    int actualFormat{};
    unsigned long itemCount{};
    unsigned long bytesAfter{};
    unsigned char* data{};
    if (XGetWindowProperty(
            display, root, clientsAtom, 0, 4096, False, XA_WINDOW, &actualType, &actualFormat,
            &itemCount, &bytesAfter, &data) != Success ||
        data == nullptr)
    {
        return std::nullopt;
    }

    const auto* windows = reinterpret_cast<Window*>(data);
    std::optional<Window> result;
    for (unsigned long index = 0; index < itemCount; ++index)
    {
        if (windowPid(display, windows[index], pidAtom) == pid)
        {
            result = windows[index];
            break;
        }
    }
    XFree(data);
    return result;
}

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
    if (argc != 3)
    {
        std::cerr << "usage: window_control PID activate|maximize|restore\n";
        return 2;
    }

    auto* display = XOpenDisplay(nullptr);
    if (display == nullptr)
    {
        return 1;
    }

    const auto window = findWindowForPid(display, std::stoul(argv[1]));
    if (!window)
    {
        XCloseDisplay(display);
        return 1;
    }

    const std::string_view action(argv[2]);
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
    else
    {
        XCloseDisplay(display);
        return 2;
    }

    XFlush(display);
    XCloseDisplay(display);
    return sent ? 0 : 1;
}