# Meshtastic Client

Qt6 desktop application for communicating with Meshtastic mesh networking devices.

## Architecture

- **Framework**: Qt6 (Widgets + WebEngine)
- **Build**: CMake
- **Protocol**: Protobuf (meshtastic protocol definitions in `proto/`)
- **Language**: C++17

## Components

| File | Purpose |
|------|---------|
| `SerialConnection` | USB serial communication (115200 baud) |
| `MeshtasticProtocol` | Protobuf packet encoding/decoding |
| `NodeManager` | Tracks mesh node state (position, battery, telemetry) |
| `MainWindow` | Main UI with toolbar and tabs |
| `MapWidget` | WebEngine-based map with Leaflet/OpenStreetMap |
| `PacketListWidget` | Packet display with filtering |

## Connection Options

1. **USB Serial** (implemented) - Primary connection method
2. **Bluetooth** (not yet implemented) - Alternative for devices without USB
3. **WiFi/TCP** (not yet implemented) - Network connection for WiFi-enabled devices

## Key Decisions

### Map Implementation
- Initially tried Qt6 Location module - **does not work** (package unavailable/deprecated)
- Switched to **QWebEngineView + Leaflet** - works reliably, better tile support
- Map files: `src/MapWidget.cpp`, `resources/map.html`

### Protocol
- Uses official Meshtastic protobuf definitions
- Proto files in `proto/meshtastic/`

## Building

```bash
cd build
cmake ..
cmake --build .
```

### Dependencies

```bash
# Required
sudo apt install qt6-base-dev qt6-serialport-dev qt6-webengine-dev libprotobuf-dev protobuf-compiler

# NOT needed (we use WebEngine instead)
# qt6-location-dev - problematic, don't use
```

## Running

```bash
./build/meshtastic-client
```

## TODO

- [ ] Implement Bluetooth connection as alternative to USB
- [ ] Implement WiFi/TCP connection
- [ ] Message sending functionality
- [ ] Channel configuration
- [ ] Device settings UI
