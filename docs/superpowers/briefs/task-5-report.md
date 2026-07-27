# Task 5 Report: `obv_server` skeleton (config, health, version)

**Status:** DONE  
**Branch:** `merge_upsteam_my`  
**Base:** `de40cfe` (Task 4 complete)  
**Commit:** `3a36592` — `feat(server): obv_server health/version skeleton`  
**Date:** 2026-07-27

---

## Summary

Scaffolded `obv_server` as a thin cpp-httplib executable linked to `obv_core`. Default bind is **127.0.0.1:8080**. Exposes:

| Method | Path | Body |
|--------|------|------|
| GET | `/api/v1/health` | `{"status":"ok"}` |
| GET | `/api/v1/version` | `{"server":"obv_server","serverVersion":"0.1.0","core":"1"}` (no secrets) |

`ServerConfig` holds host/port/`dataRoot`/`DecryptKeys`/`maxUploadBytes`/`allowDelete`. CLI: `--host`, `--port`, `--data`, `--config`. Config file: JSON object or simple `key = value` lines; optional FZ/CAE/XZZ key strings stay server-side only.

No board registry/upload (Task 6). Static mount deferred (`// static later`).

---

## Files created

| Path | Purpose |
|------|---------|
| `src/obv_server/CMakeLists.txt` | `obv_server` exe; include `third_party/`; link `obv_core` (+ `ws2_32` on WIN32) |
| `src/obv_server/third_party/httplib.h` | Vendored [cpp-httplib](https://github.com/yhirose/cpp-httplib) v0.18.3 single header |
| `src/obv_server/server_config.h` | `ServerConfig`, `LoadConfig`, `ParseArgs` |
| `src/obv_server/server_config.cpp` | Minimal JSON/KV config load + CLI overrides |
| `src/obv_server/main.cpp` | Listen + health/version routes |

## Files modified

| Path | Change |
|------|--------|
| `src/CMakeLists.txt` | `option(ENABLE_OBV_SERVER ON)`; `add_subdirectory(obv_server)` after core (requires `ENABLE_OBV_CORE`) |

---

## Interfaces

```cpp
namespace obv_server {
struct ServerConfig {
  std::string host = "127.0.0.1";
  int port = 8080;
  filesystem::path dataRoot; // boards/, overlays/, config/
  obv::DecryptKeys keys;
  size_t maxUploadBytes = 64 * 1024 * 1024;
  bool allowDelete = false;
};
ServerConfig LoadConfig(const filesystem::path &jsonOrTomlPath);
ServerConfig ParseArgs(int argc, char **argv); // --config --host --port --data
}
```

Defaults: empty `dataRoot` after parse becomes `cwd/data`. `maxUploadBytes` applied via `svr.set_payload_max_length` for Task 6.

---

## Verification

```text
cmake -S . -B build-web -DENABLE_OBV_CORE=ON -DENABLE_OBV_SERVER=ON
cmake --build build-web --target obv_server -j 8 --config Release
# -> build-web/src/obv_server/Release/obv_server.exe

obv_server.exe --host 127.0.0.1 --port 8080
# listening 127.0.0.1:8080 dataRoot=.../data

curl.exe -s http://127.0.0.1:8080/api/v1/health
# {"status":"ok"}

curl.exe -s http://127.0.0.1:8080/api/v1/version
# {"server":"obv_server","serverVersion":"0.1.0","core":"1"}
```

---

## Out of scope (not done)

- Board registry / upload / list / get (Task 6)
- Overlay HTTP routes (Task 7)
- Static `web/dist` mount (Task 12)
- Full TOML parser (KV lines + JSON only)

---

## Concerns

1. **Config parser is minimal** (first-key string/number/bool scan, not a full JSON/TOML library). Enough for host/port/data/keys; nested objects beyond flat keys are not modeled.
2. **Desktop FZ key length heuristic** (`>440` chars before decode) not copied; server decodes any non-empty hex list so short test keys work. Align with desktop later if needed.
3. **MSVC multi-config**: binary under `build-web/src/obv_server/Release/` not a single-config `build-web/src/obv_server/obv_server`.
4. **httplib size**: ~340KB single header vendored under `third_party/`; keep in git for offline builds.
