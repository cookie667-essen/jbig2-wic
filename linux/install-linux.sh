#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${1:-}" && "${1:-}" != "--user" ]]; then
  BUILD_DIR="$1"
  shift
elif [[ -f "./jbig2-cli" && -f "./libjbig2.so" ]]; then
  BUILD_DIR="."
else
  BUILD_DIR="build"
fi

PREFIX="${PREFIX:-/usr/local}"
if [[ "${1:-}" == "--user" ]]; then
  BIN_DIR="$HOME/.local/bin"
  LIB_DIR="$HOME/.local/lib"
  SHARE_DIR="$HOME/.local/share"
  USER_MODE=1
else
  BIN_DIR="$PREFIX/bin"
  LIB_DIR="$PREFIX/lib"
  SHARE_DIR="$PREFIX/share"
  USER_MODE=0
fi

ASSET_DIR="linux"
[[ -f "./jbig2.thumbnailer" ]] && ASSET_DIR="."

install -Dm755 "$BUILD_DIR/jbig2-cli" "$BIN_DIR/jbig2-cli"
install -Dm755 "$BUILD_DIR/libjbig2.so" "$LIB_DIR/libjbig2.so"
install -Dm644 "$ASSET_DIR/jbig2.thumbnailer" "$SHARE_DIR/thumbnailers/jbig2.thumbnailer"
install -Dm644 "$ASSET_DIR/jbig2-mime.xml" "$SHARE_DIR/mime/packages/jbig2-mime.xml"

if command -v update-mime-database >/dev/null 2>&1; then
  update-mime-database "$SHARE_DIR/mime"
fi
if [[ "$USER_MODE" -eq 0 ]] && command -v ldconfig >/dev/null 2>&1; then
  ldconfig
fi
echo "[OK] Installed jbig2-cli, libjbig2.so, MIME type and Freedesktop thumbnailer into $SHARE_DIR"
