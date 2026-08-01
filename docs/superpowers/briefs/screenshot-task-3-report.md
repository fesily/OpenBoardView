# Screenshot Task 3 Report

## Summary

Implemented hand-rolled `ExportPartScreenshotMetaJson` for design §5.2 meta shape (image, boardBounds, transform.boardToImage, pins[]). No HTTP routes.

## Files

| Path | Change |
|------|--------|
| `src/obv_core/include/obv_core/part_render.h` | Declared `ExportPartScreenshotMetaJson` |
| `src/obv_core/src/part_render.cpp` | Local `appendEscaped`/`appendNumber`; full §5.2 JSON export |
| `src/obv_core_tests/test_part_render.cpp` | TDD: `test_meta_json_shape` |

## TDD

### RED

Build failed: `ExportPartScreenshotMetaJson` not a member of `obv`.

### GREEN

```
cmake --build build-core-only --config Debug --target obv_core_tests
./build-core-only/src/obv_core_tests/Debug/obv_core_tests.exe
```

```
meta json ok
part_render unit ok
ok
```

## JSON shape (§5.2)

- Top: `boardId`, `sourceName`, `part`
- `image`: `width`, `height`, `scale`, `padding`, `labels`, `partName`
- `boardBounds`: `minX`, `minY`, `maxX`, `maxY`
- `transform.boardToImage`: `originBoardX`, `originBoardY`, `scale`, `flipY`
- `pins[]`: `key`, `id`, `number`, `name`, `boardShowName`, `overlayShowName`, `displayLabel`, `board.{x,y}`, `image.{x,y}`, `type`, `shape`, `diameter`, `netName`

## Commit

```
feat(core): export part screenshot meta JSON
```

## Concerns / follow-ups

- Task 4+: wire HTTP `GET .../screenshot/meta` to this helper.
- `image.scale` prefers `optsUsed.scale`, falls back to `transform.scale`.
- Empty pin string fields emit `""` (not null), matching other Export* helpers.
