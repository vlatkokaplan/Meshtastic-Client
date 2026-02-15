# Meshtastic Client - Session Progress & Takeoff Guide

Last updated: 2026-02-15

## Project Overview

- **Codebase**: ~13,656 lines of C++17 across 40 source files (20 .cpp + 20 .h)
- **Stack**: Qt6 Widgets + QWebEngine (Leaflet map) + Protobuf + OpenSSL + SQLite
- **Build**: CMake, `cmake .. && make -j$(nproc)` from `build/` dir
- **Run**: `./meshtastic-vibe-client --debug` (debug flag enables qDebug output)
- **Connection**: USB serial only (115200 baud)

## Existing Planning Documents

| Document | Location | Summary |
|----------|----------|---------|
| Feature TODO | `FEATURES_TODO.md` | Comprehensive feature comparison vs official Meshtastic web client |
| Code Improvements | `docs/CODE_IMPROVEMENTS.md` | 10 specific code improvement items |
| CI Improvements | `docs/CI_IMPROVEMENTS.md` | 7 CI/CD optimization suggestions |
| Copilot Instructions | `.github/copilot-instructions.md` | Architecture guide for AI agents |
| Claude Memory | `/home/user/claude/CLAUDE.md` | Cross-session project notes, encryption details |

## What's Already Implemented (Working)

### Core Features
- Serial USB connection with auto-reconnect
- Full Meshtastic protobuf protocol (encode/decode)
- AES-128/256-CTR packet decryption with brute force for simple keys
- Node discovery, tracking, and persistence (SQLite)
- Text messaging (DM + channel), reactions, replies, delivery status
- Multi-channel support (8 channels) with PSK management
- Interactive Leaflet map with node markers, blinking, packet flow visualization
- Traceroute with route visualization on map and SNR tracking
- Device configuration (LoRa, Device, Position, Channels)
- Telemetry graphs (temperature, humidity, pressure, battery, signal)
- Signal scanner widget
- Dashboard stats (battery, channel util, air time, firmware, uptime)
- Export nodes and messages (CSV/JSON)
- Dark theme, notifications, font size control
- Auto-connect, window state persistence
- Packet capture/filtering/database storage
- Session key management for admin operations
- Connection heartbeat for long sessions
- Old data cleanup (7-day retention for packets/telemetry)

### CI/CD
- GitHub Actions: Linux build + Windows cross-compile
- DEB package generation (CPack)
- Artifact upload on tags

## What's NOT Implemented (Priority Order)

### High Priority
1. **TCP/IP connection** - Connect to device over network (not just USB serial)
2. **MQTT integration** - Internet bridge for global mesh connectivity
3. **Store and Forward** - Message routing for offline nodes
4. **Waypoints** - Create, display, share waypoints on map
5. **Channel QR/URL sharing** - Easy channel sharing between devices
6. **Firmware updates** - OTA update capability
7. **Message search** - Search through message history

### Medium Priority
1. **Mesh Detector** - Periodic nodeinfo requests to discover nearby nodes
2. **Canned messages** - Pre-defined quick messages
3. **Remote administration** - Configure remote nodes over mesh
4. **Packet statistics** - Visual breakdown of packet types (pie chart)
5. **Packet export** - Export captured packets to file
6. **Neighbor info display** - Show node neighbors on map
7. **Node highlighting** - Emphasize specific nodes on map
8. **Bluetooth connection** - Alternative to USB serial

### Low Priority
1. Multi-language support (i18n)
2. Range test module
3. External notifications (hardware alerts)
4. Mesh topology visualization (network graph)
5. Device backup/restore
6. Lock screen / security features

### Module Configuration (all unimplemented)
- MQTT, Store and Forward, Range Test, External Notification
- Canned Messages, Audio, Detection Sensor, Paxcounter
- Remote Hardware, Neighbor Info, Ambient Lighting

### Device Config Gaps
- Bluetooth settings, Display settings, Power settings, Network (WiFi) settings

## Code Quality Issues Found

### Critical / Should Fix

1. **Duplicate node-loading code in Database.cpp** (lines ~597-696)
   - `loadNode()` and `loadAllNodes()` have ~40 identical lines of query-to-NodeInfo mapping
   - Should extract a `nodeFromQuery(QSqlQuery&)` helper

2. **srand/rand used in drawTestNodeLines()** (MainWindow.cpp:2085-2097)
   - Uses C-style `srand(time(nullptr))` and `rand()` instead of `<random>`
   - Only in test mode so low impact, but potential infinite loop if all nodes at same index

3. **`channel` field semantics mismatch** (MeshtasticProtocol.cpp:195, :580)
   - `result.channelIndex = packet.channel()` - the protobuf `channel` field is actually a **hash** (0-255), not the channel index (0-7)
   - This is documented in CLAUDE.md but means `channelIndex` on `DecodedPacket` is misleading for encrypted packets

