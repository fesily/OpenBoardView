# Task 12 Report: Serve web dist from obv_server + packaging

**Status:** DONE  
**Branch:** `merge_upsteam_my`  
**Commit:** `2fd780c` `feat(server): host web dist and release run scripts`  
**Date:** 2026-07-27

---

## Summary

`obv_server` can host the Vite SPA from the same origin as the API:

- CLI/config: `--www DIR` / `webRoot` / `www` → `ServerConfig::webRoot`
- `set_mount_point("/", webRoot)` when non-empty
- SPA fallback: non-`/api/` 404 → `index.html` (200) via `set_error_handler` (413 JSON envelope preserved)
- Run helpers: `scripts/run_obv_server.sh`, `scripts/run_obv_server.ps1` (build `web/dist` if missing, start with `--data` + `--www`)
- `scripts/build_web_release.sh` → `npm run build` (+ optional copy target)
- Example keys (placeholders only): `data/config/keys.example.json`

---

## Files

| Path | Role |
|------|------|
| `src/obv_server/server_config.h` | `webRoot` field |
| `src/obv_server/server_config.cpp` | JSON/KV/`--www` parse + help |
| `src/obv_server/main.cpp` | mount + SPA 404 fallback |
| `scripts/run_obv_server.sh` | Unix launcher |
| `scripts/run_obv_server.ps1` | Windows launcher |
| `scripts/build_web_release.sh` | production web build |
| `data/config/keys.example.json` | documented key shape (zeros) |

---

## Usage

```bash
# web
cd web && npm run build

# server
./build-web/src/obv_server/Release/obv_server \
  --host 127.0.0.1 --port 8080 --data ./data --www ./web/dist

# or
./scripts/run_obv_server.sh
# Windows: .\scripts\run_obv_server.ps1
```

Decrypt keys: copy `data/config/keys.example.json` → real values via `--config` JSON (`FZKey` / `CAEKey` / `XZZPCBKey` hex lists). Never ship real keys; never expose in `/api/v1/version`.

---

## Verification

```text
cd web && npm run build   # OK
cmake --build build-web --config Release --target obv_server  # OK

obv_server --host 127.0.0.1 --port 18080 --data tmp-data-t12 --www web/dist
curl /                 → 200 text/html (index.html)
curl /api/v1/health    → {"status":"ok"}
curl /api/v1/version   → server/core only (no keys)
curl /boards/xyz       → 200 index.html (SPA fallback)
curl /api/v1/nope      → 404 (API paths not rewritten)
```

Manual E2E checklist (upload/search/annotation/restart) remains operator smoke; not automated here.

---

## Out of scope

- Task 13 (gzip / hardening / large-board smoke)
- Dockerfile multi-stage server image (optional; not required)

---

## Fix: Task 12 Important findings

**Commit:** `fix(scripts): executable launchers and correct dist paths`

| Finding | Fix |
|---------|-----|
| Unix scripts not executable in git | `git update-index --chmod=+x` → `100755` for `scripts/run_obv_server.sh`, `scripts/build_web_release.sh` |
| Launcher builds without deps | `run_obv_server.sh` / `.ps1`: if `web/dist` missing → `npm ci \|\| npm install` then `npm run build` (sh prefers `build_web_release.sh`) |
| `build_web_release.sh` OUT relative path | Resolve relative `OUT` against repo `ROOT` before `cd web` so `data/www` is repo-root, not `web/data/www` |

Verified: `bash -n scripts/run_obv_server.sh scripts/build_web_release.sh`; git index modes `100755` for both `.sh` launchers.
