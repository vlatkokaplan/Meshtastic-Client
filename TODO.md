# Meshtastic Client — TODO

Priority-ordered backlog. Last reviewed 2026-09-21.

---

## High Priority

1. ~~**Simulated traceroutes**~~ — **DONE** (732cbd1): `scheduleTracerouteDump()` / `buildTraceroute()` in `SimulationConnection`.

2. ~~**TracerouteWidget: "Request Traceroute" button**~~ — **DONE** (732cbd1): target combo + request button in the Traceroute tab.

3. ~~**Map: draw traceroute hop paths**~~ — **DONE**: `tracerouteSelected` drives `MapWidget::drawTraceroute()` from `MainWindow`.

---

## Medium Priority

4. ~~**Windows CI: run unit tests**~~ — **DONE** (added `ctest -C Release` step to Windows workflow with Qt bin dir on PATH).

5. ~~**Config tab: "Reboot Device" button**~~ — **ALREADY DONE** (`m_rebootButton` in toolbar wired to `rebootDevice()` slot).

6. ~~**Node list: persist sort/filter state across reconnects**~~ — **DONE** (search text + sort column/order saved in `AppSettings`, restored in `setupNodeList()`).

7. ~~**Message ACK rendering audit**~~ — **DONE** (simulation now sends `ROUTING_APP` ACK 500 ms after each DM to a sim node via `buildRoutingAck()`).

8. ~~**Telemetry graph: time-range selector**~~ — **ALREADY DONE** (`m_timeRangeCombo` with 1 h / 6 h / 24 h / 7 days already implemented in `TelemetryGraphWidget`).

---

## Low Priority / Polish

9. **Bluetooth reliability** — Meshtastic BLE requires specific GATT service UUIDs, MTU negotiation (≥ 512 bytes), and CCCD subscription. Test with a physical device and harden the reconnect path in `BluetoothConnection`.

10. **Topology: show node names from DB on cold start** — On first launch the topology loads links from DB but node names are empty until a live `NodeInfo` arrives. Pre-populate node names from the `nodes` table in `loadFromDatabase()`.

11. ~~**Map: cluster markers for dense areas**~~ — **DONE** (`leaflet.markercluster` layer in `map.html` with a plain-layer fallback; `selectNode()` calls `zoomToShowLayer()` so selecting a clustered node still works).

12. **Config tab: remote node config** — All config writes currently go to `myNode → myNode` (own device). Extend the UI to select a remote node and send `AdminMessage` addressed to it (requires session key exchange for encrypted meshes).

13. **Waypoints** — Meshtastic supports waypoint packets (portnum 8 = `WAYPOINT_APP`). Add parsing in `MeshtasticProtocol` and display on the map with a distinct marker.

14. **Export: add GPX format** — Node export currently supports CSV and JSON. GPX (with timestamps and elevation) would make it easy to import tracks into mapping tools.

15. ~~**Signal Scanner: live sweep mode**~~ — **WILL NOT DO**. Sweeping traceroutes to every node costs real airtime on a duty-cycle-limited band, and spends other operators' airtime as well as ours. A tool that measures the mesh must not add load to it. Passive analysis (16-23) covers the same ground for free. Note "Scan All" in the scanner is a *filter* over passively received measurements, not a sweep - the label is misleading and should be renamed.

---

## Analyst Tooling — implemented

Turning the client from a mesh *chat* app into something that answers questions
about the mesh itself. Ordered by value; the first three run entirely on data
already being collected.

Figures below are from a live 84-node EU_868 mesh, ~2600 packets over 95
minutes, to give a sense of scale.

16. ~~**Decode success panel**~~ — **DONE**: Analytics tab. Scoped to `PacketReceived` rows only - counting device-to-app frames had buried a real 97% decode rate under an apparent 7%. Originally: **Decode success panel** — 2398 of 2597 observed packets (92%) recorded
    `portnum 0`, i.e. never decoded. Some of that is other people's encrypted
    traffic and is expected; the rest may be our own channels failing to
    decrypt. Show packets seen vs decoded, broken down by channel hash, so the
    difference between "not ours" and "our keys are wrong" becomes a number.
    This is the prerequisite for trusting any other figure, and it is pure query
    work over the existing `packets` table.

