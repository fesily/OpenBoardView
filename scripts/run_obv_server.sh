#!/usr/bin/env bash
# Build web/dist if needed and start obv_server with library boardRoot + SPA static root.
# Env: OBV_HOST, OBV_PORT, OBV_BOARDS, OBV_WWW, OBV_BUILD_DIR
# OBV_BOARDS defaults to the Windows BaiduSyncdisk pcb path (override on non-Windows).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

HOST="${OBV_HOST:-127.0.0.1}"
PORT="${OBV_PORT:-8080}"
BOARDS="${OBV_BOARDS:-C:/Users/fesil/Documents/BaiduSyncdisk/pcb}"
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
  echo "web dist missing; installing deps and building web/"
  if [[ -x "$ROOT/scripts/build_web_release.sh" ]]; then
    "$ROOT/scripts/build_web_release.sh"
  else
    (
      cd "$ROOT/web"
      if [[ ! -d node_modules ]]; then
        npm ci || npm install
      fi
      npm run build
    )
  fi
fi

SERVER="$(find_server)" || {
  echo "obv_server binary not found under $BUILD_DIR" >&2
  echo "Build first, e.g.: cmake --build $BUILD_DIR --config Release --target obv_server" >&2
  exit 1
}

# Ensure config dir exists under boardRoot for optional keys
mkdir -p "$BOARDS/config"
if [[ ! -f "$BOARDS/config/keys.json" && -f "$ROOT/data/config/keys.example.json" ]]; then
  cp "$ROOT/data/config/keys.example.json" "$BOARDS/config/keys.example.json" 2>/dev/null || true
fi

echo "Starting: $SERVER --host $HOST --port $PORT --boards $BOARDS --www $WWW"
exec "$SERVER" --host "$HOST" --port "$PORT" --boards "$BOARDS" --www "$WWW" "$@"
