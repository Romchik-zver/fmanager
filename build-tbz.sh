#!/bin/sh
set -e
VERSION="0.1.0"
ARCH="x86_64"
NAME="fmanager-${VERSION}-${ARCH}-linux"

make clean && make

rm -rf "$NAME"
mkdir -p "$NAME/bin" "$NAME/lib"
cp fmanager "$NAME/bin/"
cp "$(nix eval --raw nixpkgs#ncurses.outPath)/lib/libncursesw.so.6" "$NAME/lib/"

cat > "$NAME/install.sh" <<'EOF'
#!/bin/sh
set -e
PREFIX="${1:-/usr/local}"
DIR="$(dirname "$(readlink -f "$0")")"
install -Dm755 "$DIR/bin/fmanager" "$PREFIX/bin/fmanager"
install -Dm644 "$DIR/lib/libncursesw.so.6" "$PREFIX/lib/libncursesw.so.6"
echo "Установлено в $PREFIX/bin/fmanager"
EOF
chmod +x "$NAME/install.sh"

tar -cjf "$NAME.tbz" "$NAME"
rm -rf "$NAME"
echo "Готово: $NAME.tbz"
