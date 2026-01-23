# Copilot / AI Agent Instructions for Meshtastic Client

- Project snapshot: Qt6 C++ desktop app (Widgets + QWebEngine) using Protobuf, built with CMake. See CLAUDE.md for a short overview.

- Big picture
  - UI layer: `src/MainWindow.cpp`, `src/MapWidget.cpp` (QWebEngineView + Leaflet), `qml/MapView.qml` for optional QML views.
  - Protocol layer: Protobuf definitions in `proto/meshtastic/`; runtime encoder/decoder in `src/MeshtasticProtocol.cpp`.
  - I/O layer: `src/SerialConnection.cpp/.h` (USB serial, 115200). Bluetooth / TCP are not implemented here.
  - State management: `src/NodeManager.cpp/.h` tracks nodes (position, battery, telemetry) and publishes updates to UI widgets.
  - Resources: `resources/map.html`, `resources/resources.qrc` provide map HTML and bundled assets.

- Build & run (what works locally)
  - Standard build:

    mkdir -p build
    cd build
    cmake ..
    cmake --build .

  - Run binary: `./build/meshtastic-client`
  - Cross-compile hint: workspace contains a task and `cmake/mingw-w64.cmake` for Windows (see workspace task "Build for Windows (cross-compile)").

- When editing Protobufs
  - Edit `.proto` files under `proto/meshtastic/` only.
  - Regenerate C++ sources if required (project already contains generated pb files under `meshtastic/` in the build output). If you change protos, run the proto compiler and update CMake as needed:

    protoc --cpp_out=... proto/meshtastic/*.proto

  - Search for usages in `src/MeshtasticProtocol.cpp` and `src/` when adapting message handling.

- Common change points & examples
  - Add a new telemetry field: update proto in `proto/meshtastic/`, regenerate pb, then update parsing in `src/MeshtasticProtocol.cpp` and consumption in `src/NodeManager.cpp`.
  - Add a new transport (Bluetooth/TCP): implement a transport class mirroring `SerialConnection` API, wire it into the connection selection in `src/MainWindow.cpp`.
  - Change map behavior: modify `resources/map.html` (Leaflet JS) and `src/MapWidget.cpp` for the page interaction.

- Patterns & conventions specific to this repo
  - Qt signal/slot patterns drive UI updates; prefer emitting signals from backend classes (`NodeManager`, `SerialConnection`) and connecting in `MainWindow`/widgets.
  - Resource bundling uses Qt resource file: `resources/resources.qrc`. Update it when adding map assets.
  - C++ standard: C++17 (project uses Qt6). Keep style consistent with existing files (header `.h` / source `.cpp`).

- Integration & external deps
  - Requires Qt6 modules: `Qt6::Widgets`, `Qt6::SerialPort`, `Qt6::WebEngineWidgets`.
  - Requires Protobuf libs and `protoc` for regeneration.
  - Map uses Leaflet in `resources/map.html`; tiles come from OpenStreetMap.

- Quick pointers for troubleshooting
  - Qt WebEngine issues: check installed system package `qt6-webengine-dev` (see CLAUDE.md). If WebEngine missing, app will fail to load map page.
  - Serial problems: `src/SerialConnection.cpp` hardcodes 115200 baud; test with a real device or a virtual serial port.

- Files to inspect first when asked to change behavior
  - `src/MeshtasticProtocol.cpp` — packet encode/decode
  - `src/NodeManager.cpp` — node state lifecycle
  - `src/SerialConnection.cpp` — transport implementation
  - `src/MapWidget.cpp` + `resources/map.html` — map rendering and JS interaction
  - `CMakeLists.txt` — build/linking and generated pb sources

- What not to assume
  - There is no Bluetooth/TCP transport implemented — don't add speculative code that assumes those transports exist.
  - The project's proto-generated headers may be regenerated elsewhere; prefer editing `proto/` and running `protoc` rather than editing generated sources directly.

If anything above is unclear or you want more examples (e.g., a concrete code-snippet for adding a new node field or wiring a new transport), tell me which area to expand and I will update this file.