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

std::optional<std::string> windowTitle(Display* display, Window window)
{
    const auto utf8Atom = XInternAtom(display, "UTF8_STRING", True);
    const auto readUtf8Property =
        [display, window, utf8Atom](Atom nameAtom) -> std::optional<std::string>
    {
        if (utf8Atom == None || nameAtom == None)
        {
            return std::nullopt;
        }
        Atom actualType{};
        int actualFormat{};
        unsigned long itemCount{};
        unsigned long bytesAfter{};
        unsigned char* data{};
        if (XGetWindowProperty(
                display, window, nameAtom, 0, 1024, False, utf8Atom, &actualType, &actualFormat,
                &itemCount, &bytesAfter, &data) == Success &&
            data != nullptr)
        {
            const std::string title{reinterpret_cast<char*>(data), itemCount};
            XFree(data);
            return title;
        }
        return std::nullopt;
    };

    if (const auto title = readUtf8Property(XInternAtom(display, "_NET_WM_NAME", True)))
    {
        return title;
    }
    if (const auto title = readUtf8Property(XInternAtom(display, "WM_NAME", True)))
    {
        return title;
    }

    char* title{};
    if (XFetchName(display, window, &title) != 0 && title != nullptr)
    {
        const std::string result{title};
        XFree(title);
        return result;
    }
    return std::nullopt;
}

std::optional<Window>
findWindowForPid(Display* display, unsigned long pid, const std::optional<std::string>& title)
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