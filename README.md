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

## Testing

### Unit tests

Tests live in `tests/` and are built as part of the normal CMake build. Run them with:

```bash
cd build
ctest --output-on-failure
```

For verbose output (shows individual PASS/FAIL lines):

```bash
ctest -V
```

Two test suites:

| Suite | File | What it covers |
|-------|------|----------------|
| `Protocol` | `tests/test_protocol.cpp` | Frame parser — sync detection, split frames, two frames in one chunk, `resetParser()`, garbage bytes before sync |
| `NodeManager` | `tests/test_nodemanager.cpp` | Node CRUD, position filtering, telemetry, external-power detection, `nodesChanged` debounce |

### Simulation mode (no hardware needed)

The app can run against a built-in fake device that generates real Meshtastic protobuf packets:

```bash
# Basic: sends MyInfo + 5 peer nodes + full config dump
./meshtastic-vibe-client --simulate basic

# Reconnect: same as above, then drops connection after 10 s and reconnects after 3 s
./meshtastic-vibe-client --simulate reconnect
```

This is useful for testing the UI and connection state machine without a physical device.

### Windows

Unit tests build on Windows the same way. To run `ctest` you need the Qt and vcpkg DLLs in `PATH`:

```powershell
$env:PATH = "C:\vcpkg\installed\x64-windows\bin;$env:Qt6_DIR\..\..\..\bin;$env:PATH"
cd build-win
ctest -C Release --output-on-failure
```

## Key files

- `src/MeshtasticProtocol.cpp` — Protobuf encode/decode and parsing
- `src/SerialConnection.cpp` — USB serial transport (115200 baud)
- `src/NodeManager.cpp` — Node lifecycle and state updates
- `resources/map.html` + `src/MapWidget.cpp` — Map rendering and JS interaction
- `src/SimulationConnection.cpp` — Fake device for UI testing without hardware
- `tests/` — Qt unit tests (Qt Test framework, CTest)

## CI

A GitHub Actions workflow at `.github/workflows/ci.yml` builds on `ubuntu-latest` and `windows-latest` on every push and PR. The Linux job also runs `ctest --output-on-failure` after the build — a failing test blocks the build. Windows currently builds only (DLL path setup for ctest is left to local dev).
