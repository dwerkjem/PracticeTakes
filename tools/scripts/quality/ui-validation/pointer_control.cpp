#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
bool movePointer(Display* display, int x, int y)
{
    const auto moved = XTestFakeMotionEvent(display, DefaultScreen(display), x, y, CurrentTime);
    XSync(display, False);
    return moved != False;
}

bool pointerPosition(Display* display, int& rootX, int& rootY)
{
    Window returnedRoot{};
    Window returnedChild{};
    int windowX{};
    int windowY{};
    unsigned mask{};
    return XQueryPointer(
               display, DefaultRootWindow(display), &returnedRoot, &returnedChild, &rootX, &rootY,
               &windowX, &windowY, &mask) != False;
}
} // namespace

int main(int argc, char** argv)
{
    auto* display = XOpenDisplay(nullptr);
    if (display == nullptr)
    {
        return 1;
    }

    if (argc == 1)
    {
        int rootX{};
        int rootY{};
        const auto queried = pointerPosition(display, rootX, rootY);
        XCloseDisplay(display);
        if (queried == False)
        {
            return 1;
        }
        std::cout << rootX << ' ' << rootY << '\n';
        return 0;
    }

    if (argc == 2 && std::string_view(argv[1]) == "--park")
    {
        const auto screen = DefaultScreen(display);
        const auto moved = movePointer(display, 0, DisplayHeight(display, screen) - 1);
        int rootX{};
        int rootY{};
        const auto queried = pointerPosition(display, rootX, rootY);
        XCloseDisplay(display);
        if (!moved || !queried)
        {
            return 1;
        }
        std::cout << rootX << ' ' << rootY << '\n';
        return 0;
    }

    if (argc != 3)
    {
        XCloseDisplay(display);
        return 2;
    }

    const auto moved = movePointer(display, std::atoi(argv[1]), std::atoi(argv[2]));
    XCloseDisplay(display);
    return moved ? 0 : 1;
}