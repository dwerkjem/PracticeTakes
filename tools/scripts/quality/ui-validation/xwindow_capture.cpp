#include "x_window_lookup.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdint>
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

    auto* image = XGetImage(
        display, *window, 0, cropTop, static_cast<unsigned>(attributes.width),
        static_cast<unsigned>(attributes.height - cropTop), AllPlanes, ZPixmap);
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