17. ~~**Airtime budget**~~ — **DONE**: Analytics tab, with the region's duty cycle drawn as a limit line and a per-node table ranked by peak. Originally: **Airtime budget** — EU_868 caps duty cycle at 10%. `channel_utilization`
    and `air_util_tx` are already stored per node. Plot our own airtime against
    the regulatory ceiling over time, and rank nodes by contribution; a single
    chatty router is the usual cause of a congested mesh.

18. ~~**Reachability and single points of failure**~~ — **DONE**: Analytics tab. Hopcroft-Tarjan articulation points over traceroute hops and NeighborInfo, 13 unit tests. Originally: **Reachability and single points of failure** — Build the mesh graph from
    hops, SNR and traceroutes, then find articulation points: nodes whose loss
    would partition the network. The most valuable single view for anyone
    running infrastructure. Depends on traceroutes actually persisting (fixed in
    a65e9a9 — older databases silently had no `traceroutes` table).

19. ~~**Route churn**~~ — **DONE**: Analytics tab. Churn left uncoloured below three observations. Originally: **Route churn** — Compare repeated traceroutes to the same destination over
    a time window. A path that keeps changing is an unreliable link worth
    investigating; a stable one is not. Needs a retention window longer than the
    current default (see 22).

20. ~~**Position history on the map**~~ — **DONE**: "Show Movement History" on the node context menu draws recorded fixes as a fading track. Originally: **Position history on the map** — `position_history` holds 422 rows across
    71 nodes and nothing displays it. Draw movement tracks per node, with a time
    slider. Close to free given the data is already there.

21. ~~**`neighbor_info` is never populated**~~ — **RESOLVED, not a bug**: zero portnum-71 packets have ever been received (ports seen: 0, 3, 4, 5, 67). Our parsing and persistence are correct; no node on this mesh broadcasts NeighborInfo, as the module is off by default in firmware. Enabling it on our own node would mean periodic broadcasts, which the airtime rule rules out. Originally: **`neighbor_info` is never populated** — the table exists and is pruned, but
    holds 0 rows. Either our nodes do not broadcast NeighborInfo or we never
    request it. Worth diagnosing: it is a free topology source that does not
    cost the airtime a traceroute does.

22. ~~**Retention for trend analysis**~~ — **DONE**: split into packet log retention (7 days) and history retention (90 days). Originally: **Retention for trend analysis** — history retention now defaults to 7 days,
    which is far too short for the trend views above. At the observed rate
    (~2600 packets / 95 min) a month of full packet capture is roughly 2M rows.
    Consider a longer default for packets' *summary* statistics while keeping
    the raw rows short-lived, rather than one retention value for everything.

23. ~~**Replay / time travel**~~ — **DONE**: replay bar beneath the map, 60x-1800x, scrubbable. Originally: **Replay / time travel** — `packets` is timestamped, so mesh activity over a
    chosen window can be replayed on the map and topology view. Useful for
    explaining an outage after the fact.

---

## Code audit — 2026-09-23

Checked against firmware `develop`, meshtastic/protobufs master (2026-09-21),
the Python client and the client API docs. Build was clean and tests passed,
but the vendored `proto/` was a hand-trimmed copy that disagreed with upstream.

### Critical

24. ~~**`set_channel` used the wrong field number**~~ — **FIXED 2026-09-23** (upstream protos). Channel `id` and `module_settings` (position precision, mute) now round-trip so a save no longer resets them. Was: — local `admin.proto` had
    `set_channel = 32`; upstream 32 is `set_owner`, `set_channel` is 33. The
    firmware decodes our Channel as a User, hits a wire-type mismatch and drops
    it. Channel saves never took effect; the tab showed "Saved" regardless.

25. ~~**Reboot button sent `reboot_ota_seconds`**~~ — **FIXED 2026-09-23** (upstream protos). Was: — local `reboot_seconds = 95`;
    upstream 95 is `reboot_ota_seconds` (ESP32 reboots into the OTA loader on
    firmware < 2.7.17, no-op on current). Real `reboot_seconds` is 97; local
    96-98 were all shifted.

26. ~~**BLE writes framed bytes to ToRadio**~~ — **FIXED 2026-09-23**: `sendData` strips and validates the stream header; notify path framed via `frameFromRadio()`. Untested on hardware. Was: — `BluetoothConnection::sendData`
    wrote `94 C3 len` + protobuf; BLE ToRadio takes the bare protobuf, so every
    write is malformed. The FromRadio notify path also emitted unframed bytes.

