#include "x_window_lookup.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace
{
using xlookup::findWindowForPid;

std::uint8_t channel(unsigned long pixel, unsigned long mask)
{
    if (mask == 0)
    {
        return 0;
    }

    unsigned shift{};
    while (((mask >> shift) & 1UL) == 0)
    {
        ++shift;
    }

    const auto value = (pixel & mask) >> shift;
    const auto maximum = mask >> shift;
    return static_cast<std::uint8_t>((value * 255UL + maximum / 2UL) / maximum);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 5 && argc != 7)
    {
        std::cerr << "usage: xwindow_capture PID OUTPUT.ppm [--title WINDOW_TITLE] "
                     "[--crop-top PIXELS]\n";
        return 2;
    }

    std::optional<std::string> title;
    auto cropTop = 0;
    for (auto index = 3; index < argc; index += 2)
    {
        const std::string option{argv[index]};
        if (option == "--title")
        {
            title = argv[index + 1];
        }
        else if (option == "--crop-top")
        {
            cropTop = std::stoi(argv[index + 1]);
        }
        else
        {
            std::cerr << "unknown option: " << option << '\n';
            return 2;
        }
    }

    auto* display = XOpenDisplay(nullptr);
    if (display == nullptr)
    {
        std::cerr << "cannot open X display\n";
        return 1;
    }

    const auto pid = std::stoul(argv[1]);
    const auto window = findWindowForPid(display, pid, title);
    if (!window)
    {
        std::cerr << "no matching top-level window for PID " << pid << '\n';
        XCloseDisplay(display);
        return 1;
    }

    XWindowAttributes attributes{};
    if (!XGetWindowAttributes(display, *window, &attributes) || attributes.map_state != IsViewable)
    {
        std::cerr << "window is not viewable\n";
        XCloseDisplay(display);
        return 1;
    }
    if (cropTop < 0 || cropTop >= attributes.height)
    {
        std::cerr << "crop-top must be within the captured window\n";
        XCloseDisplay(display);
        return 2;
    }

    // Only the part of the window that is actually on the screen.
    //
    // `XGetImage` fails with BadMatch when the rectangle asked for reaches
    // outside the drawable's visible area, and a window can hang off the edge:
    // there is no window manager on a virtual display to keep one inside it, so
    // JUCE puts secondary windows -- Settings, a floating tool -- wherever it
    // likes and some of them land partly past the edge. Fourteen captures a run
    // failed this way, every one of them a window of its own.
    //
    // Clipped rather than refused. A picture of the visible nine tenths of the
    // settings window is worth having; nothing at all is not. One that is
    // wholly outside is moved on instead -- see below.
    int windowX = 0;
    int windowY = 0;
    Window ignored{};
    const Window root = DefaultRootWindow(display);
    XTranslateCoordinates(display, *window, root, 0, 0, &windowX, &windowY, &ignored);

    XWindowAttributes screen{};
    if (!XGetWindowAttributes(display, root, &screen))
    {
        std::cerr << "cannot measure the screen\n";
        XCloseDisplay(display);
        return 1;
    }

    // A window that has never been on the screen has no pixels to read: X does
    // not keep the contents of what nobody can see. That is why moving one on
    // and photographing it immediately produced an image of mostly black --
    // the move was fine, the window simply had not drawn yet.
    //
    // So it is moved on, and then given a moment to paint. Only when it is off
    // the screen: a window already visible is left exactly where it is.
    if (windowX + attributes.width <= 0 || windowY + attributes.height <= 0 ||
        windowX >= screen.width || windowY >= screen.height)
    {
        XMoveWindow(display, *window, 0, 0);
        XRaiseWindow(display, *window);
        XSync(display, False);

        // Long enough for a repaint on an idle machine, short enough that
        // paying it on every capture of a window that needed moving is not
        // worth avoiding. Only surfaces with a window of their own reach here.
        struct timespec settle
        {
            0, 400 * 1000 * 1000
        };
        nanosleep(&settle, nullptr);

        if (!XGetWindowAttributes(display, *window, &attributes) ||
            attributes.map_state != IsViewable)
        {
            std::cerr << "window is not viewable after being moved on screen\n";
            XCloseDisplay(display);
            return 1;
        }

        windowX = 0;
        windowY = 0;
    }

    const int left = std::max(0, -windowX);
    const int top = std::max(cropTop, std::max(0, -windowY));
    const int right = std::min(attributes.width, screen.width - windowX);
    const int bottom = std::min(attributes.height, screen.height - windowY);

    if (right <= left || bottom <= top)
    {
        // Should be unreachable: anything off the screen was moved on above.
        // Left in because "unreachable" is a claim about code that has changed
        // before, and the alternative to saying so is a BadMatch from
        // `XGetImage` with nothing attached explaining it.
        std::cerr << "the window is entirely off the screen\n";
        XCloseDisplay(display);
        return 1;
    }

    auto* image = XGetImage(
        display, *window, left, top, static_cast<unsigned>(right - left),
        static_cast<unsigned>(bottom - top), AllPlanes, ZPixmap);
    if (image == nullptr)
    {
        std::cerr << "cannot read window drawable\n";
        XCloseDisplay(display);
        return 1;
    }

    std::ofstream output(argv[2], std::ios::binary);
    output << "P6\n" << attributes.width << ' ' << attributes.height - cropTop << "\n255\n";
    for (int y = 0; y < attributes.height - cropTop; ++y)
    {
        for (int x = 0; x < attributes.width; ++x)
        {
            const auto pixel = XGetPixel(image, x, y);
            const char rgb[] = {
                static_cast<char>(channel(pixel, image->red_mask)),
                static_cast<char>(channel(pixel, image->green_mask)),
                static_cast<char>(channel(pixel, image->blue_mask))};
            output.write(rgb, sizeof(rgb));
        }
    }

    XDestroyImage(image);
    XCloseDisplay(display);
    return output ? 0 : 1;
}