4. **Redundant connection check** (MainWindow.cpp:1337-1347)
   - `requestConfig()` checks `m_serial->isConnected()` twice in a row

### Medium / Should Improve

5. **Database node mapping duplication** - already documented in `docs/CODE_IMPROVEMENTS.md` item 4
   - Config field mapping code is refactored (helper functions exist) but DB loading is not

6. **Timestamp unit mismatch in deleteOldPackets()** (Database.cpp:1283)
   - Uses `currentMSecsSinceEpoch()` for cutoff but `packets.timestamp` stores `QDateTime::currentMSecsSinceEpoch()` from protocol
   - Meanwhile telemetry/traceroute use seconds - inconsistent

7. **`MainWindow::disconnect()` calls wrong method** (MainWindow.cpp:434-437)
   - Calls `m_serial->disconnect()` which is `QObject::disconnect()` (disconnects signals), not `SerialConnection::disconnect()`
   - This is a name collision - `SerialConnection::disconnect()` works because it's the most derived, but it's confusing

8. **No connection timeout** - If serial connect hangs, there's no timeout mechanism. The UI just stays in "Connecting..." state.

9. **No message length validation** - Text messages sent without checking Meshtastic's 237-byte payload limit

10. **`getAllMessages()` reads packetId as 0** (Database.cpp:1002)
    - Comment says "Not stored in DB currently" but `packet_id` column exists and is stored in `saveMessage()`
    - Should read `query.value("packet_id").toUInt()` instead of hardcoding 0

### Robustness / Safety (from deep audit)

11. **Missing null checks on member pointers** (MainWindow.cpp various lines)
    - `m_dashboardStats`, `m_messagesWidget`, `m_mapWidget` dereferenced without null checks in several code paths
    - e.g. line 866: `m_dashboardStats->setFirmwareVersion()`, line 1467: `m_messagesWidget->addMessage()`

12. **Race condition on database cleanup timer** (MainWindow.cpp:485-490)
    - `QTimer::singleShot(5000, ...)` in `onConnected()` runs DB cleanup asynchronously
    - If disconnect occurs before timer fires, database may be deleted before cleanup runs

13. **`m_pendingUpdate` not thread-safe** (NodeManager.cpp:22-26)
    - `m_pendingUpdate` bool is read/written without atomics; NodeManager uses mutex for `m_nodes` but not for this flag
    - Should use `std::atomic<bool>` or `QAtomicInt`

14. **Database prepared statements use raw `new`** (Database.cpp:446-480)
    - `m_saveNodeStmt` and `m_saveMessageStmt` allocated with `new QSqlQuery`
    - Should use `std::unique_ptr` for automatic cleanup on error paths

15. **Missing database validity checks in widgets** (TracerouteWidget.cpp, TelemetryGraphWidget.cpp)
    - Widgets check `if (m_database)` but don't verify `m_database->isOpen()`

### Low Priority

16. **Hardcoded dialog size** (MainWindow.cpp:1543) - traceroute dialog is 500x400, not responsive
17. **No WAL mode for SQLite** - Could improve concurrent read/write performance
18. **`closeEvent` doesn't save database** - Nodes in memory may not be persisted if app is killed
19. **Verbose debug logging** - Many qDebug() calls that could be noisy in production; --debug flag exists but some qDebug calls happen regardless
20. **QByteArray copies from std::string** - Could use `fromRawData()` for temporary views instead of copying

## Refactoring Opportunities (from docs/CODE_IMPROVEMENTS.md)

| # | Item | Status | Priority |
|---|------|--------|----------|
| 1 | Empty heartbeat timer lambda | **FIXED** (heartbeat now sends actual packets) | Done |
| 2 | Dead `onConfigCompleteIdReceived()` code | **FIXED** (now connected and called) | Done |
| 3 | Protobuf parse error validation | **FIXED** (all ParseFromArray calls checked) | Done |
| 4 | Duplicate config field mapping | **FIXED** (extracted to static helpers) | Done |
| 5 | Duplicate packet framing code | **FIXED** (extracted `wrapInFrame()`) | Done |
| 6 | Extract magic numbers to constants | **PARTIALLY FIXED** (SYNC_BYTE_1/2 + SNR_SCALE_FACTOR added) | Low |
| 7 | Database PRAGMA error checking | **FIXED** | Done |
| 8 | Replace emojis with icons | Not done | Low |
| 9 | Optimize node list sorting | **FIXED** (caching + dirty flag) | Done |
| 10 | Save/restore splitter sizes | **FIXED** (QSettings save/restore) | Done |

## Architecture Quick Reference

