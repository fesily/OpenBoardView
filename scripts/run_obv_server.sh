#!/usr/bin/env bash
# Build web/dist if needed and start obv_server with data + SPA static root.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

HOST="${OBV_HOST:-127.0.0.1}"
PORT="${OBV_PORT:-8080}"
DATA="${OBV_DATA:-$ROOT/data}"
WWW="${OBV_WWW:-$ROOT/web/dist}"
BUILD_DIR="${OBV_BUILD_DIR:-$ROOT/build-web}"

find_server() {
  local candidates=(
    "$BUILD_DIR/src/obv_server/obv_server"
    "$BUILD_DIR/src/obv_server/Release/obv_server"
    "$BUILD_DIR/src/obv_server/Debug/obv_server"
    "$BUILD_DIR/src/obv_server/Release/obv_server.exe"
    "$BUILD_DIR/src/obv_server/Debug/obv_server.exe"
    "$BUILD_DIR/src/obv_server/obv_server.exe"
    "$ROOT/openboardview"
  )
  local c
  for c in "${candidates[@]}"; do
    if [[ -x "$c" || -f "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

if [[ ! -f "$WWW/index.html" ]]; then
  echo "web dist missing; running npm run build in web/"
  (cd "$ROOT/web" && npm run build)
fi

SERVER="$(find_server)" || {
  echo "obv_server binary not found under $BUILD_DIR" >&2
  echo "Build first, e.g.: cmake --build $BUILD_DIR --config Release --target obv_server" >&2
  exit 1
}

mkdir -p "$DATA/boards" "$DATA/overlays" "$DATA/config"
if [[ ! -f "$DATA/config/keys.json" && -f "$ROOT/data/config/keys.example.json" ]]; then
  cp "$ROOT/data/config/keys.example.json" "$DATA/config/keys.example.json" 2>/dev/null || true
fi

echo "Starting: $SERVER --host $HOST --port $PORT --data $DATA --www $WWW"
exec "$SERVER" --host "$HOST" --port "$PORT" --data "$DATA" --www "$WWW" "$@"
