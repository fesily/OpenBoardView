#!/usr/bin/env bash
# Production build of the SPA into web/dist (optionally copy to a deploy dir).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-}"

# Resolve relative OUT against repo ROOT before cd web (so data/www is at repo root).
if [[ -n "$OUT" ]]; then
  case "$OUT" in
    /*|[A-Za-z]:/*|[A-Za-z]:\\*) ;; # absolute
    *) OUT="$ROOT/$OUT" ;;
  esac
fi

cd "$ROOT/web"
if [[ ! -d node_modules ]]; then
  npm ci || npm install
fi
npm run build

if [[ -n "$OUT" ]]; then
  mkdir -p "$OUT"
  # Copy contents of dist into OUT (e.g. data/www or build-web/web_dist)
  if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete "$ROOT/web/dist/" "$OUT/"
  else
    rm -rf "${OUT:?}/"*
    cp -a "$ROOT/web/dist/." "$OUT/"
  fi
  echo "Copied web/dist -> $OUT"
else
  echo "Built $ROOT/web/dist"
fi
