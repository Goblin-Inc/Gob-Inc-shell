#!/usr/bin/env bash
set -e
DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
if [ ! -x "$DIR/build/myshell_gui" ]; then
    "$DIR/install.sh"
fi
exec "$DIR/build/myshell_gui" "$@"
