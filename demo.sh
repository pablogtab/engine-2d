#!/bin/bash
# ==============================================================================
#  Engine2D - Demo Scenes Launcher
#  Runs specific demo scenes directly or provides an interactive selector.
# ==============================================================================

set -e
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

BUILD_DIR="build"
EXE="./build/Engine2D"

# Ensure build directory and binary exist
if [ ! -f "$EXE" ]; then
    echo "[DEMO] Engine binary not found. Building project..."
    if [ ! -d "$BUILD_DIR" ]; then
        cmake -B "$BUILD_DIR" -S .
    fi
    cmake --build "$BUILD_DIR"
fi

show_menu() {
    echo ""
    echo "======================================================="
    echo "  🎮 2D GAME ENGINE - DEMO SCENE SELECTOR"
    echo "======================================================="
    echo "  1) Platformer Level (Tilemap, ECS, Physics)"
    echo "  2) 2D Platformer & Collision Layer Matrix (C++ Box2D)"
    echo "  3) Top-Down Physics & 2D Animated Sprites"
    echo "  4) 2.5D Depth & Parallax Test"
    echo "  5) GPU/CPU Stress Test (3,000 Sprites)"
    echo "  6) Standalone Game Mode (Full Window, TAB to switch)"
    echo "  7) Editor Mode (Full IDE & Tools)"
    echo "  q) Salir / Quit"
    echo "======================================================="
    read -p "Selecciona una demo [1-7]: " choice

    case "$choice" in
        1) $EXE --demo platformer --standalone ;;
        2) $EXE --demo testbed --standalone ;;
        3) $EXE --demo sprites --standalone ;;
        4) $EXE --demo depth --standalone ;;
        5) $EXE --demo stress --standalone ;;
        6) $EXE --standalone ;;
        7) $EXE --editor ;;
        q|Q) echo "Saliendo..."; exit 0 ;;
        *) echo "Opción no válida. Ejecutando por defecto..."; $EXE --standalone ;;
    esac
}

# If arguments were passed, run with them directly
if [ $# -gt 0 ]; then
    case "$1" in
        1|platformer|level|authoring)
            shift
            $EXE --demo platformer --standalone "$@"
            ;;
        2|testbed|physics|box2d|collision)
            shift
            $EXE --demo testbed --standalone "$@"
            ;;
        3|sprites|anim|animated|topdown)
            shift
            $EXE --demo sprites --standalone "$@"
            ;;
        4|depth|parallax|2.5d)
            shift
            $EXE --demo depth --standalone "$@"
            ;;
        5|stress|benchmark|gpu)
            shift
            $EXE --demo stress --standalone "$@"
            ;;
        standalone|game|-s|--standalone)
            shift
            $EXE --standalone "$@"
            ;;
        editor|ide|-e|--editor)
            shift
            $EXE --editor "$@"
            ;;
        menu|--menu)
            show_menu
            ;;
        list|--list|--list-demos|-l)
            $EXE --list-demos
            ;;
        help|--help|-h)
            $EXE --help
            ;;
        *)
            $EXE "$@"
            ;;
    esac
else
    show_menu
fi
