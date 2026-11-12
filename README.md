# Riley Files

Riley Files is the file manager of the Riley desktop, a Hyprland desktop on Fedora. It aims to be
quiet and pleasant to look at, quick from the keyboard, and careful with your files: delete goes to
the trash, every operation can be undone, and nothing runs in the background. It is written in C++20
and Qt 6, with a QML interface on top of a core library that holds all the behaviour.

## Building

You need CMake 3.22 or newer, Ninja, a C++20 compiler and Qt 6.8 or newer (Core, Gui, Qml, Quick,
Test). On Fedora:

    sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtdeclarative-devel

Then:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
    sudo cmake --install build

Pass `-DBUILD_TESTING=OFF` to skip the tests. `riley-files --check-ui` loads the main window and
exits, which is handy for checking a build on a machine without a display
(`QT_QPA_PLATFORM=offscreen riley-files --check-ui`).

## Layout

- `src/core`: the `riley-core` library, Qt Core only, no interface code.
- `src/app`: the `riley-files` program and its QML interface.
- `share`: the desktop file and the icon.
- `tests`: tests for the core library.

## Licence

Riley Files is free software, released under the GNU General Public License, version 3 or (at your
option) any later version. See [LICENSE](LICENSE).
