# Agent API Task 3 Report

## Status

**COMPLETE** — `pin_resolve` core helpers implemented and unit-tested (no HTTP routes).

## Scope

- Create: `src/obv_core/include/obv_core/pin_resolve.h`
- Create: `src/obv_core/src/pin_resolve.cpp`
- Modify: `src/obv_core/CMakeLists.txt` (`src/pin_resolve.cpp` in `OBV_CORE_SOURCES`)
- Modify: `src/obv_core_tests/test_parse_export.cpp` (TDD unit tests)

APIs:

- `PinOverlayKey`, `ExportPinId`
- `MeasureSource`, `MeasureField`, `PinResolveResult`
- `ResolveOneField` (pure priority: overlay > board > propagated)
- `FindComponent`, `FindPartPin`, `ResolvePinMeasurements`, `ResolvePartPin`
- `ExportPinResolveJson` (spec §4.4 shape)
- `AllocateConditionId` (`oc_0001` next-free deterministic)
- `NormalizeOperatingCondition` (trim, drop empty labels, length/array limits)

## TDD Evidence

### RED (Debug build — asserts live under MSVC Release NDEBUG)

Added failing tests first (header missing):

```
cmake --build build-core-only --target obv_core_tests --config Debug
```

Observed failure:

```
error C1083: Cannot open include file: "obv_core/pin_resolve.h": No such file or directory
```

### GREEN

```
cmake --build build-core-only --target obv_core_tests --config Debug
build-core-only/src/obv_core_tests/Debug/obv_core_tests.exe
# skip export
# overlay yaml ok
# operating_conditions yaml ok
# operating_conditions json ok
# resolve field priority ok
# normalize condition ok
# allocate condition id ok
# pin overlay key ok
# ok
# EXIT:0

cmake --build build-core-only --target obv_core_tests --config Release
build-core-only/src/obv_core_tests/Release/obv_core_tests.exe
# same output, exit 0
```

## Commit

```
feat(core): pin measurement resolve helpers for agent API
```

Files: `pin_resolve.h`, `pin_resolve.cpp`, `CMakeLists.txt`, `test_parse_export.cpp`

## Concerns / Notes

1. **Board API const**: `Board::Pins()` / `Components()` are non-const virtuals; resolve helpers take `const Board&` and use a local `const_cast` for read-only iteration (same pattern as other const consumers would need).
2. **Same-net identity**: propagation uses raw `pin.net` pointer equality (`Net*`), not export `netId` integers — matches brief and is more reliable than file net numbers.
3. **`ExportPinResolveJson` pin id**: self pin export id uses `ExportPinId(pin, 0)`; component pins ignore index (`name.number`). Nail-like pins may need a real global index from the board for exact parity with full board export — routes can recompute via `ExportPinId` + board pin index when wiring Task 4+.
4. **`from.pinId` for propagated**: computed with real global index via board pin scan.
5. **No board integration test** without `OBV_TEST_BOARD`; pure helpers covered by unit tests. Full `ResolvePartPin` / JSON path exercised by later route tasks.
6. **Release asserts**: same note as Task 2 — prefer Debug for assert-based RED evidence.

## Review Fix: ExportPinResolveJson netId

### Finding
`ExportPinResolveJson` wrote `pin.net->number` as `pin.netId`. Board JSON uses sequential `exportNetId(Net*)` because `Net::number` is often unset. Clients key nets by board-JSON export ids, so correlation broke.

### Fix
- Added `PinResolveResult::netId` (0 = no net).
- `computeExportNetId` mirrors `board_json.cpp`: assign sequential ids in `Nets()` order, then first encounter on pins/tracks/vias/arcs for nets missing from `Nets()`.
- `ResolvePinMeasurements` fills `out.netId` via that map; `ExportPinResolveJson` emits `r.netId` (still `null` when no net). `netName` remains `pin.net->name`.

### Tests (Debug)
```
cmake --build build-core-only --target obv_core_tests --config Debug
build-core-only/src/obv_core_tests/Debug/obv_core_tests.exe
# skip export
# overlay yaml ok
# operating_conditions yaml ok
# operating_conditions json ok
# resolve field priority ok
# normalize condition ok
# allocate condition id ok
# pin overlay key ok
# ok
```

### Commit
```
fix(core): align pin resolve netId with board JSON export ids
```