27. ~~**Heartbeat malformed**~~ — **FIXED 2026-09-23** (empty `Heartbeat` message). Was: — local `int64 heartbeat = 7`; upstream it is a
    `Heartbeat` message. Firmware logs "Ignore malformed toradio" every minute
    and never sends the QueueStatus reply.

28. ~~**Other proto wire mismatches**~~ — **FIXED 2026-09-23**: `proto/` is now a verbatim upstream copy (`scripts/update-protos.sh` to refresh), generated into `build/proto_gen`; 4 wire-format tests with hand-encoded bytes fail on the old protos. Was: — `ChannelSettings.id` / `module_settings`,
    ModuleConfig 11/12 swapped, TelemetryConfig 6/7 swapped,
    `NeighborInfo.last_sent_by_id`, `EnvironmentMetrics.wind_direction`,
    `MyNodeInfo` field 12. Missing: `MeshPacket.pki_encrypted/public_key/
    relay_node`, `User.public_key`, `Config.security`,
    `FromRadio.clientNotification`, ~40 AdminMessage variants. Fix: use upstream
    protobufs, plus a test decoding bytes captured from a real device.

### High

29. ~~**`set_config` wipes fields the UI does not send**~~ — **FIXED 2026-09-23**: each section's raw protobuf is kept from the device and saves are built on it. Tabs now start from the stored config instead of a fresh struct (Position was resetting `position_flags`, Radio was forcing `use_preset=true` and zeroing BW/SF/CR). Region/preset/role/GPS combos are built from the proto descriptors and store enum values, so presets 9-16, newer regions and roles show correctly and an unknown value is kept rather than replaced. Not done: `begin_edit_settings`/`commit_edit_settings` (each save is one section). Was: — a set replaces the
    whole section. LoRa resets `ignore_mqtt`, `config_ok_to_mqtt`,
    `sx126x_rx_boosted_gain`, `override_frequency`, `ignore_incoming`; Position
    resets GPS pins; Channel resets `position_precision` to 0 (position sharing
    off) and invents a new `id` each save. Keep the last-received protobuf and
    change only edited fields; wrap multi-section saves in
    `begin_edit_settings` / `commit_edit_settings`.

30. ~~**Outgoing reactions have no `emoji` flag**~~ — **FIXED 2026-09-23**. Was: — `createTextMessagePacket`
    never sets `decoded.emoji = 1`, so other apps show tapbacks as replies.

31. ~~**No message length limit**~~ — **FIXED 2026-09-23**: 200-byte cap (`MeshtasticProtocol::MAX_TEXT_BYTES`), byte counter by the input, text kept in the box when too long. Was: — payloads over 233 bytes fail to decode on the
    device and sit pending forever. Official apps cap at 200 UTF-8 bytes.

32. ~~**Preset channel names incomplete**~~ — **FIXED 2026-09-23**: full firmware table, "Custom" when `use_preset` is off, and every unnamed channel (not only index 0) takes the preset name, as in `Channels::getName`. Was: — `modemPresetChannelName` misses
    VeryLongSlow (2) and presets 9-16, and ignores `use_preset=false`
    ("Custom"). Wrong hash → brute force or wrong channel.

### Medium

33. ~~**Traceroute last-hop SNR appended twice**~~ — **FIXED 2026-09-23**: `rx_snr` is only appended when an old-firmware response is missing that entry; the simulator now appends it like firmware. Was: — firmware already appends it to
    `snr_back` (`TraceRouteModule::appendMyIDandSNR`, SNRonly); we append
    `rx_snr` again.

34. ~~**Packet IDs from the millisecond clock**~~ — **FIXED 2026-09-23**: `nextPacketId()` = random 22 bits + 10-bit counter (Python client scheme); config nonce is random and avoids 0 / 69420 / 69421. Was: — predictable, and two sends in
    one ms collide and get deduplicated. Use random + counter like the Python
    client. Same for `want_config_id`.

35. ~~**Brute-force decrypt on UI thread, including PKI DMs**~~ — **FIXED 2026-09-23**: PKI DMs skipped and labelled in the packet list. Threading not done: measured ~0.6 ms per undecryptable packet, well under 1% of a thread at real mesh rates. Was: — skip
    `pki_encrypted`; use the channel hash to narrow keys and infer the name.

