#!/bin/sh
set -e

VERSION="${VERSION:-0.1.0}"
ARCH="${ARCH:-x86_64}"
OUT="fmanager-${VERSION}-${ARCH}.AppImage"

RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"
RUNTIME_CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/fmanager/runtime-x86_64"

NCURSES_LIB=$(nix eval --raw nixpkgs#ncurses.outPath)/lib/libncursesw.so.6

make clean
make

mkdir -p "$(dirname "$RUNTIME_CACHE")"
if [ ! -f "$RUNTIME_CACHE" ]; then
    echo "Downloading AppImage runtime..."
    wget -q "$RUNTIME_URL" -O "$RUNTIME_CACHE"
    chmod +x "$RUNTIME_CACHE"
fi

rm -rf AppDir
mkdir -p AppDir/usr/bin AppDir/usr/lib

cp fmanager AppDir/usr/bin/
cp "$NCURSES_LIB" AppDir/usr/lib/

chmod u+w AppDir/usr/bin/fmanager
chmod u+w AppDir/usr/lib/libncursesw.so.6

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

cat "$RUNTIME_CACHE" fmanager.squashfs > "$OUT"
chmod +x "$OUT"

rm -rf AppDir fmanager.squashfs

echo "Готово: $OUT"
