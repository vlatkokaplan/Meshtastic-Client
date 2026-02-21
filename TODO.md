# Meshtastic Client — TODO

Priority-ordered backlog as of 2026-02-21.

---

## High Priority

1. **Simulated traceroutes** — `--simulate` only sends NeighborInfo; traceroutes are now the primary topology source but are never exercised without hardware. Add `buildTraceroute()` + `scheduleTracerouteDump()` to `SimulationConnection` with realistic multi-hop paths and per-hop SNR matching the existing link table.

2. **TracerouteWidget: "Request Traceroute" button** — You can only fire a traceroute by right-clicking a node in the node list. Add a target-node dropdown + "Send" button inside the Traceroute tab itself so it's discoverable.

3. **Map: draw traceroute hop paths** — `tracerouteSelected` in `TracerouteWidget` already fires when a row is clicked. Hook it up in `MainWindow` to also draw a polyline on the map (hop by hop), not just highlight the topology graph.

---

## Medium Priority

4. **Windows CI: run unit tests** — The Linux CI job runs `ctest`; the Windows job builds the NSIS installer but never runs the test suite. Add a `ctest` step to the Windows workflow.

5. **Config tab: "Reboot Device" button** — Some Meshtastic config changes (LoRa region, role) require a device reboot to take effect. Add a reboot button that sends `AdminMessage::reboot_seconds = 2` after saving.

6. **Node list: persist sort/filter state across reconnects** — The search text and column sort order reset when the connection drops and comes back. Save them in `AppSettings` and restore on reconnect.

7. **Message ACK rendering audit** — `updateMessageStatus` / `updateMessageDelivered` exist but the delivery tick/cross rendering hasn't been tested end-to-end for multi-hop DMs vs. broadcast. Verify with hardware or extend the simulation to send routing ACK packets back.

8. **Telemetry graph: time-range selector** — The graph currently shows all stored data. Add a "1 h / 6 h / 24 h / All" toggle that filters the query to `Database::loadTelemetry()`.

---

## Low Priority / Polish

9. **Bluetooth reliability** — Meshtastic BLE requires specific GATT service UUIDs, MTU negotiation (≥ 512 bytes), and CCCD subscription. Test with a physical device and harden the reconnect path in `BluetoothConnection`.

10. **Topology: show node names from DB on cold start** — On first launch the topology loads links from DB but node names are empty until a live `NodeInfo` arrives. Pre-populate node names from the `nodes` table in `loadFromDatabase()`.

11. **Map: cluster markers for dense areas** — When many nodes are close together the Leaflet map gets cluttered. Enable `leaflet.markercluster` or use a circle-packing approach at low zoom levels.

12. **Config tab: remote node config** — All config writes currently go to `myNode → myNode` (own device). Extend the UI to select a remote node and send `AdminMessage` addressed to it (requires session key exchange for encrypted meshes).

13. **Waypoints** — Meshtastic supports waypoint packets (portnum 70 = `WAYPOINT_APP`). Add parsing in `MeshtasticProtocol` and display on the map with a distinct marker.

14. **Export: add GPX format** — Node export currently supports CSV and JSON. GPX (with timestamps and elevation) would make it easy to import tracks into mapping tools.

15. **Signal Scanner: live sweep mode** — The signal scanner currently shows historical SNR/RSSI per packet. Add an optional auto-refresh that issues a traceroute to every known node every N minutes to build a live RF picture.
