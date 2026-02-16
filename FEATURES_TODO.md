# Meshtastic Client - Features Comparison & TODO

Comparison with official Meshtastic Web UI to identify features to add.

## Legend
- ✅ = Implemented
- ⚠️ = Partially implemented
- ❌ = Not implemented (TODO)

---

## Messaging Features

| Feature | Status | Notes |
|---------|--------|-------|
| Text messaging | ✅ | Send/receive to nodes and channels |
| Direct messaging (DM) | ✅ | Private 1-on-1 conversations |
| Channel messaging | ✅ | Broadcast to channel groups |
| Message reactions/emojis | ✅ | Emoji reactions |
| Message replies | ✅ | Reply with ID tracking |
| Message delivery status | ✅ | Sending, Sent, Delivered, Failed, etc. |
| Message persistence | ✅ | SQLite storage |
| Unread message tracking | ✅ | Per conversation counts |
| Message export (CSV/JSON) | ✅ | Full export capability |
| **Canned messages** | ❌ | Pre-defined quick messages |
| **Message search** | ❌ | Search through message history |

---

## Node Management

| Feature | Status | Notes |
|---------|--------|-------|
| Node discovery | ✅ | Automatic detection |
| Node info display | ✅ | Name, ID, role, battery, hops |
| Favorite nodes | ✅ | Mark favorites |
| Node details panel | ✅ | Hardware, signal, position, etc. |
| Node context menu | ✅ | DM, traceroute, request info |
| Node telemetry | ✅ | Temp, humidity, pressure |
| Node export (CSV/JSON) | ✅ | Full export |
| **Mesh Detector** | ❌ | Periodic nodeinfo request to detect nearby nodes |
| **Signal Scanner** | ✅ | Monitor signal strength between nodes for placement |
| **Node filtering** | ⚠️ | Basic search - need advanced filters |
| **Node highlighting** | ❌ | Emphasize specific nodes |
| **Neighbor info display** | ❌ | Show node neighbors on map |

---

## Map & Position

| Feature | Status | Notes |
|---------|--------|-------|
| Interactive map | ✅ | Leaflet.js + WebEngine |
| Node markers | ✅ | Position visualization |
| Tile server selection | ✅ | Multiple providers + custom |
| Node blinking | ✅ | Visual packet indicator |
| Center on node | ✅ | Click to center |
| Fit all nodes | ✅ | Zoom to show all |
| Packet flow visualization | ✅ | Experimental feature |
| **Waypoints** | ❌ | Create, display, share waypoints |
| **Offline map tiles** | ❌ | SD card / local tile storage |
| **Position precision control** | ❌ | Privacy control for location data |
| **Home position** | ❌ | Set and recall home position |
| **Map brightness/contrast** | ❌ | Display customization |
| **Multiple map styles** | ⚠️ | Have tile selection, could add more |

---

## Traceroute

| Feature | Status | Notes |
|---------|--------|-------|
| Traceroute requests | ✅ | Discover packet path |
| Route visualization | ✅ | Route to and back |
| SNR tracking | ✅ | Signal strength along path |
| Traceroute history | ✅ | Database persistence |
| **Route visualization on map** | ✅ | Draw traceroute path on map |

---

## Packet Sniffing

| Feature | Status | Notes |
|---------|--------|-------|
| Packet capture | ✅ | All mesh packets |
| Packet table | ✅ | Timestamp, type, from/to, port |
| Packet filtering | ✅ | By type, port, hide local |
| **Packet statistics** | ❌ | Breakdown by type (pie chart, etc.) |
| **Packet export** | ❌ | Export captured packets to file |

---

## Radio/LoRa Configuration

| Feature | Status | Notes |
|---------|--------|-------|
| Region selection | ✅ | US, EU433, EU868, etc. |
| Modem preset | ✅ | LongFast, ShortFast, etc. |
| Hop limit | ✅ | Max hops configuration |
| TX power | ✅ | Transmit power control |
| TX enabled toggle | ✅ | Enable/disable transmit |
| Channel number | ✅ | Frequency channel selection |
| Frequency offset | ✅ | Fine tuning |
| Advanced LoRa settings | ✅ | BW, SF, CR |

---

## Device Configuration

| Feature | Status | Notes |
|---------|--------|-------|
| Device role | ✅ | Client, Router, etc. |
| Serial port control | ✅ | Enable/disable |
| Debug logging | ✅ | Toggle |
| Button GPIO | ✅ | Configuration |
| Buzzer GPIO | ✅ | Configuration |
| Rebroadcast mode | ✅ | Settings |
| Node info broadcast interval | ✅ | Configurable |
| **Bluetooth settings** | ❌ | BT enable/disable, PIN |
| **Display settings** | ❌ | Screen timeout, flip, brightness |
| **Power settings** | ❌ | Sleep mode, shutdown time |
| **Network settings** | ❌ | WiFi AP/client configuration |

---

## Channel Management

| Feature | Status | Notes |
|---------|--------|-------|
| Multi-channel support | ✅ | Up to 8 channels |
| Channel name | ✅ | Configurable |
| PSK encryption | ✅ | Pre-shared key |
| Channel role | ✅ | Disabled, Primary, Secondary |
| Uplink/downlink | ✅ | Enable/disable |
| PSK generation | ✅ | Auto-generate keys |
| **Channel encryption status icons** | ❌ | Visual key/lock icons |
| **Channel QR code** | ✅ | Generate channel QR code dialog |
| **Channel URL sharing** | ✅ | Copy Meshtastic URL to clipboard |

---

## Position Configuration

