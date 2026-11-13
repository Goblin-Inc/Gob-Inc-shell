#!/usr/bin/env bash
set -e
DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
if command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --needed base-devel glfw mesa
elif command -v apt >/dev/null 2>&1; then
    sudo apt update && sudo apt install -y build-essential libglfw3-dev libgl1-mesa-dev
fi
mkdir -p "$DIR/build"
g++ -std=c++17 -O2 -pthread \
    "$DIR/main.cpp" "$DIR/process/pty.cpp" "$DIR/terminal/terminal.cpp" \
    "$DIR/third_party/imgui/imgui.cpp" "$DIR/third_party/imgui/imgui_draw.cpp" \
    "$DIR/third_party/imgui/imgui_tables.cpp" "$DIR/third_party/imgui/imgui_widgets.cpp" \
    "$DIR/third_party/imgui/backends/imgui_impl_glfw.cpp" \
    "$DIR/third_party/imgui/backends/imgui_impl_opengl3.cpp" \
    -I"$DIR" -I"$DIR/third_party/imgui" -I"$DIR/third_party/imgui/backends" \
    -lglfw -lGL -ldl -lutil -o "$DIR/build/myshell_gui"
echo "Готово! Запускайте: ./build/myshell_gui"
