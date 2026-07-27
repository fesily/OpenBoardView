# Task 6 Report: Board registry + upload/list/get

**Status:** DONE  
**Branch:** `merge_upsteam_my`  
**Base:** `36aa93e` (Task 5 complete)  
**Date:** 2026-07-27

---

## Summary

Implemented content-addressed board storage and HTTP routes on `obv_server`:

| Method | Path | Behavior |
|--------|------|----------|
| GET | `/api/v1/boards` | JSON array `{id,name,ok,error}` |
| POST | `/api/v1/boards` | multipart `file` or raw body + `X-Filename` → store under `dataRoot/boards/<id>_<safeName>`, parse, return `{id,ok,error,meta}` |
| GET | `/api/v1/boards/:id` | `ExportBoardJson` or 400 `PARSE_FAILED` / 404 |
| GET | `/api/v1/boards/:id/meta` | `ExportMetaJson` |
| DELETE | `/api/v1/boards/:id` | 403 unless `allowDelete` (default false) |

`boardId` = SHA-256 hex of file bytes (embedded portable `sha256.c`, no OpenSSL). Error envelope: `{"error":{"code","message"}}`. No overlay CRUD (Task 7).

---

## Files created

| Path | Purpose |
|------|---------|
| `src/obv_server/sha256.h` / `sha256.c` | Portable public-domain SHA-256 |
| `src/obv_server/board_registry.h` / `.cpp` | `BoardRegistry`: ImportUpload, List, GetParsed, BoardPath, Remove; mtime-aware parse cache |
| `src/obv_server/routes.h` / `.cpp` | Board route registration |

## Files modified

| Path | Change |
|------|--------|
| `src/obv_server/main.cpp` | Construct registry; `RegisterBoardRoutes` |
| `src/obv_server/CMakeLists.txt` | Add board_registry, routes, sha256.c |

---

## Interfaces

```cpp
class BoardRegistry {
public:
  explicit BoardRegistry(ServerConfig cfg);
  struct Entry {
    std::string id;
    filesystem::path path;
    std::string name;
    std::string parseError;
    bool ok = false;
  };
  Entry ImportUpload(const std::string &originalName, const std::string &body);
  std::vector<Entry> List() const;
  std::shared_ptr<const obv::BoardSnapshot> GetParsed(const std::string &id);
  filesystem::path BoardPath(const std::string &id) const;
  bool Remove(const std::string &id); // gated by allowDelete
};
```

Uses `obv::ParseBoardBuffer`, `ExportBoardJson`, `ExportMetaJson`.

---

## Verification

```text
cmake --build build-web --target obv_server -j 8 --config Release
# -> build-web/src/obv_server/Release/obv_server.exe

obv_server.exe --host 127.0.0.1 --port 18080 --data .../tmp-data-t6

curl -s http://127.0.0.1:18080/api/v1/boards
# []

curl -s -F "file=@tmp-invalid.bin;filename=x.bin" http://127.0.0.1:18080/api/v1/boards
# {"id":"ca3704aa0b06f5954c79ee837faa152d84d6b2d42838f0637a15eda8337dbdce",
#  "ok":false,"error":"Unrecognized file format.","meta":null}
# id == sha256("nope")

curl -s http://127.0.0.1:18080/api/v1/boards
# [{"id":"ca3704…","name":"x.bin","ok":false,"error":"Unrecognized file format."}]

curl -s .../boards/<id>
# HTTP 400 {"error":{"code":"PARSE_FAILED","message":"Unrecognized file format."}}

curl -s -X DELETE .../boards/<id>
# HTTP 403 {"error":{"code":"FORBIDDEN","message":"delete disabled (allowDelete=false)"}}

curl -s .../boards/0000…0000
# HTTP 404 NOT_FOUND
```

No redistributable sample board in-repo; success path (`{"boardSchemaVersion":1…`) not exercised here. Invalid upload + empty list + parse-fail GET covered.

---

## Out of scope

- Overlay HTTP routes (Task 7)
- Static web mount (Task 12)
- Sample board fixture for full export smoke

---

## Concerns

1. **Upload always HTTP 200** with `ok`/`error` in body so clients keep content-addressed id on parse failure; only transport/write failures use 4xx/5xx.
2. **List() re-parses** disk-scanned entries lazily on first list — fine for small LAN stores; may want index file later.
3. **MSVC C4819** avoided by keeping comments ASCII-only.
