# Meshtastic Vibe Client

Qt6 desktop client for Meshtastic mesh devices. This repository builds a Qt6 Widgets application using Protobuf messages from `proto/meshtastic/`.

Quick start (Linux):

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
./meshtastic-client
```

Notes:
- Protobuf sources are generated at build time by `protoc` (CMake custom command). Edit `.proto` files in `proto/meshtastic/` and regenerate with `protoc` if you change import paths.
- Map UI uses Qt WebEngine with `resources/map.html` and Leaflet.

Key files:
- `src/MeshtasticProtocol.cpp` — Protobuf encode/decode and parsing
- `src/SerialConnection.cpp` — USB serial transport (115200 baud)
- `src/NodeManager.cpp` — Node lifecycle and state updates
- `resources/map.html` + `src/MapWidget.cpp` — Map rendering and JS interaction

CI:
- A GitHub Actions workflow is included at `.github/workflows/ci.yml` that installs dependencies and runs CMake build on `ubuntu-latest`.
