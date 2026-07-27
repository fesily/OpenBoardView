## Open Board Viewer [![Build Status](../../actions/workflows/make_packages.yml/badge.svg?event=push)](../../actions/)

Linux SDL/ImGui edition software for viewing .brd files, intended as a drop-in
replacement for the "Test_Link" software and "Landrex".


[![Walkthrough of OpenBoardView R7.2](http://img.youtube.com/vi/6CrNRo1UP5g/0.jpg)](http://www.youtube.com/watch?v=6CrNRo1UP5g "OpenBoardView R7.2 demonstration, with voice-over")

[![Demo Video](https://github.com/OpenBoardView/OpenBoardView/blob/master/asset/screenshot.png)]()


![Common net pin halo](https://github.com/OpenBoardView/OpenBoardView/blob/master/asset/screenshot-halo.png)

![Part searching](https://github.com/OpenBoardView/OpenBoardView/blob/master/asset/screenshot-partsearch.png)


### Features

- Annotations (per board database file)
- Part and pin sizes better represented
- Better outlining of irregular objects (such as connectors)
- Drag and drop
- Recently used file history
- Non-orthagonally orientated caps/resistors/diodes now drawn more realistically
- Adjustable DPI (for working on 2K/4K screens)
- Works with multiple concurrent instances


### TODO

- Decode more board formats
- Compound project/file format


### Prerequisites

#### macOS
	$ brew install cmake sdl2

If you get the following error after running `build.sh`:

	urllib.error.URLError: <urlopen error [SSL: CERTIFICATE_VERIFY_FAILED] certificate verify failed: unable to get local issuer certificate (_ssl.c:997)>


Run the script at `/Applications/Python3.x/Install Certificates.command`.


#### Ubuntu

For Ubuntu developers, you'll need the following packages at a minimum;

	$ apt-get install git build-essential cmake libsdl2-dev libgtk-3-dev

#### Fedora

For Fedora, you will need the following packages at a minimum;

	$ dnf install @development-tools

	$ dnf install SDL2-devel gtk3-devel fontconfig-devel cmake

### Installation

1. Clone the project
```
$ git clone --recursive 'https://github.com/OpenBoardView/OpenBoardView'
```
2. Build it
```
$ ./build.sh
```
3. Sign it (skip if you're not on running macOS)
```
$ codesign --force --deep --sign - ./openboardview.app
```
4. Run it!
```
$ ./bin/openboardview
```
...or...
```
$ ./openboardview.sh
```
...or...
```
$ open ./openboardview.app
```

### Usage

- Ctrl-O: Open file select dialog

- w/a/s/d: pan viewport over board
- x: Reset zoom and center
- Mouse scroll, -/=: Zoom out/in
- Mouse click-hold-drag, Numeric pad up/down/left/right: pan viewport over board
- Numeric pad +/-: zoom board
- Numeric pad 5: Reset zoom and center
- Space, Middle mouse click: Flip board
- R/./Numpad-Del: Rotate clockwise
- ,/Numpad-Ins: Rotate counter-clockwise

- /, Ctrl-F: Search
- ESC: Clear search results and selected parts

- p: Toggle pin display
- m: Mirror board across Y-axis

- L: Show net list
- K: Show part list

### Local web server (`obv_server`)

Companion HTTP server + browser UI (optional; desktop app unchanged).

```bash
# Build SPA then run (defaults: bind 127.0.0.1:8080, data ./data)
cd web && npm ci && npm run build && cd ..
./scripts/run_obv_server.sh          # Unix
# .\scripts\run_obv_server.ps1       # Windows
```

**Bind defaults:** `ServerConfig::host` defaults to **`127.0.0.1`** (loopback only). Use `--host 0.0.0.0` only when you intentionally expose the API on the LAN (no auth in MVP).

Decrypt keys (FZ/CAE/XZZ) live in a server-side config file only; `/api/v1/version` never returns key material.

**gzip / TLS:** `obv_server` does not compress responses itself. Put nginx (or similar) in front when serving large board JSON over a network:

```nginx
# Example reverse proxy in front of obv_server on 127.0.0.1:8080
server {
  listen 443 ssl;
  server_name obv.local;
  # ssl_certificate …; ssl_certificate_key …;

  gzip on;
  gzip_types application/json text/css application/javascript application/wasm;
  gzip_min_length 1024;

  client_max_body_size 64m;  # match maxUploadBytes (default 64 MiB)

  location / {
    proxy_pass http://127.0.0.1:8080;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
  }
}
```