```
┌─────────────────────────────────────────────┐
│                 MainWindow                   │
│  ┌─────────┐ ┌──────────┐ ┌──────────────┐ │
│  │ MapTab  │ │ Messages │ │   Packets    │ │
│  │(Leaflet)│ │ (Chat)   │ │  (Capture)   │ │
│  └────┬────┘ └────┬─────┘ └──────────────┘ │
│  ┌────┴────┐ ┌────┴─────┐ ┌──────────────┐ │
│  │Traceroute│ │ Config   │ │  Telemetry   │ │
│  │ Widget  │ │ Widget   │ │   Graphs     │ │
│  └─────────┘ └──────────┘ └──────────────┘ │
└───────────────────┬─────────────────────────┘
                    │
         ┌──────────┼──────────┐
         │          │          │
    ┌────┴────┐ ┌───┴───┐ ┌───┴──────┐
    │Protocol │ │ Node  │ │ Database │
    │(Protobuf│ │Manager│ │ (SQLite) │
    │ + AES)  │ │       │ │          │
    └────┬────┘ └───────┘ └──────────┘
         │
    ┌────┴─────┐
    │  Serial  │
    │Connection│
    │(USB 115k)│
    └──────────┘
```

## Key Files to Touch First

| Task | Files |
|------|-------|
| Add new transport (TCP/BT) | `src/SerialConnection.cpp/.h` (mirror API), `src/MainWindow.cpp` (connection UI) |
| Add new packet type | `src/MeshtasticProtocol.cpp` (decode), `src/MainWindow.cpp` (handle) |
| Add new config section | `src/DeviceConfig.h/.cpp`, `src/ConfigWidget.cpp`, new config tab |
| Map changes | `resources/map.html` (JS), `src/MapWidget.cpp` (C++ bridge) |
| Database changes | `src/Database.cpp/.h` (bump SCHEMA_VERSION, add migration) |
| New telemetry field | Proto, `MeshtasticProtocol.cpp`, `NodeManager.cpp`, `NodeManager.h` |

## Session 2026-02-15: Bug Fixes, UI Improvements, Map Beautification

### Bug Fixes
- **Fixed**: Redundant `isConnected()` check in `requestConfig()` (was item #4)
- **Fixed**: `getAllMessages()` now reads `packet_id` from DB instead of hardcoding 0 (was item #10)
- **Fixed**: Renamed `SerialConnection::disconnect()` to `disconnectDevice()` to avoid `QObject::disconnect()` collision (was item #7)
  - Updated all callers: SerialConnection destructor, connectToPort, MainWindow destructor, MainWindow::disconnect

### UI Improvements
- **Unread count in Messages tab**: Tab title shows "Messages (5)" when there are unread messages
  - Added `unreadCountChanged` signal and `totalUnreadCount()` method to MessagesWidget
  - Status label now shows "Unread: X | Total: Y | Showing: Z"
- **Short name column in node list**: Added "Short" column between Name and Role (6 columns total)
- **Signal bars for direct nodes**: Replaced "Hops Away" column with "Signal" column
  - 0-hop nodes show signal bars (||||, |||, ||, |) colored green/orange/red based on SNR
  - Multi-hop nodes show "2 hops" in gray
  - Tooltip shows exact SNR/RSSI values
- **My node always at top**: Sort lambda places own node first, with bold text styling

### Map Beautification
- **Larger markers** (30x30px) with white inner border and drop shadows
- **Node name labels** below markers (visible at medium zoom)
- **Better color scheme**: My node = blue, Online (<15min) = green, Stale = gray, Low battery = red accent ring
- **Rich popups** with structured layout: name header, battery bar, signal info, relative time, altitude, role
- **Scale control** added to map
- **Dark background** color for map container
- **Semi-transparent attribution** styling

### Files Modified
| File | Changes |
|------|---------|
| `src/MainWindow.cpp` | Bug fixes, 6-column node list, signal bars, my-node sort, unread tab |
| `src/MainWindow.h` | Added `m_messagesTabIndex` member |
| `src/SerialConnection.h` | Renamed `disconnect()` to `disconnectDevice()` |
| `src/SerialConnection.cpp` | Renamed method + all internal callers |
| `src/Database.cpp` | Fixed packetId read in `getAllMessages()` |
| `src/MessagesWidget.h` | Added `unreadCountChanged` signal, `totalUnreadCount()` method |
| `src/MessagesWidget.cpp` | Implemented unread count, updated status label |
| `src/NodeManager.h` | Added `lastHeardSecs` to `toVariantMap()` |
| `resources/map.html` | Complete marker/popup/color/style overhaul |
| `src/MapWidget.cpp` | Pass extra fields (isMyNode, lastHeardSecs, snr, rssi, role, hops, voltage) to JS |

## Next Steps (Suggested)

1. **Feature**: Add TCP/IP connection support (many users can't use USB)
2. **Feature**: Add message search (frequently requested, relatively simple)
3. **Feature**: Add packet export (useful for debugging)
4. **Refactoring**: Extract DB node mapping helper to reduce duplication
5. **CI**: Add macOS build, dependency caching (from `docs/CI_IMPROVEMENTS.md`)

---
*This document is auto-generated to help resume work quickly across sessions.*
