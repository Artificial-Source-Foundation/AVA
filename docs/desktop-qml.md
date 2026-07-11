# Qt Quick Desktop Prototype

AVA's desktop prototype is an optional Qt Quick/QML target. The terminal CLI/TUI remains the default build and does not require Qt.

## Requirements

- Qt 6.5 or newer
- Qt QML, Quick, and Quick Controls 2 development packages

## Build

```sh
cmake --preset desktop-qml
cmake --build --preset desktop-qml --target ava-desktop
./build-desktop-qml/ava-desktop
```

Equivalent direct CMake invocation:

```sh
cmake -S . -B build-desktop-qml -DAVA_BUILD_DESKTOP_QML=ON -DAVA_BUILD_TESTS=OFF
cmake --build build-desktop-qml --target ava-desktop
```

## Current Scope

The target is a desktop shell prototype. It includes a QML session sidebar, transcript, composer, command palette, permission card, and a C++ `DesktopController` that demonstrates callbacks and streaming text.

The backend bridge is intentionally not wired yet. The next step is to connect the controller to AVA's existing session/runtime or JSONL RPC surface while keeping provider, tool, permission, auth, and session safety in the C++ backend.
