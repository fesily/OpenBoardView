# Library mode + split-pane UI

## Summary

Replaced upload-store board management with a **boardRoot library scan**: recursive filesystem listing under a configured directory, content id = sha256 of absolute normalized path, lazy parse on open only. Web UI is a left file list / right single-board view; upload UX and POST upload are disabled.

## Server

| Area | Change |
|------|--------|
| `ServerConfig` | `filesystem::path boardRoot`; CLI `--boards`, config `boardRoot`/`boards`; default `C:\Users\fesil\Documents\BaiduSyncdisk\pcb` |
| `GET /api/v1/config` | `{"boardRoot","host","port"}` — no keys |
| `BoardRegistry` | Scans `boardRoot` recursively; allow-list extensions; skips `.yaml`/`.sqlite3`/`.conf`; `List()` always rescans without parse; overlays stay next to real board paths |
| `POST /api/v1/boards` | **405** `UPLOAD_DISABLED` `"use boardRoot library"` |
| Scripts | `run_obv_server.ps1` / `.sh` pass `--boards` from `OBV_BOARDS` (Windows default BaiduSyncdisk path) |

## Web

| Area | Change |
|------|--------|
| Layout | `.app-shell` → left `.sidebar` (~300px) + right `.main` |
| Upload | Removed from `App.tsx`; `uploadBoard` deleted from client |
| API | `getServerConfig()`; `BoardSummary.path?` |
| UX | Click file opens single board; clears pin/search/overlay; placeholder “从左侧选择板图”; error badge after failed open |

## Verify

```
Release build: obv_server.exe OK
npm run build: OK
GET /api/v1/config → boardRoot=C:\Users\fesil\Documents\BaiduSyncdisk\pcb
GET /api/v1/boards → 17 real .bvr files (path + id path-hash), ok=true until open
GET /api/v1/boards/:id → 200 parse (e.g. 215 comps / 567 pins)
POST /api/v1/boards → 405 UPLOAD_DISABLED
```

## Commit

`feat: boardRoot library scan and split-pane UI`
