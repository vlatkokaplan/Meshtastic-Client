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

13. **Waypoints** — Meshtastic supports waypoint packets (portnum 70 = `WAYPOINT_APP`). Add parsing in `MeshtasticProtocol` and display on the map with a distinct marker.

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

## Ground rules

- **Airtime is not ours to spend.** Anything that transmits to gather data is
  out unless it is user-initiated and one-shot. A tool that measures the mesh
  must not add load to it, and the band is shared with other operators. This
  ruled out item 15.
- Every analyst view added so far is read-only over the local database.
