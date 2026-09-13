#!/bin/sh
set -e

BIN_DIR="${HOME}/.local/bin"
mkdir -p "$BIN_DIR"

cp "$(dirname "$0")/fmanager" "$BIN_DIR/fmanager"
chmod +x "$BIN_DIR/fmanager"

case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *)
        echo "Добавь в ~/.bashrc (или ~/.zshrc):"
        echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
        ;;
esac

echo "Установлено: $BIN_DIR/fmanager"
