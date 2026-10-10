#!/bin/sh
# kinecode installer — one line: curl -fsSL https://raw.githubusercontent.com/fawwelll/kinecode/main/install.sh | sh
set -e
URL="https://github.com/fawwelll/kinecode/releases/latest/download/kinecode-linux"
BIN="$HOME/.local/bin/kinecode"
echo "kinecode installer"
mkdir -p "$(dirname "$BIN")"
if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$URL" -o "$BIN"
elif command -v wget >/dev/null 2>&1; then
    wget -qO "$BIN" "$URL"
else
    echo "error: need curl or wget" >&2
    exit 1
fi
chmod +x "$BIN"
case ":$PATH:" in
    *":$HOME/.local/bin:"*) ;;
    *) echo 'note: add to PATH →  export PATH="$HOME/.local/bin:$PATH"' ;;
esac
echo "done! run:  kinecode"
