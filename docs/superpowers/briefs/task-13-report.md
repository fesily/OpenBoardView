# Task 13 Report: Hardening pass (MVP exit)

**Status:** DONE  
**Branch:** `merge_upsteam_my`  
**Commit:** `chore: MVP hardening for obv_server web path`  
**Date:** 2026-07-27

---

## Summary

MVP security/docs hardening for `obv_server` web path:

| Area | Result |
|------|--------|
| Upload size limit | Already present (`maxUploadBytes` default 64 MiB + 413 JSON envelope); retained |
| Upload extension allow-list | **Added** — rejects crazy types (e.g. `.exe`) with `400 UNSUPPORTED_TYPE` |
| `boardId` path traversal | **Tightened** to exact `[0-9a-f]{64}` (lowercase only); route-level `400 BAD_REQUEST` |
| Bind default docs | `--help` documents `127.0.0.1` vs `0.0.0.0`; README web section |
| gzip | Documented nginx reverse-proxy example (no in-process gzip) |
| `/api/v1/version` | Confirmed: `server` / `serverVersion` / `core` only — **no keys** |
| Large board smoke | No `OBV_TEST_BOARD` / sample fixture in tree; **not blocked** (see note) |

---

## Code changes

| Path | Change |
|------|--------|
| `src/obv_server/board_registry.cpp` | `isHexId` strict lowercase hex; `IsValidBoardId` public; `OverlayMutex` ignores invalid ids |
| `src/obv_server/board_registry.h` | `IsValidBoardId` declaration |
| `src/obv_server/routes.cpp` | `requireBoardId` on all `:id` routes; upload extension allow-list |
| `src/obv_server/server_config.cpp` | Expanded `--help` (bind, data, www, config, security) |
| `README.md` | Local web server section: bind defaults + nginx gzip/TLS example |

### boardId rule

- Must match **64 lowercase hex** characters (`[0-9a-f]{64}`), matching `sha256_hex` output.
- Invalid id → `400` `{"error":{"code":"BAD_REQUEST","message":"board id must be 64 lowercase hex characters"}}`.
- Valid hex but unknown → `404 NOT_FOUND` (unchanged).
- Registry already refused non-hex before path join; routes now fail closed with explicit 400.

### Upload allow-list

Allowed extensions (case-insensitive):  
`.brd .brd2 .bdv .bvr .bvr3 .fz .cae .bom .asc .cst .json .cad .pcb .alg .xzz .bin .txt .gencad`  
Bare names (no extension) still allowed for content-sniffed formats.  
Unknown extension → `400 UNSUPPORTED_TYPE` (does not write to disk).

---

## Spec coverage (Task 13)

| Spec | Coverage |
|------|----------|
| §10 bind default 127.0.0.1 | `ServerConfig::host` + `--help` + README |
| §10 upload size + extension allow-list | `maxUploadBytes` + allow-list |
| §10 path traversal on boardId | `[0-9a-f]{64}` + route guard |
| §1 / §10 keys server-only | `/api/v1/version` has no key fields (re-verified) |
| gzip | nginx example in README (proxy, not httplib) |

---

## E2E smoke checklist

Server:  
`obv_server --host 127.0.0.1 --port 18086 --data tmp-data-t13 --www web/dist`

| Check | Result |
|-------|--------|
| `GET /api/v1/health` | 200 `{"status":"ok"}` |
| `GET /api/v1/version` | 200 server/core only; **no key material** |
| `GET /` (static SPA) | 200 HTML `index.html` (~402 B shell) |
| `GET /api/v1/boards` | 200 `[]` then list after upload |
| Path traversal / short id (`not-a-hex-id`) | **400 BAD_REQUEST** |
| Uppercase 64-hex id | **400 BAD_REQUEST** |
| Upload `evil.exe` | **400 UNSUPPORTED_TYPE** |
| Upload invalid `x.bin` | 200 `{id:sha256, ok:false, …}` (parse fail OK) |
| `GET …/overlays` for uploaded id | 200 empty overlay |
| `PUT …/overlays` | 200 empty overlay round-trip |
| Valid unknown 64-hex | 404 NOT_FOUND |
| `--help` bind text | documents 127.0.0.1 default / 0.0.0.0 LAN |

### Large board smoke (Step 4)

- No sample board file / `OBV_TEST_BOARD` available in workspace.
- **Not blocking MVP.** Operator follow-up: time `GET /api/v1/boards/:id` on a real dense board; if response **>5s** or JSON **>50MB**, open issue for split endpoints / gzip-first via reverse proxy.
- gzip path is documented (nginx) for when large JSON appears.

---

## Verification commands

```bash
cmake --build build-web --config Release --target obv_server
./build-web/src/obv_server/Release/obv_server --help
# then smoke curls above on port 18086
```

---

## Out of scope / not done

- In-process gzip in cpp-httplib (documented reverse proxy instead)
- Automated CI E2E for hardening
- Desktop packaging
- Auth / multi-user isolation
