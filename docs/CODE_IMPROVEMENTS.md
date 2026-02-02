# Code Improvements TODO

## Critical/Bugs

### 1. Fix empty heartbeat timer lambda
**File:** MainWindow.cpp:66-81

The config heartbeat timer is started but the lambda is empty (only has a qDebug). No heartbeat packets are actually sent. Either implement heartbeat sending or remove the dead timer code.

---

### 2. Remove dead code: onConfigCompleteIdReceived()
**File:** MainWindow.cpp:1790-1810

The method `onConfigCompleteIdReceived()` is defined but never called or connected to any signal. Remove this dead code or connect it properly.

---

### 3. Add protobuf parse error validation
**File:** MeshtasticProtocol.cpp:414, 433, 483

Multiple `ParseFromArray()` calls have no return value validation before accessing fields. Add checks like:
```cpp
if (!msg.ParseFromArray(data.data(), data.size())) {
    qWarning() << "Failed to parse protobuf message";
    return;
}
```

---

## Refactoring

### 4. Refactor duplicate config field mapping code
**File:** MeshtasticProtocol.cpp

~50 lines of identical config field mapping code duplicated:
- Lines 216-231 vs 493-509 (device config)
- Lines 233-247 vs 511-525 (position config)
- Lines 249-265 vs 527-543 (lora config)

Extract to helper functions like `mapDeviceConfig()`, `mapPositionConfig()`, `mapLoraConfig()`.

---

### 5. Refactor duplicate packet framing code
**File:** MeshtasticProtocol.cpp:725-1023

Six nearly identical packet creation functions with duplicated sync byte framing code. Create a helper function:
```cpp
QByteArray wrapPacketFrame(const QByteArray& payload) {
    QByteArray frame;
    frame.append(0x94);
    frame.append(0xC3);
    frame.append((payload.size() >> 8) & 0xFF);
    frame.append(payload.size() & 0xFF);
    frame.append(payload);
    return frame;
}
```

---

### 6. Extract magic numbers to named constants
**Files:** MeshtasticProtocol.cpp, MainWindow.cpp

Multiple hardcoded values should be constants:
- MeshtasticProtocol.cpp:1044-1045: `0x94, 0xC3` sync bytes
- MeshtasticProtocol.cpp:448, 452, 469, 473: `4.0` SNR conversion factor
- MainWindow.cpp:862, 923: `30` seconds traceroute timeout

Add to header:
```cpp
static constexpr uint8_t SYNC_BYTE_1 = 0x94;
static constexpr uint8_t SYNC_BYTE_2 = 0xC3;
static constexpr double SNR_SCALE_FACTOR = 4.0;
static constexpr int TRACEROUTE_TIMEOUT_SEC = 30;
```

---

## Improvements

### 7. Add database PRAGMA error checking
**File:** Database.cpp:46, 140-144

`query.exec("PRAGMA foreign_keys = ON")` result not checked. ALTER TABLE commands failures silently ignored.

Add error handling:
```cpp
if (!query.exec("PRAGMA foreign_keys = ON")) {
    qWarning() << "Failed to enable foreign keys:" << query.lastError().text();
}
```

---

### 8. Replace emojis with icons for portability
**File:** MainWindow.cpp:944, 1031

Emojis may not render on all systems:
- "🔄" cooldown indicator
- "⭐" for favorites

Replace with Qt icons or ASCII alternatives for better cross-platform support.

---

### 9. Optimize node list sorting
**File:** MainWindow.cpp:996-997

`std::sort()` is called on all nodes every time `updateNodeList()` is called, including during search filter updates.

Optimize by:
1. Only sort when node data changes, not on every filter update
2. Use a sorted container or maintain sort order incrementally

---

### 10. Save/restore splitter sizes
**File:** MainWindow.cpp:262

Splitter sizes hardcoded as `{800, 200}`. Should save to QSettings on close and restore on startup for better UX.

---

## Additional Notes

### Safe Patterns Found (no action needed)
- Qt parent-child ownership properly used throughout
- Database uses parameterized queries (no SQL injection)
- Proper null checks on `m_database`, `m_webView`, `m_mapReady`
- TracerouteWidget has proper bounds checking

### Low Priority
- MainWindow.cpp:1327 - Hardcoded dialog size `(500, 400)` - not responsive
- String `.replace()` called multiple times during CSV export - minor inefficiency