36. ~~**Routing errors collapse to "Failed"**~~ — **FIXED 2026-09-23**: DutyCycleLimit, TooLarge, NoChannel, PkiNoKey, PkiUnknownPubkey, RateLimited statuses (appended to the enum, DB ints unchanged) with tooltips. Also decodes `ClientNotification` and shows it in the status bar. Was: — surface DUTY_CYCLE_LIMIT,
    TOO_LARGE, PKI_UNKNOWN_PUBKEY, RATE_LIMIT_EXCEEDED.

37. ~~**No serial wake-up bytes**~~ — **FIXED 2026-09-23**: 32 × `0xC3` on serial/TCP connect. Was: — the Python client sends 32 × `0xC3` before the
    first packet to wake the device and resync its parser.

### Low

38. ~~**FIXED 2026-09-23**~~: telemetry decoded by reflection over set fields (presence, not value); all variants incl. AirQuality / LocalStats / Health / Host; `NodeManager` takes each key only from its own variant (environment voltage no longer overwrites battery voltage). Was: Telemetry `!= 0` checks drop real zeros (0 °C, 0 %); use `has_*()`.
    LocalStats / Health / Host / AirQuality telemetry not decoded.
39. ~~**FIXED 2026-09-23**~~: hops, via-MQTT (shown as "MQTT" in the Hops column; MQTT packets no longer update SNR/RSSI/hops) and DeviceMetrics snapshot applied at connect; hops left unknown when `hop_start` is 0. `viaMqtt` is not persisted to the DB. Was: Startup NodeInfo `hops_away`, `via_mqtt`, `device_metrics`, `channel` ignored.
40. ~~**FIXED 2026-09-23**~~: table follows the Python client's supported_device.py, plus CH343 and vendor wildcards for Adafruit/Seeed nRF52 and RP2040. Was: `KNOWN_DEVICES` missing CH343 (1A86:55D3), RP2040 (2E8A:*), RAK/Seeed nRF52.
41. ~~**FIXED 2026-09-23**~~: backoff 3→6→12→24→30 s; OS keepalive; watchdog drops a TCP link silent for 3 heartbeats; reconnect state is set before `disconnected()` is emitted, so a drop with no socket error no longer clears nodes. New `test_tcp` against a local server. Was: TCP: no reconnect backoff; no dead-link detection (use QueueStatus reply to
    heartbeat once 27 is fixed).
42. ~~**FIXED 2026-09-23**~~ (item 13 corrected). Was: Item 13 above says waypoint is portnum 70 — it is 8 (70 is TRACEROUTE).
43. ~~**Empty PSK means no encryption**~~ — **FIXED 2026-09-23**: confirmation (default Cancel) before saving an empty or 0x00 key. Was: — now that channel saves reach the
    device (24), leaving the PSK field blank saves an unencrypted channel.
    Confirm with the user before saving an empty key.
44. ~~**Deprecated DeviceConfig fields in the UI**~~ — **FIXED 2026-09-23**: "Enable Serial API" and "Send Debug Logs to Clients" edit `SecurityConfig` on top of the device's raw config (keys untouched); sent only when changed, inside `begin_edit_settings`/`commit_edit_settings`, with a warning before disabling serial while connected over serial. Deprecated `device.serial_enabled` / `is_managed` now only round-trip. Was: — "Enable Serial Output"
    writes `device.serial_enabled` and `is_managed`, both moved to
    `SecurityConfig` upstream; firmware ignores them. Removed the equally dead
    "Enable Debug Logging" checkbox (field 3 is reserved upstream). Move these
    to a Security tab - but a SecurityConfig set must round-trip the keys.
45. **Factory Reset button does nothing** — `DeviceConfigTab::factoryResetRequested`
    is emitted but never connected. Either wire it to `factory_reset_config`
    (99) / `factory_reset_device` (94) or remove the button.
46. **No UI for custom modem settings** — with `use_preset` off the Radio tab
    now greys out the preset and keeps the device's BW/SF/CR, but there is no
    way to view or edit them, or to switch between preset and custom.

---

## Ground rules

- **Airtime is not ours to spend.** Anything that transmits to gather data is
  out unless it is user-initiated and one-shot. A tool that measures the mesh
  must not add load to it, and the band is shared with other operators. This
  ruled out item 15.
- Every analyst view added so far is read-only over the local database.
