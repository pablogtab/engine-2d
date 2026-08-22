#!/bin/bash

set -e
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

BUILD_DIR="build"
EXE="./build/Engine2D"
PROJECT_DIR="${ENGINE2D_PROJECT:-../projecto}"

if [ ! -f "$PROJECT_DIR/project.engine2d" ]; then
    echo "[RUN] Invalid Engine2D project: $PROJECT_DIR"
    echo "[RUN] Set ENGINE2D_PROJECT to the project directory."
    exit 1
fi
PROJECT_DIR="$(cd "$PROJECT_DIR" && pwd)"

if [ ! -f "$EXE" ]; then
    cmake -B "$BUILD_DIR" -S . -DENGINE_PROJECT_DIR="$PROJECT_DIR"
    cmake --build "$BUILD_DIR"
fi

MODE="${1:-editor}"
case "$MODE" in
    editor|--editor)
        exec "$EXE" --project "$PROJECT_DIR" --editor
        ;;
    game|standalone|--standalone)
        exec "$EXE" --project "$PROJECT_DIR" --standalone
        ;;
    help|--help|-h)
        exec "$EXE" --help
        ;;
    *)
        echo "Usage: ./demo.sh [editor|game]"
        exit 2
        ;;
esac
