# Meshtastic Client — TODO

Priority-ordered backlog. Last reviewed 2026-09-21.

---

## High Priority

1. **Simulated traceroutes** — `--simulate` only sends NeighborInfo; traceroutes are now the primary topology source but are never exercised without hardware. Add `buildTraceroute()` + `scheduleTracerouteDump()` to `SimulationConnection` with realistic multi-hop paths and per-hop SNR matching the existing link table.

2. **TracerouteWidget: "Request Traceroute" button** — You can only fire a traceroute by right-clicking a node in the node list. Add a target-node dropdown + "Send" button inside the Traceroute tab itself so it's discoverable.

3. **Map: draw traceroute hop paths** — `tracerouteSelected` in `TracerouteWidget` already fires when a row is clicked. Hook it up in `MainWindow` to also draw a polyline on the map (hop by hop), not just highlight the topology graph.

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

13. **Waypoints** — Meshtastic supports waypoint packets (portnum 70 = `WAYPOINT_APP`). Add parsing in `MeshtasticProtocol` and display on the map with a distinct marker.

14. **Export: add GPX format** — Node export currently supports CSV and JSON. GPX (with timestamps and elevation) would make it easy to import tracks into mapping tools.

15. **Signal Scanner: live sweep mode** — The signal scanner currently shows historical SNR/RSSI per packet. Add an optional auto-refresh that issues a traceroute to every known node every N minutes to build a live RF picture.

---

## Analyst Tooling

Turning the client from a mesh *chat* app into something that answers questions
about the mesh itself. Ordered by value; the first three run entirely on data
already being collected.

Figures below are from a live 84-node EU_868 mesh, ~2600 packets over 95
minutes, to give a sense of scale.

16. **Decode success panel** — 2398 of 2597 observed packets (92%) recorded
    `portnum 0`, i.e. never decoded. Some of that is other people's encrypted
    traffic and is expected; the rest may be our own channels failing to
    decrypt. Show packets seen vs decoded, broken down by channel hash, so the
    difference between "not ours" and "our keys are wrong" becomes a number.
    This is the prerequisite for trusting any other figure, and it is pure query
    work over the existing `packets` table.

17. **Airtime budget** — EU_868 caps duty cycle at 10%. `channel_utilization`
    and `air_util_tx` are already stored per node. Plot our own airtime against
    the regulatory ceiling over time, and rank nodes by contribution; a single
    chatty router is the usual cause of a congested mesh.

18. **Reachability and single points of failure** — Build the mesh graph from
    hops, SNR and traceroutes, then find articulation points: nodes whose loss
    would partition the network. The most valuable single view for anyone
    running infrastructure. Depends on traceroutes actually persisting (fixed in
    a65e9a9 — older databases silently had no `traceroutes` table).

19. **Route churn** — Compare repeated traceroutes to the same destination over
    a time window. A path that keeps changing is an unreliable link worth
    investigating; a stable one is not. Needs a retention window longer than the
    current default (see 22).

20. **Position history on the map** — `position_history` holds 422 rows across
    71 nodes and nothing displays it. Draw movement tracks per node, with a time
    slider. Close to free given the data is already there.

21. **`neighbor_info` is never populated** — the table exists and is pruned, but
    holds 0 rows. Either our nodes do not broadcast NeighborInfo or we never
    request it. Worth diagnosing: it is a free topology source that does not
    cost the airtime a traceroute does.

22. **Retention for trend analysis** — history retention now defaults to 7 days,
    which is far too short for the trend views above. At the observed rate
    (~2600 packets / 95 min) a month of full packet capture is roughly 2M rows.
    Consider a longer default for packets' *summary* statistics while keeping
    the raw rows short-lived, rather than one retention value for everything.

23. **Replay / time travel** — `packets` is timestamped, so mesh activity over a
    chosen window can be replayed on the map and topology view. Useful for
    explaining an outage after the fact.
