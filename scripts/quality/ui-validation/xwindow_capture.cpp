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
    if (argc != 3)
    {
        std::cerr << "usage: xwindow_capture PID OUTPUT.ppm\n";
        return 2;
    }

    auto* display = XOpenDisplay(nullptr);
    if (display == nullptr)
    {
        std::cerr << "cannot open X display\n";
        return 1;
    }

    const auto pid = std::stoul(argv[1]);
    const auto window = findWindowForPid(display, pid);
    if (!window)
    {
        std::cerr << "no top-level window for PID " << pid << '\n';
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

    auto* image = XGetImage(
        display, *window, 0, 0, static_cast<unsigned>(attributes.width),
        static_cast<unsigned>(attributes.height), AllPlanes, ZPixmap);
    if (image == nullptr)
    {
        std::cerr << "cannot read window drawable\n";
        XCloseDisplay(display);
        return 1;
    }

    std::ofstream output(argv[2], std::ios::binary);
    output << "P6\n" << attributes.width << ' ' << attributes.height << "\n255\n";
    for (int y = 0; y < attributes.height; ++y)
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