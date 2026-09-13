#!/usr/bin/env bash
set -e

RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"
NCURSES_LIB=$(nix eval --raw nixpkgs#ncurses.outPath 2>/dev/null)/lib/libncursesw.so.6
OUT="fmanager-x86_64.AppImage"

make clean && make

[ -f runtime-x86_64 ] || { wget -q "$RUNTIME_URL" -O runtime-x86_64; chmod +x runtime-x86_64; }

rm -rf AppDir fmanager.squashfs
mkdir -p AppDir/usr/bin AppDir/usr/lib
cp fmanager AppDir/usr/bin/
cp "$NCURSES_LIB" AppDir/usr/lib/

patchelf --set-rpath '$ORIGIN/../lib' AppDir/usr/bin/fmanager
patchelf --set-rpath '$ORIGIN' AppDir/usr/lib/libncursesw.so.6

cat > AppDir/fmanager.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=fmanager
Exec=fmanager
Icon=fmanager
Terminal=true
Categories=System;FileTools;
EOF

echo 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==' | base64 -d > AppDir/fmanager.png

cat > AppDir/AppRun <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
exec "${HERE}/usr/bin/fmanager" "$@"
EOF
chmod +x AppDir/AppRun

nix-shell -p squashfsTools --run "mksquashfs AppDir fmanager.squashfs -root-owned -noappend -comp zstd"

cat runtime-x86_64 fmanager.squashfs > "$OUT"
chmod +x "$OUT"

echo "Готово: $OUT"
