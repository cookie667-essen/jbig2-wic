#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-3.0.1}"
BUILD_DIR="${2:-build}"
OUT_DIR="${3:-.}"
ARCH="$(dpkg --print-architecture)"
PKG="jbig2-tools_${VERSION}_${ARCH}"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/$PKG/DEBIAN"
mkdir -p "$STAGE/$PKG/usr/bin"
mkdir -p "$STAGE/$PKG/usr/lib/x86_64-linux-gnu"
mkdir -p "$STAGE/$PKG/usr/include"
mkdir -p "$STAGE/$PKG/usr/share/thumbnailers"
mkdir -p "$STAGE/$PKG/usr/share/mime/packages"
mkdir -p "$STAGE/$PKG/usr/share/doc/jbig2-tools"

install -m755 "$BUILD_DIR/jbig2-cli" "$STAGE/$PKG/usr/bin/jbig2-cli"
install -m644 "$BUILD_DIR/libjbig2.so" "$STAGE/$PKG/usr/lib/x86_64-linux-gnu/libjbig2.so"
install -m644 "include/jbig2_c_api.h" "$STAGE/$PKG/usr/include/jbig2_c_api.h"
install -m644 "linux/jbig2.thumbnailer" "$STAGE/$PKG/usr/share/thumbnailers/jbig2.thumbnailer"
install -m644 "linux/jbig2-mime.xml" "$STAGE/$PKG/usr/share/mime/packages/jbig2-mime.xml"
install -m644 "LICENSE" "$STAGE/$PKG/usr/share/doc/jbig2-tools/copyright"
install -m644 "README.md" "$STAGE/$PKG/usr/share/doc/jbig2-tools/README.md"

cat > "$STAGE/$PKG/DEBIAN/control" <<EOF
Package: jbig2-tools
Version: ${VERSION}
Section: graphics
Priority: optional
Architecture: ${ARCH}
Depends: libc6 (>= 2.36), libstdc++6 (>= 12)
Maintainer: Marius Koch <m.koch.essen@gmail.com>
Homepage: https://github.com/cookie667-essen/jbig2-tools
Description: Fast zero-dependency JBIG2 (ITU-T T.88) toolkit
 Includes jbig2-cli (inspect, decode, thumbnail, multi-page JB2/PDF encode),
 libjbig2.so shared C-API library, C header, and Freedesktop thumbnailer.
EOF

cat > "$STAGE/$PKG/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
ldconfig || true
if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database /usr/share/mime || true
fi
EOF

cat > "$STAGE/$PKG/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
ldconfig || true
if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database /usr/share/mime || true
fi
EOF

chmod 755 "$STAGE/$PKG/DEBIAN/postinst" "$STAGE/$PKG/DEBIAN/postrm"
mkdir -p "$OUT_DIR"
dpkg-deb --build --root-owner-group "$STAGE/$PKG" "$OUT_DIR/${PKG}.deb"
echo "[OK] Gebaut: $OUT_DIR/${PKG}.deb"
