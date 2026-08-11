#pragma once

// Finding an application's main window on an X display, by process id.
//
// Shared by xwindow_capture and window_control, which carried byte-identical
// copies of this until a fallback had to be added to it and the copies would
// have had to be corrected in step.
//
// Two ways of looking, in order:
//
//   _NET_CLIENT_LIST  The window manager's own list of the top-level windows it
//                     manages. Authoritative when there is a window manager,
//                     and the only one of the two that can distinguish a real
//                     top-level window from an implementation detail.
//
//   the window tree   Walked from the root, matching _NET_WM_PID. Needed
//                     because a virtual display for headless capture has no
//                     window manager, so _NET_CLIENT_LIST does not exist and
//                     the first method finds nothing at all. _NET_WM_PID is set
//                     by the application rather than the window manager, so it
//                     is there either way.
//
// The fallback picks the largest viewable window belonging to the process. A
// toolkit creates several X windows per process -- unmapped placeholders, and
// on some paths a 1x1 helper -- and without a window manager to say which is
// the real one, size is what separates the window a person would see from the
// ones they would not.

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <optional>
#include <string>
#include <vector>

namespace xlookup
{
inline unsigned long windowPid(Display* display, Window window, Atom pidAtom)
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
        return 0;
    }

    const auto pid = *reinterpret_cast<unsigned long*>(data);
    XFree(data);

    return pid;
}

inline std::optional<std::string> readUtf8Property(Display* display, Window window, Atom atom)
{
    if (atom == None)
    {
        return std::nullopt;
    }

    Atom actualType{};
    int actualFormat{};
    unsigned long itemCount{};
    unsigned long bytesAfter{};
    unsigned char* data{};

    if (XGetWindowProperty(
            display, window, atom, 0, 1024, False, AnyPropertyType, &actualType, &actualFormat,
            &itemCount, &bytesAfter, &data) != Success ||
        data == nullptr)
    {
        return std::nullopt;
    }

    std::string value(reinterpret_cast<char*>(data), itemCount);
    XFree(data);

    return value;
}

inline std::optional<std::string> windowTitle(Display* display, Window window)
{
    if (const auto title =
            readUtf8Property(display, window, XInternAtom(display, "_NET_WM_NAME", True)))
    {
        return title;
    }

    return readUtf8Property(display, window, XInternAtom(display, "WM_NAME", True));
}

// The window manager's list. Empty when there is no window manager.
inline std::optional<Window> findViaClientList(
    Display* display,
    unsigned long pid,
    Atom pidAtom,
    const std::optional<std::string>& title)
{
    const auto clientsAtom = XInternAtom(display, "_NET_CLIENT_LIST", True);

    if (clientsAtom == None)
    {
        return std::nullopt;
    }

    Atom actualType{};
    int actualFormat{};
    unsigned long itemCount{};
    unsigned long bytesAfter{};
    unsigned char* data{};

    if (XGetWindowProperty(
            display, DefaultRootWindow(display), clientsAtom, 0, 4096, False, XA_WINDOW,
            &actualType, &actualFormat, &itemCount, &bytesAfter, &data) != Success ||
        data == nullptr)
    {
        return std::nullopt;
    }

    const auto* windows = reinterpret_cast<Window*>(data);
    std::optional<Window> result;

    for (unsigned long index = 0; index < itemCount; ++index)
    {
        if (windowPid(display, windows[index], pidAtom) == pid &&
            (!title.has_value() || windowTitle(display, windows[index]) == title))
        {
            result = windows[index];
            break;
        }
    }

    XFree(data);

    return result;
}

// Walk the tree instead, for a display with no window manager.
inline std::optional<Window> findViaTreeWalk(
    Display* display,
    unsigned long pid,
    Atom pidAtom,
    const std::optional<std::string>& title)
{
    std::vector<Window> pending{DefaultRootWindow(display)};
    std::optional<Window> best;
    unsigned long bestArea = 0;

    while (!pending.empty())
    {
        const auto window = pending.back();
        pending.pop_back();

        Window root{};
        Window parent{};
        Window* children{};
        unsigned int childCount{};

        if (XQueryTree(display, window, &root, &parent, &children, &childCount) != 0)
        {
            for (unsigned int index = 0; index < childCount; ++index)
            {
                pending.push_back(children[index]);
            }

            if (children != nullptr)
            {
                XFree(children);
            }
        }

        if (window == DefaultRootWindow(display) || windowPid(display, window, pidAtom) != pid)
        {
            continue;
        }

        if (title.has_value() && windowTitle(display, window) != title)
        {
            continue;
        }

        XWindowAttributes attributes{};

        if (XGetWindowAttributes(display, window, &attributes) == 0 ||
            attributes.map_state != IsViewable)
        {
            continue;
        }

        const auto area = static_cast<unsigned long>(attributes.width) *
                          static_cast<unsigned long>(attributes.height);

        if (area > bestArea)
        {
            bestArea = area;
            best = window;
        }
    }

    return best;
}

inline std::optional<Window>
findWindowForPid(Display* display, unsigned long pid, const std::optional<std::string>& title)
{
    const auto pidAtom = XInternAtom(display, "_NET_WM_PID", True);

    if (pidAtom == None)
    {
        // No process on this display has published a pid, so there is nothing
        // to match against by either route.
        return std::nullopt;
    }

    if (const auto managed = findViaClientList(display, pid, pidAtom, title))
    {
        return managed;
    }

    return findViaTreeWalk(display, pid, pidAtom, title);
}
} // namespace xlookup