| Feature | Status | Notes |
|---------|--------|-------|
| GPS enable/disable | ✅ | Toggle |
| Smart position | ✅ | Intelligent broadcasting |
| Fixed position | ✅ | Manual coordinates |
| Position broadcast interval | ✅ | Configurable |
| GPS update interval | ✅ | Configurable |
| GPS attempt time | ✅ | Settings |
| Position flags | ✅ | Control |
| Smart minimum distance | ✅ | Meters |
| Smart minimum interval | ✅ | Seconds |

---

## Module Configuration (TODO)

| Module | Status | Description |
|--------|--------|-------------|
| **MQTT** | ❌ | Internet bridge via MQTT broker |
| **Store and Forward** | ❌ | Message routing for offline nodes |
| **Range Test** | ❌ | Network performance evaluation |
| **External Notification** | ❌ | Hardware alerts (buzzer, LED, vibration) |
| **Canned Messages** | ❌ | Pre-defined messages with rotary encoder |
| **Audio** | ❌ | Voice/audio messaging |
| **Detection Sensor** | ❌ | PIR/motion sensor integration |
| **Paxcounter** | ❌ | People/device counting |
| **Remote Hardware** | ❌ | GPIO control over mesh |
| **Neighbor Info** | ✅ | Topology visualization from neighbor data |
| **Ambient Lighting** | ❌ | LED strip control |

---

## MQTT Features (TODO)

| Feature | Status | Notes |
|---------|--------|-------|
| **MQTT enable/disable** | ❌ | Toggle |
| **Server address** | ❌ | Custom MQTT broker |
| **Username/password** | ❌ | Authentication |
| **Encryption** | ❌ | TLS support |
| **JSON mode** | ❌ | Human-readable messages |
| **Root topic** | ❌ | Custom topic prefix |
| **Map reporting** | ❌ | Report to public map |
| **Client proxy** | ❌ | Proxy mode for MQTT |

---

## Remote Administration (TODO)

| Feature | Status | Notes |
|---------|--------|-------|
| **Remote config** | ❌ | Configure remote nodes over mesh |
| **Remote reboot** | ❌ | Reboot remote nodes |
| **Remote position request** | ✅ | Request position from remote nodes |
| **Remote factory reset** | ❌ | Reset remote nodes |
| **Admin channel** | ❌ | Secure admin communications |

---

## Application Features

| Feature | Status | Notes |
|---------|--------|-------|
| Auto-connect | ✅ | Remember last port |
| Dark theme | ✅ | Light/dark toggle |
| Notifications | ✅ | System notifications |
| Sound alerts | ✅ | Audio feedback |
| Message font size | ✅ | Zoom in/out |
| Settings persistence | ✅ | SQLite storage |
| Reboot device | ✅ | With delay |
| **Firmware version check** | ✅ | Check latest firmware from GitHub API |
| **Firmware update (OTA)** | ❌ | OTA firmware updates |
| **Device backup/restore** | ❌ | Full config backup |
| **Multi-language** | ❌ | i18n support (web has 18 languages) |
| **Lock screen** | ❌ | Security feature |
| **Connection via Bluetooth** | ✅ | BLE GATT connection to device |
| **Connection via TCP/IP** | ✅ | Network connection to device |

---

## Dashboard/Stats

| Feature | Status | Notes |
|---------|--------|-------|
| Device identity | ✅ | Name, model, ID, firmware |
| Battery display | ✅ | Percentage + bar |
| Channel utilization | ✅ | Usage bar |
| Air time TX | ✅ | Usage bar |
| Environmental telemetry | ✅ | Temp, humidity, pressure |
| Uptime | ✅ | Display |
| Signal strength | ✅ | SNR, RSSI |
| **Historical telemetry graphs** | ✅ | Charts over time |
| **Network statistics** | ❌ | Messages sent/received counts |
| **Mesh topology view** | ✅ | D3.js force-directed network graph |

---

## Priority Features to Add

### High Priority
1. **MQTT Integration** - Internet bridge for global connectivity
2. **Store and Forward** - Message routing for offline nodes
3. **Waypoints** - Create and share waypoints on map
4. ~~**Channel QR/URL sharing**~~ - ✅ Done
5. ~~**Firmware version check**~~ - ✅ Done
6. ~~**TCP/IP connection**~~ - ✅ Done

### Medium Priority
1. **Mesh Detector** - Active node discovery
2. **Canned Messages** - Quick pre-defined messages
3. **Remote Administration** - Configure remote nodes
4. **Packet Statistics** - Visual breakdown of packet types
5. **Packet Export** - Export packets to file

### Low Priority
1. **Multi-language support** - i18n
2. ~~**Bluetooth connection**~~ - ✅ Done
3. **Range Test module** - Performance testing
4. **External Notifications** - Hardware alerts
5. ~~**Mesh topology visualization**~~ - ✅ Done
6. **Device backup/restore** - Config backup

---

## Sources

- [Meshtastic Web Client Overview](https://meshtastic.org/docs/software/web-client/)
- [Meshtastic UI Documentation](https://meshtastic.org/docs/configuration/device-uis/meshtasticui/)
- [Channel Configuration](https://meshtastic.org/docs/configuration/radio/channels/)
- [MQTT Module Configuration](https://meshtastic.org/docs/configuration/module/mqtt/)
- [Remote Node Administration](https://meshtastic.org/docs/configuration/remote-admin/)
- [DeepWiki - Web Client and Tools](https://deepwiki.com/meshtastic/meshtastic/4.2-web-client-and-tools)
- [DeepWiki - Module Configuration](https://deepwiki.com/meshtastic/web/6.2-module-configuration)

---

*Last updated: 2026-02-15*
